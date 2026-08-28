#ifndef UI_ENGINE_H
#define UI_ENGINE_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "../weight-module/scale_driver.h"
#include "../heigth_module/length_driver.h"

// --- Display Hardware Configuration ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// FSM UI States
enum UIState {
    UI_STATE_PATIENT_ID,
    UI_STATE_TARE_PROMPT,
    UI_STATE_WEIGHING,
    UI_STATE_MEASURING_LENGTH,
    UI_STATE_DIAGNOSTIC_SUMMARY
};

class UIEngine {
private:
    Adafruit_SSD1306 display;
    UIState currentState = UI_STATE_PATIENT_ID;

    // Common Header Generator
    void drawHeader(const char* title) {
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.print(title);
        display.drawFastHLine(0, 9, 128, SSD1306_WHITE);
    }

    // Progress Bar Renderer (x, y, width, height, current, max)
    void drawProgressBar(int x, int y, int w, int h, int current, int maxVal) {
        display.drawRect(x, y, w, h, SSD1306_WHITE);
        if (maxVal <= 0) return;
        int fillWidth = map(current, 0, maxVal, 0, w - 4);
        if (fillWidth > w - 4) fillWidth = w - 4;
        if (fillWidth > 0) {
            display.fillRect(x + 2, y + 2, fillWidth, h - 4, SSD1306_WHITE);
        }
    }

public:
    UIEngine() : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET) {}

    bool begin() {
        // Wire.begin() is handled centrally by main.ino

        if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
            Serial.println("❌ OLED Allocation Failed!");
            return false;
        }

        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        drawHeader("NEO DIMETRIX v1.0");
        display.setCursor(10, 25);
        display.setTextSize(1);
        display.println("SMART STADIOMETER");
        display.setCursor(20, 45);
        display.println("INITIALIZING...");
        display.display();
        
        return true;
    }

    void setState(UIState state) {
        currentState = state;
    }

    UIState getState() const {
        return currentState;
    }

    // --- Screen 1: Patient ID Typing ---
    void renderIDScreen(const char* inputBuffer, const char* statusMsg) {
        display.clearDisplay();
        drawHeader("1. PATIENT REGISTRATION");

        display.setTextSize(1);
        display.setCursor(0, 15);
        display.print("PATIENT ID:");

        // ID Input Box
        display.drawRect(0, 26, 128, 16, SSD1306_WHITE);
        display.setCursor(4, 30);
        if (strlen(inputBuffer) == 0) {
            display.print("_");
        } else {
            display.print(inputBuffer);
            display.print("_");
        }

        // Action Hints
        display.drawFastHLine(0, 48, 128, SSD1306_WHITE);
        display.setCursor(0, 53);
        display.print("[*]Del  [#]Confirm ID");
        display.display();
    }

    // --- Screen 2: Scale Tare Prompt ---
    void renderTarePrompt(bool isZeroed) {
        display.clearDisplay();
        drawHeader("2. TARE PLATFORM");

        display.setTextSize(1);
        display.setCursor(0, 16);
        display.println("Clear scale platform");
        display.setCursor(0, 28);
        display.println("Place blanket/pad");

        display.setCursor(0, 42);
        if (isZeroed) {
            display.println("Status: ZEROED OK!");
        } else {
            display.println("Press [#] to TARE ->");
        }

        display.drawFastHLine(0, 52, 128, SSD1306_WHITE);
        display.setCursor(0, 55);
        display.print("[#] Continue to Weigh");
        display.display();
    }

    // --- Screen 3: Live Weighing ---
    void renderWeighingScreen(const WeightReading& weight) {
        display.clearDisplay();
        drawHeader("3. WEIGHING INFANT");

        display.setTextSize(2);
        display.setCursor(0, 14);
        display.printf("%.3f kg", weight.smoothedKg);

        // Progress bar for 3-second lock window (30 samples)
        drawProgressBar(0, 34, 128, 10, weight.stableProgress, 30);

        display.setTextSize(1);
        display.setCursor(0, 48);
        if (!weight.isPresent) {
            display.print("Place baby (>300g)...");
        } else if (weight.isLocked) {
            display.print("LOCKED 🔒 Press [#]");
        } else {
            display.printf("Stability Range: %.1fg", weight.stabilityRangeGrams);
        }

        display.display();
    }

    // --- Screen 4: Live Height / Length ---
    void renderLengthScreen(const LengthReading& length) {
        display.clearDisplay();
        drawHeader("4. MEASURING LENGTH");

        display.setTextSize(2);
        display.setCursor(0, 14);
        if (length.isValid) {
            display.printf("%.1f cm", length.smoothedLengthCm);
        } else {
            display.print("OUT OF BOUNDS");
        }

        // Progress bar for 2-second lock window (10 samples)
        drawProgressBar(0, 34, 128, 10, length.stableProgress, 10);

        display.setTextSize(1);
        display.setCursor(0, 48);
        if (!length.isValid) {
            display.print("Check Footboard/Pos");
        } else if (length.isLocked) {
            display.print("LOCKED 🔒 Press [#]");
        } else {
            display.printf("Jitter Range: %.1fmm", length.stabilityRangeMm);
        }

        display.display();
    }

    // --- Screen 5: Diagnostic Summary ---
    void renderDiagnosticSummary(const char* patientID, float weightKg, float lengthCm, const char* wlzCategory, float zScore) {
        display.clearDisplay();
        drawHeader("DIAGNOSTIC RESULT");

        display.setTextSize(1);
        display.setCursor(0, 12);
        display.printf("ID: %s\n", patientID);

        display.setCursor(0, 22);
        display.printf("W: %.2fkg  L: %.1fcm\n", weightKg, lengthCm);

        display.drawFastHLine(0, 32, 128, SSD1306_WHITE);

        // WHO Diagnostic Result
        display.setCursor(0, 36);
        display.printf("WLZ Score: %+.2f\n", zScore);
        display.setCursor(0, 46);
        display.printf("Status: %s\n", wlzCategory);

        display.drawFastHLine(0, 55, 128, SSD1306_WHITE);
        display.setCursor(0, 57);
        display.print("[#]Save SD  [*]Redo");

        display.display();
    }

    // Display generic error / warning message
    void renderMessage(const char* title, const char* message) {
        display.clearDisplay();
        drawHeader(title);
        display.setTextSize(1);
        display.setCursor(0, 24);
        display.println(message);
        display.display();
    }
};

#endif // UI_ENGINE_H