const { Client, GatewayIntentBits } = require("discord.js");
const axios = require("axios");
const moment = require("moment");
require("moment/locale/th");

const bot = new Client({
  intents: [
    GatewayIntentBits.Guilds,
    GatewayIntentBits.GuildMessages,
    GatewayIntentBits.MessageContent,
  ],
});

const channelId = "1306125554071896105"; // ช่องสำหรับแจ้งสถานะ
const channelId2 = "1309077712220192840"; // ช่องสำหรับแจ้งเตือน
const apiUrl =
  "https://esp32-f1440-default-rtdb.asia-southeast1.firebasedatabase.app/Devices.json"; // URL API ของ Firebase

// ตัวแปรเก็บสถานะการแจ้งเตือน
let alertSent = false;

// ฟังก์ชันดึงข้อมูลจาก API
async function getData() {
  try {
    const response = await axios.get(apiUrl);
    const data = response.data;
    const device = data["Smart_Monitor_001"];
    const date = device.Data.Date;
    const time = device.Data.Time;

    const formattedDate = moment(date, "YYYY-MM-DD").locale("th").format("D MMMM YYYY");
    const formattedTime = moment(time, "HH:mm:ss").locale("th").format("HH:mm:ss");

    const Current = device.Data.Current;
    const Voltage = device.Data.Voltage;
    const leakageCurrent = device.Data.leakageCurrent;

    return {
      Current,
      Voltage,
      formattedDate,
      formattedTime,
      leakageCurrent,
    };
  } catch (error) {
    console.error("Error fetching data:", error);
    return null;
  }
}

// ฟังก์ชันส่งข้อความแจ้งเตือน
async function sendAlertMessage(channel, data) {
  const message = `**⚠️ ตรวจพบไฟฟ้ารั่วในระบบ! ⚠️**\n
**ตำแหน่ง:** ตำแหน่งการติดตั้งสมมุติ\n
**อุปกรณ์:** Smart_Monitor_001\n
**ค่าที่วัดได้:** กระแสไฟฟ้ารั่ว: ${data.Current} mA\n
**วันที่:** ${data.formattedDate}\n
**เวลา:** ${data.formattedTime}\n
**คำแนะนำ:** กรุณาดำเนินการตรวจสอบโดยด่วน!`;

  await channel.send(message);
}

// ตรวจสอบสถานะไฟฟ้ารั่วแบบเรียลไทม์
async function monitorLeakage(channel) {
  const data = await getData();
  if (data && data.leakageCurrent === 1) {
    if (!alertSent) {
      console.log("ตรวจพบไฟฟ้ารั่ว! ส่งข้อความแจ้งเตือน...");
      await sendAlertMessage(channel, data);
      alertSent = true; // เปลี่ยนสถานะเป็นส่งแจ้งเตือนแล้ว
    }
  } else {
    if (alertSent) {
      console.log("สถานะปกติ การแจ้งเตือนถูกรีเซ็ต");
      alertSent = false; // รีเซ็ตสถานะการแจ้งเตือน
    }
  }
}

// ฟังก์ชันลบข้อความทั้งหมดในช่อง
async function deleteAllMessages(channel) {
  try {
    const messages = await channel.messages.fetch({ limit: 100 });
    await channel.bulkDelete(messages);
    console.log("ลบข้อความทั้งหมดในช่องแล้ว");
  } catch (error) {
    console.error("ไม่สามารถลบข้อความได้:", error);
  }
}

// Event เมื่อบอทพร้อมทำงาน
bot.on("ready", async () => {
  console.log("บอทพร้อมทำงานแล้ว!");
  const channel = bot.channels.cache.get(channelId);
  const channel2 = bot.channels.cache.get(channelId2);

  if (channel && channel2 && channel.isTextBased() && channel2.isTextBased()) {
    console.log("เริ่มตรวจสอบการรั่วไฟ...");
    await channel.send("ระบบเริ่มการตรวจสอบไฟฟ้ารั่ว...");

    // ตรวจสอบสถานะทุก 3 วินาที
    setInterval(async () => {
      await monitorLeakage(channel2); // ใช้ channel2 สำหรับแจ้งเตือน
    }, 3000);
  } else {
    console.error("ไม่พบช่องแชทที่กำหนด");
  }
});

// คำสั่งสำหรับผู้ใช้
bot.on("messageCreate", async (message) => {
  // คำสั่งลบข้อความทั้งหมด
  if (message.content == "-del" && message.member.permissions.has("MANAGE_MESSAGES")) {
    await deleteAllMessages(message.channel);
  }

  // คำสั่งรีเซ็ตการแจ้งเตือน
  if (message.content === "-fix") {
    alertSent = false; // รีเซ็ตสถานะการแจ้งเตือน
    console.log("สถานะการแจ้งเตือนถูกรีเซ็ตโดยผู้ใช้");
    await message.channel.send("✅ สถานะการแจ้งเตือนถูกรีเซ็ต เริ่มตรวจสอบใหม่");
  }
});

// เริ่มการล็อกอินของบอท
bot.login(
  "Toket"
);
