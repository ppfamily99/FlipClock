/**
 * @file FlipClock_RTC_PCF8574.ino
 * @brief Final firmware for a 3D Printed Flip Clock using an ESP8266.
 * @version 2.0
 * * @details This version operates offline using an RTC module and manual button controls.
 * Hardware:
 * - ESP8266 (NodeMCU/WEMOS D1 Mini)
 * - DS3231 RTC Module (I2C)
 * - PCF8574 I/O Expander (I2C) to handle limit switches due to pin shortage
 * - 2x SG90 Servo Motors (with external 5V power supply)
 * - 4x Limit Switches (connected to PCF8574)
 * - 1x4 Membrane Switch for manual time setting (connected to ESP8266)
 * * Libraries needed:
 * - RTClib by Adafruit
 * - PCF8574 by Rob Tillaart
 * * Pinout Summary:
 * - D1 (GPIO5): I2C SCL -> DS3231 SCL & PCF8574 SCL
 * - D2 (GPIO4): I2C SDA -> DS3231 SDA & PCF8574 SDA
 * - D3 (GPIO0): Minute Servo Signal
 * - D4 (GPIO2): Hour Servo Signal
 * - D5 (GPIO14): Button 1 (Mode/Cancel)
 * - D6 (GPIO12): Button 2 (Hour+)
 * - D7 (GPIO13): Button 3 (Minute+)
 * - D8 (GPIO15): Button 4 (Save)
 * - PCF8574 P0-P3: 4x Limit Switches
 */

#include <Wire.h>
#include <RTClib.h>
#include <Servo.h>
#include <PCF8574.h> 

// --- CONFIGURATION SECTION ---

// 1. I2C Address for the I/O Expander
// Default is often 0x20. If it doesn't work, try 0x38 or use an I2C Scanner sketch.
#define PCF8574_ADDRESS 0x20

// 2. GPIO Pin Assignments
// I2C Pins (Shared by RTC and Expander)
#define PIN_SDA D2 // GPIO4
#define PIN_SCL D1 // GPIO5

// Servo Pins
#define PIN_SERVO_MINUTE      D3 // GPIO0
#define PIN_SERVO_HOUR        D4 // GPIO2

// Button Pins (Connected directly to ESP8266)
#define PIN_BTN_MODE_CANCEL   D5 // GPIO14
#define PIN_BTN_HOUR_INC      D6 // GPIO12
#define PIN_BTN_MINUTE_INC    D7 // GPIO13
#define PIN_BTN_SAVE          D8 // GPIO15

// 3. Servo Movement Parameters
#define SERVO_STOP_POSITION   90 
// Adjust these offsets for speed and direction. Use opposite signs for opposite rotation.
#define SERVO_MOVE_OFFSET_MINUTE  15
#define SERVO_MOVE_OFFSET_HOUR   -15

// --- END OF CONFIGURATION ---


// --- OBJECT & VARIABLE DECLARATIONS ---

// Create objects for our hardware
RTC_DS3231 rtc;
PCF8574 pcf8574(PCF8574_ADDRESS);
Servo servoMinute;
Servo servoHour;

// Define human-readable names for expander pins
const int EXPANDER_PIN_HOME_MINUTE = 0;
const int EXPANDER_PIN_STEP_MINUTE = 1;
const int EXPANDER_PIN_HOME_HOUR   = 2;
const int EXPANDER_PIN_STEP_HOUR   = 3;

// State machine for clock operation
enum ClockState { NORMAL_MODE, SETTING_MODE };
ClockState currentMode = NORMAL_MODE;

// Variables to track the physical state of the clock display
int currentMinuteOnClock = -1; // -1 means position is unknown
int currentHourOnClock = -1;

// Temporary variables for use in SETTING_MODE
int settingHour;
int settingMinute;

// Variables for button debouncing
unsigned long lastButtonPressTime = 0;
const int debounceDelay = 250; // 250ms delay to prevent accidental double presses


// --- SETUP FUNCTION ---
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n--- Flip Clock with RTC & I/O Expander ---");

  // Initialize I2C bus first
  Wire.begin(PIN_SDA, PIN_SCL);

  // Initialize the RTC
  if (!rtc.begin()) {
    Serial.println("FATAL: Couldn't find RTC! Halting execution.");
    while (1) delay(100);
  }
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting time to compile time.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Initialize the PCF8574 I/O Expander
  pcf8574.begin();
  Serial.println("Setting up PCF8574 pins for limit switches...");
  pcf8574.pinMode(EXPANDER_PIN_HOME_MINUTE, INPUT_PULLUP);
  pcf8574.pinMode(EXPANDER_PIN_STEP_MINUTE, INPUT_PULLUP);
  pcf8574.pinMode(EXPANDER_PIN_HOME_HOUR, INPUT_PULLUP);
  pcf8574.pinMode(EXPANDER_PIN_STEP_HOUR, INPUT_PULLUP);
  
  // Setup button pins on the ESP8266
  pinMode(PIN_BTN_MODE_CANCEL, INPUT_PULLUP);
  pinMode(PIN_BTN_HOUR_INC, INPUT_PULLUP);
  pinMode(PIN_BTN_MINUTE_INC, INPUT_PULLUP);
  pinMode(PIN_BTN_SAVE, INPUT_PULLUP);
  
  // Attach servos
  servoMinute.attach(PIN_SERVO_MINUTE);
  servoHour.attach(PIN_SERVO_HOUR);
  servoMinute.write(SERVO_STOP_POSITION);
  servoHour.write(SERVO_STOP_POSITION);

  // --- CRITICAL STARTUP SEQUENCE ---
  // 1. Calibrate physical mechanism to 00:00
  Serial.println("Starting Homing Sequence to find 00:00...");
  goHomeHour();   // Home hour first
  goHomeMinute(); // Home minute second
  Serial.println("Homing Complete! Display is at 00:00.");

  // 2. Move display from 00:00 to actual time from RTC
  Serial.println("Syncing display to RTC time...");
  syncClockToRTC();
  Serial.println("Clock is ready!");
}


// --- MAIN LOOP ---
void loop() {
  // Always check for button presses
  handleButtons();

  // Main operational logic based on the current mode
  if (currentMode == NORMAL_MODE) {
    // In normal mode, just keep the clock synced to the RTC
    syncClockToRTC();
    // We only need to check once per second in this mode
    delay(1000); 
  } else { // SETTING_MODE
    // In setting mode, the clock only moves when a button is pressed.
    // A small delay prevents the loop from running too fast.
    delay(50);
  }
}


// --- BUTTON HANDLING LOGIC ---
void handleButtons() {
  // Simple debounce check
  if (millis() - lastButtonPressTime < debounceDelay) {
    return;
  }

  if (currentMode == NORMAL_MODE) {
    // In NORMAL_MODE, only the MODE/CANCEL button does anything
    if (digitalRead(PIN_BTN_MODE_CANCEL) == LOW) {
      lastButtonPressTime = millis();
      Serial.println("--- Switched to SETTING_MODE ---");
      currentMode = SETTING_MODE;
      // Load current time into temporary setting variables
      DateTime now = rtc.now();
      settingHour = now.hour();
      settingMinute = now.minute();
      Serial.printf("Ready to set time. Starting at %02d:%02d\n", settingHour, settingMinute);
    }
  } 
  else { // currentMode == SETTING_MODE
    // In SETTING_MODE, all buttons are active
    if (digitalRead(PIN_BTN_HOUR_INC) == LOW) {
      lastButtonPressTime = millis();
      settingHour = (settingHour + 1) % 24;
      Serial.printf("Hour set to: %02d. Moving display...\n", settingHour);
      moveHourToNext(); // Physically move the hour display
    }
    
    else if (digitalRead(PIN_BTN_MINUTE_INC) == LOW) {
      lastButtonPressTime = millis();
      settingMinute = (settingMinute + 1) % 60;
      Serial.printf("Minute set to: %02d. Moving display...\n", settingMinute);
      moveMinuteStep(); // Physically move the minute display
    }

    else if (digitalRead(PIN_BTN_SAVE) == LOW) {
      lastButtonPressTime = millis();
      Serial.println("Saving new time to RTC...");
      // Create a new DateTime object with the selected time and today's date from RTC
      DateTime now = rtc.now();
      rtc.adjust(DateTime(now.year(), now.month(), now.day(), settingHour, settingMinute, 0));
      Serial.println("Time saved!");
      Serial.println("--- Switched to NORMAL_MODE ---");
      currentMode = NORMAL_MODE;
    }

    else if (digitalRead(PIN_BTN_MODE_CANCEL) == LOW) {
      lastButtonPressTime = millis();
      Serial.println("Cancelled setting. Reverting display.");
      Serial.println("--- Switched to NORMAL_MODE ---");
      currentMode = NORMAL_MODE;
      // Move the display back to the actual time stored in the RTC
      syncClockToRTC();
    }
  }
}


// --- CORE CLOCK FUNCTIONS ---

/**
 * @brief Reads the RTC and moves the physical display to match it.
 * This is the primary function for keeping the clock accurate in NORMAL_MODE.
 */
void syncClockToRTC() {
  DateTime now = rtc.now();
  int actualMinute = now.minute();
  int actualHour = now.hour();

  // Keep moving the hour display until it matches the RTC hour
  while (currentHourOnClock != actualHour) {
    moveHourToNext();
  }
  // Keep moving the minute display until it matches the RTC minute
  while (currentMinuteOnClock != actualMinute) {
    moveMinuteStep();
  }
}


// --- LOW-LEVEL MOVEMENT FUNCTIONS ---

void goHomeMinute() {
  while (pcf8574.digitalRead(EXPANDER_PIN_HOME_MINUTE) == HIGH) {
    servoMinute.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_MINUTE); 
    delay(20);
  }
  servoMinute.write(SERVO_STOP_POSITION);
  currentMinuteOnClock = 0; // We are now at position 00
}

void goHomeHour() {
  while (pcf8574.digitalRead(EXPANDER_PIN_HOME_HOUR) == HIGH) {
    servoHour.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_HOUR); 
    delay(20);
  }
  servoHour.write(SERVO_STOP_POSITION);
  currentHourOnClock = 0; // We are now at position 00
}

void moveMinuteStep() {
  servoMinute.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_MINUTE);
  // Wait for the step switch to be pressed
  while (pcf8574.digitalRead(EXPANDER_PIN_STEP_MINUTE) == HIGH) { delay(10); }
  // Wait for the step switch to be released (prevents bouncing)
  while (pcf8574.digitalRead(EXPANDER_PIN_STEP_MINUTE) == LOW) { delay(10); }
  servoMinute.write(SERVO_STOP_POSITION);
  // Update internal state
  currentMinuteOnClock = (currentMinuteOnClock + 1) % 60;
}

void moveHourToNext() {
  // An hour change requires two physical steps on the 48-tooth gear
  performHourSingleStep();
  delay(100); // Small pause for mechanical stability
  performHourSingleStep();
  // Update internal state only after both steps are complete
  currentHourOnClock = (currentHourOnClock + 1) % 24;
}

void performHourSingleStep() {
  servoHour.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_HOUR);
  while (pcf8574.digitalRead(EXPANDER_PIN_STEP_HOUR) == HIGH) { delay(10); }
  while (pcf8574.digitalRead(EXPANDER_PIN_STEP_HOUR) == LOW) { delay(10); }
  servoHour.write(SERVO_STOP_POSITION);
}
