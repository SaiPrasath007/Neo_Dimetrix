#ifndef KEYPAD_DRIVER_H
#define KEYPAD_DRIVER_H

#include <Arduino.h>
#include <Keypad.h>

// --- Matrix Pin Definitions for ESP32-S3 ---
#define KEYPAD_ROWS 4
#define KEYPAD_COLS 4

// Keypad Event Types for FSM
enum KeyEventType {
    KEY_EVENT_NONE,
    KEY_EVENT_TYPING,
    KEY_EVENT_BACKSPACE,
    KEY_EVENT_SUBMIT,
    KEY_EVENT_ACTION
};

// Keypad Event Payload
struct KeypadResult {
    char pressedKey;      // Exact character pressed ('0'-'9', '*', '#', 'A'-'D')
    KeyEventType event;   // Event classification
    String buffer;        // Current accumulated text buffer
    bool isSubmitted;     // true when '#' is pressed with a non-empty buffer
};

class KeypadDriver {
private:
    char keys[KEYPAD_ROWS][KEYPAD_COLS] = {
        {'1', '2', '3', 'A'},
        {'4', '5', '6', 'B'},
        {'7', '8', '9', 'C'},
        {'*', '0', '#', 'D'}
    };

    byte rowPins[KEYPAD_ROWS] = {10, 11, 12, 13}; // Pins R1-R4
    byte colPins[KEYPAD_COLS] = {14, 15, 16, 17}; // Pins C1-C4

    Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, KEYPAD_ROWS, KEYPAD_COLS);

    String inputBuffer = "";
    const size_t MAX_BUFFER_LEN = 12; // Maximum Patient ID length

public:
    void begin() {
        inputBuffer = "";
    }

    // Clear accumulated text buffer
    void clearBuffer() {
        inputBuffer = "";
    }

    // Get current text buffer contents
    String getBuffer() const {
        return inputBuffer;
    }

    // Set custom text buffer (e.g. auto-populating patient IDs)
    void setBuffer(const String &text) {
        if (text.length() <= MAX_BUFFER_LEN) {
            inputBuffer = text;
        }
    }

    // Non-blocking update loop called from main.ino
    KeypadResult update() {
        KeypadResult result;
        result.pressedKey = NO_KEY;
        result.event = KEY_EVENT_NONE;
        result.buffer = inputBuffer;
        result.isSubmitted = false;

        char key = keypad.getKey();

        if (key != NO_KEY) {
            result.pressedKey = key;

            // 1. BACKSPACE / CANCEL ACTION (*)
            if (key == '*') {
                if (inputBuffer.length() > 0) {
                    inputBuffer.remove(inputBuffer.length() - 1);
                }
                result.event = KEY_EVENT_BACKSPACE;
            }
            // 2. ENTER / CONFIRM ACTION (#)
            else if (key == '#') {
                result.event = KEY_EVENT_SUBMIT;
                if (inputBuffer.length() > 0) {
                    result.isSubmitted = true;
                }
            }
            // 3. ACTION KEYS (A, B, C, D)
            else if (key == 'A' || key == 'B' || key == 'C' || key == 'D') {
                result.event = KEY_EVENT_ACTION;
            }
            // 4. NUMERIC INPUT (0-9)
            else {
                if (inputBuffer.length() < MAX_BUFFER_LEN) {
                    inputBuffer += key;
                    result.event = KEY_EVENT_TYPING;
                }
            }

            result.buffer = inputBuffer;
        }

        return result;
    }
};

#endif // KEYPAD_DRIVER_H