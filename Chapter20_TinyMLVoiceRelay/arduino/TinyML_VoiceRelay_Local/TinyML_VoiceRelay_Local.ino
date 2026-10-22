/*
  Chapter 20 - TinyML_VoiceRelay_Local: voice-controlled relays, local version (no Wi-Fi, no Internet)

  Same voice control as TinyML_VoiceRelay, but without any network: there
  is no Wi-Fi, no app, no MQTT and no cloud service. The whole
  speech recognition runs on the ESP32 itself - the INMP441 microphone
  feeds an Edge Impulse keyword-spotting model (TinyML) that classifies
  the audio on-device, and the result drives the relays directly.

  Voice commands (two words, in sequence):
    "one" / "two" / "three" / "four"  -> selects relay 1..4
    then, within COMMAND_TIMEOUT_MS:
    "on" / "off"                      -> switches the selected relay
  "on" or "off" said without a number first are ignored. Saying another
  number while waiting replaces the selected relay.

  Model labels: four, noise, off, on, one, three, two, unknown
  ("noise" and "unknown" never trigger anything).

  Display: "Offline - TinyML" header, the state of the 4 relays and the
  last voice command heard.

  Hardware (classic ESP32 Dev Module):
    INMP441 -> WS 25, SCK 26, SD 33, L/R to GND, VDD 3.3V, GND
    SH1106  -> SDA 21, SCL 22 (I2C), VCC 3.3V, GND
    Relays  -> IN1 16, IN2 17, IN3 18, IN4 19 (module powered at 5V)

  Arduino IDE: Board "ESP32 Dev Module",
               Partition Scheme "Huge APP (3MB No OTA/1MB SPIFFS)".

  Libraries: U8g2 (olikraus),
             VoiceRelay_inferencing (Edge Impulse export, .zip).
*/

// Remove this define if the board runs out of RAM (saves about 10KB)
#define EIDSP_QUANTIZE_FILTERBANK 0

#include <VoiceRelay_inferencing.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <driver/i2s.h>

// ---------------- Pin configuration ----------------
#define PIN_I2S_MIC_WS    25
#define PIN_I2S_MIC_BCLK  26
#define PIN_I2S_MIC_SD    33

#define RELAY_ACTIVE_LOW true // most 4-channel modules switch on with a LOW input -
                              // check your module if the relays behave inverted
const int RELAY_PINS[4] = {16, 17, 18, 19};

// ---------------- Voice command parameters ----------------
const float CONFIDENCE_THRESHOLD          = 0.80; // minimum score to accept a keyword
const unsigned long KEYWORD_REFRACTORY_MS = 1000; // ignore the same keyword again for this long
                                                  // (one spoken word spans several windows)
const unsigned long COMMAND_TIMEOUT_MS    = 3000; // time allowed between the number and on/off
const unsigned long MESSAGE_SHOW_MS       = 3000; // how long a result stays on the display

// ---------------- Microphone / audio parameters ----------------
const int SAMPLE_RATE = EI_CLASSIFIER_FREQUENCY; // 16000 Hz, from the model
const int I2S_MIC_PORT = I2S_NUM_0;
const int MIC_GAIN = 4; // digital gain applied after the 32 -> 16 bit conversion

// Volume gate: a keyword is accepted only if real sound (above the room's
// background level) was picked up in the last VOICE_HOLD_MS. This stops the
// model from "hearing" words in a silent room.
const float VOICE_RMS_THRESHOLD   = 2000.0; // tune with the "Audio level" lines on Serial
const unsigned long VOICE_HOLD_MS = 1000;   // same length as the model window
const bool PRINT_AUDIO_LEVEL      = false;  // print the peak level once per second (calibration)

// ---------------- Display (SH1106, same as Chapter 17) ----------------
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// ---------------- Relay state ----------------
bool relayOn[4] = {false, false, false, false};

// ---------------- Voice command state machine ----------------
enum VoiceState {
  VOICE_WAIT_NUMBER, // listening for "one".."four"
  VOICE_WAIT_ACTION  // a relay is selected, waiting for "on"/"off"
};

VoiceState voiceState = VOICE_WAIT_NUMBER;
int pendingRelay = -1;                 // 0..3 while in VOICE_WAIT_ACTION
unsigned long pendingStartedMs = 0;
String lastKeyword = "";
unsigned long lastKeywordMs = 0;

// ---------------- Display state ----------------
String displayMessage = "Say a number";
unsigned long displayMessageMs = 0;
bool displayMessageTemporary = false;
bool displayDirty = true;

// ---------------- Audio buffers (double buffer, one model slice each) ----------------
struct InferenceBuffers {
  int16_t* buffers[2];
  volatile uint8_t bufSelect;   // buffer currently being filled by the capture task
  volatile uint8_t readyIndex;  // buffer ready for the classifier
  volatile bool bufReady;
  volatile uint32_t overruns;   // slices dropped because the classifier was busy
  uint32_t bufCount;
  uint32_t nSamples;
};

InferenceBuffers inference;
const int CAPTURE_FRAMES = 512; // I2S frames read per call in the capture task
TaskHandle_t captureTaskHandle = NULL;

// ---------------- Audio level (volume gate) ----------------
volatile unsigned long lastVoiceMs = 0; // last time the level was above VOICE_RMS_THRESHOLD
volatile float levelPeak = 0;           // highest RMS since the last print
unsigned long lastLevelPrintMs = 0;

// =====================================================================
//  RELAYS
// =====================================================================

void writeRelay(int index, bool on) {
  relayOn[index] = on;
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(RELAY_PINS[index], level ? HIGH : LOW);
}

void setRelay(int index, bool on) {
  writeRelay(index, on);
  Serial.printf("Relay %d -> %s\n", index + 1, on ? "ON" : "OFF");
  displayDirty = true;
}

// =====================================================================
//  DISPLAY
// =====================================================================

void showMessage(const String &msg, bool temporary) {
  displayMessage = msg;
  displayMessageTemporary = temporary;
  displayMessageMs = millis();
  displayDirty = true;
}

void drawMainScreen() {
  char line[32];

  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 10, "Offline - TinyML");

  u8g2.setFont(u8g2_font_ncenB10_tr);
  for (int i = 0; i < 4; i++) {
    int x = (i % 2) * 64;
    int y = 28 + (i / 2) * 16;
    snprintf(line, sizeof(line), "R%d %s", i + 1, relayOn[i] ? "ON" : "OFF");
    u8g2.drawStr(x, y, line);
  }

  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 63, displayMessage.c_str());
}

void maintainDisplay() {
  // Temporary messages (result of a command, timeout) go back to the
  // default prompt after MESSAGE_SHOW_MS
  if (displayMessageTemporary && millis() - displayMessageMs >= MESSAGE_SHOW_MS) {
    displayMessageTemporary = false;
    displayMessage = "Say a number";
    displayDirty = true;
  }

  if (!displayDirty) return;
  displayDirty = false;

  u8g2.clearBuffer();
  drawMainScreen();
  u8g2.sendBuffer();
}

// =====================================================================
//  VOICE COMMANDS (two-word state machine)
// =====================================================================

int numberFromLabel(const String &label) {
  if (label == "one")   return 0;
  if (label == "two")   return 1;
  if (label == "three") return 2;
  if (label == "four")  return 3;
  return -1;
}

void handleKeyword(const String &label) {
  int number = numberFromLabel(label);

  if (number >= 0) {
    // A number always (re)selects the relay, also while already waiting
    pendingRelay = number;
    pendingStartedMs = millis();
    voiceState = VOICE_WAIT_ACTION;
    Serial.println("Heard: " + label + " - waiting for on/off");
    showMessage("Heard: " + label + " - on/off?", false);
    return;
  }

  if (label == "on" || label == "off") {
    if (voiceState != VOICE_WAIT_ACTION) {
      Serial.println("Heard: " + label + " without a number, ignored");
      return;
    }
    bool on = (label == "on");
    const char* names[4] = {"one", "two", "three", "four"};
    String command = String(names[pendingRelay]) + " " + label;

    setRelay(pendingRelay, on);
    Serial.println("Voice command: " + command);
    showMessage("Done: " + command, true);

    voiceState = VOICE_WAIT_NUMBER;
    pendingRelay = -1;
  }
}

void maintainVoiceTimeout() {
  if (voiceState != VOICE_WAIT_ACTION) return;
  if (millis() - pendingStartedMs >= COMMAND_TIMEOUT_MS) {
    Serial.println("No on/off received, command cancelled");
    voiceState = VOICE_WAIT_NUMBER;
    pendingRelay = -1;
    showMessage("Timeout - say a number", true);
  }
}

// =====================================================================
//  MICROPHONE (INMP441 over I2S, same configuration as Chapter 19)
// =====================================================================

void setupI2SMic() {
  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false
  };

  i2s_pin_config_t pinConfig = {
    .bck_io_num = PIN_I2S_MIC_BCLK,
    .ws_io_num = PIN_I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = PIN_I2S_MIC_SD
  };

  i2s_driver_install((i2s_port_t)I2S_MIC_PORT, &i2sConfig, 0, NULL);
  i2s_set_pin((i2s_port_t)I2S_MIC_PORT, &pinConfig);
  i2s_zero_dma_buffer((i2s_port_t)I2S_MIC_PORT);
}

// Copies converted samples into the double buffer; when one buffer holds a
// full model slice it is handed to loop() and the other one is filled next.
void storeSamples(const int16_t* samples, int count) {
  for (int i = 0; i < count; i++) {
    inference.buffers[inference.bufSelect][inference.bufCount++] = samples[i];

    if (inference.bufCount >= inference.nSamples) {
      if (inference.bufReady) inference.overruns++; // previous slice not consumed yet
      inference.readyIndex = inference.bufSelect;
      inference.bufSelect ^= 1;
      inference.bufCount = 0;
      inference.bufReady = true;
    }
  }
}

// Runs on its own task: reads the microphone continuously so no audio is
// lost while loop() is busy with the classifier or the display.
void captureTask(void* param) {
  static int32_t rawBuffer[CAPTURE_FRAMES];
  static int16_t sampleBuffer[CAPTURE_FRAMES];

  for (;;) {
    size_t bytesRead = 0;
    i2s_read((i2s_port_t)I2S_MIC_PORT, rawBuffer, sizeof(rawBuffer), &bytesRead, portMAX_DELAY);
    int frameSamples = bytesRead / sizeof(int32_t);

    float sumSquares = 0;
    for (int i = 0; i < frameSamples; i++) {
      int32_t sample = (rawBuffer[i] >> 16) * MIC_GAIN; // 32 -> 16 bit, as in Chapter 19
      if (sample > 32767) sample = 32767;
      if (sample < -32768) sample = -32768;
      sampleBuffer[i] = (int16_t)sample;
      sumSquares += (float)sample * (float)sample;
    }

    // Level of this block (32 ms of audio) for the volume gate
    if (frameSamples > 0) {
      float rms = sqrtf(sumSquares / frameSamples);
      if (rms > levelPeak) levelPeak = rms;
      if (rms >= VOICE_RMS_THRESHOLD) lastVoiceMs = millis();
    }

    storeSamples(sampleBuffer, frameSamples);
  }
}

bool startMicrophone() {
  inference.nSamples = EI_CLASSIFIER_SLICE_SIZE;
  inference.buffers[0] = (int16_t*)malloc(inference.nSamples * sizeof(int16_t));
  inference.buffers[1] = (int16_t*)malloc(inference.nSamples * sizeof(int16_t));
  if (inference.buffers[0] == NULL || inference.buffers[1] == NULL) return false;

  inference.bufSelect = 0;
  inference.readyIndex = 1;
  inference.bufReady = false;
  inference.overruns = 0;
  inference.bufCount = 0;

  setupI2SMic();
  xTaskCreatePinnedToCore(captureTask, "audioCapture", 4096, NULL, 5, &captureTaskHandle, 0);
  return true;
}

// Callback used by the Edge Impulse SDK to read the slice being classified
int getAudioSignalData(size_t offset, size_t length, float* outPtr) {
  numpy::int16_to_float(&inference.buffers[inference.readyIndex][offset], outPtr, length);
  return 0;
}

// =====================================================================
//  KEYWORD SPOTTING (Edge Impulse, continuous mode)
// =====================================================================

void runVoiceInference() {
  if (!inference.bufReady) return; // no new slice yet
  inference.bufReady = false;

  signal_t signal;
  signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
  signal.get_data = &getAudioSignalData;
  ei_impulse_result_t result = {0};

  EI_IMPULSE_ERROR err = run_classifier_continuous(&signal, &result, false);
  if (err != EI_IMPULSE_OK) {
    Serial.printf("Classifier error: %d\n", err);
    return;
  }

  // Best label of this window
  size_t best = 0;
  for (size_t i = 1; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (result.classification[i].value > result.classification[best].value) best = i;
  }
  String label = result.classification[best].label;
  float score = result.classification[best].value;

  if (label == "noise" || label == "unknown") return;
  if (score < CONFIDENCE_THRESHOLD) return;

  // Volume gate: no real sound in the last window -> not a spoken word
  if (millis() - lastVoiceMs > VOICE_HOLD_MS) return;

  // The same spoken word is seen by several overlapping windows: accept it once
  if (label == lastKeyword && millis() - lastKeywordMs < KEYWORD_REFRACTORY_MS) return;
  lastKeyword = label;
  lastKeywordMs = millis();

  Serial.printf("Keyword: %s (%.2f)\n", label.c_str(), score);
  handleKeyword(label);
}

// Prints the peak audio level once per second, to choose VOICE_RMS_THRESHOLD:
// note the value in silence and while speaking, then pick a value in between.
void printAudioLevel() {
  if (!PRINT_AUDIO_LEVEL) return;
  if (millis() - lastLevelPrintMs < 1000) return;
  lastLevelPrintMs = millis();
  Serial.printf("Audio level (RMS peak): %.0f\n", levelPeak);
  levelPeak = 0;
}

// =====================================================================
//  SETUP / LOOP
// =====================================================================

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    writeRelay(i, false);
  }

  u8g2.begin();
  u8g2.setContrast(255);

  // Keyword spotting: the model runs on every new slice of audio
  run_classifier_init();
  if (!startMicrophone()) {
    Serial.println("Error: could not allocate the audio buffers");
    showMessage("Audio buffer error", false);
  }

  maintainDisplay();
  Serial.println("TinyML_VoiceRelay_Local ready.");
}

void loop() {
  runVoiceInference();
  printAudioLevel();
  maintainVoiceTimeout();
  maintainDisplay();
}
