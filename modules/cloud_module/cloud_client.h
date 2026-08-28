#ifndef CLOUD_CLIENT_H
#define CLOUD_CLIENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

class CloudClient {
private:
    char wifiSSID[32];
    char wifiPass[64];
    char endpointUrl[128];
    char apiKeyHeader[64];
    
    unsigned long lastReconnectAttempt = 0;
    const unsigned long RECONNECT_INTERVAL_MS = 10000; // Retry Wi-Fi every 10s if dropped
    const uint16_t HTTP_TIMEOUT_MS = 5000;             // 5-second fast fail for background sync

public:
    CloudClient() {
        memset(wifiSSID, 0, sizeof(wifiSSID));
        memset(wifiPass, 0, sizeof(wifiPass));
        memset(endpointUrl, 0, sizeof(endpointUrl));
        memset(apiKeyHeader, 0, sizeof(apiKeyHeader));
    }

    // --- 1. INITIALIZATION ---
    void begin(const char* ssid, const char* pass, const char* url, const char* apiKey = nullptr) {
        strncpy(wifiSSID, ssid, sizeof(wifiSSID) - 1);
        strncpy(wifiPass, pass, sizeof(wifiPass) - 1);
        strncpy(endpointUrl, url, sizeof(endpointUrl) - 1);
        if (apiKey) {
            strncpy(apiKeyHeader, apiKey, sizeof(apiKeyHeader) - 1);
        }

        WiFi.mode(WIFI_STA);
        WiFi.begin(wifiSSID, wifiPass);
        Serial.printf("📡 [Cloud] Connecting to Wi-Fi SSID: %s\n", wifiSSID);
    }

    // --- 2. CONNECTION STATE & NON-BLOCKING RECONNECT ---
    bool isConnected() {
        return (WiFi.status() == WL_CONNECTED);
    }

    void maintainConnection() {
        if (WiFi.status() != WL_CONNECTED) {
            unsigned long currentMillis = millis();
            if (currentMillis - lastReconnectAttempt >= RECONNECT_INTERVAL_MS) {
                lastReconnectAttempt = currentMillis;
                Serial.println("🔄 [Cloud] Reconnecting Wi-Fi...");
                WiFi.reconnect();
            }
        }
    }

    // --- 3. TRANSMIT RECORD TO CLOUD BACKEND ---
    bool uploadRecord(const char* patientID, char sex, const char* date, const char* timeStr,
                      float weightKg, float lengthCm, float zScore, const char* category, 
                      const char* trajectory, float deltaZ = 0.0f, float weightVelocity = 0.0f) 
    {
        if (!isConnected()) {
            return false;
        }

        // Construct JSON Payload on stack (Zero heap fragmentation)
        char jsonPayload[384];
        snprintf(jsonPayload, sizeof(jsonPayload),
                 "{"
                 "\"patient_id\":\"%s\","
                 "\"sex\":\"%c\","
                 "\"date\":\"%s\","
                 "\"time\":\"%s\","
                 "\"weight_kg\":%.3f,"
                 "\"length_cm\":%.1f,"
                 "\"wlz_zscore\":%+.2f,"
                 "\"category\":\"%s\","
                 "\"trajectory\":\"%s\","
                 "\"delta_z\":%+.2f,"
                 "\"weight_velocity_g_month\":%.1f"
                 "}",
                 patientID, sex, date, timeStr,
                 weightKg, lengthCm, zScore, category,
                 trajectory, deltaZ, weightVelocity);

        HTTPClient http;
        http.setTimeout(HTTP_TIMEOUT_MS);
        
        // Start HTTP connection
        if (!http.begin(endpointUrl)) {
            Serial.println("❌ [Cloud] Unable to establish connection to endpoint.");
            return false;
        }

        // Headers
        http.addHeader("Content-Type", "application/json");
        if (strlen(apiKeyHeader) > 0) {
            http.addHeader("X-API-Key", apiKeyHeader);
        }

        // Execute HTTP POST
        int httpResponseCode = http.POST((uint8_t*)jsonPayload, strlen(jsonPayload));

        bool success = false;
        if (httpResponseCode == HTTP_CODE_OK || httpResponseCode == HTTP_CODE_CREATED) {
            Serial.printf("☁️ [Cloud Sync OK]: Response %d for Patient: %s\n", httpResponseCode, patientID);
            success = true;
        } else {
            Serial.printf("⚠️ [Cloud Sync Failed]: Code %d, Error: %s\n", 
                          httpResponseCode, http.errorToString(httpResponseCode).c_str());
            success = false;
        }

        http.end();
        return success;
    }

    // --- 4. FETCH LATEST PATIENT HISTORY FROM CLOUD (FOR ONLINE VISIT) ---
    bool fetchPatientHistory(const char* patientID, String& outPayload) {
        if (!isConnected()) return false;

        char queryUrl[256];
        snprintf(queryUrl, sizeof(queryUrl), "%s?patient_id=%s", endpointUrl, patientID);

        HTTPClient http;
        http.setTimeout(HTTP_TIMEOUT_MS);
        if (!http.begin(queryUrl)) return false;

        if (strlen(apiKeyHeader) > 0) {
            http.addHeader("X-API-Key", apiKeyHeader);
        }

        int httpCode = http.GET();
        bool success = false;

        if (httpCode == HTTP_CODE_OK) {
            outPayload = http.getString();
            success = true;
        }

        http.end();
        return success;
    }
};

#endif // CLOUD_CLIENT_H