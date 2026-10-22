/*
  Chapter 7 - FM Radio (Part 1: local)
  FM radio with the TEA5767 module (87.5-108 MHz). Buttons search the
  next station up or down and toggle mute; the OLED shows frequency
  and signal level.
  Board:     ESP32 Dev Module
  Hardware:  TEA5767 and SH1106 OLED (I2C SDA 21, SCL 22),
             buttons GPIO 25 (up), GPIO 26 (down), GPIO 27 (mute)
  Libraries: U8g2
*/
#include <Wire.h>
#include <U8g2lib.h>

// ---------- Display ----------
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// ---------- TEA5767 ----------
#define TEA_ADDR 0x60
#define BTN_UP    25
#define BTN_DOWN  26
#define BTN_MUTE  27

const float FREQ_MIN = 87.5;
const float FREQ_MAX = 108.0;

float currentFreq = 98.0;   // starting frequency
bool  stereo = false;
uint8_t signalLevel = 0;    // 0-15
bool  bandLimitReached = false;
bool  searching = false;
bool  muted = false;

unsigned long lastBtnCheck = 0;
const unsigned long DEBOUNCE_MS = 250;

// ---- compute the 14-bit PLL word for a given frequency (high side injection) ----
uint16_t freqToPLL(float freqMHz) {
  return (uint16_t)(4.0 * (freqMHz * 1000000.0 + 225000.0) / 32768.0);
}

// ---- write the 5 bytes to directly tune a frequency (no search), honors mute state ----
void tuneTo(float freqMHz) {
  uint16_t pll = freqToPLL(freqMHz);
  uint8_t b1 = (pll >> 8) & 0x3F;   // MUTE bit added below, SM=0, PLL high bits
  if (muted) b1 |= 0x80;            // MUTE=1
  uint8_t b2 = pll & 0xFF;          // PLL low bits
  uint8_t b3 = 0xB0;                // SUD=1, SSL=01, HLSI=1 (high side injection)
  uint8_t b4 = 0x10;                // XTAL=1 (onboard 32.768kHz crystal)
  uint8_t b5 = 0x00;

  Wire.beginTransmission(TEA_ADDR);
  Wire.write(b1);
  Wire.write(b2);
  Wire.write(b3);
  Wire.write(b4);
  Wire.write(b5);
  Wire.endTransmission();

  currentFreq = freqMHz;
}

// ---- toggle mute and re-apply current tuning ----
void toggleMute() {
  muted = !muted;
  tuneTo(currentFreq);
}

// ---- start an automatic search (SM=1) from the current frequency ----
void startSeek(bool up) {
  searching = true;
  drawScreen(); // immediate feedback "Searching..."

  // The TEA5767 evaluates the signal starting at the frequency already set:
  // if we're already on a strong station the search stops right there.
  // Step away by one increment before actually launching the search.
  currentFreq += up ? 0.1 : -0.1;
  if (currentFreq > FREQ_MAX) currentFreq = FREQ_MIN;
  if (currentFreq < FREQ_MIN) currentFreq = FREQ_MAX;

  uint16_t pll = freqToPLL(currentFreq);
  uint8_t b1 = ((pll >> 8) & 0x3F) | 0x40;   // SM=1 (search mode)
  if (muted) b1 |= 0x80;                      // keep mute state during search
  uint8_t b2 = pll & 0xFF;
  uint8_t b3 = 0x30;                          // HLSI=1, SSL=01 (lowest valid stop level)
  if (up) b3 |= 0x80;                         // SUD=1 -> search upward
  uint8_t b4 = 0x10;                          // XTAL=1
  uint8_t b5 = 0x00;

  Wire.beginTransmission(TEA_ADDR);
  Wire.write(b1);
  Wire.write(b2);
  Wire.write(b3);
  Wire.write(b4);
  Wire.write(b5);
  Wire.endTransmission();

  // poll the "ready" flag (bit7 of the first status byte)
  bool ready = false;
  unsigned long start = millis();
  while (!ready && millis() - start < 5000) {  // 5s safety timeout
    delay(60);
    readStatus(&ready);
  }

  // lock in the found station by writing again without the search bit
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
  Wire.read(); // r5 unused

  bool ready = r1 & 0x80;
  bandLimitReached = r1 & 0x40;

  uint16_t pllRead = ((r1 & 0x3F) << 8) | r2;
  currentFreq = (pllRead * 32768.0 / 4.0 - 225000.0) / 1000000.0;

  stereo = r3 & 0x80;
  signalLevel = (r4 >> 4) & 0x0F;

  if (readyOut) *readyOut = ready;
}

// ---------- Display ----------
void drawScreen() {
  u8g2.clearBuffer();

  // Frequency - large, centered
  u8g2.setFont(u8g2_font_logisoso24_tf);
  char buf[8];
  dtostrf(currentFreq, 5, 2, buf);
  u8g2.drawStr(8, 30, buf);
  u8g2.setFont(u8g2_font_7x14_tf);
  u8g2.drawStr(100, 30, "MHz");

  // Status line
  u8g2.setFont(u8g2_font_6x12_tf);
  if (searching) {
    u8g2.drawStr(0, 48, "Searching...");
  } else if (muted) {
    u8g2.drawStr(0, 48, "MUTE");
  } else {
    u8g2.drawStr(0, 48, stereo ? "ST" : "MO");

    // signal level bar (0-15)
    int barX = 30;
    int barW = 80;
    u8g2.drawFrame(barX, 40, barW, 8);
    int filled = map(signalLevel, 0, 15, 0, barW - 2);
    u8g2.drawBox(barX + 1, 41, filled, 6);
  }

  if (bandLimitReached) {
    u8g2.drawStr(0, 62, "Band limit reached");
  }

  u8g2.sendBuffer();
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_MUTE, INPUT_PULLUP);

  u8g2.begin();

  tuneTo(currentFreq);
  delay(100);
  readStatus(nullptr);
  drawScreen();
}

void loop() {
  unsigned long now = millis();

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
      toggleMute();
      drawScreen();
    }
  }

  // Periodic status polling DISABLED (test): was causing an audible
  // click every few seconds due to I2C bus switching noise coupling
  // into the TEA5767's unamplified audio output. Status now only
  // refreshes right after a search.
  /*
  static unsigned long lastPoll = 0;
  if (!searching && now - lastPoll > 5000) {
    lastPoll = now;
    readStatus(nullptr);
    drawScreen();
  }
  */
}
