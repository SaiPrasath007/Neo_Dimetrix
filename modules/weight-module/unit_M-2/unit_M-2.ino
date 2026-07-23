#include <HX711.h>

#define DT_PIN  4
#define SCK_PIN 5

HX711 scale;

// 0.42 converts Wokwi raw units directly to grams (instead of kg)
float calibrationFactor = 0.42;

unsigned long lastReadTime = 0;
const unsigned long readInterval = 500;

void setup() {
  Serial.begin(115200);

  Serial.println("\n=====================================");
  Serial.println("      NEO DIMETRIX (NDM) v1.0         ");
  Serial.println("   Smart Newborn Telemetry System     ");
  Serial.println("=====================================");

  scale.begin(DT_PIN, SCK_PIN);
  scale.set_scale(calibrationFactor);
  scale.tare(); // Zero scale on startup

  Serial.println("[NDM]: Scale zeroed and ready.");
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastReadTime >= readInterval) {
    lastReadTime = currentMillis;

    if (scale.is_ready()) {
      float rawGrams = scale.get_units(1);
      if (rawGrams < 0.0) rawGrams = 0.0;

      long totalGrams = (long)(rawGrams + 0.5);
      long kg = totalGrams / 1000;
      long g  = totalGrams % 1000;

      Serial.print("[NDM Data]: ");
      Serial.print(kg);
      Serial.print("kg ");
      Serial.print(g);
      Serial.println("g");
    } else {
      Serial.println("[NDM Warning]: HX711 not ready!");
    }
  }
}