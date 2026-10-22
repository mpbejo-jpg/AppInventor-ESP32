/*
  Chapter 19 - Video Doorphone
  Pressing the call button starts a call to the App Inventor app:
  two-way audio and video travel through the relay server
  (extra/github_render) over a secure WebSocket. The app can open the
  door through the relay. First setup from the "Videocitofono-Setup"
  Access Point (192.168.4.1); holding the button 5 s clears the Wi-Fi
  settings.
  Board:     Freenove ESP32-S3 WROOM Camera
  Hardware:  INMP441 microphone (WS 1, BCLK 14, SD 21), I2S amplifier
             (WS 38, BCLK 39, SD 47), button GPIO 41, door relay GPIO 42,
             status LED GPIO 48
  Libraries: ArduinoJson, WebSockets
*/
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>
#include "esp_camera.h"
#include "freertos/semphr.h"

#define PIN_I2S_MIC_WS    1
#define PIN_I2S_MIC_BCLK  14
#define PIN_I2S_MIC_SD    21
#define PIN_I2S_AMP_WS    38
#define PIN_I2S_AMP_BCLK  39
#define PIN_I2S_AMP_SD    47

#define PIN_BUTTON      41
#define PIN_RELAY       42
#define PIN_STATUS_LED  48

#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    15
#define CAM_PIN_SIOD    4
#define CAM_PIN_SIOC    5
#define CAM_PIN_D7      16
#define CAM_PIN_D6      17
#define CAM_PIN_D5      18
#define CAM_PIN_D4      12
#define CAM_PIN_D3      10
#define CAM_PIN_D2      8
#define CAM_PIN_D1      9
#define CAM_PIN_D0      11
#define CAM_PIN_VSYNC   6
#define CAM_PIN_HREF    7
#define CAM_PIN_PCLK    13

const char* AP_SSID = "Videocitofono-Setup";
const IPAddress AP_IP(192, 168, 4, 1);

const uint16_t RELAY_PORT = 443;
const char* RELAY_PATH = "/ws";
String relayHost;

const unsigned long LONG_PRESS_MS = 5000;
const unsigned long CALL_TIMEOUT_MS = 60000;
const unsigned long DEBOUNCE_MS = 50;
const unsigned long LED_BLINK_MS = 400;
const unsigned long WS_RECONNECT_MS = 15000;

const int SAMPLE_RATE = 16000;
const int I2S_MIC_PORT = I2S_NUM_0;
const int I2S_AMP_PORT = I2S_NUM_1;

enum DeviceState {
  STATE_PROVISIONING,
  STATE_IDLE,
  STATE_CALLING,
  STATE_IN_CALL
};

volatile DeviceState currentState = STATE_PROVISIONING;

Preferences preferences;
WebServer provisioningServer(80);
WebSocketsClient webSocket;

void setStatusLed(bool on) {
  digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
}

bool buttonPressed = false;
unsigned long buttonPressStart = 0;
bool longPressHandled = false;
unsigned long lastDebounceTime = 0;
int lastRawButtonState = HIGH;

unsigned long lastBlinkTime = 0;
bool ledBlinkState = false;

unsigned long callStartTime = 0;

bool wsConnected = false;

SemaphoreHandle_t wsMutex;

struct SendItem {
  uint8_t* data;
  size_t len;
};
QueueHandle_t audioSendQueue;
QueueHandle_t videoSendQueue;
TaskHandle_t networkSenderTaskHandle = NULL;

TaskHandle_t videoTaskHandle = NULL;
TaskHandle_t audioTaskHandle = NULL;

QueueHandle_t audioPlaybackQueue;
TaskHandle_t audioPlaybackTaskHandle = NULL;

void startProvisioning();
void handleSetWifi();
bool connectWiFiWithRetries(const String &ssid, const String &password);
void tryConnectStoredWifi();
void enterIdleState();
void enterCallingState();
void enterInCallState();
void backToIdleFromCall();
void handleButton();
void updateStatusLed();
void connectWebSocket();
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length);
void sendCallRequest();
void handleControlMessage(const uint8_t* payload, size_t length);
void handleOpenDoor();
void wsSendText(String text);
void wsSendBinary(uint8_t* data, size_t length);
void wsServiceLoop();
void setupCamera();
void setupI2SMic();
void setupI2SAmp();
void videoStreamTask(void* param);
void audioStreamTask(void* param);
void networkSenderTask(void* param);
void audioPlaybackTask(void* param);

void setup() {
  Serial.begin(115200);
  delay(200);

  wsMutex = xSemaphoreCreateRecursiveMutex();

  audioSendQueue = xQueueCreate(8, sizeof(SendItem));
  videoSendQueue = xQueueCreate(2, sizeof(SendItem));
  xTaskCreatePinnedToCore(networkSenderTask, "netSender", 4096, NULL, 2, &networkSenderTaskHandle, 1);

  audioPlaybackQueue = xQueueCreate(8, sizeof(SendItem));
  xTaskCreatePinnedToCore(audioPlaybackTask, "audioPlay", 4096, NULL, 1, &audioPlaybackTaskHandle, 1);

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);
  pinMode(PIN_STATUS_LED, OUTPUT);
  setStatusLed(false);

  preferences.begin("videocitof", false);

  String storedSsid = preferences.getString("ssid", "");
  String storedRelay = preferences.getString("relayhost", "");
  Serial.println("Boot: stored ssid=[" + storedSsid + "] relayhost=[" + storedRelay + "] free heap=" + String(ESP.getFreeHeap()));

  if (storedSsid.length() == 0) {
    startProvisioning();
  } else {
    tryConnectStoredWifi();
  }
}

void loop() {
  if (currentState == STATE_PROVISIONING) {
    provisioningServer.handleClient();
    updateStatusLed();
    return;
  }

  wsServiceLoop();
  handleButton();
  updateStatusLed();

  if (currentState == STATE_CALLING &&
      millis() - callStartTime > CALL_TIMEOUT_MS + 5000) {
    backToIdleFromCall();
  }
}

void startProvisioning() {
  currentState = STATE_PROVISIONING;

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID);

  provisioningServer.on("/setwifi", HTTP_POST, handleSetWifi);
  provisioningServer.on("/setwifi", HTTP_GET, handleSetWifi);
  provisioningServer.begin();

  Serial.print("Provisioning mode, connect to AP and open http://");
  Serial.println(AP_IP);
  Serial.println("Call /setwifi with ssid, password, relay_host (POST form-urlencoded, or GET query string)");
}

void handleSetWifi() {
  Serial.println("/setwifi received");
  Serial.printf("  ssid arg present: %d\n", provisioningServer.hasArg("ssid"));
  Serial.printf("  password arg present: %d\n", provisioningServer.hasArg("password"));
  Serial.printf("  relay_host arg present: %d\n", provisioningServer.hasArg("relay_host"));

  String ssid = provisioningServer.arg("ssid");
  String password = provisioningServer.arg("password");
  String relayHostArg = provisioningServer.arg("relay_host");

  Serial.println("  ssid value: [" + ssid + "]");
  Serial.println("  relay_host value: [" + relayHostArg + "]");

  if (ssid.length() == 0 || password.length() == 0 || relayHostArg.length() == 0) {
    Serial.println("  REJECTED: one or more fields empty, nothing saved");
    provisioningServer.send(400, "text/plain", "ssid, password and relay_host must all be non-empty");
    return;
  }

  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.putString("relayhost", relayHostArg);

  String checkSsid = preferences.getString("ssid", "");
  String checkRelay = preferences.getString("relayhost", "");
  Serial.println("  saved, read-back ssid: [" + checkSsid + "] relayhost: [" + checkRelay + "]");

  provisioningServer.send(200, "text/plain", "Credenziali salvate, riavvio in corso...");
  delay(500);
  ESP.restart();
}

bool connectWiFiWithRetries(const String &ssid, const String &password) {
  const int MAX_ATTEMPTS = 3;
  const unsigned long RETRY_DELAYS_MS[MAX_ATTEMPTS] = {0, 5000, 15000};
  const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

  for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
    if (RETRY_DELAYS_MS[attempt] > 0) {
      Serial.printf("WiFi retry %d/%d, waiting %lus before trying again\n",
                    attempt + 1, MAX_ATTEMPTS, RETRY_DELAYS_MS[attempt] / 1000);
      delay(RETRY_DELAYS_MS[attempt]);
    }

    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < WIFI_CONNECT_TIMEOUT_MS) {
      delay(300);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connected, IP: " + WiFi.localIP().toString());
      return true;
    }
  }

  Serial.println("WiFi connection failed after all retries");
  return false;
}

void tryConnectStoredWifi() {
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");
  relayHost = preferences.getString("relayhost", "");

  if (relayHost.length() == 0) {
    Serial.println("No relay host stored, falling back to provisioning");
    startProvisioning();
    return;
  }

  if (!connectWiFiWithRetries(ssid, password)) {
    Serial.println("WiFi connection failed, falling back to provisioning (credentials kept)");
    startProvisioning();
    return;
  }

  setupCamera();
  setupI2SMic();
  setupI2SAmp();
  connectWebSocket();

  enterIdleState();
}

void enterIdleState() {
  currentState = STATE_IDLE;
  setStatusLed(false);

}

void enterCallingState() {
  currentState = STATE_CALLING;
  Serial.println("DEBUG -> STATE_CALLING");
  setStatusLed(true);
  callStartTime = millis();

  sendCallRequest();

  if (videoTaskHandle == NULL) {
    xTaskCreatePinnedToCore(videoStreamTask, "videoStream", 8192, NULL, 1, &videoTaskHandle, 0);
  }
  if (audioTaskHandle == NULL) {
    xTaskCreatePinnedToCore(audioStreamTask, "audioStream", 6144, NULL, 1, &audioTaskHandle, 1);
  }
}

void enterInCallState() {
  currentState = STATE_IN_CALL;
  Serial.println("-> STATE_IN_CALL");
  setStatusLed(true);
}

void backToIdleFromCall() {
  enterIdleState();
}

void handleButton() {
  int reading = digitalRead(PIN_BUTTON);

  if (reading != lastRawButtonState) {
    Serial.println("DEBUG Button raw: " + String(reading));
    lastDebounceTime = millis();
  }

  if (millis() - lastDebounceTime > DEBOUNCE_MS) {
    bool isPressedNow = (reading == LOW);

    if (isPressedNow && !buttonPressed) {
      buttonPressed = true;
      buttonPressStart = millis();
      longPressHandled = false;
    } else if (!isPressedNow && buttonPressed) {
      buttonPressed = false;
      unsigned long pressDuration = millis() - buttonPressStart;

      if (!longPressHandled && pressDuration < LONG_PRESS_MS) {
        Serial.println("DEBUG Short press, state=" + String(currentState));
        if (currentState == STATE_IDLE) {
          enterCallingState();
        }
      }
    }
  }

  if (buttonPressed && !longPressHandled &&
      millis() - buttonPressStart >= LONG_PRESS_MS) {
    longPressHandled = true;

    if (currentState == STATE_IDLE) {
      Serial.println("Long press: clearing WiFi credentials");
      preferences.remove("ssid");
      preferences.remove("password");
      preferences.remove("relayhost");
      delay(200);
      ESP.restart();
    }
  }

  lastRawButtonState = reading;
}

void updateStatusLed() {
  if (currentState == STATE_PROVISIONING) {
    if (millis() - lastBlinkTime > LED_BLINK_MS) {
      lastBlinkTime = millis();
      ledBlinkState = !ledBlinkState;
      setStatusLed(ledBlinkState);
    }
  }
}

void connectWebSocket() {
  webSocket.beginSSL(relayHost.c_str(), RELAY_PORT, RELAY_PATH);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(WS_RECONNECT_MS);
  webSocket.enableHeartbeat(15000, 3000, 2);
}

void wsServiceLoop() {
  if (xSemaphoreTakeRecursive(wsMutex, portMAX_DELAY) == pdTRUE) {
    webSocket.loop();
    xSemaphoreGiveRecursive(wsMutex);
  }
}

void wsSendText(String text) {
  if (xSemaphoreTakeRecursive(wsMutex, portMAX_DELAY) == pdTRUE) {
    webSocket.sendTXT(text);
    xSemaphoreGiveRecursive(wsMutex);
  }
}

void wsSendBinary(uint8_t* data, size_t length) {
  if (xSemaphoreTakeRecursive(wsMutex, portMAX_DELAY) == pdTRUE) {
    webSocket.sendBIN(data, length);
    xSemaphoreGiveRecursive(wsMutex);
  }
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsConnected = true;
      Serial.println("WebSocket connected to relay, free heap=" + String(ESP.getFreeHeap()));
      wsSendText("{\"role\":\"esp32\"}");
      break;

    case WStype_DISCONNECTED:
      wsConnected = false;
      Serial.println("WebSocket disconnected, free heap=" + String(ESP.getFreeHeap()));
      break;

    case WStype_TEXT:
      handleControlMessage(payload, length);
      break;

    case WStype_BIN:
      if (length > 0 && payload[0] == 0x02) {
        if (currentState == STATE_IN_CALL) {
          size_t payloadLen = length - 1;
          uint8_t* buf = (uint8_t*)malloc(payloadLen);
          if (buf) {
            memcpy(buf, payload + 1, payloadLen);
            SendItem item = { buf, payloadLen };
            if (xQueueSend(audioPlaybackQueue, &item, 0) != pdTRUE) {
              free(buf);
            }
          }
        }
      }
      break;

    default:
      break;
  }
}

void sendCallRequest() {
  StaticJsonDocument<64> doc;
  doc["cmd"] = "call_request";
  String out;
  serializeJson(doc, out);
  wsSendText(out);
}

void handleControlMessage(const uint8_t* payload, size_t length) {
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    return;
  }

  const char* event = doc["event"];
  const char* cmd = doc["cmd"];

  if (event != nullptr) {
    if (strcmp(event, "app_connected") == 0 && currentState == STATE_CALLING) {
      enterInCallState();
    } else if (strcmp(event, "call_timeout") == 0 &&
               (currentState == STATE_CALLING || currentState == STATE_IN_CALL)) {
      backToIdleFromCall();
    }
  }

  if (cmd != nullptr) {
    if (strcmp(cmd, "open_door") == 0) {
      handleOpenDoor();
    }
  }
}

void handleOpenDoor() {
  digitalWrite(PIN_RELAY, HIGH);
  delay(1000);
  digitalWrite(PIN_RELAY, LOW);
}

void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_PIN_D0;
  config.pin_d1 = CAM_PIN_D1;
  config.pin_d2 = CAM_PIN_D2;
  config.pin_d3 = CAM_PIN_D3;
  config.pin_d4 = CAM_PIN_D4;
  config.pin_d5 = CAM_PIN_D5;
  config.pin_d6 = CAM_PIN_D6;
  config.pin_d7 = CAM_PIN_D7;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_sscb_sda = CAM_PIN_SIOD;
  config.pin_sscb_scl = CAM_PIN_SIOC;
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.xclk_freq_hz = 6000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
  }
}

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
}

void setupI2SAmp() {
  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false
  };

  i2s_pin_config_t pinConfig = {
    .bck_io_num = PIN_I2S_AMP_BCLK,
    .ws_io_num = PIN_I2S_AMP_WS,
    .data_out_num = PIN_I2S_AMP_SD,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install((i2s_port_t)I2S_AMP_PORT, &i2sConfig, 0, NULL);
  i2s_set_pin((i2s_port_t)I2S_AMP_PORT, &pinConfig);
}

void videoStreamTask(void* param) {
  for (;;) {
    if (currentState != STATE_CALLING && currentState != STATE_IN_CALL) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      uint8_t* frame = (uint8_t*)malloc(fb->len + 1);
      if (frame) {
        frame[0] = 0x01;
        memcpy(frame + 1, fb->buf, fb->len);
        SendItem item = { frame, fb->len + 1 };
        if (xQueueSend(videoSendQueue, &item, 0) != pdTRUE) {
          free(frame);
        }
      }
      esp_camera_fb_return(fb);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void audioStreamTask(void* param) {
  const int frameCount = 256;
  int32_t rawBuffer[frameCount];

  for (;;) {
    if (currentState != STATE_CALLING && currentState != STATE_IN_CALL) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    size_t bytesRead = 0;
    i2s_read((i2s_port_t)I2S_MIC_PORT, rawBuffer, sizeof(rawBuffer), &bytesRead, portMAX_DELAY);

    int frameSamples = bytesRead / sizeof(int32_t);

    uint8_t* outFrame = (uint8_t*)malloc(1 + frameSamples * 2);
    if (outFrame) {
      outFrame[0] = 0x02;

      for (int i = 0; i < frameSamples; i++) {
        int16_t sample16 = (int16_t)(rawBuffer[i] >> 16);
        outFrame[1 + i * 2] = sample16 & 0xFF;
        outFrame[1 + i * 2 + 1] = (sample16 >> 8) & 0xFF;
      }

      SendItem item = { outFrame, (size_t)(1 + frameSamples * 2) };
      if (xQueueSend(audioSendQueue, &item, pdMS_TO_TICKS(50)) != pdTRUE) {
        free(outFrame);
      }
    }

  }
}

void networkSenderTask(void* param) {
  SendItem item;
  for (;;) {
    bool sentSomething = false;

    while (xQueueReceive(audioSendQueue, &item, 0) == pdTRUE) {
      wsSendBinary(item.data, item.len);
      free(item.data);
      sentSomething = true;
    }

    if (xQueueReceive(videoSendQueue, &item, 0) == pdTRUE) {
      wsSendBinary(item.data, item.len);
      free(item.data);
      sentSomething = true;
    }

    if (!sentSomething) {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

void audioPlaybackTask(void* param) {
  SendItem item;
  for (;;) {
    if (xQueueReceive(audioPlaybackQueue, &item, portMAX_DELAY) == pdTRUE) {
      size_t sampleCount = item.len / 2;
      uint8_t* stereo = (uint8_t*)malloc(sampleCount * 4);
      if (stereo) {
        for (size_t i = 0; i < sampleCount; i++) {
          stereo[i * 4]     = item.data[i * 2];
          stereo[i * 4 + 1] = item.data[i * 2 + 1];
          stereo[i * 4 + 2] = item.data[i * 2];
          stereo[i * 4 + 3] = item.data[i * 2 + 1];
        }
        size_t bytesWritten = 0;
        i2s_write((i2s_port_t)I2S_AMP_PORT, stereo, sampleCount * 4, &bytesWritten, pdMS_TO_TICKS(200));
        free(stereo);
      }
      free(item.data);
    }
  }
}
