/*
  Chapter 18 - RadarCamFirebase
  Freenove ESP32-S3-CAM (WROOM) + HC-SR501 PIR motion sensor

  On motion detected:
    - capture a photo with the onboard camera
    - upload the photo to Firebase Storage
    - write date/time + download token to Firebase Realtime Database
    (push notifications will be added in a later chapter)

  Hardware:
    - HC-SR501 OUT         -> GPIO1  (digital input, HIGH on motion)
        The HC-SR501 output is already a 3.3V-level signal when triggered, so
        it connects directly to GPIO1 -- no voltage divider needed.
        HC-SR501 VCC must be powered from 5V (NOT 3.3V -- an underpowered
        supply was found to cause the sensor's output to latch stuck HIGH).
        GND shared with the board.
        The module has two onboard trimmers: sensitivity and time delay
        (hold time), plus a jumper selecting single vs. repeatable trigger
        mode (use repeatable, the default).
    - Reset button        -> GPIO42 (input pull-up, hold 3s to reset settings)
        NOT GPIO2: this board has a built-in blue indicator LED wired to
        GPIO2, which fights with an INPUT_PULLUP button on the same pin.
    - Status LED           -> onboard WS2812 RGB LED, GPIO48 (Adafruit_NeoPixel)

  GPIOs 4-13,15-18 are used by the camera; 35-37 are reserved for the
  octal PSRAM; 19-20 are the native USB pins; 38-40 are the onboard SD card;
  0/3/45/46 are boot-strapping pins. GPIO2 has a built-in blue LED on this
  board (blinks if driven with INPUT_PULLUP), so it's avoided. GPIO1/GPIO42
  were picked as clear of all of these.

  IMPORTANT fix for this board: HTTPS calls (sign-in, Storage upload,
  Realtime Database writes) were failing intermittently with HTTPClient
  error -1, alongside Wi-Fi association warnings ("Association refused",
  "CCMP replay detected"). Several theories were tested and ruled out with
  evidence (DRAM fragmentation from the camera driver, TLS client reuse
  across hosts, startup ordering, NTP timing) before finding the real
  cause: the camera's XCLK clock at the default 20MHz was interfering with
  the Wi-Fi radio on this specific board. Lowering it to 6MHz
  (config.xclk_freq_hz in initCamera()) fixed it completely -- no other
  workaround was needed, so the camera stays initialized normally for the
  whole session.

  Configuration endpoints (called by the App Inventor app, same GET-with-
  query-params pattern as the other book chapters). All 4 are available in
  both AP and STA mode, so the app can send every parameter from one screen
  while the device is still in AP mode: send /setwifi LAST, since it
  triggers a reboot into the newly configured Wi-Fi network.
    /setwifi?ssid=..&pass=..
    /setfirebase?url=..&storage=..&apikey=..&email=..&fbpass=..
    /setcooldown?sec=..
    /setphoto?size=640x480|320x240

  LED behavior (WS2812, one pixel):
    - slow amber blink -> Wi-Fi not configured (AP config portal active)
    - off               -> Wi-Fi configured and idle
    - solid red         -> motion detected, capturing/uploading photo

  Required libraries (Library Manager):
    - ArduinoJson
    - Adafruit NeoPixel

  After every successful sign-in, the board's local IP is saved to
  /{uid}/deviceInfo/ip in the Realtime Database, so the app can look it up
  (e.g. when opening a "send parameters" screen) instead of needing to know
  it in advance -- see writeDeviceIp().

  Preferences namespace: "radarcam_cfg"
    ssid, password, firebaseURL, storageURL, firebaseApiKey,
    email, fbPassword, cooldownSec, photoSize
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <time.h>
#include "esp_camera.h"

// ---------- Pin mapping ----------
#define RADAR_PIN    1
#define BUTTON_PIN  42
#define LED_PIN     48   // onboard WS2812 RGB LED data pin
#define NUM_LEDS     1

Adafruit_NeoPixel pixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Freenove ESP32-S3-CAM camera pins (CAMERA_MODEL_FREENOVE_ESP32S3_CAM)
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM      4
#define SIOC_GPIO_NUM      5
#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM        8
#define Y3_GPIO_NUM        9
#define Y2_GPIO_NUM       11
#define VSYNC_GPIO_NUM     6
#define HREF_GPIO_NUM      7
#define PCLK_GPIO_NUM     13

// ---------- Timing constants ----------
const unsigned long DEBOUNCE_MS        = 250;   // radar must stay HIGH this long to be valid
const unsigned long BUTTON_HOLD_MS     = 3000;  // long-press duration to reset settings
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long TOKEN_REFRESH_MARGIN_MS = 60000; // refresh 60s before expiry

// PIR warm-up: ignore readings for this long after boot. The HC-SR501
// datasheet itself calls for roughly 60s to stabilize after power-up,
// during which its output can be unreliable.
const unsigned long RADAR_WARMUP_MS = 60000;
unsigned long bootMillis = 0;

// ---------- Globals ----------
Preferences prefs;
WebServer server(80);

// Shared TLS client for every HTTPS call (Firebase Auth, Storage, RTDB).
// setInsecure() skips root-certificate validation -- simplest approach for
// a hobbyist project; without a configured secure client at all, HTTPClient
// fails the TLS handshake outright (error -1), regardless of Wi-Fi status.
WiFiClientSecure secureClient;

String ssid, wifiPassword, firebaseURL, storageURL, firebaseApiKey;
String email, firebasePassword;
int cooldownSec = 5;
String photoSize = "640x480"; // "640x480" or "320x240"

String idToken, refreshToken, firebaseUid;
unsigned long tokenExpiryMillis = 0;

bool wifiConfigured = false;
bool apMode = false;

unsigned long radarHighSince = 0;
unsigned long lastEventMillis = 0;

unsigned long buttonPressStart = 0;
bool buttonHeld = false;

unsigned long lastLedToggle = 0;
bool ledState = false;

// ---------- Forward declarations ----------
void loadConfig();
void startAccessPointMode();
void setupWebServer();
void handleSetWifi();
void handleSetFirebase();
void handleSetCooldown();
void handleSetPhoto();
framesize_t photoSizeFromString(const String &s);
bool connectWiFi();
bool initCamera();
bool firebaseSignIn();
void writeDeviceIp();
bool firebaseRefreshToken();
void ensureValidToken();
void captureAndSend();
bool uploadToStorage(const uint8_t *data, size_t len, String &filePath, String &downloadToken);
bool writeEventToDatabase(const String &dateStr, const String &timeStr, const String &downloadToken);
bool getTimestampStrings(String &dateStr, String &timeStr, String &compactStamp);
void setLed(bool on);
void blinkLedSlow();
void checkResetButton();

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(RADAR_PIN, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pixel.begin();
  setLed(false); // LED off = idle/unknown at boot

  secureClient.setInsecure(); // skip cert validation, needed for any HTTPS call below

  prefs.begin("radarcam_cfg", false);
  loadConfig();

  if (ssid.length() == 0) {
    Serial.println("[SETUP] No Wi-Fi credentials found, starting access point");
    startAccessPointMode();
    return; // startAccessPointMode() takes over the loop via server.handleClient()
  }

  if (!connectWiFi()) {
    Serial.println("[SETUP] Could not connect to Wi-Fi, starting access point");
    startAccessPointMode();
    return;
  }

  wifiConfigured = true;
  setLed(false); // Wi-Fi configured -> LED off

  setupWebServer();
  server.begin();

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  if (email.length() > 0 && firebasePassword.length() > 0) {
    if (firebaseSignIn()) {
      writeDeviceIp();
    }
  }

  if (!initCamera()) {
    Serial.println("[SETUP] Camera init failed");
  }

  bootMillis = millis();
  Serial.println("[SETUP] Ready, monitoring radar");
}

// ---------------------------------------------------------------------------
void loop() {
  if (apMode) {
    server.handleClient();
    blinkLedSlow();
    return;
  }

  server.handleClient(); // serves /setfirebase, /setcooldown, /setphoto

  checkResetButton();

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    return;
  }

  ensureValidToken();

  bool radarWarmingUp = (millis() - bootMillis) < RADAR_WARMUP_MS;
  if (radarWarmingUp) {
    radarHighSince = 0; // ignore any activity while the sensor settles
    return;
  }

  bool radarActive = digitalRead(RADAR_PIN) == HIGH;

  if (radarActive) {
    if (radarHighSince == 0) {
      radarHighSince = millis();
    }
    bool debounced = (millis() - radarHighSince) >= DEBOUNCE_MS;
    bool cooldownElapsed = (millis() - lastEventMillis) >= (unsigned long)cooldownSec * 1000UL;

    if (debounced && cooldownElapsed) {
      captureAndSend();
      lastEventMillis = millis();
    }
  } else {
    radarHighSince = 0;
  }
}

// ---------------------------------------------------------------------------
void loadConfig() {
  ssid             = prefs.getString("ssid", "");
  wifiPassword     = prefs.getString("password", "");
  firebaseURL      = prefs.getString("firebaseURL", "");
  storageURL       = prefs.getString("storageURL", "");
  firebaseApiKey   = prefs.getString("firebaseApiKey", "");
  email            = prefs.getString("email", "");
  firebasePassword = prefs.getString("fbPassword", "");
  cooldownSec      = prefs.getInt("cooldownSec", 5);
  photoSize        = prefs.getString("photoSize", "640x480");
}

// ---------------------------------------------------------------------------
// Wi-Fi: Access Point mode (no credentials saved yet)
void startAccessPointMode() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("RadarCam-Setup");
  Serial.print("[AP] Started, IP: ");
  Serial.println(WiFi.softAPIP());

  setupWebServer();
  server.begin();
}

// All 4 endpoints are registered regardless of AP or STA mode, same as the
// other book chapters: the app sends every parameter on one screen while
// still in AP mode, /setwifi last, which triggers the reboot into the
// configured Wi-Fi network with everything already saved.
void setupWebServer() {
  server.on("/setwifi", HTTP_GET, handleSetWifi);
  server.on("/setfirebase", HTTP_GET, handleSetFirebase);
  server.on("/setcooldown", HTTP_GET, handleSetCooldown);
  server.on("/setphoto", HTTP_GET, handleSetPhoto);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
}

// Example call from App Inventor:
// http://192.168.4.1/setwifi?ssid=MyHomeWifi&pass=MyPassword
void handleSetWifi() {
  if (!server.hasArg("ssid") || !server.hasArg("pass")) {
    server.send(400, "text/plain", "Missing ssid or pass parameter");
    return;
  }

  prefs.putString("ssid", server.arg("ssid"));
  prefs.putString("password", server.arg("pass"));

  server.send(200, "text/plain", "WiFi credentials saved. Rebooting...");
  delay(500);
  ESP.restart();
}

// ---------------------------------------------------------------------------
// Configuration handlers (available in both AP and STA mode)

// Example call from App Inventor:
// http://<esp32-ip>/setfirebase?url=https://project.firebaseio.com&storage=project.appspot.com&apikey=XXXX&email=user@example.com&fbpass=secret
void handleSetFirebase() {
  bool updated = false;

  if (server.hasArg("url")) {
    firebaseURL = server.arg("url");
    prefs.putString("firebaseURL", firebaseURL);
    updated = true;
  }
  if (server.hasArg("storage")) {
    storageURL = server.arg("storage");
    prefs.putString("storageURL", storageURL);
    updated = true;
  }
  if (server.hasArg("apikey")) {
    firebaseApiKey = server.arg("apikey");
    prefs.putString("firebaseApiKey", firebaseApiKey);
    updated = true;
  }
  if (server.hasArg("email")) {
    email = server.arg("email");
    prefs.putString("email", email);
    updated = true;
  }
  if (server.hasArg("fbpass")) {
    firebasePassword = server.arg("fbpass");
    prefs.putString("fbPassword", firebasePassword);
    updated = true;
  }

  if (!updated) {
    server.send(400, "text/plain", "No recognized parameters");
    return;
  }

  Serial.println("[CONFIG] Firebase settings saved");
  server.send(200, "text/plain", "Firebase configuration saved");

  // Credentials changed, re-authenticate with the new ones -- but only if
  // actually connected to a real network. Attempting an HTTPS sign-in while
  // still in AP mode (no internet route at all) was found to trip a
  // watchdog reset during the TLS handshake, not just fail cleanly.
  if (!apMode && email.length() > 0 && firebasePassword.length() > 0) {
    if (firebaseSignIn()) {
      writeDeviceIp();
    }
  }
}

// Example call from App Inventor:
// http://<esp32-ip>/setcooldown?sec=5
void handleSetCooldown() {
  if (!server.hasArg("sec")) {
    server.send(400, "text/plain", "Missing sec parameter");
    return;
  }

  cooldownSec = server.arg("sec").toInt();
  prefs.putInt("cooldownSec", cooldownSec);

  Serial.printf("[CONFIG] Cooldown set to %d seconds\n", cooldownSec);
  server.send(200, "text/plain", "Cooldown configuration saved");
}

// Maps the two sizes exposed to the app onto the esp_camera framesize_t enum.
framesize_t photoSizeFromString(const String &s) {
  if (s == "320x240") return FRAMESIZE_QVGA;
  return FRAMESIZE_VGA; // default / "640x480"
}

// Example call from App Inventor:
// http://<esp32-ip>/setphoto?size=640x480   (or size=320x240)
void handleSetPhoto() {
  if (!server.hasArg("size")) {
    server.send(400, "text/plain", "Missing size parameter");
    return;
  }

  String size = server.arg("size");
  if (size != "640x480" && size != "320x240") {
    server.send(400, "text/plain", "size must be 640x480 or 320x240");
    return;
  }

  photoSize = size;
  prefs.putString("photoSize", photoSize);

  // Apply live if the camera is already running, no reboot needed.
  sensor_t *camSensor = esp_camera_sensor_get();
  if (camSensor != nullptr) {
    camSensor->set_framesize(camSensor, photoSizeFromString(photoSize));
  }

  Serial.println("[CONFIG] Photo size set to " + photoSize);
  server.send(200, "text/plain", "Photo size configuration saved");
}

// ---------------------------------------------------------------------------
bool connectWiFi() {
  // If the board reset abruptly (e.g. brownout) without a clean disconnect,
  // the router can keep the previous association alive for a while and
  // refuse an immediate reconnect ("Association refused too many times").
  // A few retries with growing delays give it time to forget the stale
  // session, instead of requiring a manual power cycle.
  const int MAX_ATTEMPTS = 3;
  const unsigned long RETRY_DELAYS_MS[MAX_ATTEMPTS] = {0, 5000, 15000};

  for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
    if (RETRY_DELAYS_MS[attempt] > 0) {
      Serial.printf("[WIFI] Retry %d/%d, waiting %lus before trying again\n",
                    attempt + 1, MAX_ATTEMPTS, RETRY_DELAYS_MS[attempt] / 1000);
      delay(RETRY_DELAYS_MS[attempt]);
    }

    Serial.print("[WIFI] Connecting to ");
    Serial.println(ssid);
    WiFi.disconnect(true); // clear any stale local state before retrying
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), wifiPassword.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
      delay(300);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WIFI] Connected, IP: ");
      Serial.println(WiFi.localIP());
      return true;
    }
  }

  Serial.println("[WIFI] Connection failed after all retries");
  return false;
}

// ---------------------------------------------------------------------------
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  // Reduced from the default 20MHz to 6MHz: on this specific board, the
  // XCLK camera clock signal at 20MHz was found (in a separate CameraWebServer
  // test) to interfere with the Wi-Fi radio -- consistent with the
  // "Association refused"/"CCMP replay detected" symptoms seen here.
  config.xclk_freq_hz = 6000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Frame size comes from configuration (default 640x480), not hardcoded;
  // jpeg quality/buffer count still depend on PSRAM availability.
  config.frame_size = photoSizeFromString(photoSize);
  if (psramFound()) {
    config.jpeg_quality = 12;
    config.fb_count = 2;
  } else {
    config.jpeg_quality = 15;
    config.fb_count = 1;
  }
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAMERA] Init failed with error 0x%x\n", err);
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
bool firebaseSignIn() {
  if (firebaseApiKey.length() == 0) return false;

  HTTPClient http;
  String url = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" + firebaseApiKey;
  http.begin(secureClient, url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> reqDoc;
  reqDoc["email"] = email;
  reqDoc["password"] = firebasePassword;
  reqDoc["returnSecureToken"] = true;
  String body;
  serializeJson(reqDoc, body);

  int code = http.POST(body);
  bool ok = false;

  if (code == 200) {
    String resp = http.getString();
    StaticJsonDocument<1024> respDoc;
    if (deserializeJson(respDoc, resp) == DeserializationError::Ok) {
      idToken = respDoc["idToken"].as<String>();
      refreshToken = respDoc["refreshToken"].as<String>();
      firebaseUid = respDoc["localId"].as<String>();
      long expiresIn = respDoc["expiresIn"].as<long>(); // seconds, usually 3600
      tokenExpiryMillis = millis() + (expiresIn * 1000UL);
      Serial.println("[AUTH] Sign-in successful");
      ok = true;
    }
  } else {
    String errResp = http.getString();
    Serial.printf("[AUTH] Sign-in failed, HTTP code %d\n", code);
    Serial.println("[AUTH] Response: " + errResp);
  }

  http.end();
  secureClient.stop(); // close the TLS session explicitly before reusing this client for a
                       // different host -- Storage succeeding then Database timing out
                       // right after, in the same session, pointed at a stale session being
                       // reused across hosts, not a random Wi-Fi/router glitch
  return ok;
}

bool firebaseRefreshToken() {
  if (refreshToken.length() == 0) return firebaseSignIn();

  HTTPClient http;
  String url = "https://securetoken.googleapis.com/v1/token?key=" + firebaseApiKey;
  http.begin(secureClient, url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "grant_type=refresh_token&refresh_token=" + refreshToken;
  int code = http.POST(body);
  bool ok = false;

  if (code == 200) {
    String resp = http.getString();
    StaticJsonDocument<1024> respDoc;
    if (deserializeJson(respDoc, resp) == DeserializationError::Ok) {
      idToken = respDoc["id_token"].as<String>();
      refreshToken = respDoc["refresh_token"].as<String>();
      firebaseUid = respDoc["user_id"].as<String>();
      long expiresIn = respDoc["expires_in"].as<long>();
      tokenExpiryMillis = millis() + (expiresIn * 1000UL);
      Serial.println("[AUTH] Token refreshed");
      ok = true;
    }
  } else {
    Serial.printf("[AUTH] Token refresh failed, HTTP code %d\n", code);
  }

  http.end();
  secureClient.stop(); // close the TLS session explicitly before reusing this client for a
                       // different host -- Storage succeeding then Database timing out
                       // right after, in the same session, pointed at a stale session being
                       // reused across hosts, not a random Wi-Fi/router glitch
  return ok;
}

void ensureValidToken() {
  if (idToken.length() == 0) {
    firebaseSignIn();
    return;
  }
  if (millis() + TOKEN_REFRESH_MARGIN_MS >= tokenExpiryMillis) {
    firebaseRefreshToken();
  }
}

// ---------------------------------------------------------------------------
void captureAndSend() {
  Serial.println("[EVENT] Motion confirmed, capturing photo");
  setLed(true);

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[EVENT] Camera capture failed");
    setLed(false);
    return;
  }

  String dateStr, timeStr, compactStamp;
  getTimestampStrings(dateStr, timeStr, compactStamp);
  String filePath = "photos/IMG_" + compactStamp + ".jpg";

  String downloadToken;
  bool uploaded = uploadToStorage(fb->buf, fb->len, filePath, downloadToken);
  esp_camera_fb_return(fb);

  if (uploaded) {
    writeEventToDatabase(dateStr, timeStr, downloadToken);
  } else {
    Serial.println("[EVENT] Upload failed, skipping database write");
  }

  setLed(false);
}

// ---------------------------------------------------------------------------
bool uploadToStorage(const uint8_t *data, size_t len, String &filePath, String &downloadToken) {
  if (storageURL.length() == 0 || idToken.length() == 0) return false;

  // Firebase Storage REST upload: POST raw JPEG bytes, authenticated with the
  // Firebase user idToken as a standard Bearer token (validated against
  // Storage security rules, e.g. "allow read, write: if request.auth != null").
  String encodedPath = filePath;
  encodedPath.replace("/", "%2F");

  HTTPClient http;
  String url = "https://firebasestorage.googleapis.com/v0/b/" + storageURL +
               "/o?name=" + encodedPath + "&uploadType=media";
  http.begin(secureClient, url);
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("Authorization", "Bearer " + idToken);

  int code = http.POST((uint8_t *)data, len);
  bool ok = false;

  if (code == 200) {
    String resp = http.getString();
    StaticJsonDocument<1024> respDoc;
    if (deserializeJson(respDoc, resp) == DeserializationError::Ok) {
      if (respDoc.containsKey("downloadTokens")) {
        downloadToken = respDoc["downloadTokens"].as<String>();
        Serial.println("[STORAGE] Upload successful");
        ok = true;
      }
    }
  } else {
    String errResp = http.getString();
    Serial.printf("[STORAGE] Upload failed, HTTP code %d\n", code);
    Serial.println("[STORAGE] Response: " + errResp);
  }

  http.end();
  secureClient.stop(); // close the TLS session explicitly before reusing this client for a
                       // different host -- Storage succeeding then Database timing out
                       // right after, in the same session, pointed at a stale session being
                       // reused across hosts, not a random Wi-Fi/router glitch
  return ok;
}

// ---------------------------------------------------------------------------
bool writeEventToDatabase(const String &dateStr, const String &timeStr, const String &downloadToken) {
  if (firebaseURL.length() == 0 || idToken.length() == 0 || firebaseUid.length() == 0) return false;

  // Security rules restrict access to paths under the authenticated user's
  // own UID ("$uid": { ".read"/".write": "auth.uid === $uid" }), so events
  // are written under /{uid}/events rather than at the database root.
  //
  // Structure: /{uid}/events/{date}/{time} = downloadToken
  // This lets the app query all events for a given date directly
  // (GET /{uid}/events/{date}.json) instead of scanning auto-generated
  // push keys. Colons in the time string aren't valid raw path characters
  // in the URL we build here, so they're replaced with '-' for the key.
  String timeKey = timeStr;
  timeKey.replace(":", "-");

  HTTPClient http;
  String url = firebaseURL + "/" + firebaseUid + "/events/" + dateStr + "/" + timeKey +
               ".json?auth=" + idToken;
  http.begin(secureClient, url);
  http.addHeader("Content-Type", "application/json");

  String body = "\"" + downloadToken + "\""; // RTDB PUT expects a JSON-encoded value

  int code = http.PUT(body);
  bool ok = (code == 200);
  if (ok) {
    Serial.println("[DATABASE] Event written");
  } else {
    String errResp = http.getString();
    Serial.printf("[DATABASE] Write failed, HTTP code %d\n", code);
    Serial.println("[DATABASE] Response: " + errResp);
  }

  http.end();
  secureClient.stop(); // close the TLS session explicitly before reusing this client for a
                       // different host -- Storage succeeding then Database timing out
                       // right after, in the same session, pointed at a stale session being
                       // reused across hosts, not a random Wi-Fi/router glitch
  return ok;
}

// ---------------------------------------------------------------------------
// Saves the board's current local IP under /{uid}/deviceInfo/ip, so the app
// can look it up (e.g. right before showing the "send parameters" screen)
// instead of needing to know it in advance. Called after every successful
// sign-in, since the IP is only meaningful once actually connected, and a
// fresh sign-in is a convenient, already-authenticated moment to write it.
void writeDeviceIp() {
  if (firebaseURL.length() == 0 || idToken.length() == 0 || firebaseUid.length() == 0) return;

  HTTPClient http;
  String url = firebaseURL + "/" + firebaseUid + "/deviceInfo/ip.json?auth=" + idToken;
  http.begin(secureClient, url);
  http.addHeader("Content-Type", "application/json");

  String body = "\"" + WiFi.localIP().toString() + "\"";
  int code = http.PUT(body);

  if (code == 200) {
    Serial.println("[DATABASE] Device IP saved: " + WiFi.localIP().toString());
  } else {
    Serial.printf("[DATABASE] Failed to save device IP, HTTP code %d\n", code);
  }

  http.end();
  secureClient.stop();
}

// ---------------------------------------------------------------------------
bool getTimestampStrings(String &dateStr, String &timeStr, String &compactStamp) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5000)) {
    Serial.println("[TIME] Failed to obtain NTP time");
    dateStr = "0000-00-00";
    timeStr = "00:00:00";
    compactStamp = String(millis());
    return false;
  }

  char dateBuf[11];
  char timeBuf[9];
  char compactBuf[16];
  strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &timeinfo);
  strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &timeinfo);
  strftime(compactBuf, sizeof(compactBuf), "%Y%m%d_%H%M%S", &timeinfo);

  dateStr = String(dateBuf);
  timeStr = String(timeBuf);
  compactStamp = String(compactBuf);
  return true;
}

// ---------------------------------------------------------------------------
// "on" is used for the solid-red "capturing/uploading" indication.
void setLed(bool on) {
  if (on) {
    pixel.setPixelColor(0, pixel.Color(255, 0, 0)); // solid red
  } else {
    pixel.setPixelColor(0, 0); // off
  }
  pixel.show();
  ledState = on;
}

void blinkLedSlow() {
  if (millis() - lastLedToggle >= 500) {
    lastLedToggle = millis();
    ledState = !ledState;
    if (ledState) {
      pixel.setPixelColor(0, pixel.Color(255, 140, 0)); // amber blink = AP mode
    } else {
      pixel.setPixelColor(0, 0);
    }
    pixel.show();
  }
}

// ---------------------------------------------------------------------------
void checkResetButton() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (!buttonHeld) {
      buttonHeld = true;
      buttonPressStart = millis();
    } else if (millis() - buttonPressStart >= BUTTON_HOLD_MS) {
      Serial.println("[BUTTON] Long press detected, resetting settings");
      prefs.clear();
      delay(300);
      ESP.restart();
    }
  } else {
    buttonHeld = false;
  }
}
