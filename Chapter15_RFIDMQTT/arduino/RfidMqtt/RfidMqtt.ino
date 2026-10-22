/*
  Chapter 15 - RFID Access Control over MQTT (single ESP32)

  A standalone RFID access control node on a single ESP32: it connects
  directly to Wi-Fi and MQTT and receives badge commands straight from
  the App Inventor app. No ESP-NOW, no Firebase: the badge whitelist lives
  only in this board's Preferences and is not backed up anywhere else.

  Behaviour:
    - Wi-Fi/MQTT provisioning: identical pattern to Chapter 14's thermostat.
      Credentials stored in Preferences ("wifi_cfg"), AP fallback on
      192.168.4.1 if no credentials are saved, non-blocking STA connection
      retried from loop(). The button on GPIO33, held 5s, erases the
      stored Wi-Fi/MQTT configuration and returns to AP mode.
    - Blue status LED: blinking while not connected (AP mode or STA still
      retrying), solid on once Wi-Fi is connected.
    - MQTT (CloudAMQP, TLS): subscribes to home/badge/add and
      home/badge/remove, updating the local whitelist directly (no
      ESP-NOW hop in between). Publishes to home/badge/status
      ("ADDED:<uid>" / "REMOVED:<uid>") so the app can show the user
      whether the badge was actually added or removed.
    - RC522 reading, relay and red/green LED feedback. The blue status
      LED is switched off for the
      duration of the action so it doesn't mix with red/green.

  Hardware:
    RC522   -> RST 22, SDA/CS 21, SCK 18, MISO 19, MOSI 23, 3.3V, GND
    Relay   -> GPIO5 (door strike, energized for 3s on valid badge)
    RGB LED -> Red GPIO17, Green GPIO16, Blue GPIO4
    Button  -> GPIO33 (INPUT_PULLUP, hold 5s = erase Wi-Fi/MQTT config)

  Library: MFRC522 by GithubCommunity / miguelbalboa.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// ---------------- Pin configuration ----------------
#define RST_PIN       22
#define SS_PIN        21
#define RELAY_PIN     5
#define RELAY_ACTIVE_LOW true // check your module's datasheet if the door
                               // behaves inverted
#define LED_RED_PIN   17
#define LED_GREEN_PIN 16
#define LED_BLUE_PIN  4
#define WIFI_RESET_BTN_PIN 33 // hold 5s to erase the Wi-Fi/MQTT configuration

const unsigned long ACTION_DURATION_MS = 3000; // relay + confirmation LED time
const unsigned long RESET_HOLD_MS      = 5000; // hold WIFI_RESET_BTN_PIN this long to erase config

// ---------------- Wi-Fi / networking parameters ----------------
const char* AP_SSID = "RFID_Setup";

MFRC522 rfid(SS_PIN, RST_PIN);
WebServer server(80);
Preferences preferences;  // namespace "wifi_cfg" - Wi-Fi + MQTT credentials
Preferences badgePrefs;   // namespace "badge_cfg" - one key per valid UID

// ---------------- Networking state ----------------
bool staConnected = false;
bool wifiConnected = false;
unsigned long lastWifiBeginMs = 0;    // last time WiFi.begin() was called, for the retry cadence
unsigned long wifiFirstAttemptMs = 0; // when the very first attempt started
const unsigned long WIFI_RETRY_INTERVAL_MS = 15000;
String savedSsid = "";
String savedPass = "";
unsigned long resetButtonPressedAtMs = 0;
bool resetButtonHeld = false;

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
const char* TOPIC_BADGE_STATUS = "home/badge/status";

// ---------------- Status LED (blue) blink state ----------------
unsigned long lastBlinkToggleMs = 0;
bool blinkLedState = false;
const unsigned long BLINK_INTERVAL_MS = 500;

// ---------------- Pending action (relay + red/green confirmation LED) ----------------
bool actionActive = false;
unsigned long actionStartedMs = 0;

// =====================================================================
//  WHITELIST (Preferences "badge_cfg" - one key per UID, no index needed)
// =====================================================================

bool isBadgeValid(const String &uid) {
  return badgePrefs.isKey(uid.c_str());
}

void addBadge(const String &uid) {
  if (isBadgeValid(uid)) {
    Serial.println("Badge already present, ADD ignored: " + uid);
    return;
  }
  badgePrefs.putUChar(uid.c_str(), 1);
  Serial.println("Badge added: " + uid);
  mqttClient.publish(TOPIC_BADGE_STATUS, ("ADDED:" + uid).c_str());
}

void removeBadge(const String &uid) {
  badgePrefs.remove(uid.c_str());
  Serial.println("Badge removed: " + uid);
  mqttClient.publish(TOPIC_BADGE_STATUS, ("REMOVED:" + uid).c_str());
}

// =====================================================================
//  RELAY / BADGE FEEDBACK LED
// =====================================================================

void setRelay(bool energize) {
  bool level = RELAY_ACTIVE_LOW ? !energize : energize;
  digitalWrite(RELAY_PIN, level ? HIGH : LOW);
}

void startAction(bool valid) {
  actionActive = true;
  actionStartedMs = millis();
  setRelay(valid); // only a valid badge actually opens the door
  digitalWrite(LED_BLUE_PIN, LOW); // suspend the status LED so it doesn't mix with red/green
  digitalWrite(LED_GREEN_PIN, valid ? HIGH : LOW);
  digitalWrite(LED_RED_PIN, valid ? LOW : HIGH);
  Serial.println(valid ? "Badge OK - relay energized" : "Badge NOT recognized");
}

void maintainAction() {
  if (!actionActive) return;
  if (millis() - actionStartedMs >= ACTION_DURATION_MS) {
    actionActive = false;
    setRelay(false);
    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_RED_PIN, LOW);
  }
}

// =====================================================================
//  RC522 POLLING
// =====================================================================

String uidToString(const MFRC522::Uid &uid) {
  String s = "";
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) s += "0";
    s += String(uid.uidByte[i], HEX);
  }
  s.toUpperCase();
  return s;
}

void pollRfid() {
  if (actionActive) return; // ignore new reads while a relay/LED action is running,
                             // so holding a badge near the reader does not retrigger it
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  String uid = uidToString(rfid.uid);
  Serial.println("Badge detected: " + uid);
  startAction(isBadgeValid(uid));

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// =====================================================================
//  MQTT (CloudAMQP, TLS on port 8883) - badge commands from App Inventor
// =====================================================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String payloadStr;
  for (unsigned int i = 0; i < length; i++) payloadStr += (char)payload[i];
  payloadStr.trim();

  String topicStr(topic);
  if (topicStr == TOPIC_BADGE_ADD) {
    addBadge(payloadStr);
  } else if (topicStr == TOPIC_BADGE_REMOVE) {
    removeBadge(payloadStr);
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

  String clientId = "rfid-" + WiFi.macAddress();
  Serial.print("Connecting to MQTT broker...");
  bool ok = mqttUser.length() > 0
    ? mqttClient.connect(clientId.c_str(), mqttUser.c_str(), mqttPass.c_str())
    : mqttClient.connect(clientId.c_str());

  if (ok) {
    Serial.println(" connected");
    mqttClient.subscribe(TOPIC_BADGE_ADD);
    mqttClient.subscribe(TOPIC_BADGE_REMOVE);
  } else {
    Serial.print(" failed, rc=");
    Serial.println(mqttClient.state());
  }
}

// =====================================================================
//  WI-FI PROVISIONING (same pattern as Chapter 14's thermostat: non-
//  blocking STA connection, retried from loop() instead of blocking setup())
// =====================================================================

void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  Serial.print("AP mode started, connect to: ");
  Serial.println(AP_SSID);
  Serial.print("Configuration address: http://");
  Serial.println(WiFi.softAPIP());
}

// Resets the WiFi radio state fully before starting a new connection
// attempt: on some ESP32 boards, reconnecting right after a reset/power
// cycle fails intermittently because leftover radio state from the
// previous session blocks a clean reassociation. This function only
// KICKS OFF the attempt, it does not wait for it.
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

// Called every loop() iteration. Detects connect/disconnect transitions
// and retries WiFi.begin() every WIFI_RETRY_INTERVAL_MS while offline -
// indefinitely, never falling back to AP on its own. AP mode is only
// ever entered at boot if there are no saved credentials at all
// (handled in setup()).
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

void setupWebServer() {
  server.on("/setconfig", HTTP_POST, handleSetConfig);
  server.begin();
  Serial.println("Web server started (configuration only)");
}

// =====================================================================
//  PHYSICAL BUTTON (hold 5s to erase Wi-Fi/MQTT configuration)
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
      Serial.println("Reset button held: erasing Wi-Fi/MQTT configuration");
      preferences.remove("ssid");
      preferences.remove("pass");
      preferences.remove("mqttBroker");
      preferences.remove("mqttPort");
      preferences.remove("mqttUser");
      preferences.remove("mqttPass");
      delay(300);
      ESP.restart();
    }
  }
}

// =====================================================================
//  STATUS LED (blue) - blinking while not connected, solid once Wi-Fi
//  is connected. Suspended while a badge action is showing red/green.
// =====================================================================

void updateStatusLed() {
  if (actionActive) return; // badge feedback (red/green) takes priority

  if (wifiConnected) {
    digitalWrite(LED_BLUE_PIN, HIGH);
    return;
  }

  if (millis() - lastBlinkToggleMs >= BLINK_INTERVAL_MS) {
    lastBlinkToggleMs = millis();
    blinkLedState = !blinkLedState;
    digitalWrite(LED_BLUE_PIN, blinkLedState ? HIGH : LOW);
  }
}

// =====================================================================
//  SETUP / LOOP
// =====================================================================

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  pinMode(WIFI_RESET_BTN_PIN, INPUT_PULLUP);
  setRelay(false);
  digitalWrite(LED_RED_PIN, LOW);
  digitalWrite(LED_GREEN_PIN, LOW);
  digitalWrite(LED_BLUE_PIN, LOW);

  SPI.begin();
  rfid.PCD_Init();

  badgePrefs.begin("badge_cfg", false);

  preferences.begin("wifi_cfg", false);
  savedSsid = preferences.getString("ssid", "");
  savedPass = preferences.getString("pass", "");

  mqttBroker = preferences.getString("mqttBroker", "");
  mqttPort = preferences.getUShort("mqttPort", 8883);
  mqttUser = preferences.getString("mqttUser", "");
  mqttPass = preferences.getString("mqttPass", "");
  setupMqtt();

  // Non-blocking Wi-Fi start (same pattern as Chapter 14): if no
  // credentials are saved at all, go straight to AP provisioning mode.
  // Otherwise kick off the STA connection attempt and move on
  // immediately - maintainWifiConnection() (called from loop()) tracks
  // and retries it.
  if (savedSsid.length() == 0) {
    startAccessPoint();
    staConnected = false;
  } else {
    startWifiRadio();
  }

  setupWebServer();

  Serial.println("RFID + MQTT node ready.");
}

void loop() {
  maintainWifiConnection();
  maintainMqttConnection();
  handleResetButton();
  pollRfid();
  maintainAction();
  updateStatusLed();
  server.handleClient();
}
