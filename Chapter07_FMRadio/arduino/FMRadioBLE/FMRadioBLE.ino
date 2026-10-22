/*
  Chapter 7 - FM Radio (Part 2: BLE)
  Same as Part 1, plus control from the App Inventor app over BLE
  (device name "ESP32-Radio"): search up/down, mute and direct tuning
  from the slider. The current frequency is sent back to the app.
  Board:     ESP32 Dev Module
  Hardware:  TEA5767 and SH1106 OLED (I2C SDA 21, SCL 22),
             buttons GPIO 25 (up), GPIO 26 (down), GPIO 27 (mute)
  Libraries: U8g2
*/
#include <Wire.h>
#include <U8g2lib.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"

// ---------- Display ----------
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// ---------- TEA5767 ----------
#define TEA_ADDR 0x60
#define BTN_UP    25
#define BTN_DOWN  26
#define BTN_MUTE  27

const float FREQ_MIN = 87.5;
const float FREQ_MAX = 108.0;

float currentFreq = 98.0;
bool  stereo = false;
uint8_t signalLevel = 0;
bool  bandLimitReached = false;
bool  searching = false;
bool  muted = false;

unsigned long lastBtnCheck = 0;
const unsigned long DEBOUNCE_MS = 250;

// ---------- BLE ----------
#define SERVICE_UUID       "9c4a0000-1a2b-4c3d-8e5f-6a7b8c9d0e1f"
#define FREQ_CHAR_UUID     "9c4a0001-1a2b-4c3d-8e5f-6a7b8c9d0e1f"
#define SETFREQ_CHAR_UUID  "9c4a0002-1a2b-4c3d-8e5f-6a7b8c9d0e1f"
#define CMD_CHAR_UUID      "9c4a0003-1a2b-4c3d-8e5f-6a7b8c9d0e1f"

BLEServer* pServer = nullptr;
BLECharacteristic* pFreqChar = nullptr;
BLECharacteristic* pSetFreqChar = nullptr;
BLECharacteristic* pCmdChar = nullptr;

bool deviceConnected = false;
volatile bool needsRedraw = false;

// forward declarations
void tuneTo(float freqMHz);
void startSeek(bool up);
void setMute(bool m);
void readStatus(bool *readyOut);
void drawScreen();
void notifyFrequency();

// ---- BLE server connection callbacks ----
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) override {
    deviceConnected = true;
    needsRedraw = true;
  }
  void onDisconnect(BLEServer* server) override {
    deviceConnected = false;
    needsRedraw = true;
    // restart advertising so the app can reconnect
    server->getAdvertising()->start();
  }
};

// ---- App writes a frequency directly (used by the slider) ----
class SetFreqCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String value = c->getValue();
    Serial.print("Ricevuto: [");
    Serial.print(value);
    Serial.println("]");
    if (value.length() == 0) return;
    float f = value.toFloat();
    Serial.print("Parsato: ");
    Serial.println(f);
    if (f >= FREQ_MIN && f <= FREQ_MAX) {
      tuneTo(f);
      drawScreen();
    }
  }
};

// ---- App sends a command: U / D / M / S ----
class CmdCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String value = c->getValue();
    if (value.length() == 0) return;
    char cmd = value[0];

    if (cmd == 'U') {
      startSeek(true);
      drawScreen();
    } else if (cmd == 'D') {
      startSeek(false);
      drawScreen();
    } else if (cmd == 'M') {
      setMute(true);
      drawScreen();
    } else if (cmd == 'S') {
      setMute(false);
      drawScreen();
    }
  }
};

// ---- send current frequency to the app (if connected) ----
void notifyFrequency() {
  if (!deviceConnected || pFreqChar == nullptr) return;
  char buf[8];
  dtostrf(currentFreq, 5, 2, buf);
  pFreqChar->setValue((uint8_t*)buf, strlen(buf));
  pFreqChar->notify();
}

// ---- compute the 14-bit PLL word for a given frequency (high side injection) ----
uint16_t freqToPLL(float freqMHz) {
  return (uint16_t)(4.0 * (freqMHz * 1000000.0 + 225000.0) / 32768.0);
}

// ---- write the 5 bytes to directly tune a frequency (no search), honors mute state ----
// Every frequency change in the whole sketch goes through this function,
// so BLE notify and mute state stay consistent no matter what triggered it.
void tuneTo(float freqMHz) {
  uint16_t pll = freqToPLL(freqMHz);
  uint8_t b1 = (pll >> 8) & 0x3F;
  if (muted) b1 |= 0x80;
  uint8_t b2 = pll & 0xFF;
  uint8_t b3 = 0xB0;   // SUD=1, SSL=01, HLSI=1
  uint8_t b4 = 0x10;   // XTAL=1
  uint8_t b5 = 0x00;

  Wire.beginTransmission(TEA_ADDR);
  Wire.write(b1);
  Wire.write(b2);
  Wire.write(b3);
  Wire.write(b4);
  Wire.write(b5);
  Wire.endTransmission();

  currentFreq = freqMHz;
  notifyFrequency();
}

// ---- set mute state and re-apply current tuning ----
void setMute(bool m) {
  muted = m;
  tuneTo(currentFreq);
}

// ---- start an automatic search (SM=1) from the current frequency ----
void startSeek(bool up) {
  searching = true;
  drawScreen();

  // Step away by one increment first: the TEA5767 evaluates the signal
  // starting at the frequency already set, so if we're already on a
  // strong station the search would stop right there without moving.
  currentFreq += up ? 0.1 : -0.1;
  if (currentFreq > FREQ_MAX) currentFreq = FREQ_MIN;
  if (currentFreq < FREQ_MIN) currentFreq = FREQ_MAX;

  uint16_t pll = freqToPLL(currentFreq);
  uint8_t b1 = ((pll >> 8) & 0x3F) | 0x40;   // SM=1
  if (muted) b1 |= 0x80;
  uint8_t b2 = pll & 0xFF;
  uint8_t b3 = 0x30;                          // HLSI=1, SSL=01
  if (up) b3 |= 0x80;                         // SUD=1 -> upward
  uint8_t b4 = 0x10;
  uint8_t b5 = 0x00;

  Wire.beginTransmission(TEA_ADDR);
  Wire.write(b1);
  Wire.write(b2);
  Wire.write(b3);
  Wire.write(b4);
  Wire.write(b5);
  Wire.endTransmission();

  bool ready = false;
  unsigned long start = millis();
  while (!ready && millis() - start < 5000) {
    delay(60);
    readStatus(&ready);
  }

  // lock in the found station (also sends the BLE notify)
  tuneTo(currentFreq);
  searching = false;
}

// ---- read the 5 status bytes from the TEA5767 ----
void readStatus(bool *readyOut) {
  Wire.requestFrom(TEA_ADDR, 5);
  if (Wire.available() < 5) return;

  uint8_t r1 = Wire.read();
  uint8_t r2 = Wire.read();
  uint8_t r3 = Wire.read();
  uint8_t r4 = Wire.read();
  Wire.read();

  bool ready = r1 & 0x80;
  bandLimitReached = r1 & 0x40;

  uint16_t pllRead = ((r1 & 0x3F) << 8) | r2;
  currentFreq = (pllRead * 32768.0 / 4.0 - 225000.0) / 1000000.0;

  stereo = r3 & 0x80;
  signalLevel = (r4 >> 4) & 0x0F;

  if (readyOut) *readyOut = ready;
}

// ----  16x16 Bluetooth icon, used image2cpp ----

static const unsigned char icon_bluetooth_16x16[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x01, 0x80, 0x01, 0x40, 0x01, 0x20, 0x09, 0x20, 0x05, 0x40, 0x03, 0x80, 
	0x01, 0x00, 0x03, 0x80, 0x05, 0x40, 0x09, 0x20, 0x01, 0x40, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00
};

// ---------- Display ----------
void drawScreen() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_logisoso24_tf);
  char buf[8];
  dtostrf(currentFreq, 5, 2, buf);
  u8g2.drawStr(8, 30, buf);
  u8g2.setFont(u8g2_font_7x14_tf);
  u8g2.drawStr(100, 30, "MHz");

  u8g2.setFont(u8g2_font_6x12_tf);
  if (searching) {
    u8g2.drawStr(0, 48, "Searching...");
  } else if (muted) {
    u8g2.drawStr(0, 48, "MUTE");
  } else {
    u8g2.drawStr(0, 48, stereo ? "ST" : "MO");

    int barX = 30;
    int barW = 80;
    u8g2.drawFrame(barX, 40, barW, 8);
    int filled = map(signalLevel, 0, 15, 0, barW - 2);
    u8g2.drawBox(barX + 1, 41, filled, 6);
  }

  if (bandLimitReached) {
    u8g2.drawStr(0, 62, "Band limit reached");
  }

  if (deviceConnected) {
    u8g2.drawBitmap(112, 0, 2, 16, icon_bluetooth_16x16);
  }

  u8g2.sendBuffer();
}

// ---------- BLE setup ----------
void setupBLE() {
  BLEDevice::init("ESP32-Radio");

  // Lower the BLE transmit power: less radiated RF means less noise
  // picked up in the TEA5767 audio. Range is reduced, but on a desk
  // maximum range isn't needed anyway.
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_N12);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N12);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* service = pServer->createService(SERVICE_UUID);

  pFreqChar = service->createCharacteristic(
    FREQ_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pFreqChar->addDescriptor(new BLE2902());

  pSetFreqChar = service->createCharacteristic(
    SETFREQ_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pSetFreqChar->setCallbacks(new SetFreqCallback());

  pCmdChar = service->createCharacteristic(
    CMD_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pCmdChar->setCallbacks(new CmdCallback());

  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_MUTE, INPUT_PULLUP);

  u8g2.begin();

  setupBLE();

  tuneTo(currentFreq);
  delay(100);
  readStatus(nullptr);
  drawScreen();
}

void loop() {
  unsigned long now = millis();

  if (needsRedraw) {
    needsRedraw = false;
    drawScreen();
  }

  if (now - lastBtnCheck > DEBOUNCE_MS) {
    if (digitalRead(BTN_UP) == LOW) {
      lastBtnCheck = now;
      startSeek(true);
      drawScreen();
    } else if (digitalRead(BTN_DOWN) == LOW) {
      lastBtnCheck = now;
      startSeek(false);
      drawScreen();
    } else if (digitalRead(BTN_MUTE) == LOW) {
      lastBtnCheck = now;
      setMute(!muted);
      drawScreen();
    }
  }

  // Reconnection watchdog: if the BLE stack reports no connected client
  // but our flag still says connected, resync and restart advertising.
  static unsigned long lastBleCheck = 0;
  if (now - lastBleCheck > 1000) {
    lastBleCheck = now;
    if (pServer != nullptr && pServer->getConnectedCount() == 0 && deviceConnected) {
      deviceConnected = false;
      pServer->getAdvertising()->start();
      drawScreen();
    }
  }
}
