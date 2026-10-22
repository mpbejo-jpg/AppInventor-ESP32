/*
  Chapter 6 - OLED Temperature (Part 1: local)
  Shows the DS18B20 temperature on the SH1106 OLED display, updated
  every second. The button switches between Celsius and Fahrenheit.
  Board:     ESP32 Dev Module
  Hardware:  DS18B20 GPIO 4, button GPIO 25, SH1106 OLED (I2C SDA 21, SCL 22)
  Libraries: OneWire, DallasTemperature, U8g2
*/
#include <OneWire.h>
#include <DallasTemperature.h>
#include <U8g2lib.h>
#include <Wire.h>

// --- Pins ---
#define ONE_WIRE_BUS 4
#define BUTTON_PIN   25

// --- Sensor ---
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// --- Display (SH1106, confirmed via visual test) ---
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// --- State ---
bool useFahrenheit = false;
float lastTempC = 0;

// --- Sensor read timing (non-blocking) ---
unsigned long lastReadMillis = 0;
const unsigned long READ_INTERVAL = 1000; // 1 second

// --- Button debounce ---
bool lastFlickerableState = HIGH; // last raw reading (bounces)
bool buttonState = HIGH;          // stable state confirmed after debounce
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50;

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  sensors.begin();
  u8g2.begin();
  u8g2.setContrast(255);

  // First reading right away, don't wait 1s
  sensors.requestTemperatures();
  lastTempC = sensors.getTempCByIndex(0);
  lastReadMillis = millis();

  updateDisplay();
}

void loop() {
  unsigned long now = millis();

  // --- Update temperature every 1s ---
  if (now - lastReadMillis >= READ_INTERVAL) {
    lastReadMillis = now;
    sensors.requestTemperatures();
    float t = sensors.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C) {
      lastTempC = t;
    } else {
      Serial.println("Error: sensor not responding.");
    }
    updateDisplay();
  }

  // --- Button handling with two-state debounce (toggle on press) ---
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastFlickerableState) {
    lastDebounceTime = now;      // signal changed: restart the timer
    lastFlickerableState = reading;
  }

  if ((now - lastDebounceTime) > DEBOUNCE_DELAY) {
    // signal has been stable for more than DEBOUNCE_DELAY: update confirmed state
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) { // confirmed falling edge = press
        useFahrenheit = !useFahrenheit;
        updateDisplay();
      }
    }
  }
}

void drawThermometerIcon(u8g2_uint_t x, u8g2_uint_t y) {
  // (x,y) = top-left corner of the icon area (~10x18px footprint)
  u8g2.drawRBox(x + 2, y, 4, 11, 2);              // stem
  u8g2.drawDisc(x + 4, y + 14, 4, U8G2_DRAW_ALL); // bulb
}

void updateDisplay() {
  float displayTemp = useFahrenheit ? (lastTempC * 9.0 / 5.0 + 32.0) : lastTempC;
  const char* unit = useFahrenheit ? "F" : "C";

  char tempStr[16];
  dtostrf(displayTemp, 4, 1, tempStr);

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, "Temperature:");

  // Icon placed right before the numeric value, on the value line
  drawThermometerIcon(0, 24);

  u8g2.setFont(u8g2_font_ncenB14_tr);
  char valueStr[24];
  snprintf(valueStr, sizeof(valueStr), "%s %s", tempStr, unit);
  u8g2.drawUTF8(14, 42, valueStr);
  u8g2.sendBuffer();

  Serial.print("Temperature: ");
  Serial.print(tempStr);
  Serial.println(unit);
}


