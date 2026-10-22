/*
  Chapter 17 - Heart Rate & SpO2 (Part 1: OLED)
  Reads the MAX30102 sensor and shows heart rate and blood oxygen
  saturation (SpO2) on the OLED display, updated every second.
  Board:     ESP32 Dev Module
  Hardware:  MAX30102 and SH1106 OLED (I2C SDA 21, SCL 22)
  Libraries: SparkFun MAX3010x, U8g2
*/
#include <Wire.h>
#include <U8g2lib.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

// --- I2C bus (shared by SH1106 and MAX30102) ---
// SDA -> GPIO21, SCL -> GPIO22

// --- Display (SH1106, same as Chapter 6) ---
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// --- Sensor ---
MAX30105 particleSensor;

// --- Sample buffers (Maxim algorithm expects 100 samples at 25 sps = 4 s) ---
const int BUFFER_LENGTH = 100;
const int SHIFT_SAMPLES = 25;          // recompute every 25 new samples (1 s)
uint32_t irBuffer[BUFFER_LENGTH];
uint32_t redBuffer[BUFFER_LENGTH];
int bufferIndex = 0;

// --- Finger detection ---
const uint32_t FINGER_THRESHOLD = 50000; // IR level below this = no finger
bool fingerPresent = false;

// --- Results ---
int32_t heartRate = 0;
int8_t  heartRateValid = 0;
int32_t spo2 = 0;
int8_t  spo2Valid = 0;

// --- Heart rate moving average ---
const int   HR_HISTORY_SIZE   = 5;     // number of valid readings averaged
const float HR_MAX_DEVIATION  = 0.15;  // reject readings more than 15% off the average
const int   HR_MAX_REJECTIONS = 3;     // after 3 consecutive rejections, accept (real HR change)
int32_t hrHistory[HR_HISTORY_SIZE];
int hrCount = 0;                       // valid readings currently stored
int hrNextIndex = 0;                   // circular buffer write position
int hrRejections = 0;                  // consecutive rejected readings
int32_t averagedHeartRate = 0;

// --- Display timing (non-blocking) ---
unsigned long lastReadMillis = 0;
const unsigned long READ_INTERVAL = 1000; // 1 second

void setup() {
  Serial.begin(115200);

  u8g2.begin();
  u8g2.setContrast(255);

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("Error: MAX30102 not found. Check wiring.");
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 12, "MAX30102 not found");
    u8g2.sendBuffer();
    while (true) delay(1000);
  }

  // Sensor configuration: 100 sps averaged by 4 -> 25 sps effective
  byte ledBrightness = 60;   // 0 = off, 255 = 50 mA
  byte sampleAverage = 4;    // 1, 2, 4, 8, 16, 32
  byte ledMode       = 2;    // 2 = Red + IR
  int  sampleRate    = 100;  // samples per second
  int  pulseWidth    = 411;  // 69, 118, 215, 411 us
  int  adcRange      = 4096; // 2048, 4096, 8192, 16384
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);

  lastReadMillis = millis();

  updateDisplay();
}

void loop() {
  unsigned long now = millis();

  // --- Read all new samples from the sensor FIFO ---
  particleSensor.check();
  while (particleSensor.available()) {
    uint32_t red = particleSensor.getFIFORed();
    uint32_t ir  = particleSensor.getFIFOIR();
    particleSensor.nextSample();

    if (ir < FINGER_THRESHOLD) {
      // No finger: discard the buffer and invalidate the results
      fingerPresent = false;
      bufferIndex = 0;
      heartRateValid = 0;
      spo2Valid = 0;
      resetHeartRateAverage();
      continue;
    }

    fingerPresent = true;
    redBuffer[bufferIndex] = red;
    irBuffer[bufferIndex]  = ir;
    bufferIndex++;

    if (bufferIndex == BUFFER_LENGTH) {
      maxim_heart_rate_and_oxygen_saturation(irBuffer, BUFFER_LENGTH, redBuffer,
                                             &spo2, &spo2Valid, &heartRate, &heartRateValid);
      if (heartRateValid) {
        addHeartRateReading(heartRate);
      }

      // Drop the oldest 25 samples, keep the latest 75
      memmove(redBuffer, redBuffer + SHIFT_SAMPLES, (BUFFER_LENGTH - SHIFT_SAMPLES) * sizeof(uint32_t));
      memmove(irBuffer,  irBuffer  + SHIFT_SAMPLES, (BUFFER_LENGTH - SHIFT_SAMPLES) * sizeof(uint32_t));
      bufferIndex = BUFFER_LENGTH - SHIFT_SAMPLES;
    }
  }

  // --- Update display every 1s ---
  if (now - lastReadMillis >= READ_INTERVAL) {
    lastReadMillis = now;
    updateDisplay();
  }
}

void resetHeartRateAverage() {
  hrCount = 0;
  hrNextIndex = 0;
  hrRejections = 0;
  averagedHeartRate = 0;
}

void addHeartRateReading(int32_t newHeartRate) {
  // Outlier check: only once there are enough readings to trust the average
  if (hrCount >= 3) {
    float deviation = abs(newHeartRate - averagedHeartRate) / (float)averagedHeartRate;
    if (deviation > HR_MAX_DEVIATION) {
      hrRejections++;
      if (hrRejections < HR_MAX_REJECTIONS) {
        Serial.print("HR outlier rejected: ");
        Serial.println(newHeartRate);
        return;
      }
      // Too many consecutive rejections: the heart rate really changed, start over
      Serial.println("HR changed: restarting average");
      resetHeartRateAverage();
    }
  }
  hrRejections = 0;

  // Store in circular buffer
  hrHistory[hrNextIndex] = newHeartRate;
  hrNextIndex = (hrNextIndex + 1) % HR_HISTORY_SIZE;
  if (hrCount < HR_HISTORY_SIZE) hrCount++;

  // Recompute the average
  int32_t sum = 0;
  for (int i = 0; i < hrCount; i++) sum += hrHistory[i];
  averagedHeartRate = sum / hrCount;
}

void updateDisplay() {
  char hrStr[16];
  char spo2Str[16];
  char lineStr[32];

  if (fingerPresent && hrCount > 0) {
    snprintf(hrStr, sizeof(hrStr), "%ld", (long)averagedHeartRate);
  } else {
    snprintf(hrStr, sizeof(hrStr), "--");
  }

  if (fingerPresent && spo2Valid) {
    snprintf(spo2Str, sizeof(spo2Str), "%ld", (long)spo2);
  } else {
    snprintf(spo2Str, sizeof(spo2Str), "--");
  }

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_ncenB08_tr);
  if (!fingerPresent) {
    u8g2.drawStr(0, 10, "Place your finger");
  } else if (hrCount == 0 || !spo2Valid) {
    u8g2.drawStr(0, 10, "Measuring...");
  } else {
    u8g2.drawStr(0, 10, "Reading OK");
  }

  u8g2.setFont(u8g2_font_ncenB10_tr);
  snprintf(lineStr, sizeof(lineStr), "HR:   %s bpm", hrStr);
  u8g2.drawStr(0, 36, lineStr);
  snprintf(lineStr, sizeof(lineStr), "SpO2: %s %%", spo2Str);
  u8g2.drawStr(0, 58, lineStr);

  u8g2.sendBuffer();

  Serial.print("HR: ");
  Serial.print(hrStr);
  Serial.print(" bpm | SpO2: ");
  Serial.print(spo2Str);
  Serial.println(" %");
}
