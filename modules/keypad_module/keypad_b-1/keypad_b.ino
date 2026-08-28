/*
 * Standalone Keypad & OLED Node (Direct GPIO Connection)
 * Microcontroller: ESP32-S3
 * Display: 0.96" SSD1306 OLED (GPIO 8 SDA, GPIO 9 SCL)
 * Keypad: 4x4 Matrix (Direct GPIOs 10-17)
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>

// --- Pin Definitions ---
#define SDA_PIN       8
#define SCL_PIN       9
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// --- Keypad Configuration (Direct GPIOs) ---
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {10, 11, 12, 13}; // Pins R1-R4
byte colPins[COLS] = {14, 15, 16, 17}; // Pins C1-C4

// --- Hardware Instances ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// --- Buffer Variables ---
String inputBuffer = "";
String lastSubmitted = "None";
String statusMessage = "READY";

void setup() {
  Serial.begin(115200);

  // Initialize I2C for OLED
  Wire.begin(SDA_PIN, SCL_PIN);

  // Start OLED Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[Error]: OLED allocation failed!");
    for (;;);
  }

  // Boot splash screen
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(15, 25);
  display.println("DIRECT KEYPAD READY");
  display.display();
  delay(1000);

  updateScreen();
}

void loop() {
  char key = keypad.getKey();

  if (key != NO_KEY) {

    // 1. BACKSPACE ACTION (*)
    if (key == '*') {
      if (inputBuffer.length() > 0) {
        inputBuffer.remove(inputBuffer.length() - 1);
        statusMessage = "BACKSPACE";
      } else {
        statusMessage = "BUFFER EMPTY";
      }
    }

    // 2. ENTER / SUBMIT ACTION (#)
    else if (key == '#') {
      if (inputBuffer.length() > 0) {
        lastSubmitted = inputBuffer;
        statusMessage = "SUBMITTED!";
        Serial.println("[ENTER] Submitted ID: " + inputBuffer);
        inputBuffer = "";
      } else {
        statusMessage = "ENTER SOME TEXT!";
      }
    }

    // 3. CHARACTER TYPING (0-9, A-D)
    else {
      if (inputBuffer.length() < 12) {
        inputBuffer += key;
        statusMessage = "TYPING...";
      } else {
        statusMessage = "BUFFER FULL!";
      }
    }

    updateScreen();
  }
}

void updateScreen() {
  display.clearDisplay();

  // Title Header
  display.setTextSize(1);
  display.setCursor(12, 0);
  display.println("KEYPAD INPUT NODE");
  display.drawFastHLine(0, 9, 128, SSD1306_WHITE);

  // Typing Field
  display.setCursor(0, 14);
  display.print("TYPING: ");
  if (inputBuffer.length() == 0) {
    display.println("_");
  } else {
    display.println(inputBuffer + "_");
  }

  // Saved Record
  display.setCursor(0, 28);
  display.print("SAVED ID: ");
  display.setCursor(0, 38);
  display.println(lastSubmitted);

  // Footer Status Bar
  display.drawFastHLine(0, 50, 128, SSD1306_WHITE);
  display.setCursor(0, 54);
  display.print("STATUS: ");
  display.print(statusMessage);

  display.display();
}