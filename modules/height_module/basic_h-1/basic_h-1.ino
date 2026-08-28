/*
 * Project: NEO DIMETRIX (NDM) - Standalone ToF Height Module
 * Hardware: ESP32-S3 + VL53L0X Time-of-Flight Distance Sensor
 * 
 * Pin Connections:
 *   - VL53L0X VCC  -> ESP32-S3 3V3
 *   - VL53L0X GND  -> ESP32-S3 GND
 *   - VL53L0X SDA  -> ESP32-S3 GPIO 8
 *   - VL53L0X SCL  -> ESP32-S3 GPIO 9
 */

#include <Wire.h>
#include <Adafruit_VL53L0X.h>

// I2C Pin Definitions for ESP32-S3
#define SDA_PIN  8
#define SCL_PIN  9

// Stadiometer Frame Config (800mm = 80cm total bed length)
const int BED_LENGTH_MM = 800;

Adafruit_VL53L0X lox = Adafruit_VL53L0X();

// Non-blocking timer variables
unsigned long lastReadTime = 0;
const unsigned long readInterval = 500; // Read distance every 500 ms

void setup() {
  Serial.begin(115200);

  Serial.println("\n=====================================");
  Serial.println("   NEO DIMETRIX (NDM) - ToF Test Node ");
  Serial.println("=====================================");

  // 1. Initialize Hardware I2C on GPIO 8 (SDA) and GPIO 9 (SCL)
  Wire.begin(SDA_PIN, SCL_PIN);

  // 2. Initialize VL53L0X ToF Sensor
  if (!lox.begin()) {
    Serial.println("[NDM Error]: VL53L0X sensor not detected!");
    Serial.println("Please check power (3V3/GND) and I2C lines (GPIO 8/9).");
    while (1); // Stop execution if sensor initialization fails
  }

  Serial.println("[NDM]: VL53L0X ToF Sensor Online & Ready!");
}

void loop() {
  unsigned long currentMillis = millis(); // Non-blocking check

  if (currentMillis - lastReadTime >= readInterval) {
    lastReadTime = currentMillis;

    VL53L0X_RangingMeasurementData_t measure;
    lox.rangingTest(&measure, false); // Perform single laser ranging scan

    // RangeStatus != 4 means a valid photon reflection was returned
    if (measure.RangeStatus != 4) {
      int distanceMm = measure.RangeMilliMeter;

      // Stadiometer Math: Infant Length = Bed Length - Distance to Footboard
      int infantLengthMm = BED_LENGTH_MM - distanceMm;
      if (infantLengthMm < 0) infantLengthMm = 0; // Clamp negative values

      float infantLengthCm = infantLengthMm / 10.0; // Convert mm to cm

      // Serial Output
      Serial.print("[NDM ToF]: Sensor Distance: ");
      Serial.print(distanceMm);
      Serial.print(" mm  |  Infant Length: ");
      Serial.print(infantLengthCm, 1);
      Serial.println(" cm");

    } else {
      Serial.println("[NDM Warning]: Target out of range or signal blocked!");
    }
  }

  // CPU remains 100% free here
}