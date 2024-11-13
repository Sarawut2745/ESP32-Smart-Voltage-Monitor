#include <WiFi.h>
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include <time.h>
#include "addons/TokenHelper.h"
#include <PZEM004Tv30.h>

// ตั้งค่า API Key และ URL ของ Firebase Realtime Database
#define API_KEY ""
#define DATABASE_URL ""

// กำหนดขา (Pin) สำหรับรีเลย์และบัซเซอร์
#define RELAY_PIN_1 27
#define RELAY_PIN_2 26
#define BUZZER_PIN 23
#define RX_PIN 16
#define TX_PIN 17
#define BOOT_BUTTON_PIN 0  // BOOT button pin on ESP32

// กำหนด ID สำหรับอุปกรณ์ ESP32
#define DEVICE_ID "ESP32_DEVICE_002"

// สร้างออบเจ็กต์สำหรับการเชื่อมต่อ Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// สร้างออบเจ็กต์สำหรับการวัดแรงดันไฟฟ้า (Voltage)
PZEM004Tv30 pzem(Serial2, RX_PIN, TX_PIN);
float voltage;

// ตัวแปรสถานะการเชื่อมต่อ Firebase
bool signupOK = false;
unsigned long sendDataPrevMillis = 0;
const long sendDataIntervalMillis = 10000;  // กำหนดช่วงเวลาการส่งข้อมูล (10 วินาที)

// ฟังก์ชันตั้งค่าเวลาจาก NTP Server
void configTimeForNTP() {
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");  // ใช้เขตเวลา GMT+7
}

// ฟังก์ชันรับเวลาปัจจุบันเป็นข้อความ
String getCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("ไม่สามารถดึงเวลาได้");
    return "";
  }
  char timeString[30];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeString);
}

void setup() {
  Serial.begin(115200);
  Serial.println("เริ่มต้นโปรแกรม");

  // ตั้งค่าเริ่มต้นให้บัซเซอร์ปิดอยู่
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);

  // ตั้งค่า BOOT button pin เป็น input
  pinMode(BOOT_BUTTON_PIN, INPUT);

  // เชื่อมต่อ WiFi โดยใช้ WiFiManager
  WiFiManager wifiManager;
  wifiManager.autoConnect("ESP32_AP");
  Serial.println("\nเชื่อมต่อ WiFi สำเร็จ");

  // ตั้งค่าเวลา NTP
  configTimeForNTP();

  // ตั้งค่าการเชื่อมต่อ Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;

  // ลงทะเบียนเข้าสู่ Firebase แบบไม่ระบุตัวตน (Anonymous)
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("ลงทะเบียน Firebase สำเร็จ");
    signupOK = true;
  } else {
    Serial.printf("ลงทะเบียน Firebase ไม่สำเร็จ: %s\n", config.signer.signupError.message.c_str());
  }

  // เริ่มการใช้งาน Firebase
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // ส่ง ID ของอุปกรณ์ไปยัง Firebase
  if (Firebase.RTDB.setString(&fbdo, "/Devices/" DEVICE_ID "/ID", DEVICE_ID)) {
    Serial.println("ส่ง Device ID สำเร็จ");
  } else {
    Serial.printf("ส่ง Device ID ไม่สำเร็จ: %s\n", fbdo.errorReason().c_str());
  }

  // ตั้งค่า Pin ของรีเลย์
  pinMode(RELAY_PIN_1, OUTPUT);
  pinMode(RELAY_PIN_2, OUTPUT);
}

unsigned long voltageCheckPrevMillis = 0;
const long voltageCheckIntervalMillis = 500;  // กำหนดช่วงเวลาสำหรับการอ่านค่าแรงดันไฟฟ้า (500 ms)
unsigned long relayControlPrevMillis = 0;
const long relayControlIntervalMillis = 60000;  // กำหนดช่วงเวลาสำหรับการควบคุมรีเลย์ (60 วินาที)

void loop() {
  unsigned long currentMillis = millis();

  // ตรวจสอบการกดปุ่ม BOOT เพื่อล้างค่า WiFi
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {  // หาก BOOT ปุ่มถูกกด
    Serial.println("ล้างค่า WiFi...");
    WiFiManager wifiManager;
    wifiManager.resetSettings();  // ล้างค่า WiFi ที่บันทึกไว้
    delay(1000);                  // รอให้การรีเซ็ตเสร็จสิ้น
    ESP.restart();                // รีสตาร์ท ESP32 เพื่อกลับไปยังโหมดการตั้งค่า WiFi
  }

  // (ต่อจากนี้เป็นโค้ดเดิมที่ทำงานตามปกติ)
  // อ่านค่าแรงดันไฟฟ้าเป็นช่วงเวลาที่กำหนด
  if (currentMillis - voltageCheckPrevMillis >= voltageCheckIntervalMillis) {
    voltageCheckPrevMillis = currentMillis;
    voltage = pzem.voltage();
    Serial.print("แรงดันไฟฟ้า: ");
    Serial.print(voltage);
    Serial.println(" V");
  }

  // ตรวจสอบและควบคุมรีเลย์และบัซเซอร์ตามสถานะแรงดันไฟฟ้า
  if (voltage >= 80.0) {
    digitalWrite(RELAY_PIN_2, HIGH);  // เปิดรีเลย์ 2 เมื่อแรงดันเกิน 80V
    digitalWrite(BUZZER_PIN, LOW);    // เปิดบัซเซอร์เมื่อแรงดันสูง
    delay(500);                       // หน่วงเวลาสำหรับบัซเซอร์
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(RELAY_PIN_1, LOW);  // ปิดรีเลย์ 1

    // ส่งข้อมูลกระแสรั่วไหลและเวลาไปยัง Firebase เมื่อครบช่วงเวลา
    if (Firebase.ready() && signupOK && (currentMillis - sendDataPrevMillis > sendDataIntervalMillis || sendDataPrevMillis == 0)) {
      sendDataPrevMillis = currentMillis;
      String currentTime = getCurrentTime();

      if (Firebase.RTDB.setFloat(&fbdo, "/Devices/" DEVICE_ID "/Data/leakageCurrent", 1)) {
        Serial.println("ส่งข้อมูลกระแสรั่วไหลสำเร็จ");
      } else {
        Serial.printf("ส่งข้อมูลกระแสรั่วไหลไม่สำเร็จ: %s\n", fbdo.errorReason().c_str());
      }

      if (Firebase.RTDB.setString(&fbdo, "/Devices/" DEVICE_ID "/Data/Timestamp", currentTime)) {
        Serial.println("ส่งข้อมูลเวลา Timestamp สำเร็จ");
      } else {
        Serial.printf("ส่งข้อมูลเวลา Timestamp ไม่สำเร็จ: %s\n", fbdo.errorReason().c_str());
      }
    }
  } else {
    digitalWrite(RELAY_PIN_1, HIGH);  // เปิดรีเลย์ 1 เมื่อแรงดันต่ำกว่า 80V
    digitalWrite(BUZZER_PIN, HIGH);   // ปิดบัซเซอร์เมื่อแรงดันต่ำ
    digitalWrite(RELAY_PIN_2, LOW);   // ปิดรีเลย์ 2

    // ส่งข้อมูลกระแสรั่วไหลและเวลาไปยัง Firebase เมื่อครบช่วงเวลา
    if (Firebase.ready() && signupOK && (currentMillis - sendDataPrevMillis > sendDataIntervalMillis || sendDataPrevMillis == 0)) {
      sendDataPrevMillis = currentMillis;
      String currentTime = getCurrentTime();

      if (Firebase.RTDB.setFloat(&fbdo, "/Devices/" DEVICE_ID "/Data/leakageCurrent", 0)) {
        Serial.println("ส่งข้อมูลกระแสรั่วไหลสำเร็จ");
      } else {
        Serial.printf("ส่งข้อมูลกระแสรั่วไหลไม่สำเร็จ: %s\n", fbdo.errorReason().c_str());
      }

      if (Firebase.RTDB.setString(&fbdo, "/Devices/" DEVICE_ID "/Data/Timestamp", currentTime)) {
        Serial.println("ส่งข้อมูลเวลา Timestamp สำเร็จ");
      } else {
        Serial.printf("ส่งข้อมูลเวลา Timestamp ไม่สำเร็จ: %s\n", fbdo.errorReason().c_str());
      }
    }
  }

  // หน่วงเวลา 30 วินาที (กรณีที่คุณต้องการ)
  if (currentMillis - relayControlPrevMillis >= relayControlIntervalMillis) {
    relayControlPrevMillis = currentMillis;
  }
}
