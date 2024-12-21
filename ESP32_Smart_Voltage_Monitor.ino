// ไลบรารีที่จำเป็น
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include <PZEM004Tv30.h>
#include <ArduinoJson.h>

// =============== ค่าคงที่สำหรับการตั้งค่า Firebase ===============
#define DATABASE_URL_API "https://esp32-f1440-default-rtdb.asia-southeast1.firebasedatabase.app/Detected.json"
#define API_KEY "AIzaSyAZAvntnhQdktxIBKr4KEyyh-8cOmiptH0"
#define DATABASE_URL "https://esp32-f1440-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define DEVICE_ID "Smart_Monitor_001"

// =============== การกำหนดขา GPIO ===============
// ขาสำหรับรีเลย์และบัซเซอร์
#define RELAY_PIN_1 27        // รีเลย์ตัวที่ 1
#define RELAY_PIN_2 26        // รีเลย์ตัวที่ 2
#define BUZZER_PIN 23        // บัซเซอร์หลัก
#define ALERT_BUZZER_PIN 21  // บัซเซอร์สำหรับแจ้งเตือน
#define BOOT_BUTTON_PIN 0    // ปุ่ม BOOT สำหรับรีเซ็ต WiFi

// ขาสำหรับ PZEM-004T
#define RX_PIN 16
#define TX_PIN 17

// =============== ตัวแปรสำหรับ Firebase ===============
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

// =============== ตัวแปรสำหรับการวัดค่าไฟฟ้า ===============
PZEM004Tv30 pzem(Serial2, RX_PIN, TX_PIN);
float voltage = 0.0, lastVoltage = -1.0;
float current = 0.0, lastCurrent = -1.0;
bool lastLeakageStatus = false;

// =============== ตัวแปรสำหรับการจัดการเวลา ===============
unsigned long sendDataPrevMillis = 0;
const long sendDataIntervalMillis = 1000;  // ส่งข้อมูลทุก 1 วินาที

// =============== ฟังก์ชันสำหรับการจัดการเวลา ===============
void configTimeForNTP() {
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");  // ตั้งค่าเวลาโซน +7 (ประเทศไทย)
}

void getCurrentDateTime(String &date, String &time) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("ไม่สามารถรับข้อมูลเวลาได้");
        return;
    }
    // จัดรูปแบบวันที่และเวลา
    date = String(timeinfo.tm_year + 1900) + "-" + String(timeinfo.tm_mon + 1) + "-" + String(timeinfo.tm_mday);
    time = String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec);
}

// =============== ฟังก์ชันตรวจสอบการรีเซ็ต WiFi ===============
void checkButtonForWiFiReset() {
    static unsigned long buttonPressStart = 0;
    static bool buttonHeld = false;

    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        if (!buttonHeld) {
            buttonPressStart = millis();
            buttonHeld = true;
        } else if (millis() - buttonPressStart >= 3000) {  // กดค้าง 3 วินาที
            Serial.println("กำลังรีเซ็ตการตั้งค่า WiFi...");
            WiFiManager wifiManager;
            wifiManager.resetSettings();
            ESP.restart();
        }
    } else {
        buttonHeld = false;
    }
}

// =============== ฟังก์ชัน Setup ===============
void setup() {
    Serial.begin(115200);
    Serial.println("เริ่มต้นระบบ...");

    // ตั้งค่าขา GPIO
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(ALERT_BUZZER_PIN, OUTPUT);
    pinMode(BOOT_BUTTON_PIN, INPUT);
    pinMode(RELAY_PIN_1, OUTPUT);
    pinMode(RELAY_PIN_2, OUTPUT);

    // ตั้งค่าสถานะเริ่มต้น
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(ALERT_BUZZER_PIN, HIGH);

    // เชื่อมต่อ WiFi
    WiFiManager wifiManager;
    wifiManager.autoConnect("ESP32_AP");
    Serial.println("เชื่อมต่อ WiFi สำเร็จ");

    // ตั้งค่าเวลา
    configTimeForNTP();

    // ตั้งค่า Firebase
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    config.token_status_callback = tokenStatusCallback;

    // ลงทะเบียน Firebase
    if (Firebase.signUp(&config, &auth, "", "")) {
        Serial.println("ลงทะเบียน Firebase สำเร็จ");
        signupOK = true;
    } else {
        Serial.printf("ลงทะเบียน Firebase ไม่สำเร็จ: %s\n", config.signer.signupError.message.c_str());
    }

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    // ส่ง Device ID ไปยัง Firebase
    if (Firebase.RTDB.setString(&fbdo, "/Devices/" DEVICE_ID "/ID", DEVICE_ID)) {
        Serial.println("ส่ง Device ID สำเร็จ");
    } else {
        Serial.printf("ส่ง Device ID ไม่สำเร็จ: %s\n", fbdo.errorReason().c_str());
    }
}

// =============== ฟังก์ชัน Loop ===============
void loop() {
    unsigned long currentMillis = millis();
    
    // ตรวจสอบปุ่มรีเซ็ต WiFi
    checkButtonForWiFiReset();

    // อ่านค่าไฟฟ้า
    voltage = pzem.voltage();
    current = pzem.current();

    // ตรวจสอบค่าที่วัดได้
    if (isnan(voltage) || voltage <= 0) voltage = 0.0;
    if (isnan(current) || current <= 0) current = 0.0;

    // ตรวจสอบสถานะการรั่วไหล
    bool leakageStatus = (voltage >= 80.0 || current >= 5.0);

    // ควบคุมรีเลย์
    digitalWrite(RELAY_PIN_2, leakageStatus ? HIGH : LOW);
    digitalWrite(RELAY_PIN_1, leakageStatus ? LOW : HIGH);

    // ส่งข้อมูลไปยัง Firebase
    if (Firebase.ready() && signupOK && (currentMillis - sendDataPrevMillis >= sendDataIntervalMillis)) {
        sendDataPrevMillis = currentMillis;
        
        // รับข้อมูลวันที่และเวลา
        String date, time;
        getCurrentDateTime(date, time);

        // ส่งข้อมูลไปยัง Firebase
        Firebase.RTDB.setString(&fbdo, "/Devices/" DEVICE_ID "/Data/Date", date);
        Firebase.RTDB.setString(&fbdo, "/Devices/" DEVICE_ID "/Data/Time", time);
        Firebase.RTDB.setFloat(&fbdo, "/Devices/" DEVICE_ID "/Data/leakageCurrent", leakageStatus ? 1 : 0);
        Firebase.RTDB.setFloat(&fbdo, "/Devices/" DEVICE_ID "/Data/Voltage", voltage);
        Firebase.RTDB.setFloat(&fbdo, "/Devices/" DEVICE_ID "/Data/Current", current);
    }

    // ตรวจสอบข้อมูลจาก Firebase
    HTTPClient http;
    http.begin(DATABASE_URL_API);
    int httpCode = http.GET();

    if (httpCode > 0) {
        String payload = http.getString();
        Serial.println("อ่านข้อมูลจาก Firebase สำเร็จ");

        // แปลงข้อมูล JSON
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, payload);
        int personDetected = doc["person_detected"];

        // ควบคุมบัซเซอร์เมื่อตรวจพบคนและมีไฟฟ้ารั่ว
        if (personDetected == 1 && leakageStatus) {
            digitalWrite(BUZZER_PIN, LOW);
            digitalWrite(ALERT_BUZZER_PIN, LOW);
        } else {
            digitalWrite(BUZZER_PIN, HIGH);
            digitalWrite(ALERT_BUZZER_PIN, HIGH);
        }
    } else {
        Serial.printf("ไม่สามารถรับข้อมูลจาก Firebase: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();

    // แสดงข้อมูลที่ Serial Monitor
    Serial.printf("แรงดันไฟฟ้า: %.2f V, กระแสไฟฟ้า: %.2f A, สถานะการรั่ว: %s\n", 
                  voltage, current, leakageStatus ? "มีการรั่วไหล" : "ปกติ");

    delay(500);
}
