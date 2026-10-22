/*
  Chapter 14 - ESP-NOW Home Automation
  WINDOW SENSOR - battery-friendly board with a magnetic reed contact

  Behaviour:
    1. On first boot (no thermostat MAC stored yet) it runs a PAIRING
       sequence: broadcasts a pairing request a few times and waits for
       the thermostat to answer with its MAC address. Once received,
       the thermostat's MAC is saved to flash (Preferences) and used
       for all future messages - no hardcoded MAC address anywhere.
    2. On every normal boot (cold boot or wake from deep sleep) it reads
       the reed switch, sends the current state to the thermostat over
       ESP-NOW (unicast, using the paired MAC), then goes back to deep
       sleep.
    3. It wakes up ONLY when the pin state actually changes (ext0
       wakeup). Since ext0 can only trigger on one fixed level, the
       trick is: before sleeping, read the current level and arm the
       wakeup on the OPPOSITE level. This way the board wakes up on
       both the opening and the closing of the window, and stays
       asleep the rest of the time.

  Hardware: a magnetic reed switch between WINDOW_PIN and GND. No
  external resistor is needed - the internal pull-up is enough. One
  leg of the switch to WINDOW_PIN, the other to GND. Mount the magnet
  on the moving part (the window) and the reed switch on the fixed
  frame. LOW = window closed (magnet present, contact closed to GND),
  HIGH = window open (contact open, pin pulled up).
*/

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include "driver/rtc_io.h"

#define WINDOW_PIN GPIO_NUM_33 // must be an RTC-capable GPIO for ext0 wakeup
#define RESET_BTN_PIN GPIO_NUM_27 // hold + power-cycle to erase the paired thermostat MAC
#define LED_PIN 32 // on while sending, blinks 3x on failure, off before sleep

const unsigned long SEND_RESULT_TIMEOUT_MS = 1000; // how long to wait for delivery confirmation

const unsigned long PAIR_RETRY_INTERVAL_MS = 300;
const int PAIR_MAX_ATTEMPTS = 15; // ~4.5s of retries before giving up for this cycle

enum MessageType : uint8_t {
  MSG_PAIR_REQUEST = 0,
  MSG_PAIR_ACK     = 1,
  MSG_WINDOW_STATE = 2
};

typedef struct {
  uint8_t type;
  bool windowOpen;
} EspNowMessage;

Preferences preferences;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

volatile bool pairAckReceived = false;
uint8_t thermostatMac[6];
uint8_t pairedChannel = 1; // Wi-Fi channel the thermostat answered on, saved after pairing

volatile bool sendResultKnown = false;
volatile bool sendSuccess = false;

void onDataSent(const wifi_tx_info_t *txInfo, esp_now_send_status_t status) {
  sendSuccess = (status == ESP_NOW_SEND_SUCCESS);
  sendResultKnown = true;
  Serial.print("ESP-NOW send status: ");
  Serial.println(sendSuccess ? "OK" : "FAILED");
}

// Only active during pairing: listens for the thermostat's PAIR_ACK
// and captures its MAC address from the packet's sender info.
void onPairingRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(EspNowMessage)) return;
  EspNowMessage msg;
  memcpy(&msg, data, sizeof(msg));

  if (msg.type == MSG_PAIR_ACK) {
    memcpy(thermostatMac, info->src_addr, 6);
    pairAckReceived = true;
  }
}

bool addPeerIfMissing(const uint8_t *mac, uint8_t channel) {
  if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac); // re-add in case the channel changed
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = channel;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

// Broadcasts pairing requests, scanning through Wi-Fi channels 1-13,
// until the thermostat answers on one of them. This is needed because
// once the thermostat connects to the home router it moves onto
// whatever channel the router uses (no longer always channel 1), and
// ESP-NOW only works between devices on the same channel. Returns true
// and fills thermostatMac + pairedChannel on success.
bool pairWithThermostat() {
  Serial.println("No thermostat paired yet - starting pairing (scanning channels)...");
  esp_now_register_recv_cb(onPairingRecv);

  EspNowMessage request;
  request.type = MSG_PAIR_REQUEST;
  request.windowOpen = false;

  for (int attempt = 0; attempt < PAIR_MAX_ATTEMPTS && !pairAckReceived; attempt++) {
    for (uint8_t channel = 1; channel <= 13 && !pairAckReceived; channel++) {
      esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
      addPeerIfMissing(broadcastAddress, channel);
      esp_now_send(broadcastAddress, (uint8_t *)&request, sizeof(request));

      unsigned long waitStart = millis();
      while (!pairAckReceived && millis() - waitStart < 100) {
        delay(10);
      }
      if (pairAckReceived) {
        pairedChannel = channel;
      }
    }
  }

  esp_now_unregister_recv_cb();

  if (!pairAckReceived) {
    Serial.println("Pairing failed, no thermostat answered on any channel. Will retry next boot.");
    return false;
  }

  preferences.putBytes("peerMac", thermostatMac, 6);
  preferences.putUChar("channel", pairedChannel);
  esp_wifi_set_channel(pairedChannel, WIFI_SECOND_CHAN_NONE);
  addPeerIfMissing(thermostatMac, pairedChannel);
  Serial.print("Paired successfully on channel ");
  Serial.print(pairedChannel);
  Serial.println(", thermostat MAC saved.");
  return true;
}

void blinkLedTimes(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(onMs);
    digitalWrite(LED_PIN, LOW);
    delay(offMs);
  }
}

// Sends the window state, keeps the LED on while waiting for delivery
// confirmation, then either turns it off (success) or blinks it 3
// times before turning off (failure / no confirmation in time).
bool sendWindowStateAndWait(const uint8_t *peerMac, bool isOpen) {
  EspNowMessage msg;
  msg.type = MSG_WINDOW_STATE;
  msg.windowOpen = isOpen;

  sendResultKnown = false;
  sendSuccess = false;
  digitalWrite(LED_PIN, HIGH);

  esp_err_t result = esp_now_send(peerMac, (uint8_t *)&msg, sizeof(msg));
  if (result != ESP_OK) {
    Serial.println("esp_now_send() failed to queue the message");
    digitalWrite(LED_PIN, LOW);
    blinkLedTimes(3, 150, 150);
    return false;
  }

  unsigned long waitStart = millis();
  while (!sendResultKnown && millis() - waitStart < SEND_RESULT_TIMEOUT_MS) {
    delay(5);
  }
  digitalWrite(LED_PIN, LOW);

  if (!sendResultKnown || !sendSuccess) {
    Serial.println("Window state not confirmed delivered");
    blinkLedTimes(3, 150, 150);
    return false;
  }

  Serial.println("Window state delivered successfully");
  return true;
}

void goToSleepUntilPinChanges(gpio_num_t pin) {
  int currentLevel = digitalRead(pin);
  int wakeLevel = (currentLevel == HIGH) ? 0 : 1; // arm wakeup on the opposite level

  rtc_gpio_pullup_en(pin);     // regular pinMode pull-ups do not survive deep sleep
  rtc_gpio_pulldown_dis(pin);

  esp_sleep_enable_ext0_wakeup(pin, wakeLevel);

  Serial.print("Going to deep sleep, will wake on pin level = ");
  Serial.println(wakeLevel);
  Serial.flush();

  esp_deep_sleep_start();
  // execution never returns from here: on wakeup the board reboots
  // and setup() runs again from the top.
}

void setup() {
  Serial.begin(115200);
  delay(50); // let the serial line settle after waking up

  pinMode(WINDOW_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  bool windowOpen = (digitalRead(WINDOW_PIN) == HIGH);

  Serial.print("Window state: ");
  Serial.println(windowOpen ? "OPEN" : "CLOSED");

  preferences.begin("window_cfg", false);

  // Reset button is only sampled once, right here, at boot/wake time -
  // it does nothing while the board is actually asleep. To trigger a
  // reset, hold the button AND press the board's physical EN/RESET
  // button (or power-cycle it) at the same time.
  pinMode(RESET_BTN_PIN, INPUT_PULLUP);
  if (digitalRead(RESET_BTN_PIN) == LOW) {
    Serial.println("Reset button held at boot: erasing paired thermostat MAC");
    preferences.remove("peerMac");
    preferences.remove("channel");
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); // make sure we are not trying to associate to any AP

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed, sleeping anyway");
    goToSleepUntilPinChanges(WINDOW_PIN);
    return;
  }
  esp_now_register_send_cb(onDataSent);

  size_t storedLen = preferences.getBytes("peerMac", thermostatMac, 6);
  bool paired = (storedLen == 6);

  if (!paired) {
    paired = pairWithThermostat();
  } else {
    pairedChannel = preferences.getUChar("channel", 1);
    esp_wifi_set_channel(pairedChannel, WIFI_SECOND_CHAN_NONE);
    addPeerIfMissing(thermostatMac, pairedChannel);
  }

  if (paired) {
    sendWindowStateAndWait(thermostatMac, windowOpen);
  }

  goToSleepUntilPinChanges(WINDOW_PIN);
}

void loop() {
  // never reached: the board sleeps at the end of setup()
}
