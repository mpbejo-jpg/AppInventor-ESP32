/*
  Chapter 6 - OLED Temperature (Part 2: BLE)
  Same as Part 1, plus the temperature is sent to the App Inventor app
  over BLE (device name "ESP32_ThermoDisplay"), and the app can switch
  the unit by sending "C" or "F".
  Board:     ESP32 Dev Module
  Hardware:  DS18B20 GPIO 4, button GPIO 25, SH1106 OLED (I2C SDA 21, SCL 22)
  Libraries: OneWire, DallasTemperature, U8g2
*/
#include <OneWire.h>
#include <DallasTemperature.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- Pins ---
#define ONE_WIRE_BUS 4
#define BUTTON_PIN   25

// --- BLE UUIDs (generate your own if you need uniqueness across projects) ---
#define SERVICE_UUID           "12345678-1234-1234-1234-1234567890ab"
#define TEMP_CHARACTERISTIC_UUID "12345678-1234-1234-1234-1234567890ac" // Notify: ESP32 -> App
#define UNIT_CHARACTERISTIC_UUID "12345678-1234-1234-1234-1234567890ad" // Write: App -> ESP32

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

// --- BLE state ---
BLECharacteristic *tempCharacteristic;
BLECharacteristic *unitCharacteristic;
BLEServer *pServer;
bool deviceConnected = false;

// --- Forward declarations (needed: used inside UnitCallbacks before their definition) ---
void updateDisplay();
void notifyTemperature();

class ServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* server) {
    deviceConnected = true;
    Serial.println("BLE client connected.");
  }
  void onDisconnect(BLEServer* server) {
    deviceConnected = false;
    Serial.println("BLE client disconnected. Restarting advertising.");
    server->getAdvertising()->start(); // resume advertising after disconnect
  }
};

// Called when the App Inventor button writes "C" or "F" to the unit characteristic
class UnitCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) {
    String value = characteristic->getValue();
    if (value.length() == 0) return;

    char c = value[0];
    if (c == 'F' || c == 'f') {
      useFahrenheit = true;
      Serial.println("Unit set to Fahrenheit via BLE.");
    } else if (c == 'C' || c == 'c') {
      useFahrenheit = false;
      Serial.println("Unit set to Celsius via BLE.");
    } else {
      Serial.println("Unknown BLE command, ignored.");
      return;
    }
    updateDisplay();
    notifyTemperature(); // push the updated value/unit right away
  }
};

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
  setupBLE();
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
    notifyTemperature();
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
        notifyTemperature();
      }
    }
  }
}

void setupBLE() {
  BLEDevice::init("ESP32_ThermoDisplay");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Temperature characteristic: Notify (ESP32 pushes updates to the App)
  tempCharacteristic = pService->createCharacteristic(
    TEMP_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  tempCharacteristic->addDescriptor(new BLE2902()); // required for notify to work

  // Unit characteristic: Write (App sends "C" or "F" to the ESP32)
  unitCharacteristic = pService->createCharacteristic(
    UNIT_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  unitCharacteristic->setCallbacks(new UnitCallbacks());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("BLE advertising started as 'ESP32_ThermoDisplay'.");
}

void notifyTemperature() {
  if (!deviceConnected) return;

  float displayTemp = useFahrenheit ? (lastTempC * 9.0 / 5.0 + 32.0) : lastTempC;
  const char* unit = useFahrenheit ? "F" : "C";

  char payload[16];
  snprintf(payload, sizeof(payload), "%.1f %s", displayTemp, unit);

  tempCharacteristic->setValue(payload);
  tempCharacteristic->notify();
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

