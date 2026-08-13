#ifndef LENGTH_DRIVER_H
#define LENGTH_DRIVER_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <Preferences.h> // ESP32 Non-Volatile Storage (NVS)
#include <algorithm>     // For std::sort (median filtering)

// --- ToF Diagnostic Payload ---
struct LengthReading {
    float rawDistanceMm;       // Instantaneous distance measured by laser (mm)
    float smoothedLengthCm;    // Median-filtered infant length (cm)
    float stabilityRangeMm;    // Max - Min delta over window (mm)
    bool isValid;              // true if laser reflection AND physical bounds valid
    bool isLocked;             // true if length is stable
    int stableProgress;        // Circular buffer fill count (0 to 10)
};

class LengthDriver {
private:
    Adafruit_VL53L0X lox = Adafruit_VL53L0X();
    Preferences prefs;

    // Physical WHO & Bed Limits
    const float MIN_CLINICAL_LENGTH_MM = 450.0f; // 45.0 cm (WHO minimum WLZ limit)
    int bedLengthMm = 800;                       // Maximum physical frame limit (80.0 cm)
    const float RANGE_THRESHOLD_MM = 5.0f;       // Max allowed jitter (5 mm)

    // Rolling window buffer (10 samples @ 5Hz = 2 seconds)
    static const int BUFFER_SIZE = 10;
    float sampleBuffer[BUFFER_SIZE];
    int bufferIndex = 0;
    int bufferCount = 0;

    // Filter & Lock state
    bool isLockedState = false;
    float lockedLengthCm = 0.0f;

    // Non-blocking timer (5Hz sampling rate = 200ms)
    unsigned long lastReadTime = 0;
    const unsigned long readInterval = 200; 

    void resetBuffer() {
        bufferIndex = 0;
        bufferCount = 0;
        for (int i = 0; i < BUFFER_SIZE; i++) {
            sampleBuffer[i] = 0.0f;
        }
    }

public:
    bool begin() {
        // Wire.begin() is handled centrally by main.ino

        // Initialize VL53L0X ToF Sensor over shared I2C bus
        if (!lox.begin()) {
            Serial.println("❌ VL53L0X ToF Sensor NOT detected!");
            return false;
        }

        // Load Bed Length limit calibration from NVS Flash
        prefs.begin("ndm_tof", true); // Read-only mode
        bedLengthMm = prefs.getInt("bed_len_mm", 800);
        prefs.end();

        resetBuffer();
        Serial.printf("✅ VL53L0X Online (Headboard Mode). Valid Range: 45.0 cm to %.1f cm\n", (float)bedLengthMm / 10.0f);
        return true;
    }

    // Remeasure command (unlock frozen reading)
    void unlock() {
        isLockedState = false;
        lockedLengthCm = 0.0f;
        resetBuffer();
    }

    // Save a new physical bed frame limit to NVS Flash
    void saveBedLength(int newBedLengthMm) {
        bedLengthMm = newBedLengthMm;

        prefs.begin("ndm_tof", false); // Read-Write mode
        prefs.putInt("bed_len_mm", bedLengthMm);
        prefs.end();

        Serial.printf("💾 Saved new Bed Length Limit to NVS: %d mm\n", bedLengthMm);
    }

    // Non-blocking update loop (called from main.ino at 5Hz)
    LengthReading read() {
        LengthReading reading;
        reading.rawDistanceMm = 0.0f;
        reading.smoothedLengthCm = 0.0f;
        reading.stabilityRangeMm = 999.0f;
        reading.isValid = false;
        reading.isLocked = isLockedState;
        reading.stableProgress = bufferCount;

        // Return frozen measurement if locked
        if (isLockedState) {
            reading.smoothedLengthCm = lockedLengthCm;
            reading.isValid = true;
            reading.stabilityRangeMm = 0.0f;
            return reading;
        }

        unsigned long currentMillis = millis();
        if (currentMillis - lastReadTime >= readInterval) {
            lastReadTime = currentMillis;

            VL53L0X_RangingMeasurementData_t measure;
            lox.rangingTest(&measure, false);

            // 1. Hardware Status Gate (RangeStatus != 4 check)
            if (measure.RangeStatus != 4) {
                float distanceMm = (float)measure.RangeMilliMeter;
                reading.rawDistanceMm = distanceMm;

                // 2. Strict Physical Boundary Check (45.0 cm <= distance <= bedLengthMm)
                // NO silent clamping: Out-of-bounds readings invalidate the measurement
                if (distanceMm < MIN_CLINICAL_LENGTH_MM || distanceMm > (float)bedLengthMm) {
                    reading.isValid = false;
                    resetBuffer(); // Purge stale buffer data
                    return reading;
                }

                reading.isValid = true;

                // 3. Push valid reading to circular buffer
                sampleBuffer[bufferIndex] = distanceMm;
                bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;
                if (bufferCount < BUFFER_SIZE) {
                    bufferCount++;
                }
                reading.stableProgress = bufferCount;

                // 4. Sort buffer copy to calculate median (eliminates optical noise)
                float sortedBuffer[BUFFER_SIZE];
                for (int i = 0; i < bufferCount; i++) {
                    sortedBuffer[i] = sampleBuffer[i];
                }
                std::sort(sortedBuffer, sortedBuffer + bufferCount);

                float medianDistanceMm = sortedBuffer[bufferCount / 2];
                float range = sortedBuffer[bufferCount - 1] - sortedBuffer[0];
                reading.stabilityRangeMm = range;

                // 5. Final Clinical Length Calculation
                float calculatedLengthCm = medianDistanceMm / 10.0f; // Convert mm to cm

                // 6. Evaluate Stability Lock (Full 10-sample buffer + Range <= 5mm)
                if (bufferCount >= BUFFER_SIZE && range <= RANGE_THRESHOLD_MM) {
                    isLockedState = true;
                    lockedLengthCm = calculatedLengthCm;
                    reading.isLocked = true;
                    reading.smoothedLengthCm = lockedLengthCm;
                } else {
                    reading.smoothedLengthCm = calculatedLengthCm;
                }
            } else {
                // Sensor reflection invalid or target out of range
                reading.isValid = false;
                resetBuffer();
            }
        }

        return reading;
    }
};

#endif // LENGTH_DRIVER_H