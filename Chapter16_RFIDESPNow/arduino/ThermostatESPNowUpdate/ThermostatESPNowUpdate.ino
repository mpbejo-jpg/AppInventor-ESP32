/*
  Chapter 16 - ESP-NOW Home Automation
  Phase 2: THERMOSTAT - touch UI + relay logic + Wi-Fi provisioning
           (same pattern as Chapter 10/11) + ESP-NOW receiver for the
           window sensor.

  Board: ESP32 with ILI9341 touch TFT (same wiring as Chapter 9 / Chapter 11)
  Sensor: DS18B20 (OneWire) - same probe used in previous chapters
  Actuators: 4-channel HL54S relay module
    RELAY_1 -> fan stage 1 (low speed)
    RELAY_2 -> fan stage 2 (medium speed)
    RELAY_3 -> fan stage 3 (high speed)
    RELAY_4 -> solenoid valve (elettrovalvola)

  Networking:
    - Wi-Fi STA/AP provisioning identical in spirit to Chapter 10/11:
      credentials stored in Preferences ("wifi_cfg"), AP fallback on
      192.168.4.1 if no credentials or STA connection fails, physical
      reset button (hold 5s) to erase stored credentials.
    - WebServer exposes REST-style endpoints for the App Inventor app.
    - ESP-NOW (broadcast, no pairing needed) receives the window sensor
      state directly from board 2. ESP-NOW does NOT need the home
      router: it keeps working even if the Wi-Fi STA connection is
      offline, so the thermostat can still block/unblock itself and
      run its local relay logic without any internet/router connection.
      Only the WebServer (App Inventor remote control) needs the STA
      connection to be reachable from the phone.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_now.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// ---------------- Pin configuration ----------------
// TFT + touch pins are configured in TFT_eSPI's User_Setup.h,
// same ILI9341 + XPT2046 wiring already used in Chapter 9 and Chapter 11.
#define ONE_WIRE_PIN        26   // DS18B20 data pin
#define RELAY_1_PIN         13   // Fan stage 1 (low) - same header row as GPIO33 on the
                                  // ESP32-WROOM-DA board (GPIO25 was tried but proved
                                  // unreliable on this specific board)
#define RELAY_2_PIN         33   // Fan stage 2 (medium)
#define RELAY_3_PIN         32   // Fan stage 3 (high)
#define RELAY_4_PIN         14   // Solenoid valve
#define RELAY_ACTIVE_LOW    true // HL54S modules energize the relay on a LOW signal
#define WIFI_RESET_BTN_PIN  27   // hold 5s to erase stored Wi-Fi credentials (same pattern as Chapter 10/11)
#define PAIRING_BTN_PIN     16   // press to open/close the ESP-NOW pairing window

// ---------------- Wi-Fi / networking parameters ----------------
const char* AP_SSID = "Thermostat_Setup";
const unsigned long RESET_HOLD_MS = 5000;

// ---------------- Thermostat parameters ----------------
const float STAGE_THRESHOLD_1 = 0.5f;
const float STAGE_THRESHOLD_2 = 1.0f;
const float STAGE_THRESHOLD_3 = 1.5f;
const float STAGE_HYSTERESIS  = 0.2f;
const float TEMP_STEP         = 0.5f;
const float TEMP_MIN          = 16.0f;
const float TEMP_MAX          = 30.0f;

const unsigned long TEMP_READ_INTERVAL_MS = 2000;
const unsigned long TOUCH_DEBOUNCE_MS     = 250;

// ---------------- Global objects ----------------
TFT_eSPI tft = TFT_eSPI();
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature tempSensor(&oneWire);
WebServer server(80);
Preferences preferences;

// ---------------- Networking state ----------------
bool staConnected = false;
bool wifiConnected = false;
unsigned long lastWifiBeginMs = 0;   // last time WiFi.begin() was called, for the retry cadence
unsigned long wifiFirstAttemptMs = 0; // when the very first attempt started, for the display message
const unsigned long WIFI_RETRY_INTERVAL_MS = 15000;
const unsigned long WIFI_DISPLAY_TIMEOUT_MS = 15000; // switch the on-screen message after this long
String savedSsid = "";
String savedPass = "";
unsigned long resetButtonPressedAtMs = 0;
bool resetButtonHeld = false;

// ---------------- ESP-NOW pairing state ----------------
bool pairingModeActive = false;
bool pairingBtnPreviouslyPressed = false;
unsigned long pairingBtnPressedAtMs = 0;
bool pairingBtnLongPressDone = false;
unsigned long clearedBannerUntilMs = 0; // shows a confirmation banner for a few seconds
unsigned long lastPairingBtnMs = 0;
const unsigned long PAIRING_BTN_DEBOUNCE_MS = 300;
const unsigned long PAIRING_CLEAR_HOLD_MS = 5000; // hold pairing button this long to clear all paired devices

// ---------------- ESP-NOW messages (window sensor + RFID badge node) ----------------
enum MessageType : uint8_t {
  MSG_PAIR_REQUEST = 0,
  MSG_PAIR_ACK     = 1,
  MSG_WINDOW_STATE = 2,
  MSG_BADGE_ADD    = 3,
  MSG_BADGE_REMOVE = 4,
  MSG_BADGE_CLEAR  = 5
};

enum DeviceType : uint8_t {
  DEVICE_WINDOW = 0,
  DEVICE_RFID   = 1
};

typedef struct {
  uint8_t type;
  uint8_t deviceType;   // only meaningful for MSG_PAIR_REQUEST / MSG_PAIR_ACK
  union {
    bool windowOpen;                  // MSG_WINDOW_STATE
    struct { char uid[16]; } badge;   // MSG_BADGE_ADD / MSG_BADGE_REMOVE
  };
} EspNowMessage;

bool windowOpen = false;
bool windowStateKnown = false;
unsigned long lastWindowMsgMs = 0;

// ---------------- Persisted ESP-NOW peers (any node that has paired) ----------------
// Separate namespace from wifi_cfg, so the existing Wi-Fi credential
// handling code is left untouched.
Preferences espnowPrefs;

uint8_t windowMac[6];   bool windowMacKnown = false;
uint8_t rfidMac[6];     bool rfidMacKnown   = false;

// ---------------- MQTT (CloudAMQP, TLS) ----------------
WiFiClientSecure wifiSecureClient;
PubSubClient mqttClient(wifiSecureClient);

String mqttBroker = "";
uint16_t mqttPort = 8883;
String mqttUser = "";
String mqttPass = "";

unsigned long lastMqttAttemptMs = 0;
const unsigned long MQTT_RETRY_INTERVAL_MS = 15000;

const char* TOPIC_BADGE_ADD    = "home/badge/add";
const char* TOPIC_BADGE_REMOVE = "home/badge/remove";
const char* TOPIC_BADGE_CLEAR  = "home/badge/clear";

const char* TOPIC_THERMOSTAT_SETTEMP   = "home/thermostat/settemp";
const char* TOPIC_THERMOSTAT_SETSEASON = "home/thermostat/setseason";
const char* TOPIC_THERMOSTAT_SETFAN    = "home/thermostat/setfan";
const char* TOPIC_THERMOSTAT_STATE     = "home/thermostat/state";

// ---------------- Thermostat state ----------------
float setTemperature    = 22.0f;
float sensedTemperature = 22.0f;
bool  summerMode        = true;   // true = summer (cooling), false = winter (heating)
bool  autoFanMode       = true;
int   fanStage          = 0;      // 0..3

// last values actually drawn on screen, used to redraw only what changed
float lastDrawnSetTemp     = -999;
float lastDrawnSensedTemp  = -999;
bool  lastDrawnSummerMode  = true;
bool  lastDrawnAutoMode    = true;
int   lastDrawnFanStage    = -1;
String lastDrawnWifiStatus = "";
bool  firstDraw            = true;

unsigned long lastTempReadMs  = 0;
unsigned long lastTouchMs     = 0;

// ---------------- Touch zones ----------------
// Redesigned layout to avoid the big set-temperature number overlapping
// the +/- buttons: the buttons are tall, narrow columns on the far left
// and right, and the number lives in the space between them.
struct TouchZone { int x, y, w, h; };

TouchZone zonePlus      = {248,  50,  66, 105};
TouchZone zoneMinus     = {  6,  50,  66, 105};
TouchZone zoneSeason    = {248,   4,  66,  40};
TouchZone zoneFanSmall  = {  8, 205, 145,  33};
TouchZone zoneFanLarge  = {167, 205, 145,  33};

// touch calibration data - run the standard TFT_eSPI calibration sketch
// (the same one used in Chapter 9) and paste your own values here.
uint16_t calData[5] = {459, 3291, 510, 3056, 7};

// =====================================================================
//  RELAY / FAN STAGE LOGIC
// =====================================================================

void setRelay(int pin, bool energize) {
  bool level = RELAY_ACTIVE_LOW ? !energize : energize;
  digitalWrite(pin, level ? HIGH : LOW);
}

void applyRelaysForStage(int stage) {
  // safety: whenever the window sensor reports the window open,
  // every relay is forced off regardless of the requested stage.
  if (windowOpen) stage = 0;

  bool valveOpen = (stage > 0);
  setRelay(RELAY_4_PIN, valveOpen);
  setRelay(RELAY_1_PIN, stage == 1);
  setRelay(RELAY_2_PIN, stage == 2);
  setRelay(RELAY_3_PIN, stage == 3);
}

// Computes the next automatic stage from the previous stage and the
// temperature error (delta). Uses hysteresis so the stage does not
// flicker back and forth around a threshold.
int computeAutoStage(int previousStage, float delta) {
  int stage = previousStage;

  // move up a stage (more cooling/heating power needed)
  if (stage == 0 && delta > STAGE_THRESHOLD_1) stage = 1;
  else if (stage == 1 && delta > STAGE_THRESHOLD_2) stage = 2;
  else if (stage == 2 && delta > STAGE_THRESHOLD_3) stage = 3;

  // move down a stage (less power needed, with hysteresis margin)
  if (stage == 3 && delta < STAGE_THRESHOLD_3 - STAGE_HYSTERESIS) stage = 2;
  if (stage == 2 && delta < STAGE_THRESHOLD_2 - STAGE_HYSTERESIS) stage = 1;
  if (stage == 1 && delta < STAGE_THRESHOLD_1 - STAGE_HYSTERESIS) stage = 0;

  return stage;
}

float computeTemperatureError() {
  // summer: positive error means "too hot", winter: positive error means "too cold"
  return summerMode ? (sensedTemperature - setTemperature)
                     : (setTemperature - sensedTemperature);
}

void updateAutoFanStage() {
  if (!autoFanMode) return;
  float delta = computeTemperatureError();
  fanStage = computeAutoStage(fanStage, delta);
  applyRelaysForStage(fanStage);
}

// =====================================================================
//  BUTTON ACTIONS
// =====================================================================

void increaseSetTemperature() {
  setTemperature += TEMP_STEP;
  if (setTemperature > TEMP_MAX) setTemperature = TEMP_MAX;
  publishState();
}

void decreaseSetTemperature() {
  setTemperature -= TEMP_STEP;
  if (setTemperature < TEMP_MIN) setTemperature = TEMP_MIN;
  publishState();
}

void toggleSeason() {
  summerMode = !summerMode;
  // switching season: force everything off and back to automatic,
  // so the system never starts the new mode with a wrong relay state
  autoFanMode = true;
  fanStage = 0;
  applyRelaysForStage(0);
  publishState();
}

void handleFanSmallPress() {
  if (autoFanMode) {
    autoFanMode = false; // enter manual mode, keep the current auto stage as starting point
  } else {
    fanStage = max(0, fanStage - 1);
  }
  applyRelaysForStage(fanStage);
  publishState();
}

void handleFanLargePress() {
  if (autoFanMode) {
    autoFanMode = false;
    fanStage = min(3, fanStage + 1);
  } else if (fanStage >= 3) {
    autoFanMode = true;   // already at max manual speed -> go back to automatic
  } else {
    fanStage++;
  }
  applyRelaysForStage(fanStage);
  publishState();
}

// =====================================================================
//  TOUCH HANDLING
// =====================================================================

bool isInZone(int tx, int ty, TouchZone z) {
  return (tx >= z.x && tx <= z.x + z.w && ty >= z.y && ty <= z.y + z.h);
}

void handleTouch() {
  if (windowOpen) return; // all touch buttons are disabled while the window is open

  uint16_t tx, ty;
  if (!tft.getTouch(&tx, &ty)) return;

  unsigned long now = millis();
  if (now - lastTouchMs < TOUCH_DEBOUNCE_MS) return;
  lastTouchMs = now;

  if (isInZone(tx, ty, zonePlus))          increaseSetTemperature();
  else if (isInZone(tx, ty, zoneMinus))    decreaseSetTemperature();
  else if (isInZone(tx, ty, zoneSeason))   toggleSeason();
  else if (isInZone(tx, ty, zoneFanSmall)) handleFanSmallPress();
  else if (isInZone(tx, ty, zoneFanLarge)) handleFanLargePress();
}

// =====================================================================
//  ESP-NOW (window sensor receiver)
// =====================================================================

bool addPeerIfMissing(const uint8_t *mac, uint8_t channel) {
  if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = channel; // 0 = use the thermostat's current Wi-Fi channel
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

// core 3.3.x callback signature: the sender MAC is inside "info",
// not passed directly as a separate argument like in older cores.
// Handles pairing requests from ANY node type (window sensor or RFID
// badge node) through a single generic pairing window, plus the
// window-state updates and badge commands once a node is paired.
void onEspNowMessageReceived(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(EspNowMessage)) return;
  EspNowMessage msg;
  memcpy(&msg, data, sizeof(msg));

  if (msg.type == MSG_PAIR_REQUEST) {
    if (!pairingModeActive) {
      // ignore pairing requests unless someone explicitly opened the
      // pairing window on this thermostat - prevents a node from
      // binding to the wrong thermostat when more than one is in range
      Serial.println("Pairing request ignored (pairing mode is closed)");
      return;
    }

    addPeerIfMissing(info->src_addr, 0);

    EspNowMessage ack;
    ack.type = MSG_PAIR_ACK;
    ack.deviceType = msg.deviceType;
    esp_now_send(info->src_addr, (uint8_t *)&ack, sizeof(ack));

    // persist this peer so it survives a thermostat reboot without a
    // new manual pairing - every node type that pairs gets remembered
    uint8_t currentChannel = WiFi.channel();
    if (msg.deviceType == DEVICE_WINDOW) {
      memcpy(windowMac, info->src_addr, 6);
      windowMacKnown = true;
      espnowPrefs.putBytes("windowMac", windowMac, 6);
      espnowPrefs.putUChar("windowCh", currentChannel);
      Serial.println("Window sensor paired and saved to flash");
    } else if (msg.deviceType == DEVICE_RFID) {
      memcpy(rfidMac, info->src_addr, 6);
      rfidMacKnown = true;
      espnowPrefs.putBytes("rfidMac", rfidMac, 6);
      espnowPrefs.putUChar("rfidCh", currentChannel);
      Serial.println("RFID node paired and saved to flash");
    }
    return;
  }

  if (msg.type == MSG_WINDOW_STATE) {
    windowOpen = msg.windowOpen;
    windowStateKnown = true;
    lastWindowMsgMs = millis();

    Serial.print("Window sensor state received: ");
    Serial.println(windowOpen ? "OPEN (thermostat blocked)" : "CLOSED");

    // immediately re-apply relay state so an open window cuts the
    // system off without waiting for the next loop iteration
    applyRelaysForStage(fanStage);
    publishState();
    return;
  }

  // MSG_BADGE_* only ever travel thermostat -> RFID node (see
  // forwardBadgeCommand()), so there is nothing to do with them here
  // if they were ever echoed back; no action needed.
}

void loadPersistedPeers() {
  size_t len = espnowPrefs.getBytes("windowMac", windowMac, 6);
  if (len == 6) {
    windowMacKnown = true;
    uint8_t ch = espnowPrefs.getUChar("windowCh", 1);
    addPeerIfMissing(windowMac, ch);
    Serial.println("Window sensor peer restored from flash");
  }

  len = espnowPrefs.getBytes("rfidMac", rfidMac, 6);
  if (len == 6) {
    rfidMacKnown = true;
    uint8_t ch = espnowPrefs.getUChar("rfidCh", 1);
    addPeerIfMissing(rfidMac, ch);
    Serial.println("RFID node peer restored from flash");
  }
}

// Forwards a badge ADD/REMOVE/CLEAR command to the RFID node. Silently
// dropped (with a log line) if that node has never paired yet.
void forwardBadgeCommand(MessageType type, const String &uid) {
  if (!rfidMacKnown) {
    Serial.println("Badge command ignored: RFID node not paired yet");
    return;
  }
  EspNowMessage msg;
  msg.type = type;
  msg.deviceType = DEVICE_RFID;
  memset(msg.badge.uid, 0, sizeof(msg.badge.uid));
  uid.toCharArray(msg.badge.uid, sizeof(msg.badge.uid));
  esp_now_send(rfidMac, (uint8_t *)&msg, sizeof(msg));
  Serial.print("Badge command forwarded to RFID node: ");
  Serial.println(uid);
}

void setupEspNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_recv_cb(onEspNowMessageReceived);

  espnowPrefs.begin("espnow_cfg", false);
  loadPersistedPeers();

  Serial.println("ESP-NOW receiver ready");
}

// =====================================================================
//  MQTT (CloudAMQP, TLS on port 8883) - badge commands from App Inventor
// =====================================================================

// Publishes the current thermostat state (same JSON previously served by
// the removed /state HTTP endpoint) so App Inventor gets it in real time
// instead of polling.
void publishState() {
  if (!mqttClient.connected()) return;
  mqttClient.publish(TOPIC_THERMOSTAT_STATE, buildStateJson().c_str());
}

void setThermostatTemperature(float value) {
  setTemperature = constrain(value, TEMP_MIN, TEMP_MAX);
  Serial.print("MQTT setpoint change: ");
  Serial.println(setTemperature);
  publishState();
}

void setThermostatSeason(bool wantSummer) {
  if (wantSummer != summerMode) toggleSeason(); // reuses the safe reset logic
  publishState();
}

// payload is either "auto" or "manual:<stage 0-3>", mirroring the old
// HTTP endpoint's mode/stage parameters without pulling in a JSON library
void setThermostatFan(const String &payload) {
  if (payload == "auto") {
    autoFanMode = true;
    updateAutoFanStage(); // recompute immediately so the published state
                           // reflects the real relay state right away
  } else if (payload.startsWith("manual:")) {
    autoFanMode = false;
    fanStage = constrain(payload.substring(7).toInt(), 0, 3);
    applyRelaysForStage(fanStage);
  }
  publishState();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String payloadStr;
  for (unsigned int i = 0; i < length; i++) payloadStr += (char)payload[i];
  payloadStr.trim();

  String topicStr(topic);
  if (topicStr == TOPIC_BADGE_ADD) {
    forwardBadgeCommand(MSG_BADGE_ADD, payloadStr);
  } else if (topicStr == TOPIC_BADGE_REMOVE) {
    forwardBadgeCommand(MSG_BADGE_REMOVE, payloadStr);
  } else if (topicStr == TOPIC_BADGE_CLEAR) {
    forwardBadgeCommand(MSG_BADGE_CLEAR, "");
  } else if (topicStr == TOPIC_THERMOSTAT_SETTEMP) {
    setThermostatTemperature(payloadStr.toFloat());
  } else if (topicStr == TOPIC_THERMOSTAT_SETSEASON) {
    setThermostatSeason(payloadStr == "summer");
  } else if (topicStr == TOPIC_THERMOSTAT_SETFAN) {
    setThermostatFan(payloadStr);
  }
}

void setupMqtt() {
  wifiSecureClient.setInsecure();
  mqttClient.setServer(mqttBroker.c_str(), mqttPort);
  mqttClient.setCallback(mqttCallback);
}

// Non-blocking, same retry pattern as maintainWifiConnection(): only
// tries once the Wi-Fi STA link is actually up, and only if MQTT
// credentials have been provisioned at all.
void maintainMqttConnection() {
  if (!wifiConnected) return;
  if (mqttBroker.length() == 0) return;

  if (mqttClient.connected()) {
    mqttClient.loop();
    return;
  }

  if (millis() - lastMqttAttemptMs < MQTT_RETRY_INTERVAL_MS) return;
  lastMqttAttemptMs = millis();

  String clientId = "thermostat-" + WiFi.macAddress();
  Serial.print("Connecting to MQTT broker...");
  bool ok = mqttUser.length() > 0
    ? mqttClient.connect(clientId.c_str(), mqttUser.c_str(), mqttPass.c_str())
    : mqttClient.connect(clientId.c_str());

  if (ok) {
    Serial.println(" connected");
    mqttClient.subscribe(TOPIC_BADGE_ADD);
    mqttClient.subscribe(TOPIC_BADGE_REMOVE);
    mqttClient.subscribe(TOPIC_BADGE_CLEAR);
    mqttClient.subscribe(TOPIC_THERMOSTAT_SETTEMP);
    mqttClient.subscribe(TOPIC_THERMOSTAT_SETSEASON);
    mqttClient.subscribe(TOPIC_THERMOSTAT_SETFAN);
    publishState(); // give the app a fresh snapshot as soon as we connect
  } else {
    Serial.print(" failed, rc=");
    Serial.println(mqttClient.state());
  }
}

// =====================================================================
//  WI-FI PROVISIONING (same pattern as Chapter 11: non-blocking STA
//  connection, retried from loop() instead of blocking setup())
// =====================================================================

void startAccessPoint() {
  WiFi.mode(WIFI_AP_STA); // AP for provisioning, STA stays active for ESP-NOW
  WiFi.softAP(AP_SSID);
  Serial.print("AP mode started, connect to: ");
  Serial.println(AP_SSID);
  Serial.print("Configuration address: http://");
  Serial.println(WiFi.softAPIP());
}

// Resets the WiFi radio state fully before starting a new connection
// attempt: on some ESP32 boards, reconnecting right after a reset/power
// cycle fails intermittently (works one time out of two) because leftover
// radio state from the previous session blocks a clean reassociation.
// Explicitly cycling WIFI_OFF -> WIFI_STA and disabling modem sleep avoids
// that. This function only KICKS OFF the attempt, it does not wait for it.
void startWifiRadio() {
  Serial.println("Starting WiFi radio...");

  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  delay(100);

  WiFi.begin(savedSsid.c_str(), savedPass.c_str());

  wifiConnected = false;
  lastWifiBeginMs = millis();
  wifiFirstAttemptMs = millis();
}

// Called every loop() iteration. Non-blocking: identical pattern to
// Chapter 11's maintainWifiConnection(). Detects connect/disconnect
// transitions and retries WiFi.begin() every WIFI_RETRY_INTERVAL_MS
// while offline - indefinitely, never falling back to AP on its own.
// AP mode is only ever entered at boot if there are no saved
// credentials at all (handled in setup()).
void maintainWifiConnection() {
  if (savedSsid.length() == 0) return;

  bool nowConnected = (WiFi.status() == WL_CONNECTED);

  if (nowConnected != wifiConnected) {
    wifiConnected = nowConnected;
    staConnected = wifiConnected;
    Serial.println(wifiConnected ? "WiFi connected" : "WiFi connection lost");
    if (wifiConnected) {
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    }
  }

  if (!wifiConnected && millis() - lastWifiBeginMs >= WIFI_RETRY_INTERVAL_MS) {
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());
    lastWifiBeginMs = millis();
  }
}

void handleSetConfig() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "Missing ssid");
    return;
  }
  String ssid = server.arg("ssid");
  String pass = server.hasArg("pass") ? server.arg("pass") : "";

  preferences.putString("ssid", ssid);
  preferences.putString("pass", pass);

  if (server.hasArg("mqttBroker")) preferences.putString("mqttBroker", server.arg("mqttBroker"));
  if (server.hasArg("mqttPort")) preferences.putUShort("mqttPort", server.arg("mqttPort").toInt());
  if (server.hasArg("mqttUser")) preferences.putString("mqttUser", server.arg("mqttUser"));
  if (server.hasArg("mqttPass")) preferences.putString("mqttPass", server.arg("mqttPass"));

  server.send(200, "text/plain", "OK, rebooting");
  delay(300);
  ESP.restart();
}

// =====================================================================
//  STATE JSON - now published over MQTT instead of served over HTTP
// =====================================================================

String buildStateJson() {
  int reportedStage = windowOpen ? 0 : fanStage; // relays are forced off while the window is open
  String json = "{";
  json += "\"sensed\":" + String(sensedTemperature, 1) + ",";
  json += "\"set\":" + String(setTemperature, 1) + ",";
  json += "\"season\":\"" + String(summerMode ? "summer" : "winter") + "\",";
  json += "\"fanMode\":\"" + String(autoFanMode ? "auto" : "manual") + "\",";
  json += "\"fanStage\":" + String(reportedStage) + ",";
  json += "\"windowOpen\":" + String(windowOpen ? "true" : "false") + ",";
  json += "\"windowKnown\":" + String(windowStateKnown ? "true" : "false");
  json += "}";
  return json;
}

void setupWebServer() {
  server.on("/setconfig", HTTP_POST, handleSetConfig);
  server.begin();
  Serial.println("Web server started (configuration only)");
}

// =====================================================================
//  PHYSICAL RESET BUTTON (hold 5s to erase Wi-Fi credentials)
// =====================================================================

void handleResetButton() {
  bool pressed = (digitalRead(WIFI_RESET_BTN_PIN) == LOW);

  if (pressed && !resetButtonHeld) {
    resetButtonHeld = true;
    resetButtonPressedAtMs = millis();
  } else if (!pressed && resetButtonHeld) {
    resetButtonHeld = false;
  } else if (pressed && resetButtonHeld) {
    if (millis() - resetButtonPressedAtMs >= RESET_HOLD_MS) {
      Serial.println("Reset button held: erasing Wi-Fi credentials");
      preferences.remove("ssid");
      preferences.remove("pass");
      delay(300);
      ESP.restart();
    }
  }
}

// =====================================================================
//  PAIRING BUTTON (single press: open/close pairing. Hold 5s: clear
//  every paired device immediately, both from flash and from the live
//  ESP-NOW peer table, instead of leaving orphaned entries behind when
//  a physical device is replaced with a different one of the same type)
// =====================================================================

// Removes both the window sensor and the RFID node from Preferences AND
// from the live ESP-NOW peer table, so a replaced physical device never
// leaves an orphaned entry behind (see the chapter's discussion on why
// addPeerIfMissing() alone cannot clean up a peer that changed MAC).
void clearAllPairedDevices() {
  if (windowMacKnown && esp_now_is_peer_exist(windowMac)) esp_now_del_peer(windowMac);
  if (rfidMacKnown && esp_now_is_peer_exist(rfidMac)) esp_now_del_peer(rfidMac);

  espnowPrefs.remove("windowMac");
  espnowPrefs.remove("windowCh");
  espnowPrefs.remove("rfidMac");
  espnowPrefs.remove("rfidCh");

  windowMacKnown = false;
  rfidMacKnown = false;

  clearedBannerUntilMs = millis() + 3000; // shown by updateDynamicUI()
  Serial.println("Pairing button held 5s: all paired devices cleared");
}

void handlePairingButton() {
  bool pressed = (digitalRead(PAIRING_BTN_PIN) == LOW);
  unsigned long now = millis();

  if (pressed && !pairingBtnPreviouslyPressed) {
    // press just started - remember when, so a later loop() iteration
    // can tell a short tap from a 5s hold
    pairingBtnPressedAtMs = now;
    pairingBtnLongPressDone = false;
  } else if (pressed && pairingBtnPreviouslyPressed) {
    // still held down: watch for the 5s threshold
    if (!pairingBtnLongPressDone && (now - pairingBtnPressedAtMs >= PAIRING_CLEAR_HOLD_MS)) {
      clearAllPairedDevices();
      pairingBtnLongPressDone = true; // so releasing the button does not also toggle pairing mode
    }
  } else if (!pressed && pairingBtnPreviouslyPressed) {
    // button released: a short tap toggles the pairing window, exactly
    // as before - but only if this release did not follow a 5s hold
    // that already triggered the clear action above
    if (!pairingBtnLongPressDone && (now - lastPairingBtnMs > PAIRING_BTN_DEBOUNCE_MS)) {
      pairingModeActive = !pairingModeActive;
      lastPairingBtnMs = now;
      Serial.println(pairingModeActive ? "Pairing window OPENED" : "Pairing window CLOSED");
    }
  }
  pairingBtnPreviouslyPressed = pressed;
}

// =====================================================================
//  DISPLAY
// =====================================================================

uint16_t currentThemeColor() {
  return TFT_YELLOW; // fixed colour, no longer changes with the season
}

void drawDegreeMark(int x, int y, uint16_t color) {
  tft.drawCircle(x, y, 3, color);
}

// Open-window bitmap, 80x80px, generated directly from the reference PNG
// with LSB-first bit packing (the actual bit order TFT_eSPI's
// drawXBitmap() reads: `byte & (1 << (i & 7))`, confirmed against the
// library source - NOT MSB-first like drawBitmap() or like image2cpp's
// default export). 1 = draw the pixel, 0 = transparent/background.
const unsigned char windowOpenIcon80x80[] PROGMEM = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x0c, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78,
	0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xec, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x67, 0x8c, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x63, 0x0c, 0x07,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x61, 0x0c, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xf0, 0x60, 0x0c, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x60, 0x0c, 0x76, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xce, 0x60, 0x0c, 0xe6, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x60,
	0x0c, 0x86, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc3, 0x60, 0x0c, 0x06, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xc1, 0x60, 0x0c, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x80, 0xc0, 0x60, 0x0c, 0x06,
	0x03, 0x00, 0x00, 0x00, 0x00, 0x80, 0xc0, 0x60, 0x0c, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x80,
	0xc0, 0x60, 0x0c, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x80, 0xc0, 0x60, 0x0c, 0x06, 0x03, 0x00,
	0x00, 0x00, 0x00, 0x80, 0xc0, 0x60, 0x0c, 0x06, 0x03, 0x00, 0x00, 0x00, 0x03, 0x80, 0xc0, 0x60,
	0x0c, 0x06, 0x03, 0x00, 0x00, 0xc0, 0x0f, 0x80, 0xc0, 0x60, 0x0c, 0x06, 0x03, 0x00, 0x00, 0xe0,
	0x1c, 0x80, 0xc0, 0x60, 0x0c, 0x06, 0x03, 0x00, 0x00, 0x60, 0x18, 0x80, 0xc0, 0x60, 0x0c, 0x06,
	0x03, 0x00, 0x00, 0x30, 0x30, 0x80, 0xc0, 0x60, 0x0c, 0x06, 0x03, 0x00, 0x00, 0x30, 0x30, 0x80,
	0xc0, 0x60, 0x3c, 0x06, 0x03, 0x00, 0x00, 0x60, 0x18, 0x80, 0xc0, 0x78, 0xfc, 0x07, 0x03, 0x00,
	0x00, 0xe0, 0x1c, 0x80, 0xc0, 0x7f, 0xcc, 0x0f, 0x03, 0x00, 0x00, 0xc0, 0x0f, 0x80, 0xe0, 0x67,
	0x0c, 0x7e, 0x03, 0x00, 0x00, 0x00, 0x03, 0x80, 0xfc, 0x60, 0x0c, 0xf6, 0x03, 0x00, 0x00, 0x00,
	0x00, 0x80, 0xcf, 0x60, 0x0c, 0x86, 0x03, 0x00, 0x00, 0x00, 0x00, 0x80, 0xc3, 0x60, 0x0c, 0x06,
	0x1f, 0x00, 0x00, 0x00, 0x00, 0x80, 0xc0, 0x60, 0x0c, 0x06, 0xff, 0x03, 0x00, 0x00, 0x00, 0xc0,
	0xc0, 0x60, 0x0c, 0x06, 0xe3, 0x7f, 0x00, 0x00, 0xc0, 0xff, 0xc0, 0x60, 0x0c, 0x06, 0x03, 0xf8,
	0x03, 0x00, 0xfe, 0xff, 0xc0, 0x60, 0x0c, 0x06, 0x03, 0x80, 0x1f, 0xc0, 0x1f, 0x80, 0xc0, 0x60,
	0x0c, 0x06, 0x03, 0x00, 0x3c, 0xf0, 0x01, 0x80, 0xc0, 0x60, 0x0c, 0x06, 0x03, 0x00, 0xf0, 0x3c,
	0x00, 0x80, 0xc0, 0x60, 0x0c, 0x06, 0x03, 0x00, 0xc0, 0x0f, 0x00, 0x80, 0xc0, 0x60, 0x0c, 0x06,
	0x03, 0x00, 0x80, 0x03, 0x00, 0x80, 0xc0, 0x60, 0x0c, 0x06, 0x03, 0x00, 0xc0, 0x01, 0x00, 0x80,
	0xc0, 0x60, 0x0c, 0x06, 0x03, 0x00, 0x70, 0x00, 0x00, 0x80, 0xc0, 0x60, 0x0c, 0x06, 0x03, 0x00,
	0x38, 0x00, 0x00, 0x80, 0xc0, 0x60, 0x0c, 0x06, 0x03, 0x00, 0x1c, 0x00, 0x00, 0x80, 0xc0, 0x60,
	0x0c, 0x06, 0x03, 0x00, 0x0e, 0x00, 0x00, 0x80, 0xc0, 0x60, 0x0c, 0x06, 0x03, 0x00, 0x07, 0x00,
	0x00, 0x80, 0xc0, 0x60, 0x0c, 0x06, 0x03, 0x00, 0x03, 0x00, 0x00, 0x80, 0xc0, 0x60, 0x0c, 0x06,
	0x03, 0x80, 0x01, 0x00, 0x00, 0x80, 0xc0, 0x60, 0x0c, 0x06, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xc1, 0x60, 0x0c, 0xe6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xcf, 0x60, 0x0c, 0xfe, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xfe, 0x60, 0x0c, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x61,
	0xec, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x67, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x7e, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// classic 6-point snowflake: 3 crossing lines through the centre, each
// with small perpendicular ticks near both ends
void drawSnowflake(int cx, int cy, int r, uint16_t color) {
  for (int i = 0; i < 3; i++) {
    float angle = i * 60.0f * DEG_TO_RAD;
    float dx = r * cos(angle);
    float dy = r * sin(angle);
    tft.drawLine(cx - dx, cy - dy, cx + dx, cy + dy, color);

    for (int sign = -1; sign <= 1; sign += 2) {
      float tickX = cx + sign * dx * 0.6f;
      float tickY = cy + sign * dy * 0.6f;
      float perp = angle + 90.0f * DEG_TO_RAD;
      int tx1 = tickX + 4 * cos(perp);
      int ty1 = tickY + 4 * sin(perp);
      int tx2 = tickX - 4 * cos(perp);
      int ty2 = tickY - 4 * sin(perp);
      tft.drawLine(tx1, ty1, tx2, ty2, color);
    }
  }
}

void drawSun(int cx, int cy, int r, uint16_t color) {
  tft.fillCircle(cx, cy, r / 2, color);
  for (int i = 0; i < 8; i++) {
    float angle = i * 45.0f * DEG_TO_RAD;
    int x1 = cx + round((r / 2 + 3) * cos(angle));
    int y1 = cy + round((r / 2 + 3) * sin(angle));
    int x2 = cx + round(r * cos(angle));
    int y2 = cy + round(r * sin(angle));
    tft.drawLine(x1, y1, x2, y2, color);
    tft.drawLine(x1 + 1, y1, x2 + 1, y2, color); // thicken the ray a bit
  }
}

// pinwheel-style fan icon: hub + 3 triangular blades with a rounded tip,
// much easier to read at a glance than plain lines
void drawFanIcon(int cx, int cy, int r, uint16_t color) {
  tft.fillCircle(cx, cy, 3, color);
  for (int i = 0; i < 3; i++) {
    float angle = i * 120.0f * DEG_TO_RAD;
    float perp = angle + 90.0f * DEG_TO_RAD;
    int tipX = cx + round(r * cos(angle));
    int tipY = cy + round(r * sin(angle));
    int base1X = cx + round(4 * cos(perp));
    int base1Y = cy + round(4 * sin(perp));
    int base2X = cx - round(4 * cos(perp));
    int base2Y = cy - round(4 * sin(perp));
    tft.fillTriangle(base1X, base1Y, base2X, base2Y, tipX, tipY, color);
    tft.fillCircle(tipX, tipY, 3, color);
  }
}

void drawButtonBox(TouchZone z, uint16_t color) {
  tft.drawRoundRect(z.x, z.y, z.w, z.h, 8, color);
}

void drawFanButton(TouchZone z, uint16_t color, int iconRadius, const char* label) {
  drawButtonBox(z, color);
  int iconCx = z.x + 28;
  int iconCy = z.y + z.h / 2;
  drawFanIcon(iconCx, iconCy, iconRadius, color);

  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString(label, z.x + 55, z.y + (z.h - 16) / 2, 1);
}

void drawStaticUI() {
  tft.fillScreen(TFT_BLACK);
  uint16_t color = currentThemeColor();

  drawButtonBox(zonePlus, color);
  drawButtonBox(zoneMinus, color);

  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(4);
  tft.drawCentreString("+", zonePlus.x + zonePlus.w / 2, zonePlus.y + zonePlus.h / 2 - 14, 1);
  tft.drawCentreString("-", zoneMinus.x + zoneMinus.w / 2, zoneMinus.y + zoneMinus.h / 2 - 14, 1);

  drawFanButton(zoneFanSmall, color, 9, "MIN");
  drawFanButton(zoneFanLarge, color, 13, "MAX");

  drawSeasonIcon(true); // force-draw season icon
}

void drawSeasonIcon(bool force) {
  static bool lastSummer = !summerMode; // force mismatch on first call
  if (!force && lastSummer == summerMode) return;
  lastSummer = summerMode;

  uint16_t color = currentThemeColor();
  int cx = zoneSeason.x + zoneSeason.w / 2;
  int cy = zoneSeason.y + zoneSeason.h / 2;

  tft.fillRect(zoneSeason.x, zoneSeason.y, zoneSeason.w, zoneSeason.h, TFT_BLACK);
  drawButtonBox(zoneSeason, color);
  if (summerMode) drawSnowflake(cx, cy, 16, color);  // tap to switch TO winter
  else            drawSun(cx, cy, 16, color);        // tap to switch TO summer
}

void updateDynamicUI() {
  uint16_t color = currentThemeColor();

  static bool lastWindowOpenForLayout = false;
  static bool layoutFirstDraw = true;
  bool windowJustChanged = layoutFirstDraw || (lastWindowOpenForLayout != windowOpen);

  // full redraw if the color theme changed (season toggled)
  if (firstDraw || lastDrawnSummerMode != summerMode) {
    drawStaticUI();
    lastDrawnSummerMode = summerMode;
    firstDraw = false;
    lastDrawnSetTemp = -999;
    lastDrawnSensedTemp = -999;
    lastDrawnFanStage = -1;
    lastDrawnWifiStatus = ""; // the fillScreen in drawStaticUI() wiped the IP text too
    windowJustChanged = true; // force the centre area to redraw too
  } else {
    drawSeasonIcon(false);
  }

  // small sensed temperature, top-left - always shown, window or not
  if (lastDrawnSensedTemp != sensedTemperature) {
    tft.fillRect(5, 5, 95, 34, TFT_BLACK);
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString(String(sensedTemperature, 1), 5, 5, 1);
    drawDegreeMark(5 + tft.textWidth(String(sensedTemperature, 1)) + 4, 8, color);
    lastDrawnSensedTemp = sensedTemperature;
  }

  // small, always-visible Wi-Fi status line (same idea as Chapter 11's
  // persistent small IP indicator): "Connecting..." until the STA
  // connection succeeds (or the message switches to a failure notice
  // after WIFI_DISPLAY_TIMEOUT_MS - the retry itself keeps going in the
  // background regardless, this only changes what is shown on screen).
  String wifiStatus;
  if (savedSsid.length() == 0) {
    wifiStatus = "AP " + WiFi.softAPIP().toString();
  } else if (wifiConnected) {
    wifiStatus = WiFi.localIP().toString();
  } else if (millis() - wifiFirstAttemptMs >= WIFI_DISPLAY_TIMEOUT_MS) {
    wifiStatus = "Unable to connect, check credentials";
  } else {
    wifiStatus = "Connecting...";
  }
  if (wifiStatus != lastDrawnWifiStatus) {
    tft.fillRect(105, 5, 138, 26, TFT_BLACK);
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(1);
    if (wifiStatus == "Unable to connect, check credentials") {
      tft.drawString("Unable to connect,", 105, 5, 1);
      tft.drawString("check credentials", 105, 15, 1);
    } else {
      tft.drawString(wifiStatus, 105, 5, 1);
    }
    lastDrawnWifiStatus = wifiStatus;
  }

  // centre area: big set temperature, OR the open-window icon while blocked
  // confined between the +/- button columns (end at x=72, start at x=248)
  if (windowJustChanged) {
    tft.fillRect(74, 50, 172, 105, TFT_BLACK);
    if (windowOpen) {
      tft.drawXBitmap(120, 62, windowOpenIcon80x80, 80, 80, TFT_YELLOW);
    }
    lastDrawnSetTemp = -999; // force the number to redraw once the window closes again
  }
  if (!windowOpen && lastDrawnSetTemp != setTemperature) {
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(6);
    tft.drawCentreString(String(setTemperature, 1), 160, 78, 1);
    lastDrawnSetTemp = setTemperature;
  }

  // fan stage number + AUTO/MAN label - forced to 0 while the window is open
  int displayedStage = windowOpen ? 0 : fanStage;
  if (lastDrawnFanStage != displayedStage || lastDrawnAutoMode != autoFanMode || windowJustChanged) {
    tft.fillRect(90, 160, 140, 25, TFT_BLACK);
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(2);
    String label = String(displayedStage) + "  " + (autoFanMode ? "AUTO" : "MAN");
    tft.drawCentreString(label, 160, 162, 1);
    lastDrawnFanStage = displayedStage;
    lastDrawnAutoMode = autoFanMode;
  }

  // window-open warning banner / pairing indicator / cleared-devices confirmation,
  // only redrawn when something actually changes
  static bool lastDrawnPairingMode = false;
  static bool lastDrawnClearedBanner = false;
  static bool bannerFirstDraw = true;
  bool showClearedBanner = (millis() < clearedBannerUntilMs);
  if (bannerFirstDraw || windowJustChanged || lastDrawnPairingMode != pairingModeActive || lastDrawnClearedBanner != showClearedBanner) {
    tft.fillRect(0, 186, 320, 16, TFT_BLACK);
    if (showClearedBanner) {
      // takes priority over everything else for its brief 3s window: it
      // confirms a deliberate action the person just triggered on purpose
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.setTextSize(1);
      tft.drawCentreString("PAIRED DEVICES CLEARED", 160, 189, 1);
    } else if (pairingModeActive) {
      // pairing takes priority on the banner line: it is a deliberate,
      // time-limited action the person just triggered on purpose
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.setTextSize(1);
      tft.drawCentreString("PAIRING MODE ACTIVE - PRESS BUTTON TO CLOSE", 160, 189, 1);
    } else if (windowOpen) {
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.setTextSize(1);
      tft.drawCentreString("WINDOW OPEN - THERMOSTAT BLOCKED", 160, 189, 1);
    }
    lastDrawnPairingMode = pairingModeActive;
    lastDrawnClearedBanner = showClearedBanner;
    bannerFirstDraw = false;
  }

  lastWindowOpenForLayout = windowOpen;
  layoutFirstDraw = false;
}

// =====================================================================
//  SETUP / LOOP
// =====================================================================

void readTemperatureIfDue() {
  unsigned long now = millis();
  if (now - lastTempReadMs < TEMP_READ_INTERVAL_MS) return;
  lastTempReadMs = now;

  tempSensor.requestTemperatures();
  float reading = tempSensor.getTempCByIndex(0);

  if (reading > -100.0f && reading < 125.0f) { // basic sanity check, discard sensor errors
    sensedTemperature = reading;
  } else {
    // -127.0 = DEVICE_DISCONNECTED_C: no sensor answered on the bus.
    Serial.println("DS18B20 read error (sensor not responding), keeping last value");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_1_PIN, OUTPUT);
  pinMode(RELAY_2_PIN, OUTPUT);
  pinMode(RELAY_3_PIN, OUTPUT);
  pinMode(RELAY_4_PIN, OUTPUT);
  pinMode(WIFI_RESET_BTN_PIN, INPUT_PULLUP);
  pinMode(PAIRING_BTN_PIN, INPUT_PULLUP);
  applyRelaysForStage(0);

  tempSensor.begin();

  preferences.begin("wifi_cfg", false);
  savedSsid = preferences.getString("ssid", "");
  savedPass = preferences.getString("pass", "");

  mqttBroker = preferences.getString("mqttBroker", "");
  mqttPort = preferences.getUShort("mqttPort", 8883);
  mqttUser = preferences.getString("mqttUser", "");
  mqttPass = preferences.getString("mqttPass", "");
  setupMqtt();

  // Non-blocking Wi-Fi start (same pattern as Chapter 11): if no
  // credentials are saved at all, go straight to AP provisioning mode.
  // Otherwise kick off the STA connection attempt and move on
  // immediately - the display never waits for the handshake to finish;
  // maintainWifiConnection() (called from loop()) tracks and retries it.
  if (savedSsid.length() == 0) {
    startAccessPoint();
    staConnected = false;
  } else {
    startWifiRadio();
  }
  delay(100); // brief stabilization before touching the display, same as Chapter 11

  setupEspNow();     // works regardless of STA connection status
  setupWebServer();  // reachable once STA connects, or always via AP if no credentials

  tft.init();
  tft.setRotation(1);
  tft.setTouch(calData);

  // tft.init() can cause a brief voltage dip on the 3.3V rail (same
  // issue already documented in Chapter 11), which can glitch a relay
  // output for an instant. Re-assert the correct relay state right
  // after the display is up, to force it back regardless of any glitch.
  applyRelaysForStage(fanStage);

  readTemperatureIfDue();
  updateDynamicUI();

  Serial.println("Thermostat Phase 2 (Wi-Fi + ESP-NOW) ready.");
}

void loop() {
  maintainWifiConnection();
  maintainMqttConnection();
  readTemperatureIfDue();
  updateAutoFanStage();
  handleTouch();
  handleResetButton();
  handlePairingButton();
  server.handleClient();
  updateDynamicUI();
}
