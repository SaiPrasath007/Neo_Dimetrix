#include <Arduino.h>
#include <HX711.h>

// --- Hardware Pin Definitions ---
#define DT_PIN  4
#define SCK_PIN 5

HX711 scale;

// 0.42 converts Wokwi raw units directly to grams
float calibrationFactor = 0.42;

// --- EWMA Filter & Lock Constants ---
const float ALPHA = 0.15;              // Smoothing factor (15% raw + 85% smoothed)
const float ACTIVATION_GRAMS = 300.0;  // 300g presence threshold
const float STABILITY_DELTA = 5.0;     // 5g max variation allowed for stability lock
const int REQUIRED_SAMPLES = 30;       // 30 samples at 10Hz = 3 seconds of stability

// --- Filter State Variables ---
float smoothedGrams = 0.0;
int stableCount = 0;
bool isLocked = false;

// --- Timing Variables ---
unsigned long lastReadTime = 0;
const unsigned long readInterval = 100; // Sample every 100ms (10Hz sampling rate)

void setup() {
  Serial.begin(115200);

  Serial.println("\n=====================================");
  Serial.println("       NEO DIMETRIX (NDM) v1.0         ");
  Serial.println("   Smart Newborn Scale EWMA Engine     ");
  Serial.println("=====================================");

  scale.begin(DT_PIN, SCK_PIN);
  scale.set_scale(calibrationFactor);
  scale.tare(); // Zero scale on startup

  Serial.println("[NDM Setup]: Scale zeroed and EWMA engine active.");
}

void loop() {
  unsigned long currentMillis = millis();

  // Non-blocking timer running at 10Hz
  if (currentMillis - lastReadTime >= readInterval) {
    lastReadTime = currentMillis;

    if (!scale.is_ready()) {
      Serial.println("[NDM Warning]: HX711 not ready!");
      return;
    }

    // 1. Fetch raw reading in grams
    float rawGrams = scale.get_units(1);
    if (rawGrams < 0.0) rawGrams = 0.0;

    // 2. Presence Check: Wait for weight > 300g
    if (rawGrams < ACTIVATION_GRAMS) {
      smoothedGrams = 0.0;
      stableCount = 0;
      isLocked = false;
      Serial.print("[NDM Status]: WAITING | Raw: ");
      Serial.print(rawGrams, 1);
      Serial.println("g | Place baby on scale (>300g)...");
      return;
    }

    // 3. EWMA Recursive Equation: S_t = (ALPHA * X_t) + ((1 - ALPHA) * S_{t-1})
    if (smoothedGrams == 0.0) {
      smoothedGrams = rawGrams; // First sample initialization (S_0 = X_0)
    } else {
      smoothedGrams = (ALPHA * rawGrams) + ((1.0 - ALPHA) * smoothedGrams);
    }

    // 4. Calculate Absolute Delta
    float delta = abs(rawGrams - smoothedGrams);

    // 5. Evaluate Stability Counter
    if (delta <= STABILITY_DELTA) {
      if (stableCount < REQUIRED_SAMPLES) {
        stableCount++;
      }
    } else {
      stableCount = 0; // Reset counter on movement spikes > 5g
    }

    // 6. Check Lock Condition
    if (stableCount >= REQUIRED_SAMPLES) {
      isLocked = true;
    }

    // 7. Format Output Metrics for Serial Output
    long totalGrams = (long)(smoothedGrams + 0.5);
    long kg = totalGrams / 1000;
    long g  = totalGrams % 1000;

    Serial.print("[NDM Telemetry] Raw: ");
    Serial.print(rawGrams, 1);
    Serial.print("g | Smoothed: ");
    Serial.print(kg);
    Serial.print("kg ");
    Serial.print(g);
    Serial.print("g | Stability: ");
    Serial.print(stableCount);
    Serial.print("/");
    Serial.print(REQUIRED_SAMPLES);
    Serial.print(" | Status: ");
    Serial.println(isLocked ? "LOCKED 🔒" : "WEIGHING... ⏳");
  }
}