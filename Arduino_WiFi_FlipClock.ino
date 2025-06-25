#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Servo.h>

// 1. WiFi Credentials
const char* ssid     = "SSID_Name";       // ใส่ชื่อ WiFi ของคุณ
const char* password = "WiFi_PASSWORD";   // ใส่รหัสผ่าน WiFi

// 2. GPIO Pins
#define PIN_SERVO_MINUTE      2
#define PIN_SERVO_HOUR        0

#define PIN_HOME_SWITCH_MINUTE 4
#define PIN_STEP_SWITCH_MINUTE 13
#define PIN_HOME_SWITCH_HOUR   14
#define PIN_STEP_SWITCH_HOUR   12


// ตำแหน่งหยุดนิ่งของ Servo (ควรเป็นค่าเดียวกัน)
#define SERVO_STOP_POSITION   90 

// ค่า Offset สำหรับการหมุน (กำหนดทิศทางตรงข้ามกัน)
// ตัวอย่าง: นาทีหมุนตามเข็ม (+), ชั่วโมงหมุนทวนเข็ม (-)
// คุณต้องทดลองว่าค่าบวกหรือลบที่ทำให้กลไกของคุณหมุนไปข้างหน้า
#define SERVO_MOVE_OFFSET_MINUTE  30  // เช่น หมุนไปข้างหน้าด้วยค่าบวก
#define SERVO_MOVE_OFFSET_HOUR   -30  // หมุนไปข้างหน้าด้วยค่าลบ (กลับด้าน)


// NTP Client Settings
const char* ntpServer = "time.google.com";
const long  gmtOffset_sec = 7 * 3600; // Timezone ประเทศไทย (GMT+7)
const int   daylightOffset_sec = 0;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, gmtOffset_sec, daylightOffset_sec);

// Servo Objects
Servo servoMinute;
Servo servoHour;

// Global variables to track clock state
int currentMinuteOnClock = -1; // -1 หมายถึงยังไม่รู้ตำแหน่ง
int currentHourOnClock = -1;

void setup() {
  Serial.begin(115200);
  Serial.println("\nStarting Flip Clock (Revised Logic)...");

  // Setup pin modes
  pinMode(PIN_HOME_SWITCH_MINUTE, INPUT_PULLUP);
  pinMode(PIN_STEP_SWITCH_MINUTE, INPUT_PULLUP);
  pinMode(PIN_HOME_SWITCH_HOUR, INPUT_PULLUP);
  pinMode(PIN_STEP_SWITCH_HOUR, INPUT_PULLUP);

  // Attach servos
  servoMinute.attach(PIN_SERVO_MINUTE);
  servoHour.attach(PIN_SERVO_HOUR);
  servoMinute.write(SERVO_STOP_POSITION);
  servoHour.write(SERVO_STOP_POSITION);

  // Connect to WiFi
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // Start NTP client
  timeClient.begin();

  // Homing sequence
  Serial.println("Starting Homing Sequence...");
  goHomeMinute();
  goHomeHour();
  Serial.println("Homing Complete!");

  // Get initial time
  while(!timeClient.update()) {
    timeClient.forceUpdate();
  }
  Serial.print("Initial Time: ");
  Serial.println(timeClient.getFormattedTime());
}

void loop() {
  timeClient.update();

  int actualMinute = timeClient.getMinutes();
  int actualHour = timeClient.getHours();
  
  // Minute Logic 
  if (currentMinuteOnClock != actualMinute) {
    Serial.printf("Minute mismatch! Clock: %02d, NTP: %02d. Moving...\n", currentMinuteOnClock, actualMinute);
    moveMinuteStep();
  }

  // Hour Logic
  if (currentHourOnClock != actualHour) {
    Serial.printf("Hour mismatch! Clock: %02d, NTP: %02d. Moving to next hour...\n", currentHourOnClock, actualHour);
    moveHourToNext();
  }

  delay(1000); // Wait a second before checking again
}

void goHomeMinute() {
  Serial.println("Homing minute dial...");
  while (digitalRead(PIN_HOME_SWITCH_MINUTE) == HIGH) {
    servoMinute.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_MINUTE);
    delay(200);
  }
  servoMinute.write(SERVO_STOP_POSITION);
  currentMinuteOnClock = 0;
  Serial.println("Minute dial is at HOME (00)");
  delay(500);
}

void goHomeHour() {
  Serial.println("Homing hour dial...");
  while (digitalRead(PIN_HOME_SWITCH_HOUR) == HIGH) {
    servoHour.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_HOUR);
    delay(200);
  }
  servoHour.write(SERVO_STOP_POSITION);
  currentHourOnClock = 0;
  Serial.println("Hour dial is at HOME (00)");
  delay(500);
}

void moveMinuteStep() {
  servoMinute.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_MINUTE);
  while (digitalRead(PIN_STEP_SWITCH_MINUTE) == HIGH) { delay(10); } // รอจนกว่าจะกด
  while (digitalRead(PIN_STEP_SWITCH_MINUTE) == LOW) { delay(10); } // รอจนกว่าจะปล่อย (ป้องกันการเด้ง)
  servoMinute.write(SERVO_STOP_POSITION);
  
  currentMinuteOnClock = (currentMinuteOnClock + 1) % 60;
  Serial.printf("Minute moved to: %02d\n", currentMinuteOnClock);
}


void performHourSingleStep() {
  servoHour.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_HOUR);
  while (digitalRead(PIN_STEP_SWITCH_HOUR) == HIGH) { delay(10); }
  while (digitalRead(PIN_STEP_SWITCH_HOUR) == LOW) { delay(10); }
  servoHour.write(SERVO_STOP_POSITION);
  Serial.println("Hour moved 1 step.");
}


void moveHourToNext() {
  Serial.println("Performing 2 steps for next hour...");
  performHourSingleStep(); // ขยับ step ที่ 1
  delay(100); // หน่วงเวลาสั้นๆ ให้กลไกนิ่ง
  performHourSingleStep(); // ขยับ step ที่ 2
  
  currentHourOnClock = (currentHourOnClock + 1) % 24;
  Serial.printf("Hour display is now: %02d\n", currentHourOnClock);
}
