#ifndef SD_DRIVER_H
#define SD_DRIVER_H

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>

// --- Default SPI Pin Definitions for ESP32-S3 ---
#define DEFAULT_SD_CS_PIN   5
#define DEFAULT_SD_SCK_PIN  18
#define DEFAULT_SD_MISO_PIN 19
#define DEFAULT_SD_MOSI_PIN 23

// Diagnostic Struct for Silent Background Processing
struct PendingRecord {
    char filename[64];         // Full path in /post_val/
    char patientID[16];        // Birth Certificate ID
    char sex;                  // 'M' or 'F'
    char date[11];             // "YYYY-MM-DD"
    char time[9];              // "HH:MM:SS"
    float weightKg;            // Measured weight
    float lengthCm;            // Measured length
    float zScore;              // Current WHO WLZ Z-Score Z(t)
    char category[16];         // "SAM", "MAM", "NORMAL"
    char trajectory[16];       // "PENDING"
};

class SDDriver {
private:
    uint8_t csPin;
    bool cardMounted = false;

    const char* PRE_VAL_DIR  = "/pre_val";
    const char* POST_VAL_DIR = "/post_val";

    void createDirectories() {
        if (!cardMounted) return;
        if (!SD.exists(PRE_VAL_DIR))  SD.mkdir(PRE_VAL_DIR);
        if (!SD.exists(POST_VAL_DIR)) SD.mkdir(POST_VAL_DIR);
    }

public:
    SDDriver() : csPin(DEFAULT_SD_CS_PIN) {}

    bool begin(uint8_t cs = DEFAULT_SD_CS_PIN, int8_t sck = -1, int8_t miso = -1, int8_t mosi = -1) {
        csPin = cs;

        if (sck != -1 && miso != -1 && mosi != -1) {
            SPI.begin(sck, miso, mosi, csPin);
        } else {
            SPI.begin();
        }

        if (!SD.begin(csPin)) {
            Serial.println("⚠️ SD Card mount failed (Card missing, unformatted, or SPI error).");
            cardMounted = false;
            return false;
        }

        if (SD.cardType() == CARD_NONE) {
            Serial.println("⚠️ No SD card detected in slot.");
            cardMounted = false;
            return false;
        }

        cardMounted = true;
        createDirectories();
        Serial.printf("✅ SD Subsystem Ready. Pending Outbox Items: %d\n", getPendingCount());
        return true;
    }

    bool isAvailable() const {
        return cardMounted;
    }

    // --- 1. BEDSIDE SAVE: Write offline record to /post_val/ with PENDING trajectory ---
    bool saveOfflinePostVal(const char* patientID, char sex, const char* date, const char* timeStr,
                            float weightKg, float lengthCm, float zScore, const char* category) {
        if (!cardMounted && !begin(csPin)) return false;

        char filePath[64];
        snprintf(filePath, sizeof(filePath), "%s/%s_%lu.json", POST_VAL_DIR, patientID, millis());

        File file = SD.open(filePath, FILE_WRITE);
        if (!file) {
            Serial.printf("❌ Failed to create outbox file: %s\n", filePath);
            return false;
        }

        char jsonBuffer[256];
        snprintf(jsonBuffer, sizeof(jsonBuffer),
                 "{\"id\":\"%s\",\"sex\":\"%c\",\"date\":\"%s\",\"time\":\"%s\",\"weight\":%.3f,\"length\":%.1f,\"wlz\":%+.2f,\"cat\":\"%s\",\"trajectory\":\"PENDING\"}",
                 (patientID && strlen(patientID) > 0) ? patientID : "ANON",
                 sex,
                 date ? date : "2026-01-01",
                 timeStr ? timeStr : "00:00:00",
                 weightKg,
                 lengthCm,
                 zScore,
                 category ? category : "UNKNOWN");

        size_t bytesWritten = file.println(jsonBuffer);
        file.flush();
        file.close();

        if (bytesWritten == 0) {
            Serial.printf("❌ Write failed on %s\n", filePath);
            SD.remove(filePath);
            return false;
        }

        Serial.printf("📥 [Outbox Queued]: %s\n", filePath);
        return true;
    }

    // --- 2. SILENT WORKER: Read next pending file in /post_val/ without altering OLED ---
    bool getNextPendingPostVal(PendingRecord& rec) {
        if (!cardMounted) return false;

        File root = SD.open(POST_VAL_DIR);
        if (!root || !root.isDirectory()) {
            if (root) root.close();
            return false;
        }

        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                snprintf(rec.filename, sizeof(rec.filename), "%s/%s", POST_VAL_DIR, file.name());

                char line[256];
                size_t idx = 0;
                while (file.available() && idx < sizeof(line) - 1) {
                    char c = file.read();
                    if (c == '\n' || c == '\r') break;
                    line[idx++] = c;
                }
                line[idx] = '\0';
                file.close();
                root.close();

                // Lightweight zero-heap parse
                sscanf(line, "{\"id\":\"%[^\"]\",\"sex\":\"%c\",\"date\":\"%[^\"]\",\"time\":\"%[^\"]\",\"weight\":%f,\"length\":%f,\"wlz\":%f,\"cat\":\"%[^\"]\",\"trajectory\":\"%[^\"]\"}",
                       rec.patientID, &rec.sex, rec.date, rec.time, &rec.weightKg, &rec.lengthCm, &rec.zScore, rec.category, rec.trajectory);

                return true;
            }
            file = root.openNextFile();
        }
        root.close();
        return false;
    }

    // --- 3. EDGE ΔZ TRAJECTORY ENGINE: Z(t) - Z(t-1) Growth Velocity Analysis ---
    bool computeEdgeTrajectory(const char* patientID, float currentZScore, char* outTrajectory, size_t maxLen, float* outDeltaZ = nullptr) {
        char prePath[64];
        snprintf(prePath, sizeof(prePath), "%s/%s.csv", PRE_VAL_DIR, patientID);

        // Case A: First ever visit for this infant
        if (!SD.exists(prePath)) {
            strncpy(outTrajectory, "BASELINE", maxLen);
            if (outDeltaZ) *outDeltaZ = 0.0f;
            return true;
        }

        File file = SD.open(prePath, FILE_READ);
        if (!file) {
            strncpy(outTrajectory, "STABLE", maxLen);
            if (outDeltaZ) *outDeltaZ = 0.0f;
            return false;
        }

        // Read last recorded visit row
        char lastLine[128] = "";
        char currentLine[128] = "";
        size_t idx = 0;

        while (file.available()) {
            char c = file.read();
            if (c == '\n') {
                currentLine[idx] = '\0';
                if (idx > 0) {
                    strncpy(lastLine, currentLine, sizeof(lastLine));
                }
                idx = 0;
            } else if (c != '\r' && idx < sizeof(currentLine) - 1) {
                currentLine[idx++] = c;
            }
        }
        file.close();

        // Parse previous Z-score Z(t-1) from schema: Date,Time,Weight,Length,WLZ_ZScore,Category,Trajectory
        float prevW = 0.0f, prevL = 0.0f, prevZ = 0.0f;
        int parsedFields = sscanf(lastLine, "%*[^,],%*[^,],%f,%f,%f", &prevW, &prevL, &prevZ);

        if (parsedFields < 3) {
            strncpy(outTrajectory, "BASELINE", maxLen);
            if (outDeltaZ) *outDeltaZ = 0.0f;
            return true;
        }

        // Calculate Delta Z = Z(t) - Z(t-1)
        float deltaZ = currentZScore - prevZ;
        if (outDeltaZ) *outDeltaZ = deltaZ;

        // Clinical WHO Centile Velocity Thresholds
        if (deltaZ <= -0.50f) {
            strncpy(outTrajectory, "FALTERING", maxLen); // Downward centile crossing (Early Alert)
        } else if (deltaZ >= 0.50f) {
            strncpy(outTrajectory, "CATCH_UP", maxLen);  // Upward centile recovery
        } else {
            strncpy(outTrajectory, "STABLE", maxLen);    // Tracking existing centile path
        }

        return true;
    }

    // --- 4. STRICT GUARDED TRANSACTION: Only delete post_val on verified pre_val commit ---
    bool commitSyncedRecord(const char* postValPath, const char* patientID, const char* date, const char* timeStr,
                            float weightKg, float lengthCm, float zScore, const char* category, const char* trajectory) {
        if (!cardMounted) return false;

        char prePath[64];
        snprintf(prePath, sizeof(prePath), "%s/%s.csv", PRE_VAL_DIR, patientID);

        bool isNew = !SD.exists(prePath);
        File preFile = SD.open(prePath, FILE_APPEND);
        if (!preFile) {
            Serial.printf("❌ Abort Commit: Cannot open %s. Outbox preserved.\n", prePath);
            return false; // Leave postValPath intact in queue
        }

        if (isNew) {
            preFile.println("Date,Time,Weight_kg,Length_cm,WLZ_ZScore,Category,Trajectory");
        }

        size_t written = preFile.printf("%s,%s,%.3f,%.1f,%+.2f,%s,%s\n", 
                                        date, timeStr, weightKg, lengthCm, zScore, category, trajectory);
        preFile.flush();
        preFile.close();

        // Strict verification: ONLY delete outbox if sectors committed cleanly
        if (written > 0) {
            SD.remove(postValPath);
            Serial.printf("🧹 [Guarded Commit Complete]: Appended %s.csv -> Deleted %s\n", patientID, postValPath);
            return true;
        } else {
            Serial.printf("❌ Write verification failed on %s. Outbox preserved.\n", prePath);
            return false;
        }
    }

    int getPendingCount() {
        if (!cardMounted || !SD.exists(POST_VAL_DIR)) return 0;

        File root = SD.open(POST_VAL_DIR);
        if (!root || !root.isDirectory()) return 0;

        int count = 0;
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) count++;
            file = root.openNextFile();
        }
        root.close();
        return count;
    }
};

#endif // SD_DRIVER_H