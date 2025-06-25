#include <Wire.h>
#include <RTClib.h>
#include <Servo.h>

// 1. GPIO Pins 
// I2C Pins for RTC
#define PIN_SDA D2 
#define PIN_SCL D1 
// Servo Pins
#define PIN_SERVO_MINUTE      
#define PIN_SERVO_HOUR        

// Switch Pins
#define PIN_BTN_MODE_CANCEL   D5 
#define PIN_BTN_HOUR_INC      D6 
#define PIN_BTN_MINUTE_INC    D7 
#define PIN_BTN_SAVE          D8 


// 2. Servo Parameters 
#define SERVO_STOP_POSITION   90 
#define SERVO_MOVE_OFFSET_MINUTE  5
#define SERVO_MOVE_OFFSET_HOUR   -5


// RTC Object
RTC_DS3231 rtc;

// Servo Objects
Servo servoMinute;
Servo servoHour;

// Clock State Variables
enum ClockState { NORMAL_MODE, SETTING_MODE };
ClockState currentMode = NORMAL_MODE;

int currentMinuteOnClock = -1; // -1 = Unknown
int currentHourOnClock = -1;   // -1 = Unknown

// Temporary variables for time setting
int settingHour;
int settingMinute;

unsigned long lastButtonPressTime = 0;
const int debounceDelay = 250; // หน่วงเวลา 250ms เพื่อป้องกันปุ่มเบิ้ล

void setup() {
  Serial.begin(115200);
  Serial.println("\nStarting RTC Flip Clock...");

  // Setup pin modes for switches
  pinMode(PIN_BTN_MODE_CANCEL, INPUT_PULLUP);
  pinMode(PIN_BTN_HOUR_INC, INPUT_PULLUP);
  pinMode(PIN_BTN_MINUTE_INC, INPUT_PULLUP);
  pinMode(PIN_BTN_SAVE, INPUT_PULLUP);

  // Attach servos
  servoMinute.attach(PIN_SERVO_MINUTE);
  servoHour.attach(PIN_SERVO_HOUR);
  servoMinute.write(SERVO_STOP_POSITION);
  servoHour.write(SERVO_STOP_POSITION);

  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC! Halting.");
    while (1);
  }

  // หาก RTC ไม่เคยทำงาน (เช่น ถ่านหมด หรือยังไม่เคยตั้งเวลา)
  // ให้ตั้งเวลาเป็นเวลาตอนที่ Compile โค้ด
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, let's set the time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Homing sequence (สำคัญมาก)
  Serial.println("Starting Homing Sequence...");
  goHomeMinute();
  goHomeHour();
  Serial.println("Homing Complete!");

  // ซิงค์เวลาจาก RTC มาที่หน้าปัดครั้งแรก
  syncClockToRTC();
}

void loop() {
  // อ่านสถานะปุ่มกด
  handleButtons();

  // ทำงานตามสถานะปัจจุบัน
  if (currentMode == NORMAL_MODE) {
    // โหมดปกติ: อัปเดตเวลาจาก RTC
    syncClockToRTC();
    delay(1000); // หน่วงเวลา 1 วินาทีในโหมดปกติ
  } else {
    // โหมดตั้งค่า: ไม่ต้องทำอะไร รอรับคำสั่งจากปุ่มใน handleButtons()
    // อาจจะให้มีไฟ LED กระพริบเพื่อบอกสถานะก็ได้
    delay(50);
  }
}

void handleButtons() {
  // Debounce: เช็คว่ามีการกดปุ่มไปล่าสุดเมื่อไหร่
  if (millis() - lastButtonPressTime < debounceDelay) {
    return;
  }

  if (currentMode == NORMAL_MODE) {
    // ในโหมดปกติ รอแค่ปุ่ม Mode
    if (digitalRead(PIN_BTN_MODE_CANCEL) == LOW) {
      lastButtonPressTime = millis();
      Serial.println("Entering Setting Mode...");
      currentMode = SETTING_MODE;
      // ดึงเวลาปัจจุบันมาเป็นค่าเริ่มต้นในการตั้งค่า
      DateTime now = rtc.now();
      settingHour = now.hour();
      settingMinute = now.minute();
      Serial.printf("Current time is %02d:%02d. Ready to set.\n", settingHour, settingMinute);
    }
  } else { // currentMode == SETTING_MODE
    // ในโหมดตั้งค่า
    if (digitalRead(PIN_BTN_HOUR_INC) == LOW) {
      lastButtonPressTime = millis();
      settingHour = (settingHour + 1) % 24;
      Serial.printf("Set Hour to: %02d\n", settingHour);
      // ขยับหน้าปัดชั่วโมงตาม
      moveHourToNext();
    }
    
    if (digitalRead(PIN_BTN_MINUTE_INC) == LOW) {
      lastButtonPressTime = millis();
      settingMinute = (settingMinute + 1) % 60;
      Serial.printf("Set Minute to: %02d\n", settingMinute);
      // ขยับหน้าปัดนาทีตาม
      moveMinuteStep();
    }

    if (digitalRead(PIN_BTN_SAVE) == LOW) {
      lastButtonPressTime = millis();
      Serial.println("Saving time to RTC...");
      // สร้าง DateTime object ใหม่แล้วบันทึกลง RTC
      DateTime newTime(2025, 6, 23, settingHour, settingMinute, 0); // ปี,เดือน,วัน ไม่สำคัญมากนัก
      rtc.adjust(newTime);
      Serial.println("Time saved! Exiting Setting Mode.");
      currentMode = NORMAL_MODE;
    }

    if (digitalRead(PIN_BTN_MODE_CANCEL) == LOW) {
      lastButtonPressTime = millis();
      Serial.println("Cancelled setting. Exiting Setting Mode.");
      currentMode = NORMAL_MODE;
      // ซิงค์หน้าปัดกลับไปเป็นเวลาเดิมใน RTC
      syncClockToRTC();
    }
  }
}

void syncClockToRTC() {
  DateTime now = rtc.now();
  int actualMinute = now.minute();
  int actualHour = now.hour();

  while (currentHourOnClock != actualHour) {
    moveHourToNext();
  }
  while (currentMinuteOnClock != actualMinute) {
    moveMinuteStep();
  }
}


// --- ฟังก์ชันควบคุมการหมุน (เหมือนเดิม) ---
void goHomeMinute() {
  Serial.println("Homing minute dial...");
  while (digitalRead(PIN_HOME_SWITCH_MINUTE) == HIGH) {
    servoMinute.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_MINUTE); delay(20);
  }
  servoMinute.write(SERVO_STOP_POSITION);
  currentMinuteOnClock = 0;
  Serial.println("Minute dial is at HOME (00)");
}

void goHomeHour() {
  Serial.println("Homing hour dial...");
  while (digitalRead(PIN_HOME_SWITCH_HOUR) == HIGH) {
    servoHour.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_HOUR); delay(20);
  }
  servoHour.write(SERVO_STOP_POSITION);
  currentHourOnClock = 0;
  Serial.println("Hour dial is at HOME (00)");
}

void moveMinuteStep() {
  servoMinute.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_MINUTE);
  while (digitalRead(PIN_STEP_SWITCH_MINUTE) == HIGH) { delay(10); }
  while (digitalRead(PIN_STEP_SWITCH_MINUTE) == LOW) { delay(10); }
  servoMinute.write(SERVO_STOP_POSITION);
  currentMinuteOnClock = (currentMinuteOnClock + 1) % 60;
  Serial.printf("Minute display is now: %02d\n", currentMinuteOnClock);
}

void performHourSingleStep() {
  servoHour.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_HOUR);
  while (digitalRead(PIN_STEP_SWITCH_HOUR) == HIGH) { delay(10); }
  while (digitalRead(PIN_STEP_SWITCH_HOUR) == LOW) { delay(10); }
  servoHour.write(SERVO_STOP_POSITION);
}

void moveHourToNext() {
  Serial.println("Hour moving 2 steps...");
  performHourSingleStep();
  delay(100);
  performHourSingleStep();
  currentHourOnClock = (currentHourOnClock + 1) % 24;
  Serial.printf("Hour display is now: %02d\n", currentHourOnClock);
}
