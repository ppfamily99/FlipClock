/**
 * @file Arduino_WiFi_FlipClock_with_comment.ino
 * @author PPFamily99
 * @brief Firmware for the WiFi-enabled version of the 3D Printed Flip Clock.
 * @version 1.1
 * @details This version connects to a WiFi network to fetch the current time from an NTP server.
 * It does not require an RTC module or any manual input buttons.
 *
 * Hardware Dependencies:
 * - ESP8266 (NodeMCU, WEMOS D1 Mini, etc.)
 * - 2x SG90 Servos (Modified for 360-degree continuous rotation)
 * - 4x Limit Switches
 * - External 5V Power Supply for Servos
 */

// --- LIBRARIES ---
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Servo.h>

// --- CONFIGURATION SECTION (NEEDS USER ADJUSTMENT) ---

// 1. WiFi Network Credentials
const char* ssid     = "SSID_Name";      // <-- Enter your WiFi network name (SSID) here
const char* password = "WiFi_PASSWORD";  // <-- Enter your WiFi password here

// 2. GPIO Pin Assignments
// Please ensure these match your physical wiring.
#define PIN_SERVO_MINUTE       2  // D4 on NodeMCU
#define PIN_SERVO_HOUR         0  // D3 on NodeMCU

#define PIN_HOME_SWITCH_MINUTE 4  // D2 on NodeMCU
#define PIN_STEP_SWITCH_MINUTE 13 // D7 on NodeMCU
#define PIN_HOME_SWITCH_HOUR   14 // D5 on NodeMCU
#define PIN_STEP_SWITCH_HOUR   12 // D6 on NodeMCU

// 3. Servo Calibration Parameters (CRITICAL - MUST BE TUNED)
// The pulse value (in microseconds) that makes the servo stop completely.
// 90 is the standard default, but you may need to fine-tune it (e.g., 89, 91).
#define SERVO_STOP_POSITION   90

// The offset from the STOP position to make the servo rotate.
// This controls both speed and direction. A larger value means faster rotation.
// You MUST use opposite signs for opposite rotation directions.
#define SERVO_MOVE_OFFSET_MINUTE  30  // e.g., move forward with a positive value
#define SERVO_MOVE_OFFSET_HOUR   -30  // e.g., move forward with a negative value (reversed)

// 4. NTP (Network Time Protocol) Settings
const char* ntpServer = "time.google.com";
// Set your timezone offset from GMT in seconds.
// Example: GMT+7 (Thailand) = 7 * 3600 = 25200
const long  gmtOffset_sec = 7 * 3600;
// Daylight Saving Time offset in seconds (usually 0 or 3600).
const int   daylightOffset_sec = 0;


// --- GLOBAL OBJECTS AND VARIABLES ---

// Initialize NTP client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, gmtOffset_sec, daylightOffset_sec);

// Initialize Servo objects
Servo servoMinute;
Servo servoHour;

// Global variables to track the clock's physical display state
int currentMinuteOnClock = -1; // -1 means the position is unknown until homed
int currentHourOnClock = -1;


// --- SETUP FUNCTION ---
// This runs once when the ESP8266 boots up.
void setup() {
  Serial.begin(115200);
  Serial.println("\nStarting WiFi Flip Clock...");

  // Configure the limit switch pins as inputs with internal pull-up resistors.
  // This means the pin is HIGH by default and goes LOW when the switch is pressed.
  pinMode(PIN_HOME_SWITCH_MINUTE, INPUT_PULLUP);
  pinMode(PIN_STEP_SWITCH_MINUTE, INPUT_PULLUP);
  pinMode(PIN_HOME_SWITCH_HOUR, INPUT_PULLUP);
  pinMode(PIN_STEP_SWITCH_HOUR, INPUT_PULLUP);

  // Attach servo objects to their assigned GPIO pins
  servoMinute.attach(PIN_SERVO_MINUTE);
  servoHour.attach(PIN_SERVO_HOUR);
  // Command servos to stop immediately on startup
  servoMinute.write(SERVO_STOP_POSITION);
  servoHour.write(SERVO_STOP_POSITION);

  // Establish WiFi connection
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected successfully!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Initialize the NTP client
  timeClient.begin();

  // --- CRITICAL STARTUP SEQUENCE ---
  // 1. Home both mechanisms to find the "00" position.
  Serial.println("Starting Homing Sequence...");
  goHomeMinute();
  goHomeHour();
  Serial.println("Homing Complete! Display is now at 00:00.");

  // 2. Fetch the initial time and sync the display.
  Serial.println("Fetching initial time from NTP server...");
  while(!timeClient.update()) {
    timeClient.forceUpdate();
  }
  Serial.print("Initial time fetched: ");
  Serial.println(timeClient.getFormattedTime());
}


// --- MAIN LOOP ---
// This function runs repeatedly after setup() is complete.
void loop() {
  // Update the time from the NTP server
  timeClient.update();

  int actualMinute = timeClient.getMinutes();
  int actualHour = timeClient.getHours();
  
  // --- Minute Logic ---
  // If the physical minute display does not match the actual time, move it.
  if (currentMinuteOnClock != actualMinute) {
    Serial.printf("Minute mismatch! Clock: %02d, NTP: %02d. Moving...\n", currentMinuteOnClock, actualMinute);
    moveMinuteStep();
  }

  // --- Hour Logic ---
  // If the physical hour display does not match the actual time, move it.
  if (currentHourOnClock != actualHour) {
    Serial.printf("Hour mismatch! Clock: %02d, NTP: %02d. Moving to next hour...\n", currentHourOnClock, actualHour);
    moveHourToNext();
  }

  // Wait a second before checking the time again.
  delay(1000);
}


// --- HOMING FUNCTIONS ---

// Moves the minute mechanism until its home switch is triggered.
void goHomeMinute() {
  Serial.println("Homing minute dial...");
  // While the home switch is not pressed (pin is HIGH)...
  while (digitalRead(PIN_HOME_SWITCH_MINUTE) == HIGH) {
    // ...keep rotating the servo.
    servoMinute.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_MINUTE);
    delay(200); // A small delay to allow movement
  }
  servoMinute.write(SERVO_STOP_POSITION); // Stop the servo
  currentMinuteOnClock = 0; // We are now at position "00"
  Serial.println("Minute dial is at HOME (00)");
  delay(500); // Pause for stability
}

// Moves the hour mechanism until its home switch is triggered.
void goHomeHour() {
  Serial.println("Homing hour dial...");
  while (digitalRead(PIN_HOME_SWITCH_HOUR) == HIGH) {
    servoHour.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_HOUR);
    delay(200);
  }
  servoHour.write(SERVO_STOP_POSITION);
  currentHourOnClock = 0; // We are now at position "00"
  Serial.println("Hour dial is at HOME (00)");
  delay(500);
}


// --- MOVEMENT FUNCTIONS ---

// Moves the minute mechanism forward by exactly one step.
void moveMinuteStep() {
  servoMinute.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_MINUTE);
  // Wait for the step switch to be pressed (signal goes LOW)
  while (digitalRead(PIN_STEP_SWITCH_MINUTE) == HIGH) { delay(10); }
  // Wait for the step switch to be released (signal goes HIGH) - for debouncing
  while (digitalRead(PIN_STEP_SWITCH_MINUTE) == LOW) { delay(10); }
  servoMinute.write(SERVO_STOP_POSITION);
  
  // Update the internal state variable
  currentMinuteOnClock = (currentMinuteOnClock + 1) % 60; // Wraps from 59 back to 0
  Serial.printf("Minute moved to: %02d\n", currentMinuteOnClock);
}

// Performs a single physical step of the hour mechanism.
void performHourSingleStep() {
  servoHour.write(SERVO_STOP_POSITION + SERVO_MOVE_OFFSET_HOUR);
  while (digitalRead(PIN_STEP_SWITCH_HOUR) == HIGH) { delay(10); }
  while (digitalRead(PIN_STEP_SWITCH_HOUR) == LOW) { delay(10); }
  servoHour.write(SERVO_STOP_POSITION);
  Serial.println("Hour moved 1 step.");
}

// Moves the hour mechanism to the next hour (which requires 2 steps).
void moveHourToNext() {
  Serial.println("Performing 2 steps for next hour...");
  performHourSingleStep(); // Move step 1
  delay(100);              // Short delay for mechanical stability
  performHourSingleStep(); // Move step 2
  
  // Update the internal state variable
  currentHourOnClock = (currentHourOnClock + 1) % 24; // Wraps from 23 back to 0
  Serial.printf("Hour display is now: %02d\n", currentHourOnClock);
}
