#ifndef SCALE_DRIVER_H
#define SCALE_DRIVER_H

#include <Arduino.h>
#include <HX711.h>
#include <Preferences.h> // ESP32 Non-Volatile Storage (NVS)

// --- Hardware Pin Definitions ---
#define HX711_DT_PIN  4
#define HX711_SCK_PIN 5

// --- Extended Diagnostic Payload ---
struct WeightReading {
    float rawGrams;             // Instantaneous raw reading (g)
    float smoothedKg;           // EWMA-filtered weight (kg)
    float stabilityRangeGrams;  // Rolling Max - Min range over 30 samples (g)
    bool isPresent;             // true if weight > 300g
    bool isLocked;              // true if rolling range <= threshold
    int stableProgress;         // Circular buffer fill count (0 to 30)
};

class ScaleDriver {
private:
    HX711 scale;
    Preferences prefs; // NVS instance for persistent calibration

    float calibrationFactor = 0.42f;       // Default fallback factor
    const float ALPHA = 0.15f;              // EWMA smoothing factor
    const float ACTIVATION_GRAMS = 300.0f;  // Presence threshold (300g)
    const float RANGE_THRESHOLD = 8.0f;     // Max allowable Max-Min delta (8g)

    // Rolling window buffer (30 samples @ 10Hz = 3 seconds)
    static const int BUFFER_SIZE = 30;
    float sampleBuffer[BUFFER_SIZE];
    int bufferIndex = 0;
    int bufferCount = 0;

    // Filter & Lock state variables
    float smoothedGrams = 0.0f;
    bool isLockedState = false;
    float lockedWeightKg = 0.0f;

    // Non-blocking timer
    unsigned long lastReadTime = 0;
    const unsigned long readInterval = 100; // 10Hz sampling rate (100ms)

    // Clear rolling window memory
    void resetBuffer() {
        bufferIndex = 0;
        bufferCount = 0;
        for (int i = 0; i < BUFFER_SIZE; i++) {
            sampleBuffer[i] = 0.0f;
        }
    }

public:
    void begin() {
        // 1. Hardware Initialization
        scale.begin(HX711_DT_PIN, HX711_SCK_PIN);

        // 2. Load calibration factor from ESP32 Flash NVS
        prefs.begin("ndm_scale", true); // Read-only mode
        calibrationFactor = prefs.getFloat("cal_factor", 0.42f);
        prefs.end();

        scale.set_scale(calibrationFactor);
        scale.tare(); // Zero scale on startup
        resetBuffer();
    }

    // Zero out scale and clear lock state
    void tare() {
        scale.tare();
        smoothedGrams = 0.0f;
        isLockedState = false;
        lockedWeightKg = 0.0f;
        resetBuffer();
    }

    // Remeasure command (e.g., triggered by '*' key on keypad)
    void unlock() {
        isLockedState = false;
        lockedWeightKg = 0.0f;
        resetBuffer();
    }

    // Save a new physical calibration factor to ESP32 Flash NVS
    void saveCalibrationFactor(float newFactor) {
        calibrationFactor = newFactor;
        scale.set_scale(calibrationFactor);

        prefs.begin("ndm_scale", false); // Read-Write mode
        prefs.putFloat("cal_factor", calibrationFactor);
        prefs.end();
        
        Serial.printf("💾 Saved new calibration factor to NVS: %.4f\n", calibrationFactor);
    }

    // Non-blocking update loop (called from main.ino at 10Hz)
    WeightReading read() {
        WeightReading reading;
        reading.rawGrams = 0.0f;
        reading.smoothedKg = 0.0f;
        reading.stabilityRangeGrams = 999.0f; // Default high value
        reading.isPresent = false;
        reading.isLocked = isLockedState;
        reading.stableProgress = bufferCount;

        // If weight is locked, return frozen measurement until unlocked or removed
        if (isLockedState) {
            reading.smoothedKg = lockedWeightKg;
            reading.isPresent = true;
            reading.stabilityRangeGrams = 0.0f;
            return reading;
        }

        unsigned long currentMillis = millis();
        if (currentMillis - lastReadTime >= readInterval) {
            lastReadTime = currentMillis;

            if (!scale.is_ready()) {
                return reading;
            }

            // 1. Fetch raw units
            float raw = scale.get_units(1);
            if (raw < 0.0f) raw = 0.0f;
            reading.rawGrams = raw;

            // 2. Presence Check
            if (raw < ACTIVATION_GRAMS) {
                smoothedGrams = 0.0f;
                isLockedState = false;
                resetBuffer();
                return reading; // isPresent remains false
            }

            reading.isPresent = true;

            // 3. EWMA Filtering
            if (smoothedGrams == 0.0f) {
                smoothedGrams = raw; // First sample initialization
            } else {
                smoothedGrams = (ALPHA * raw) + ((1.0f - ALPHA) * smoothedGrams);
            }

            // 4. Rolling Circular Buffer Update
            sampleBuffer[bufferIndex] = raw; // Push raw reading to monitor motion
            bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;
            if (bufferCount < BUFFER_SIZE) {
                bufferCount++;
            }
            reading.stableProgress = bufferCount;

            // 5. Compute Rolling Window Min, Max, and Range
            float minVal = sampleBuffer[0];
            float maxVal = sampleBuffer[0];
            for (int i = 1; i < bufferCount; i++) {
                if (sampleBuffer[i] < minVal) minVal = sampleBuffer[i];
                if (sampleBuffer[i] > maxVal) maxVal = sampleBuffer[i];
            }
            float range = maxVal - minVal;
            reading.stabilityRangeGrams = range;

            // 6. Evaluate Stability Lock
            // Requires a full buffer (30 samples) AND range <= 8g
            if (bufferCount >= BUFFER_SIZE && range <= RANGE_THRESHOLD) {
                isLockedState = true;
                lockedWeightKg = smoothedGrams / 1000.0f; // Freeze value in kg
                reading.isLocked = true;
                reading.smoothedKg = lockedWeightKg;
            } else {
                reading.smoothedKg = smoothedGrams / 1000.0f;
            }
        }

        return reading;
    }
};

#endif // SCALE_DRIVER_H