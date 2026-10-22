/*
  Chapter 12 - Air Quality Monitor (SDS011 + Google Sheet)

  Reads PM2.5 and PM10 from an SDS011 laser dust sensor over UART, and logs
  both values to a Google Sheet through an Apps Script Web App. The same
  Apps Script also sends a single Gmail alert (via MailApp.sendEmail on the
  Google side) when PM2.5 crosses a user-configurable threshold - no
  third-party automation service needed.

  Behaviour:
    - Wi-Fi provisioning: non-blocking STA connection retried from loop(),
      credentials stored in Preferences ("wifi_cfg"), AP fallback if no
      credentials are saved. Physical button (GPIO27), held 5s, erases the
      stored Wi-Fi configuration and returns to AP mode.
    - Status LED: blinking while not connected (AP mode or STA still
      retrying), OFF once Wi-Fi is connected and idle. Turns ON only for
      the duration of a sensor read + upload cycle.
    - Application parameters (PM2.5 threshold, reading interval, Google
      Sheet URL, alert email) are configurable from the App Inventor app
      via simple HTTP GET endpoints and stored in Preferences ("air_cfg"),
      so they survive a reboot.
    - Every "interval" minutes: reads PM2.5/PM10 from the SDS011 and sends
      both to the Google Sheet. If PM2.5 exceeds the threshold, the same
      request also asks Apps Script to send ONE alert email (no repeats
      while still above threshold, and never more than once per hour
      overall). The alarm re-arms once the reading drops back below
      threshold.

  Hardware:
    SDS011 -> UART2: ESP32 GPIO16 (RX, wired to SDS011 TX)
                      ESP32 GPIO17 (TX, wired to SDS011 RX)
              5V and GND
    Button -> GPIO27 (INPUT_PULLUP, hold 5s = erase Wi-Fi config)
    LED    -> GPIO26 (status / activity indicator)

  Note: the SDS011 is factory-calibrated and reports PM2.5/PM10 directly in
  micrograms per cubic meter (ug/m3) - unlike the MQ135 gas sensor, no
  manual calibration curve is needed here.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ---------------- Pin configuration ----------------
#define SDS_RX_PIN          16 // ESP32 RX, wired to SDS011 TX
#define SDS_TX_PIN          17 // ESP32 TX, wired to SDS011 RX
#define WIFI_RESET_BTN_PIN  27 // INPUT_PULLUP, hold 5s = erase Wi-Fi config
#define LED_PIN             26

const unsigned long RESET_HOLD_MS = 5000; // hold WIFI_RESET_BTN_PIN this long to erase config

// ---------------- Wi-Fi / networking parameters ----------------
const char* AP_SSID = "AirQuality_Setup";

WebServer server(80);
Preferences preferences; // namespace "wifi_cfg" - Wi-Fi credentials
Preferences airPrefs;    // namespace "air_cfg"  - application parameters
HardwareSerial sdsSerial(2); // UART2 for the SDS011

// ---------------- Networking state ----------------
bool staConnected = false;
bool wifiConnected = false;
unsigned long lastWifiBeginMs = 0;
unsigned long wifiFirstAttemptMs = 0;
const unsigned long WIFI_RETRY_INTERVAL_MS = 15000;
String savedSsid = "";
String savedPass = "";
unsigned long resetButtonPressedAtMs = 0;
bool resetButtonHeld = false;

// ---------------- Status LED blink state ----------------
unsigned long lastBlinkToggleMs = 0;
bool blinkLedState = false;
const unsigned long BLINK_INTERVAL_MS = 500;

// ---------------- Application parameters (Preferences "air_cfg") ----------------
float thresholdPm25 = 50.0;       // key "threshold" - PM2.5, ug/m3 (WHO 24h guideline is 15, but 50 is a
                                   // more practical hobby-project default; adjust freely from the app)
unsigned long intervalMinutes = 5; // key "interval", minimum 1
String sheetUrl = "";             // key "sheet_url"
String alertEmail = "";           // key "alert_email"

// ---------------- SDS011 read timeout ----------------
const unsigned long SDS_READ_TIMEOUT_MS = 3000; // the sensor streams a frame roughly once a second

// ---------------- Reading cycle state ----------------
unsigned long lastReadingMs = 0;
bool firstReadingDone = false;

// ---------------- Alarm anti-spam state (RAM only) ----------------
bool alarmActive = false;
unsigned long lastEmailMs = 0;
bool everSentEmail = false;
const unsigned long MIN_EMAIL_INTERVAL_MS = 3600000UL; // 1 hour

// Defined here, before any function, so Arduino IDE's auto-generated
// function prototypes (which it inserts at the top of the file) can see
// this type. Leaving it further down causes a "does not name a type"
// compile error on the readSensor()/sendToGoogleSheet() prototypes.
struct SensorReading {
  float pm25;
  float pm10;
  bool valid;
};

// =====================================================================
//  SDS011 READING
// =====================================================================

// Blocks for up to SDS_READ_TIMEOUT_MS waiting for one valid SDS011 frame
// (10 bytes: 0xAA 0xC0 PM2.5_lo PM2.5_hi PM10_lo PM10_hi id1 id2 checksum
// 0xAB). Returns reading.valid = false if no valid frame arrived in time.
SensorReading readSensor() {
  SensorReading reading;
  reading.pm25 = 0;
  reading.pm10 = 0;
  reading.valid = false;

  unsigned long startMs = millis();

  while (millis() - startMs < SDS_READ_TIMEOUT_MS) {
    if (sdsSerial.available() < 10) {
      delay(10);
      continue;
    }

    if (sdsSerial.peek() != 0xAA) {
      sdsSerial.read(); // discard and resync
      continue;
    }

    uint8_t frame[10];
    sdsSerial.readBytes(frame, 10);

    if (frame[0] != 0xAA || frame[1] != 0xC0 || frame[9] != 0xAB) continue;

    uint8_t checksum = 0;
    for (int i = 2; i <= 7; i++) checksum += frame[i];
    if (checksum != frame[8]) continue; // corrupted frame, try again

    reading.pm25 = ((frame[3] << 8) | frame[2]) / 10.0;
    reading.pm10 = ((frame[5] << 8) | frame[4]) / 10.0;
    reading.valid = true;
    return reading;
  }

  Serial.println("SDS011: no valid frame received within timeout.");
  return reading;
}

// =====================================================================
//  GOOGLE SHEET (Apps Script Web App) + ALERT EMAIL
// =====================================================================

// Decides whether this reading should also trigger an alert email, applying
// the anti-spam rule (one email per excursion above threshold, never more
// than once per hour overall). Does not send anything itself - it only
// updates the RAM state and reports the decision to sendToGoogleSheet().
bool shouldSendAlert(float pm25) {
  if (pm25 <= thresholdPm25) {
    alarmActive = false; // re-arm once back under threshold
    return false;
  }

  if (alarmActive) return false; // already alerted for this excursion

  if (everSentEmail && (millis() - lastEmailMs < MIN_EMAIL_INTERVAL_MS)) {
    Serial.println("Threshold exceeded but minimum email interval not reached yet.");
    return false;
  }

  alarmActive = true;
  everSentEmail = true;
  lastEmailMs = millis();
  return true;
}

// Sends the reading to the Google Sheet. When the PM2.5 threshold has just
// been exceeded (see shouldSendAlert()), the same request also asks the
// Apps Script to send the alert email (MailApp.sendEmail on the Google
// side) - no separate service or webhook involved.
void sendToGoogleSheet(const SensorReading &reading) {
  if (sheetUrl.length() == 0) {
    Serial.println("Sheet URL not configured, skipping upload.");
    return;
  }

  bool triggerAlert = shouldSendAlert(reading.pm25);

  String url = sheetUrl + "?pm25=" + String(reading.pm25, 1) +
               "&pm10=" + String(reading.pm10, 1);

  if (triggerAlert) {
    Serial.println("Threshold exceeded: asking Apps Script to send the alert email.");
    url += "&alert=1&email=" + alertEmail;
  }

  HTTPClient http;
  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  http.begin(secureClient, url);
  // Apps Script Web Apps first reply with a 302 redirect to a
  // script.googleusercontent.com URL; doGet() only actually runs once
  // that redirect is followed. HTTPClient does not follow redirects by
  // default, so without this the script (and its row/email logic) never
  // executes even though the request "succeeds".
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int httpCode = http.GET();
  Serial.print("Google Sheet upload, HTTP code: ");
  Serial.println(httpCode);
  Serial.print("Response body: ");
  Serial.println(http.getString());
  http.end();
}

// =====================================================================
//  READING CYCLE (every "interval" minutes)
// =====================================================================

void maintainReadingCycle() {
  if (!wifiConnected) return;

  unsigned long intervalMs = intervalMinutes * 60000UL;

  if (firstReadingDone && (millis() - lastReadingMs < intervalMs)) return;

  lastReadingMs = millis();
  firstReadingDone = true;

  digitalWrite(LED_PIN, HIGH);

  SensorReading reading = readSensor();
  if (reading.valid) {
    Serial.print("PM2.5: ");
    Serial.print(reading.pm25, 1);
    Serial.print(" ug/m3   PM10: ");
    Serial.print(reading.pm10, 1);
    Serial.println(" ug/m3");
    sendToGoogleSheet(reading);
  } else {
    Serial.println("Skipping this cycle: no valid SDS011 reading.");
  }

  digitalWrite(LED_PIN, LOW);
}

// =====================================================================
//  WI-FI PROVISIONING (non-blocking STA connection, retried from loop())
// =====================================================================

void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  Serial.print("AP mode started, connect to: ");
  Serial.println(AP_SSID);
  Serial.print("Configuration address: http://");
  Serial.println(WiFi.softAPIP());
}

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

// Called once right after Wi-Fi connects (see maintainWifiConnection()).
// Writes the ESP32's current local IP to a fixed cell on the Google Sheet
// (via Apps Script), so the App Inventor app can look it up after the
// device reboots into a new DHCP-assigned address - the app has no other
// way to know it in advance.
void registerIpToSheet() {
  if (sheetUrl.length() == 0) {
    Serial.println("Sheet URL not configured yet, skipping IP registration.");
    return;
  }

  String url = sheetUrl + "?mode=register_ip&ip=" + WiFi.localIP().toString();

  HTTPClient http;
  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  http.begin(secureClient, url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int httpCode = http.GET();
  Serial.print("IP registration, HTTP code: ");
  Serial.println(httpCode);
  http.end();
}

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
      registerIpToSheet();
    }
  }

  if (!wifiConnected && millis() - lastWifiBeginMs >= WIFI_RETRY_INTERVAL_MS) {
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());
    lastWifiBeginMs = millis();
  }
}

// =====================================================================
//  HTTP CONFIGURATION ENDPOINTS
// =====================================================================

void handleSetConfig() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "Missing ssid");
    return;
  }
  String ssid = server.arg("ssid");
  String pass = server.hasArg("pass") ? server.arg("pass") : "";

  preferences.putString("ssid", ssid);
  preferences.putString("pass", pass);

  server.send(200, "text/plain", "OK, rebooting");
  delay(300);
  ESP.restart();
}

void handleSetThreshold() {
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing value");
    return;
  }
  thresholdPm25 = server.arg("value").toFloat();
  airPrefs.putFloat("threshold", thresholdPm25);
  server.send(200, "text/plain", "PM2.5 threshold set to " + String(thresholdPm25, 1));
}

void handleSetInterval() {
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing value");
    return;
  }
  long value = server.arg("value").toInt();
  if (value < 1) value = 1; // minimum 1 minute, enforced on firmware side
  intervalMinutes = (unsigned long)value;
  airPrefs.putULong("interval", intervalMinutes);
  server.send(200, "text/plain", "Interval set to " + String(intervalMinutes) + " min");
}

void handleSetSheetUrl() {
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing value");
    return;
  }
  sheetUrl = server.arg("value");
  airPrefs.putString("sheet_url", sheetUrl);
  server.send(200, "text/plain", "Sheet URL saved");
}

void handleSetEmail() {
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing value");
    return;
  }
  alertEmail = server.arg("value");
  airPrefs.putString("alert_email", alertEmail);
  server.send(200, "text/plain", "Alert email saved");
}

void handlePing() {
  server.send(200, "text/plain", "PONG");
}

void handleStatus() {
  String status = "wifi_connected=" + String(wifiConnected ? "true" : "false") + "\n" +
                   "ip=" + (wifiConnected ? WiFi.localIP().toString() : "n/a") + "\n" +
                   "threshold_pm25=" + String(thresholdPm25, 1) + "\n" +
                   "interval_minutes=" + String(intervalMinutes) + "\n" +
                   "sheet_url=" + (sheetUrl.length() ? sheetUrl : "(not set)") + "\n" +
                   "alert_email=" + (alertEmail.length() ? alertEmail : "(not set)") + "\n" +
                   "alarm_active=" + String(alarmActive ? "true" : "false");
  server.send(200, "text/plain", status);
}

void setupWebServer() {
  server.on("/setconfig", HTTP_GET, handleSetConfig);
  server.on("/setThreshold", HTTP_GET, handleSetThreshold);
  server.on("/setInterval", HTTP_GET, handleSetInterval);
  server.on("/setSheetUrl", HTTP_GET, handleSetSheetUrl);
  server.on("/setEmail", HTTP_GET, handleSetEmail);
  server.on("/ping", HTTP_GET, handlePing);
  server.on("/status", HTTP_GET, handleStatus);
  server.begin();
  Serial.println("Web server started (configuration endpoints)");
}

// =====================================================================
//  PHYSICAL BUTTON (hold 5s to erase Wi-Fi configuration)
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
      Serial.println("Reset button held: erasing Wi-Fi configuration");
      preferences.remove("ssid");
      preferences.remove("pass");
      delay(300);
      ESP.restart();
    }
  }
}

// =====================================================================
//  STATUS LED - blinking while not connected, OFF once connected and
//  idle. maintainReadingCycle() takes over the pin during a read/upload.
// =====================================================================

void updateStatusLed() {
  if (wifiConnected) {
    digitalWrite(LED_PIN, LOW);
    return;
  }

  if (millis() - lastBlinkToggleMs >= BLINK_INTERVAL_MS) {
    lastBlinkToggleMs = millis();
    blinkLedState = !blinkLedState;
    digitalWrite(LED_PIN, blinkLedState ? HIGH : LOW);
  }
}

// =====================================================================
//  SETUP / LOOP
// =====================================================================

void setup() {
  Serial.begin(115200);
  sdsSerial.begin(9600, SERIAL_8N1, SDS_RX_PIN, SDS_TX_PIN);

  pinMode(LED_PIN, OUTPUT);
  pinMode(WIFI_RESET_BTN_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);

  preferences.begin("wifi_cfg", false);
  savedSsid = preferences.getString("ssid", "");
  savedPass = preferences.getString("pass", "");

  airPrefs.begin("air_cfg", false);
  thresholdPm25 = airPrefs.getFloat("threshold", 50.0);
  intervalMinutes = airPrefs.getULong("interval", 5);
  if (intervalMinutes < 1) intervalMinutes = 1;
  sheetUrl = airPrefs.getString("sheet_url", "");
  alertEmail = airPrefs.getString("alert_email", "");

  // Non-blocking Wi-Fi start: if no credentials are saved at all, go
  // straight to AP provisioning mode.
  if (savedSsid.length() == 0) {
    startAccessPoint();
    staConnected = false;
  } else {
    startWifiRadio();
  }

  setupWebServer();

  Serial.println("Air Quality node ready (SDS011).");
}

void loop() {
  maintainWifiConnection();
  handleResetButton();
  updateStatusLed();
  maintainReadingCycle();
  server.handleClient();
}
