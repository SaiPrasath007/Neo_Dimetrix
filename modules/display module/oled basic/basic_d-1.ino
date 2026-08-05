/*
 * Project: NEO DIMETRIX (NDM) - Standalone OLED Display Test
 * Controller: ESP32-S3
 * Display: 0.96" SSD1306 OLED (128x64)
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

#define SDA_PIN 8
#define SCL_PIN 9

// Create display instance
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void setup() {
  Serial.begin(115200);

  // Initialize I2C on GPIO 8 & GPIO 9
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize OLED at address 0x3C
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[NDM Error]: OLED allocation failed!");
    for (;;); // Halt execution if screen fail
  }

  // Step 1: Wipe RAM memory
  display.clearDisplay();

  // Step 2: Set text size & color
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Step 3: Draw Project Name (Centered Title)
  display.setCursor(25, 20);
  display.println("NEO DIMETRIX");

  // Subtitle
  display.setCursor(52, 35);
  display.println("NDM");

  // Draw decorative horizontal lines above & below
  display.drawFastHLine(10, 12, 108, SSD1306_WHITE);
  display.drawFastHLine(10, 50, 108, SSD1306_WHITE);

  // Step 4: Push memory buffer to physical screen glass
  display.display();
}

void loop() {
  // Static banner display
}