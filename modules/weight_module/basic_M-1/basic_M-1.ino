#include <HX711.h>

#define DT_PIN  4
#define SCK_PIN 5

HX711 scale;

// Non-blocking timer variables
unsigned long lastReadTime = 0;
const unsigned long readInterval = 500; // Read every 500 milliseconds

void setup() {
  Serial.begin(115200);
  
  Serial.println("Initializing HX711 Scale...");
  scale.begin(DT_PIN, SCK_PIN);
  
  // Zero out the scale on startup
  scale.tare();
  Serial.println("Scale zeroed! Press on the load cell with your thumb...");
}

void loop() {
  unsigned long currentMillis = millis(); // Grab current internal stopwatch time

  // Check if 500 ms have passed since the last reading
  if (currentMillis - lastReadTime >= readInterval) {
    lastReadTime = currentMillis; // Reset timer mark

    if (scale.is_ready()) {
      long rawValue = scale.read(); // Reads raw 24-bit integer
      Serial.print("Raw Scale Reading: ");
      Serial.println(rawValue);
    } else {
      Serial.println("HX711 not ready — check wiring!");
    }
  }

  // Your CPU is 100% free here to run other tasks without freezing!
}