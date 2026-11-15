#include "Wire.h"      
#include "I2Cdev.h"    
#include "MPU6050.h"   

// ==========================================
// 1. PIN DEFINITIONS
// ==========================================
const int FSR_PIN = A0;       // FSR 402 connected to A0 (with 1kΩ pull-down to GND)
const int ALERT_LED = 13;     // Onboard LED or active buzzer

// ==========================================
// 2. FSR 402 & SEDENTARY TIMER CONFIGURATION
// ==========================================
const int HEAD_RESTING_THRESHOLD = 700; // Trigger "Wearing" above this value
const int HEAD_REMOVED_THRESHOLD = 600; // Trigger "Empty" below this value

const unsigned long MAX_SITTING_DURATION = 30000; // Max sitting duration before beep (30s for presentation)
unsigned long sittingStartTime = 0;
bool sedentaryAlertActive = false;

// Pulsing beep timing for sedentary alert
const unsigned long BEEP_INTERVAL = 400; // 400ms ON, 400ms OFF
unsigned long lastBeepToggle = 0;
bool beepState = false;

const int NUM_READINGS = 10;
int readings[NUM_READINGS];
int readIndex = 0;
int total = 0;
int averageFSR = 0;
bool isWearingPillow = false;

// ==========================================
// 3. MPU-6050 CONFIGURATION
// ==========================================
MPU6050 mpu;
int16_t ax, ay, az;
int16_t gx, gy, gz;

int16_t ax_offset = 0;
int16_t ay_offset = 0;
int16_t az_offset = 0;

// Tilt thresholds (0 - 255 mapped scale)
const byte TILT_ALERT_THRESHOLD = 50;    // X and Y threshold
const byte Z_TILT_ALERT_THRESHOLD = 100; // Z threshold

struct TiltData {
  byte X;
  byte Y;
  byte Z;
};
TiltData tilt;

void setup() {
  Serial.begin(9600);
  Wire.begin();

  pinMode(ALERT_LED, OUTPUT);
  digitalWrite(ALERT_LED, LOW);

  // Initialize FSR moving average buffer
  for (int i = 0; i < NUM_READINGS; i++) {
    readings[i] = 0;
  }

  // Initialize MPU-6050
  Serial.println("Initializing Wellness Neck Pillow...");
  mpu.initialize();
  
  if (!mpu.testConnection()) {
    Serial.println("ERROR: MPU-6050 not found! Check SDA (A4) and SCL (A5) wiring.");
    while (1);
  }

  // Keep pillow still for 1 second to calibrate resting baseline
  delay(1000);
  int32_t sum_ax = 0, sum_ay = 0, sum_az = 0;
  const int samples = 50;

  for (int i = 0; i < samples; i++) {
    mpu.getAcceleration(&ax, &ay, &az);
    sum_ax += ax;
    sum_ay += ay;
    sum_az += az;
    delay(5);
  }

  ax_offset = sum_ax / samples;
  ay_offset = sum_ay / samples;
  az_offset = sum_az / samples;

  Serial.println("Calibration complete! Ready for use.");
}

void loop() {
  unsigned long currentMillis = millis();

  // ------------------------------------------
  // STEP 1: READ & FILTER FSR PRESSURE
  // ------------------------------------------
  total = total - readings[readIndex];
  readings[readIndex] = analogRead(FSR_PIN);
  total = total + readings[readIndex];
  readIndex = (readIndex + 1) % NUM_READINGS;
  averageFSR = total / NUM_READINGS;

  if (!isWearingPillow && averageFSR > HEAD_RESTING_THRESHOLD) {
    isWearingPillow = true;
    sittingStartTime = currentMillis; // Record exact time user sat down
    sedentaryAlertActive = false;
    beepState = false;
    Serial.println("[STATUS] Pillow Worn: ACTIVE (30s Timer Started)");
  } 
  else if (isWearingPillow && averageFSR < HEAD_REMOVED_THRESHOLD) {
    isWearingPillow = false;
    sedentaryAlertActive = false; // Reset sedentary alert once user gets up
    beepState = false;
    digitalWrite(ALERT_LED, LOW);
    Serial.println("[STATUS] Pillow Removed: STANDBY (Timer Reset)");
  }

  // Check if user has been sitting continuously for 30 seconds
  if (isWearingPillow && !sedentaryAlertActive) {
    if (currentMillis - sittingStartTime >= MAX_SITTING_DURATION) {
      sedentaryAlertActive = true;
      lastBeepToggle = currentMillis;
      beepState = true;
    }
  }

  // ------------------------------------------
  // STEP 2: READ & COMPUTE RELATIVE HEAD TILT
  // ------------------------------------------
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  int16_t diff_x = ax - ax_offset;
  int16_t diff_y = ay - ay_offset;
  int16_t diff_z = az - az_offset;

  tilt.X = map(constrain(abs(diff_x), 0, 17000), 0, 17000, 0, 255);
  tilt.Y = map(constrain(abs(diff_y), 0, 17000), 0, 17000, 0, 255);
  tilt.Z = map(constrain(abs(diff_z), 0, 17000), 0, 17000, 0, 255);

  // ------------------------------------------
  // STEP 3: POSTURE & WELLNESS LOGIC
  // ------------------------------------------
  bool excessiveTilt = (tilt.X > TILT_ALERT_THRESHOLD || 
                        tilt.Y > TILT_ALERT_THRESHOLD || 
                        tilt.Z > Z_TILT_ALERT_THRESHOLD);

  if (isWearingPillow && sedentaryAlertActive) {
    // Priority 1: 30-second timer exceeded -> Pausing beeps (400ms ON / 400ms OFF)
    if (currentMillis - lastBeepToggle >= BEEP_INTERVAL) {
      lastBeepToggle = currentMillis;
      beepState = !beepState;
    }
    digitalWrite(ALERT_LED, beepState ? HIGH : LOW);
    Serial.print("[ALERT: STAND UP - 30s LIMIT REACHED] ");
  } 
  else if (isWearingPillow && excessiveTilt) {
    // Priority 2: Poor posture -> Solid continuous beep
    digitalWrite(ALERT_LED, HIGH);
    Serial.print("[ALERT: EXCESSIVE HEAD TILT] ");
  } 
  else {
    digitalWrite(ALERT_LED, LOW);
    Serial.print(isWearingPillow ? "[POSTURE OK] " : "[STANDBY] ");
  }

  // ------------------------------------------
  // STEP 4: SERIAL TELEMETRY OUTPUT
  // ------------------------------------------
  Serial.print("FSR: "); Serial.print(averageFSR);
  if (isWearingPillow) {
    Serial.print(" | Time: ");
    Serial.print((currentMillis - sittingStartTime) / 1000);
    Serial.print("s");
  }
  Serial.print(" | Tilt X: "); Serial.print(tilt.X);
  Serial.print(" | Tilt Y: "); Serial.print(tilt.Y);
  Serial.print(" | Tilt Z: "); Serial.println(tilt.Z);

  // delay(200);
}