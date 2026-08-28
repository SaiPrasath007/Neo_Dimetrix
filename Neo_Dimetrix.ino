#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

// --- SUB-SYSTEM DRIVERS ---
#include "modules/keypad_module/keypad_driver.h"
#include "modules/weight_module/scale_driver.h"
#include "modules/height_module/height_driver.h"
#include "modules/display_module/ui_engine.h"
#include "modules/sd_module/sd_driver.h"
#include "modules/cloud_module/cloud_client.h"
#include "processing_units/who_diagnostics/who_diagnostics.h"

// --- HARDWARE PIN ASSIGNMENTS (ESP32-S3) ---
#define I2C_SDA_PIN         8
#define I2C_SCL_PIN         9

// MicroSD SPI Pins (ESP32-S3 Default Hardware SPI)
#define SD_CS_PIN           10
#define SD_MOSI_PIN         11
#define SD_SCK_PIN          12
#define SD_MISO_PIN         13

// Load Cell HX711 Pins
#define HX711_DOUT_PIN      4
#define HX711_SCK_PIN       6

// 4x4 Matrix Keypad Pinout
const uint8_t KEYPAD_ROW_PINS[4] = {1, 2, 42, 41};
const uint8_t KEYPAD_COL_PINS[4] = {40, 39, 38, 37};

// --- NETWORK & CLOUD CREDENTIALS ---
const char* WIFI_SSID     = "Parthasarathi_5G";
const char* WIFI_PASS     = "rajasekar";
const char* CLOUD_API_URL = "https://webhook.site/mock-test";
const char* CLOUD_API_KEY = "dummy_key_2026";

// --- SYSTEM STATES ---
enum SystemState {
    STATE_PATIENT_REGISTRATION,
    STATE_TARE_PROMPT,
    STATE_LIVE_WEIGHING,
    STATE_LIVE_LENGTH,
    STATE_DIAGNOSTIC_SUMMARY
};

// --- DRIVER INSTANCES ---
KeypadDriver  keypadDriver;
ScaleDriver   scaleDriver;
HeightDriver  heightDriver;
UIEngine      uiEngine;
SDDriver      sdDriver;
CloudClient   cloudClient;

// --- RUNTIME APPLICATION BUFFERS ---
SystemState      currentState         = STATE_PATIENT_REGISTRATION;
char             currentPatientID[16] = "";
uint8_t          patientIDIndex       = 0;
bool             patientIsMale        = true;
uint8_t          patientAgeMonths     = 6;

float            lockedWeightKg       = 0.0f;
float            lockedLengthCm       = 0.0f;
DiagnosticResult currentDiagnostic;

// --- FORWARD DECLARATIONS ---
void handleRegistrationState(char key);
void handleTareState(char key);
void handleWeighingState(char key);
void handleLengthState(char key);
void handleSummaryState(char key);
void commitBedsideRecord();
void runSilentBackgroundSync();

// ============================================================================
// SETUP: HARDWARE INITIALIZATION
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n==========================================");
    Serial.println("   NEO DIMETRIX: SMART INFANT PLATFORM    ");
    Serial.println("==========================================");

    // 1. Central I2C Bus Setup (OLED + VL53L0X ToF)
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);

    // 2. Display Engine Initialization
    if (!uiEngine.begin()) {
        Serial.println("❌ UI Engine (SSD1306) failed to start.");
    }

    // 3. SD Card Filesystem & Folder Structure (ESP32-S3 SPI)
    if (!sdDriver.begin(SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN)) {
        Serial.println("⚠️ SD Card offline. System will operate in volatile mode.");
    }

    // 4. Matrix Keypad Scanner
    keypadDriver.begin(KEYPAD_ROW_PINS, KEYPAD_COL_PINS);

    // 5. Weight Scale Subsystem (HX711)
    scaleDriver.begin(HX711_DOUT_PIN, HX711_SCK_PIN);

    // 6. Height Laser Subsystem (VL53L0X)
    if (!heightDriver.begin()) {
        Serial.println("❌ VL53L0X Laser Ranging sensor offline.");
    }

    // 7. Cloud Connectivity (Non-blocking Wi-Fi)
    cloudClient.begin(WIFI_SSID, WIFI_PASS, CLOUD_API_URL, CLOUD_API_KEY);

    // Initial Display Screen
    uiEngine.showRegistrationScreen(currentPatientID, patientIsMale);
    Serial.println("✅ System Initialized. Ready in STATE_PATIENT_REGISTRATION.");
}

// ============================================================================
// MAIN LOOP: NON-BLOCKING FINITE STATE MACHINE
// ============================================================================
void loop() {
    // 1. Background Wi-Fi health maintainer
    cloudClient.maintainConnection();

    // 2. Scan Keypad non-blockingly
    char key = keypadDriver.getKey();

    // 3. FSM Routing
    switch (currentState) {
        case STATE_PATIENT_REGISTRATION:
            handleRegistrationState(key);
            runSilentBackgroundSync();
            break;

        case STATE_TARE_PROMPT:
            handleTareState(key);
            break;

        case STATE_LIVE_WEIGHING:
            handleWeighingState(key);
            break;

        case STATE_LIVE_LENGTH:
            handleLengthState(key);
            break;

        case STATE_DIAGNOSTIC_SUMMARY:
            handleSummaryState(key);
            break;
    }
}

// ============================================================================
// STATE 1: PATIENT REGISTRATION HANDLER
// ============================================================================
void handleRegistrationState(char key) {
    if (!key) return;

    if (key >= '0' && key <= '9') {
        if (patientIDIndex < sizeof(currentPatientID) - 1) {
            currentPatientID[patientIDIndex++] = key;
            currentPatientID[patientIDIndex] = '\0';
            uiEngine.showRegistrationScreen(currentPatientID, patientIsMale);
        }
    } else if (key == 'A') { // Toggle Sex: Male / Female
        patientIsMale = !patientIsMale;
        uiEngine.showRegistrationScreen(currentPatientID, patientIsMale);
    } else if (key == '*') { // Backspace
        if (patientIDIndex > 0) {
            patientIDIndex--;
            currentPatientID[patientIDIndex] = '\0';
            uiEngine.showRegistrationScreen(currentPatientID, patientIsMale);
        }
    } else if (key == '#') { // Confirm ID
        if (patientIDIndex > 0) {
            currentState = STATE_TARE_PROMPT;
            uiEngine.showTarePrompt();
            Serial.printf("📝 Patient Registered: %s (Sex: %c)\n", currentPatientID, patientIsMale ? 'M' : 'F');
        }
    }
}

// ============================================================================
// STATE 2: SCALE TARE PROMPT HANDLER
// ============================================================================
void handleTareState(char key) {
    if (key == '#') {
        uiEngine.showFeedback("Zeroing Bed...");
        scaleDriver.tare();
        scaleDriver.resetFilter();
        currentState = STATE_LIVE_WEIGHING;
        Serial.println("⚖️ Scale Tared. Proceeding to Weighing...");
    } else if (key == '*') {
        currentState = STATE_PATIENT_REGISTRATION;
        uiEngine.showRegistrationScreen(currentPatientID, patientIsMale);
    }
}

// ============================================================================
// STATE 3: LIVE WEIGHING HANDLER
// ============================================================================
void handleWeighingState(char key) {
    scaleDriver.update();
    float currentWeight = scaleDriver.getFilteredWeight();
    bool isStable = scaleDriver.isStable();

    uiEngine.showWeighingScreen(currentWeight, isStable);

    if (key == '#' && isStable) {
        lockedWeightKg = currentWeight;
        heightDriver.resetFilter();
        currentState = STATE_LIVE_LENGTH;
        Serial.printf("🔒 Weight Locked: %.3f kg\n", lockedWeightKg);
    } else if (key == '*') {
        currentState = STATE_TARE_PROMPT;
        uiEngine.showTarePrompt();
    }
}

// ============================================================================
// STATE 4: LIVE LENGTH MEASUREMENT HANDLER
// ============================================================================
void handleLengthState(char key) {
    heightDriver.update();
    float currentLength = heightDriver.getFilteredHeight();
    bool isStable = heightDriver.isStable();

    uiEngine.showLengthScreen(currentLength, isStable);

    if (key == '#' && isStable) {
        lockedLengthCm = currentLength;

        CurrentMeasurement measurement;
        measurement.weightKg   = lockedWeightKg;
        measurement.lengthCm   = lockedLengthCm;
        measurement.isMale     = patientIsMale;
        measurement.ageMonths  = patientAgeMonths;

        CloudPatientHistory history;
        history.hasHistory = false;

        currentDiagnostic = evaluatePatientDiagnostics(measurement, history);

        currentState = STATE_DIAGNOSTIC_SUMMARY;
        uiEngine.showDiagnosticSummary(currentPatientID, lockedWeightKg, lockedLengthCm, 
                                      currentDiagnostic.currentZScore, 
                                      getAcuteStatusString(currentDiagnostic.currentStatus));
        Serial.printf("📏 Length Locked: %.1f cm | WHO Z-Score: %+.2f (%s)\n", 
                      lockedLengthCm, currentDiagnostic.currentZScore, 
                      getAcuteStatusString(currentDiagnostic.currentStatus));
    } else if (key == '*') {
        currentState = STATE_LIVE_WEIGHING;
    }
}

// ============================================================================
// STATE 5: DIAGNOSTIC SUMMARY & SAVE HANDLER
// ============================================================================
void handleSummaryState(char key) {
    if (key == '#') {
        commitBedsideRecord();
        memset(currentPatientID, 0, sizeof(currentPatientID));
        patientIDIndex = 0;
        currentState = STATE_PATIENT_REGISTRATION;
        delay(1200);
        uiEngine.showRegistrationScreen(currentPatientID, patientIsMale);
    } else if (key == '*') {
        currentState = STATE_LIVE_WEIGHING;
    }
}

// ============================================================================
// DUAL-PATH COMMIT DISPATCHER (ONLINE HTTP / OFFLINE SD OUTBOX)
// ============================================================================
void commitBedsideRecord() {
    const char* dateStr = "2026-08-28";
    const char* timeStr = "12:00:00";
    const char* statusStr = getAcuteStatusString(currentDiagnostic.currentStatus);

    if (cloudClient.isConnected()) {
        char computedTrajectory[16];
        float deltaZ = 0.0f;
        sdDriver.computeEdgeTrajectory(currentPatientID, currentDiagnostic.currentZScore, 
                                       computedTrajectory, sizeof(computedTrajectory), &deltaZ);

        bool uploadSuccess = cloudClient.uploadRecord(
            currentPatientID,
            patientIsMale ? 'M' : 'F',
            dateStr,
            timeStr,
            lockedWeightKg,
            lockedLengthCm,
            currentDiagnostic.currentZScore,
            statusStr,
            computedTrajectory,
            deltaZ,
            currentDiagnostic.weightVelocityGramsPerMonth
        );

        if (uploadSuccess) {
            sdDriver.commitSyncedRecord("", currentPatientID, dateStr, timeStr, 
                                        lockedWeightKg, lockedLengthCm, 
                                        currentDiagnostic.currentZScore, statusStr, computedTrajectory);
            uiEngine.showFeedback("Synced to Cloud ☁️");
            Serial.println("✅ Online Bedside Commit Successful.");
            return;
        }
    }

    // Offline Outbox Fallback (/post_val/)
    bool offlineSaved = sdDriver.saveOfflinePostVal(
        currentPatientID,
        patientIsMale ? 'M' : 'F',
        dateStr,
        timeStr,
        lockedWeightKg,
        lockedLengthCm,
        currentDiagnostic.currentZScore,
        statusStr
    );

    if (offlineSaved) {
        uiEngine.showFeedback("Saved Offline 💾");
        Serial.println("💾 Stored in Offline Outbox (/post_val/).");
    } else {
        uiEngine.showFeedback("Save Failed ❌");
        Serial.println("❌ Critical: Failed to write to SD card.");
    }
}

// ============================================================================
// SILENT BACKGROUND SYNC WORKER
// ============================================================================
void runSilentBackgroundSync() {
    if (!cloudClient.isConnected() || currentState != STATE_PATIENT_REGISTRATION) {
        return;
    }

    static unsigned long lastSyncCheck = 0;
    if (millis() - lastSyncCheck < 4000) return;
    lastSyncCheck = millis();

    PendingRecord rec;
    if (sdDriver.getNextPendingPostVal(rec)) {
        char computedTrajectory[16];
        float deltaZ = 0.0f;

        sdDriver.computeEdgeTrajectory(rec.patientID, rec.zScore, 
                                       computedTrajectory, sizeof(computedTrajectory), &deltaZ);

        bool uploadSuccess = cloudClient.uploadRecord(
            rec.patientID,
            rec.sex,
            rec.date,
            rec.time,
            rec.weightKg,
            rec.lengthCm,
            rec.zScore,
            rec.category,
            computedTrajectory,
            deltaZ,
            0.0f
        );

        if (uploadSuccess) {
            sdDriver.commitSyncedRecord(
                rec.filename,
                rec.patientID,
                rec.date,
                rec.time,
                rec.weightKg,
                rec.lengthCm,
                rec.zScore,
                rec.category,
                computedTrajectory
            );
            Serial.printf("🧹 [Silent Sync]: Purged %s for Patient %s\n", rec.filename, rec.patientID);
        }
    }
}