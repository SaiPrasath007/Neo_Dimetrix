#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <SD.h>
#include <Keypad.h>
#include <vector>

// --- OLED Configuration (ESP32-S3) ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 8
#define OLED_SCL 9
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- MicroSD Pin Configuration (ESP32-S3) ---
#define SD_CS_PIN 10
#define SPI_MOSI  11
#define SPI_SCK   12
#define SPI_MISO  13

// --- Keypad Configuration ---
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {1, 2, 41, 42};
byte colPins[COLS] = {39, 40, 37, 38};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// --- State Machine & Buffers ---
enum SystemMode { MODE_MENU, MODE_WRITE, MODE_READ };
SystemMode currentMode = MODE_MENU;

String inputBuffer = "";
std::vector<String> sdLines;
int currentLineIndex = 0;

void updateOLED(String header, String line1, String line2 = "");
void saveBufferToSD();
void loadLinesFromSD();

void setup() {
  Serial.begin(115200);

  // 1. Initialize I2C for OLED on ESP32-S3 GPIO 8 & 9
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED Failed!");
    while (1);
  }

  // 2. Initialize SPI for SD Card on ESP32-S3 GPIO 10, 11, 12, 13
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN)) {
    updateOLED("SD CARD ERROR", "Check Wiring / Card", "Restart Board");
    while (1);
  }

  updateOLED("NEO DIMETRIX", "Press A: Write Mode", "Press B: Read Mode");
}

void loop() {
  char key = keypad.getKey();
  if (!key) return;

  switch (currentMode) {
    case MODE_MENU:
      if (key == 'A') {
        currentMode = MODE_WRITE;
        inputBuffer = "";
        updateOLED("WRITE MODE", "Type text...", "Press # to Save");
      } else if (key == 'B') {
        currentMode = MODE_READ;
        loadLinesFromSD();
        currentLineIndex = 0;
        if (sdLines.size() > 0) {
          updateOLED("LINE 1/" + String(sdLines.size()), sdLines[0], "1:Prev | 3:Next");
        } else {
          updateOLED("READ MODE", "SD File Empty!", "Press A to Write");
        }
      }
      break;

    case MODE_WRITE:
      if (key == '#') {
        if (inputBuffer.length() > 0) {
          saveBufferToSD();
          updateOLED("SUCCESS!", "Saved to SD Card", "Returning to menu...");
          delay(1200);
          currentMode = MODE_MENU;
          updateOLED("NEO DIMETRIX", "Press A: Write Mode", "Press B: Read Mode");
        }
      } else if (key == '*') {
        if (inputBuffer.length() > 0) {
          inputBuffer.remove(inputBuffer.length() - 1);
        }
        updateOLED("TYPING (* = Del)", inputBuffer, "# = Save to SD");
      } else if (key >= '0' && key <= '9') {
        inputBuffer += key;
        updateOLED("TYPING (* = Del)", inputBuffer, "# = Save to SD");
      } else if (key == 'B') {
        currentMode = MODE_READ;
        loadLinesFromSD();
        currentLineIndex = 0;
        if (sdLines.size() > 0) {
          updateOLED("LINE 1/" + String(sdLines.size()), sdLines[0], "1:Prev | 3:Next");
        }
      }
      break;

    case MODE_READ:
      if (key == '3') {
        if (sdLines.size() > 0 && currentLineIndex < (int)sdLines.size() - 1) {
          currentLineIndex++;
          updateOLED("LINE " + String(currentLineIndex + 1) + "/" + String(sdLines.size()), 
                     sdLines[currentLineIndex], "1:Prev | 3:Next");
        }
      } else if (key == '1') {
        if (sdLines.size() > 0 && currentLineIndex > 0) {
          currentLineIndex--;
          updateOLED("LINE " + String(currentLineIndex + 1) + "/" + String(sdLines.size()), 
                     sdLines[currentLineIndex], "1:Prev | 3:Next");
        }
      } else if (key == 'A') {
        currentMode = MODE_WRITE;
        inputBuffer = "";
        updateOLED("WRITE MODE", "Type text...", "Press # to Save");
      }
      break;
  }
}

void updateOLED(String header, String line1, String line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(header);
  display.println("--------------------");
  display.println(line1);
  if (line2.length() > 0) {
    display.setCursor(0, 48);
    display.println(line2);
  }
  display.display();
}

void saveBufferToSD() {
  File file = SD.open("/notes.txt", FILE_APPEND);
  if (file) {
    file.println(inputBuffer);
    file.close();
  }
}

void loadLinesFromSD() {
  sdLines.clear();
  File file = SD.open("/notes.txt", FILE_READ);
  if (file) {
    String currentLine = "";
    while (file.available()) {
      char ch = (char)file.read();
      if (ch == '\n') {
        currentLine.trim();
        if (currentLine.length() > 0) sdLines.push_back(currentLine);
        currentLine = "";
      } else {
        currentLine += ch;
      }
    }
    currentLine.trim();
    if (currentLine.length() > 0) sdLines.push_back(currentLine);
    file.close();
  }
}