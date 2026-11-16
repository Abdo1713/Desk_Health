#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ==========================================
// 1. PIN DEFINITIONS & OBJECTS
// ==========================================
// I2C LCD: Address is usually 0x27 (change to 0x3F if screen stays blank)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// DHT11 Setup
#define DHTPIN 2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// HC-SR04 Ultrasonic Setup
const int TRIG_PIN = 9;
const int ECHO_PIN = 10;

// ==========================================
// 2. WARNING THRESHOLDS
// ==========================================
const float TEMP_MAX_THRESHOLD = 27.0; // Warning if temperature > 27.0°C
const float HUM_MAX_THRESHOLD  = 75.0; // Warning if humidity > 75%
const long  DIST_MIN_THRESHOLD = 20;   // Warning if distance < 20 cm (adjust as needed)

// ==========================================
// 3. TIMING & SENSOR VARIABLES
// ==========================================
unsigned long lastDHTRead = 0;
const unsigned long DHT_INTERVAL = 2000; // DHT11 updates every 2 seconds

float temperature = 0.0;
float humidity = 0.0;
long distanceCm = 0;

void setup() {
  delay(500); // Stabilization delay for external power supplies

  Serial.begin(9600);
  Wire.begin();
  Wire.setWireTimeout(3000, true); // Prevent I2C lockups

  // Initialize Ultrasonic pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Initialize DHT11
  dht.begin();

  // Initialize I2C LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("System 2 Ready");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(1500);
  lcd.clear();
}

void loop() {
  unsigned long currentMillis = millis();

  // ------------------------------------------
  // STEP 1: READ ULTRASONIC DISTANCE (cm)
  // ------------------------------------------
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Timeout after 30,000 µs (~500 cm max range) so loop never hangs
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration > 0) {
    distanceCm = duration * 0.0343 / 2;
  }

  delay(10); // Let power rail settle after ultrasonic ping before I2C write

  // ------------------------------------------
  // STEP 2: READ DHT11 (Every 2 Seconds)
  // ------------------------------------------
  if (currentMillis - lastDHTRead >= DHT_INTERVAL) {
    lastDHTRead = currentMillis;

    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (!isnan(h) && !isnan(t)) {
      humidity = h;
      temperature = t;
    } else {
      Serial.println("DHT11 read failed!");
    }
  }

  // ------------------------------------------
  // STEP 3: EVALUATE WARNING CONDITIONS
  // ------------------------------------------
  bool highTemp = (temperature > TEMP_MAX_THRESHOLD);
  bool highHum  = (humidity > HUM_MAX_THRESHOLD);
  bool tooClose = (distanceCm > 0 && distanceCm < DIST_MIN_THRESHOLD);

  // ------------------------------------------
  // STEP 4: UPDATE 16x2 I2C LCD DISPLAY
  // ------------------------------------------
  // Row 0: Temperature & Humidity (or Temp/Hum Warning)
  lcd.setCursor(0, 0);
  if (highTemp && highHum) {
    lcd.print("WARN:TEMP & HUM!");
  } else if (highTemp) {
    lcd.print("WARN: HIGH TEMP!");
  } else if (highHum) {
    lcd.print("WARN: HIGH HUM! ");
  } else {
    lcd.print("T:");
    lcd.print(temperature, 1);
    lcd.print((char)223); // Degree symbol (°)
    lcd.print("C H:");
    lcd.print((int)humidity);
    lcd.print("%   ");
  }

  // Row 1: Ultrasonic Distance (or Proximity Warning)
  lcd.setCursor(0, 1);
  if (tooClose) {
    lcd.print("WARN: TOO CLOSE!");
  } else {
    lcd.print("Dist: ");
    lcd.print(distanceCm);
    lcd.print(" cm     "); // Trailing spaces clear old digits
  }

  // ------------------------------------------
  // STEP 5: SERIAL TELEMETRY
  // ------------------------------------------
  if (Serial.availableForWrite() > 32) {
    Serial.print("Temp: "); Serial.print(temperature, 1);
    Serial.print(" C | Hum: "); Serial.print(humidity, 1);
    Serial.print(" % | Dist: "); Serial.print(distanceCm);
    Serial.println(" cm");
  }

  delay(250);
}