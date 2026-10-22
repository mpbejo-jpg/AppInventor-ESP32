/*
  Chapter 13 - Water Quality Monitor (ThingSpeak)

  Reads TDS, turbidity (both analog) and water temperature (DS18B20,
  OneWire) and logs them to a ThingSpeak channel via a plain HTTP GET
  request. Unlike the Google Sheets chapter, ThingSpeak's /update
  endpoint replies directly (no redirect to follow) and needs no TLS,
  so no WiFiClientSecure is required here.

  Note: this chapter originally also included a PH-4502C pH sensor, but
  that module was dropped after its analog output turned out to be
  stuck at full scale regardless of the pH of the sample (likely a
  faulty board - trimmer/soldering issue on that specific unit), so pH
  is not part of this version of the project.

  Behaviour:
    - Wi-Fi provisioning: non-blocking STA connection retried from loop(),
      credentials stored in Preferences ("wifi_cfg"), AP fallback if no
      credentials are saved. Physical button (GPIO27), held 5s, erases the
      stored Wi-Fi configuration and returns to AP mode.
    - Status LED: blinking while not connected (AP mode or STA still
      retrying), OFF once Wi-Fi is connected and idle. Turns ON only for
      the duration of a sensor read + upload cycle.
    - Application parameters (ThingSpeak Write API Key, reading interval)
      are configurable from the App Inventor app via simple HTTP GET
      endpoints and stored in Preferences ("water_cfg"), so they survive
      a reboot.
    - Every "interval" minutes: reads TDS, turbidity and temperature,
      then uploads them to ThingSpeak in a single request:
      field1=TDS, field2=turbidity, field3=temperature, field4=ESP32's
      current local IP address (so the App Inventor app can read it from
      ThingSpeak instead of requiring manual entry).

  Hardware:
    TDS Meter  -> AOUT -> GPIO35 (ADC1_CH7), powered at 3.3V
    JD23-2     -> OUT  -> GPIO32 (ADC1_CH4), powered at 3.3V
    DS18B20    -> DATA -> GPIO4  (OneWire, breakout board with built-in
                                  pull-up), powered at 3.3V
    Button     -> GPIO27 (INPUT_PULLUP, hold 5s = erase Wi-Fi config)
    LED        -> GPIO26 (status / activity indicator)

  Note: both analog sensors are powered at 3.3V instead of their nominal
  5V, so their analog outputs stay safely within the ESP32 ADC's 3.3V
  maximum input - this avoids the need for a voltage divider.

  Conversion formulas below (TDS, turbidity) use placeholder coefficients
  and MUST be calibrated against reference solutions before the readings
  are meaningful - see the calibration section of the chapter.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ---------------- Pin configuration ----------------
#define TDS_PIN             35  // ADC1_CH7
#define TURBIDITY_PIN       32  // ADC1_CH4
#define ONE_WIRE_PIN        4   // DS18B20 data pin
#define WIFI_RESET_BTN_PIN  27  // INPUT_PULLUP, hold 5s = erase Wi-Fi config
#define LED_PIN             26

const unsigned long RESET_HOLD_MS = 5000; // hold WIFI_RESET_BTN_PIN this long to erase config

// ---------------- Calibration constants ----------------
// Fill these in after running the companion WaterQuality_Calibration.ino
// sketch. Same names are used in both sketches on purpose - copy the
// printed values here directly, no renaming needed.
const float TDS_PPM_PER_VOLT = 231.4262f;
const float TURBIDITY_CLEAR_VOLTAGE = 1.5529f;
const float TURBIDITY_NTU_PER_VOLT = 1354.7043f;

// ---------------- Wi-Fi / networking parameters ----------------
const char* AP_SSID = "WaterQuality_Setup";

WebServer server(80);
Preferences preferences; // namespace "wifi_cfg"  - Wi-Fi credentials
Preferences waterPrefs;  // namespace "water_cfg" - application parameters

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature tempSensor(&oneWire);

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

// ---------------- Application parameters (Preferences "water_cfg") ----------------
String thingspeakApiKey = "";      // key "ts_key" - ThingSpeak Write API Key
unsigned long intervalMinutes = 1; // key "interval", minimum 1 (ThingSpeak free tier
                                    // needs at least 15s between updates; 1 minute
                                    // stays safely above that limit)

// ---------------- Reading cycle state ----------------
unsigned long lastReadingMs = 0;
bool firstReadingDone = false;

// Defined here, before any function, so Arduino IDE's auto-generated
// function prototypes (which it inserts at the top of the file) can see
// this type. Leaving it further down causes a "does not name a type"
// compile error on the readSensors()/sendToThingSpeak() prototypes.
struct SensorReading {
  float tds;
  float turbidity;
  float temperature;
  bool tempValid;
};

// =====================================================================
//  SENSOR READING (TDS, turbidity: analog / DS18B20: OneWire)
// =====================================================================

// Converts a raw ADC reading (0-4095, ESP32 12-bit) to volts, using the
// 3.3V reference (both analog sensors are powered at 3.3V here).
float adcToVoltage(int rawAdc) {
  return (rawAdc / 4095.0f) * 3.3f;
}

// TDS conversion: temperature-compensated, then linear (ppm proportional
// to compensated voltage) - simpler than a manufacturer polynomial and a
// better match for a homemade multi-point calibration (distilled water +
// known NaCl concentrations, see the chapter's calibration section).
float voltageToTds(float voltage, float tempC) {
  float compensationCoefficient = 1.0f + 0.02f * (tempC - 25.0f);
  float compensatedVoltage = voltage / compensationCoefficient;
  return compensatedVoltage * TDS_PPM_PER_VOLT;
}

// Turbidity conversion: linear, calibrated against a clear-water sample
// (TURBIDITY_CLEAR_VOLTAGE) and a turbid reference sample (slope
// TURBIDITY_NTU_PER_VOLT) - see the chapter's calibration section.
float voltageToTurbidity(float voltage) {
  if (voltage >= TURBIDITY_CLEAR_VOLTAGE) return 0.0f;
  return (TURBIDITY_CLEAR_VOLTAGE - voltage) * TURBIDITY_NTU_PER_VOLT;
}

SensorReading readSensors() {
  SensorReading reading;

  float tdsVoltage = adcToVoltage(analogRead(TDS_PIN));
  float turbidityVoltage = adcToVoltage(analogRead(TURBIDITY_PIN));

  tempSensor.requestTemperatures();
  float tempReading = tempSensor.getTempCByIndex(0);

  if (tempReading > -100.0f && tempReading < 125.0f) { // basic sanity check, discard sensor errors
    reading.temperature = tempReading;
    reading.tempValid = true;
  } else {
    // -127.0 = DEVICE_DISCONNECTED_C: no sensor answered on the bus.
    Serial.println("DS18B20 read error (sensor not responding), using 25.0C fallback for TDS compensation");
    reading.temperature = 25.0f;
    reading.tempValid = false;
  }

  reading.tds = voltageToTds(tdsVoltage, reading.temperature);
  reading.turbidity = voltageToTurbidity(turbidityVoltage);

  return reading;
}

// =====================================================================
//  THINGSPEAK UPLOAD
// =====================================================================

// Sends all readings (plus the current local IP, in field4) to ThingSpeak
// in a single request. ThingSpeak's
// /update endpoint answers directly with the new entry ID (no redirect to
// follow, unlike the Apps Script Web App used in the previous chapter),
// so a plain HTTPClient over HTTP is enough - no WiFiClientSecure needed.
void sendToThingSpeak(const SensorReading &reading) {
  if (thingspeakApiKey.length() == 0) {
    Serial.println("ThingSpeak API key not configured, skipping upload.");
    return;
  }

  String url = "http://api.thingspeak.com/update?api_key=" + thingspeakApiKey +
               "&field1=" + String(reading.tds, 1) +
               "&field2=" + String(reading.turbidity, 1) +
               "&field3=" + String(reading.temperature, 1) +
               "&field4=" + WiFi.localIP().toString();

  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  Serial.print("ThingSpeak upload, HTTP code: ");
  Serial.println(httpCode);
  Serial.print("Response body (entry ID, 0 = rejected): ");
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

  SensorReading reading = readSensors();
  Serial.print("TDS: ");
  Serial.print(reading.tds, 1);
  Serial.print(" ppm   Turbidity: ");
  Serial.print(reading.turbidity, 1);
  Serial.print(" NTU   Temperature: ");
  Serial.print(reading.temperature, 1);
  Serial.println(" C");

  sendToThingSpeak(reading);

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

void handleSetApiKey() {
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing value");
    return;
  }
  thingspeakApiKey = server.arg("value");
  waterPrefs.putString("ts_key", thingspeakApiKey);
  server.send(200, "text/plain", "ThingSpeak API key saved");
}

void handleSetInterval() {
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing value");
    return;
  }
  long value = server.arg("value").toInt();
  if (value < 1) value = 1; // minimum 1 minute, enforced on firmware side
  intervalMinutes = (unsigned long)value;
  waterPrefs.putULong("interval", intervalMinutes);
  server.send(200, "text/plain", "Interval set to " + String(intervalMinutes) + " min");
}

void handlePing() {
  server.send(200, "text/plain", "PONG");
}

void handleStatus() {
  String status = "wifi_connected=" + String(wifiConnected ? "true" : "false") + "\n" +
                   "ip=" + (wifiConnected ? WiFi.localIP().toString() : "n/a") + "\n" +
                   "thingspeak_key=" + (thingspeakApiKey.length() ? "(set)" : "(not set)") + "\n" +
                   "interval_minutes=" + String(intervalMinutes);
  server.send(200, "text/plain", status);
}

void setupWebServer() {
  server.on("/setconfig", HTTP_GET, handleSetConfig);
  server.on("/setApiKey", HTTP_GET, handleSetApiKey);
  server.on("/setInterval", HTTP_GET, handleSetInterval);
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

  pinMode(LED_PIN, OUTPUT);
  pinMode(WIFI_RESET_BTN_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);

  tempSensor.begin();

  preferences.begin("wifi_cfg", false);
  savedSsid = preferences.getString("ssid", "");
  savedPass = preferences.getString("pass", "");

  waterPrefs.begin("water_cfg", false);
  thingspeakApiKey = waterPrefs.getString("ts_key", "");
  intervalMinutes = waterPrefs.getULong("interval", 1);
  if (intervalMinutes < 1) intervalMinutes = 1;

  // Non-blocking Wi-Fi start: if no credentials are saved at all, go
  // straight to AP provisioning mode.
  if (savedSsid.length() == 0) {
    startAccessPoint();
    staConnected = false;
  } else {
    startWifiRadio();
  }

  setupWebServer();

  Serial.println("Water Quality node ready.");
}

void loop() {
  maintainWifiConnection();
  handleResetButton();
  updateStatusLed();
  maintainReadingCycle();
  server.handleClient();
}
