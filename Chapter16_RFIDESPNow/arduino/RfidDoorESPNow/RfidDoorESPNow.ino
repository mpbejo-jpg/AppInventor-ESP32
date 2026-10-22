/*
  Chapter 16 - Home Automation Part 2: RFID Access Control
  RFID NODE - RC522 badge reader + relay (door strike) + RGB status LED

  Behaviour:
    - Pairs with the thermostat over ESP-NOW using the same broadcast +
      channel-scan handshake as the window sensor (Chapter 12), declaring
      itself as DEVICE_RFID. The thermostat's MAC + channel are stored in
      Preferences ("pair_cfg") once paired, and reused on every boot.
    - Maintains a local badge whitelist in Preferences ("badge_cfg"): one
      key per UID (hex string), value unused (presence = valid). This
      node never needs to enumerate the whitelist - only test membership
      (isKey), add a key, remove a key, or clear() the whole namespace -
      so no separate index has to be maintained.
    - Receives MSG_BADGE_ADD / MSG_BADGE_REMOVE / MSG_BADGE_CLEAR commands
      from the thermostat over ESP-NOW and updates the whitelist
      accordingly. A duplicate ADD (UID already present) is ignored.
    - Reads the RC522 continuously. A valid badge energizes the relay for
      3 seconds (door strike) and lights the green LED for 3 seconds; an
      unknown badge lights the red LED for 3 seconds instead. The blue
      LED stays on at all times as an idle/power indicator.
    - Physical button (GPIO33): short press triggers a new pairing
      attempt at runtime (broadcast scan); held 5s, erases only the
      stored thermostat MAC/channel ("pair_cfg") - the badge whitelist
      is untouched. A successful pairing blinks the blue LED 4 times
      (distinct from the red/green badge-feedback LEDs) before it goes
      back to steady on.

  Hardware:
    RC522   -> RST 22, SDA/CS 21, SCK 18, MISO 19, MOSI 23, 3.3V, GND
    Relay   -> GPIO13 (door strike, energized for 3s on valid badge)
    RGB LED -> Red GPIO17, Green GPIO16, Blue GPIO4 (blue always on)
    Button  -> GPIO33 (INPUT_PULLUP, short press = pair, 5s hold = erase pairing)

  Library: MFRC522 by GithubCommunity / miguelbalboa (install via Library
  Manager) - this is the de facto standard Arduino library for this
  reader; not yet confirmed with you, flag it if you use a different one.
*/

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <SPI.h>
#include <MFRC522.h>

// ---------------- Pin configuration ----------------
#define RST_PIN       22
#define SS_PIN        21
#define RELAY_PIN     5
#define RELAY_ACTIVE_LOW true // assumed same convention as the thermostat's
                               // relay module (Chapter 14) - check your
                               // module's datasheet if the door behaves inverted
#define LED_RED_PIN   17
#define LED_GREEN_PIN 16
#define LED_BLUE_PIN  4
#define BUTTON_PIN    33

const unsigned long ACTION_DURATION_MS     = 3000; // relay + confirmation LED time
const unsigned long BUTTON_HOLD_MS         = 5000; // hold to erase pairing
const unsigned long PAIR_RETRY_INTERVAL_MS = 300;
const int PAIR_MAX_ATTEMPTS = 15;

MFRC522 rfid(SS_PIN, RST_PIN);
Preferences pairPrefs;   // namespace "pair_cfg"  - thermostat MAC + channel
Preferences badgePrefs;  // namespace "badge_cfg" - one key per valid UID

// ---------------- ESP-NOW message definitions ----------------
// Mirrors the struct used in ThermostatESPNow.ino - keep both files in
// sync whenever this layout changes.
enum MessageType : uint8_t {
  MSG_PAIR_REQUEST  = 0,
  MSG_PAIR_ACK      = 1,
  MSG_WINDOW_STATE  = 2,
  MSG_BADGE_ADD     = 3,
  MSG_BADGE_REMOVE  = 4,
  MSG_BADGE_CLEAR   = 5
};

enum DeviceType : uint8_t {
  DEVICE_WINDOW = 0,
  DEVICE_RFID   = 1
};

typedef struct {
  uint8_t type;
  uint8_t deviceType;
  union {
    bool windowOpen;
    struct { char uid[16]; } badge;
  };
} EspNowMessage;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

uint8_t thermostatMac[6];
bool thermostatPaired = false;
uint8_t pairedChannel = 1;

volatile bool pairAckReceived = false;

// ---------------- Button state ----------------
bool buttonHeld = false;
unsigned long buttonPressedAtMs = 0;
bool longPressActionDone = false;

// ---------------- Pending action (relay + confirmation LED) ----------------
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
}

void removeBadge(const String &uid) {
  badgePrefs.remove(uid.c_str());
  Serial.println("Badge removed: " + uid);
}

void clearBadges() {
  badgePrefs.clear();
  Serial.println("Badge whitelist cleared");
}

// =====================================================================
//  RELAY / LED
// =====================================================================

void setRelay(bool energize) {
  bool level = RELAY_ACTIVE_LOW ? !energize : energize;
  digitalWrite(RELAY_PIN, level ? HIGH : LOW);
}

void startAction(bool valid) {
  actionActive = true;
  actionStartedMs = millis();
  setRelay(valid); // only a valid badge actually opens the door
  digitalWrite(LED_BLUE_PIN, LOW); // blue off while showing the badge result
  digitalWrite(LED_GREEN_PIN, valid ? HIGH : LOW);
  digitalWrite(LED_RED_PIN, valid ? LOW : HIGH);
  Serial.println(valid ? "Badge OK - relay energized" : "Badge NOT recognized");
}

// Confirms a successful pairing: blinks the blue LED (normally always
// on) a few times, then leaves it back on. Kept separate from the
// red/green badge-feedback LEDs so it can never be confused with a
// badge read.
void blinkPairingSuccess() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(LED_BLUE_PIN, LOW);
    delay(150);
    digitalWrite(LED_BLUE_PIN, HIGH);
    delay(150);
  }
}

void maintainAction() {
  if (!actionActive) return;
  if (millis() - actionStartedMs >= ACTION_DURATION_MS) {
    actionActive = false;
    setRelay(false);
    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_RED_PIN, LOW);
    digitalWrite(LED_BLUE_PIN, HIGH); // back to idle
  }
}

// =====================================================================
//  ESP-NOW
// =====================================================================

bool addPeerIfMissing(const uint8_t *mac, uint8_t channel) {
  if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = channel;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

String uidToString(const MFRC522::Uid &uid) {
  String s = "";
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) s += "0";
    s += String(uid.uidByte[i], HEX);
  }
  s.toUpperCase();
  return s;
}

void onEspNowMessageReceived(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(EspNowMessage)) return;
  EspNowMessage msg;
  memcpy(&msg, data, sizeof(msg));

  if (msg.type == MSG_PAIR_ACK) {
    memcpy(thermostatMac, info->src_addr, 6);
    pairAckReceived = true;
    return;
  }

  if (msg.type == MSG_BADGE_ADD) {
    addBadge(String(msg.badge.uid));
  } else if (msg.type == MSG_BADGE_REMOVE) {
    removeBadge(String(msg.badge.uid));
  } else if (msg.type == MSG_BADGE_CLEAR) {
    clearBadges();
  }
}

// Broadcasts pairing requests scanning channels 1-13, same approach as
// the window sensor (Chapter 14). Blocking for a few seconds is fine
// here: it only runs on a deliberate button press, not in the middle of
// normal operation.
bool pairWithThermostat() {
  Serial.println("Starting pairing (scanning channels)...");
  pairAckReceived = false;

  EspNowMessage request;
  request.type = MSG_PAIR_REQUEST;
  request.deviceType = DEVICE_RFID;

  for (int attempt = 0; attempt < PAIR_MAX_ATTEMPTS && !pairAckReceived; attempt++) {
    for (uint8_t channel = 1; channel <= 13 && !pairAckReceived; channel++) {
      esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
      addPeerIfMissing(broadcastAddress, channel);
      esp_now_send(broadcastAddress, (uint8_t *)&request, sizeof(request));

      unsigned long waitStart = millis();
      while (!pairAckReceived && millis() - waitStart < 100) {
        delay(10);
      }
      if (pairAckReceived) pairedChannel = channel;
    }
  }

  if (!pairAckReceived) {
    Serial.println("Pairing failed, no thermostat answered. Will retry later.");
    return false;
  }

  pairPrefs.putBytes("peerMac", thermostatMac, 6);
  pairPrefs.putUChar("channel", pairedChannel);
  esp_wifi_set_channel(pairedChannel, WIFI_SECOND_CHAN_NONE);
  addPeerIfMissing(thermostatMac, pairedChannel);
  thermostatPaired = true;

  Serial.print("Paired successfully on channel ");
  Serial.println(pairedChannel);
  blinkPairingSuccess();
  return true;
}

// =====================================================================
//  BUTTON (short press = pair now, 5s hold = erase pairing only)
// =====================================================================

void handleButton() {
  bool pressed = (digitalRead(BUTTON_PIN) == LOW);

  if (pressed && !buttonHeld) {
    buttonHeld = true;
    buttonPressedAtMs = millis();
    longPressActionDone = false;
  } else if (pressed && buttonHeld) {
    if (!longPressActionDone && millis() - buttonPressedAtMs >= BUTTON_HOLD_MS) {
      Serial.println("Button held 5s: erasing stored thermostat pairing");
      pairPrefs.remove("peerMac");
      pairPrefs.remove("channel");
      thermostatPaired = false;
      longPressActionDone = true;
    }
  } else if (!pressed && buttonHeld) {
    buttonHeld = false;
    if (!longPressActionDone) {
      // short press: (re)attempt pairing now
      pairWithThermostat();
    }
  }
}

// =====================================================================
//  RC522 POLLING
// =====================================================================

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
//  SETUP / LOOP
// =====================================================================

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  setRelay(false);
  digitalWrite(LED_RED_PIN, LOW);
  digitalWrite(LED_GREEN_PIN, LOW);
  digitalWrite(LED_BLUE_PIN, HIGH); // always on

  SPI.begin();
  rfid.PCD_Init();

  badgePrefs.begin("badge_cfg", false);
  pairPrefs.begin("pair_cfg", false);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
  } else {
    esp_now_register_recv_cb(onEspNowMessageReceived);
  }

  size_t storedLen = pairPrefs.getBytes("peerMac", thermostatMac, 6);
  if (storedLen == 6) {
    pairedChannel = pairPrefs.getUChar("channel", 1);
    esp_wifi_set_channel(pairedChannel, WIFI_SECOND_CHAN_NONE);
    addPeerIfMissing(thermostatMac, pairedChannel);
    thermostatPaired = true;
    Serial.println("Thermostat peer restored from flash");
  } else {
    Serial.println("No thermostat paired yet - press the button to pair");
  }

  Serial.println("RFID node ready.");
}

void loop() {
  handleButton();
  pollRfid();
  maintainAction();
}
