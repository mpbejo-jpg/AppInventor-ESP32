/*
 * Chapter 11 - ESP32 + ILI9341 + DS3231 + Settings Reset Button
 * Phase 1: AP/STA fallback, Preferences storage, DS3231 RTC, display state machine
 * Phase 2: Weather screen (OpenWeatherMap current + forecast, PROGMEM icons)
 *
 * Flow:
 *  - On boot, check Preferences for saved WiFi credentials.
 *  - If missing -> start SoftAP, show SSID/IP on display, wait for App Inventor
 *    to send credentials via HTTP GET to /setwifi
 *  - If present -> connect as Station. Once connected, wait for the App to
 *    send date/time via /setdatetime (used to set the DS3231 the first time).
 *  - The DS3231 keeps time on battery backup even without mains power, so the
 *    clock screen always reads from the RTC, never from NTP.
 *  - A long press (3s) on the reset button clears all saved Preferences and
 *    reboots into AP mode, so the user can reconfigure everything from scratch.
 *
 * Libraries required:
 *  - TFT_eSPI      (ILI9341 display driver, configured via User_Setup.h)
 *  - RTClib        (Adafruit, DS3231 support)
 *  - Preferences   (ESP32 core, NVS storage)
 *  - WiFi / WebServer (ESP32 core)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <RTClib.h>
#include <TFT_eSPI.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>   // Requires ArduinoJson library v7+
#include "WeatherIcons.h"  // Generated 48x48 RGB565 icon arrays

// ---------------------------------------------------------------------------
// Hardware configuration
// ---------------------------------------------------------------------------
// NOTE: GPIO4 is already used as TFT_RST and GPIO21 as TOUCH_CS in this
// board's TFT_eSPI configuration (see Chapter 10), so both were moved to
// free pins to avoid conflicts with the display/touch SPI wiring.
#define RESET_BUTTON_PIN   27     // Active LOW, internal pull-up
#define RESET_HOLD_MS      3000   // Long-press duration to trigger a reset

// I2C pins for DS3231 (moved away from GPIO21/22 to avoid clashing with
// TOUCH_CS on GPIO21, part of the ILI9341/XPT2046 SPI wiring)
#define I2C_SDA_PIN        32
#define I2C_SCL_PIN        33

// Backlight is driven manually from the sketch (NOT via TFT_BL in
// User_Setup.h), so it can stay OFF while WiFi/RTC init and only turn ON
// once the display itself is safely initialized. Same pin/pattern as
// Chapter 10's Tic Tac Toe WiFi version.
#define BACKLIGHT_PIN      15

// ---------------------------------------------------------------------------
// Access point configuration (used only when no WiFi credentials are saved)
// ---------------------------------------------------------------------------
const char* AP_SSID = "ESP32_ArtClock_Setup";
const char* AP_PASSWORD = "12345678"; // Minimum 8 chars required by ESP32 SoftAP

TFT_eSPI tft = TFT_eSPI();
RTC_DS3231 rtc;
Preferences preferences;
WebServer server(80);

// ---------------------------------------------------------------------------
// Display state machine
// ---------------------------------------------------------------------------
enum DisplayState {
  STATE_AP_CONFIG,
  STATE_CLOCK,
  STATE_WEATHER
};

DisplayState currentState = STATE_AP_CONFIG;
DisplayState lastDrawnState = STATE_AP_CONFIG; // Forces a redraw on first loop
unsigned long lastClockRefresh = 0;

// ---------------------------------------------------------------------------
// WiFi connection status (Station mode only)
// ---------------------------------------------------------------------------
// The AP_CONFIG screen is now reached ONLY by: (a) no saved credentials at
// all on first boot, or (b) a deliberate 3s long-press on the reset button.
// A failed/lost STA connection NEVER switches the display into AP_CONFIG:
// the clock keeps working offline from the DS3231, and a small status
// indicator (dot + IP) in the top-right corner shows connected/offline.
bool wifiConnected = false;
unsigned long lastWifiAttempt = 0;
const unsigned long WIFI_RETRY_INTERVAL_MS = 30000; // retry every 30s if offline

// ---------------------------------------------------------------------------
// Weather (OpenWeatherMap)
// ---------------------------------------------------------------------------
enum WeatherIconType {
  ICON_CLEAR,
  ICON_CLOUDS,
  ICON_RAIN,
  ICON_THUNDERSTORM,
  ICON_SNOW,
  ICON_MIST,
  ICON_UNKNOWN
};

struct WeatherInfo {
  bool valid = false;
  int tempMax = 0;     // rounded, in whatever unit savedTempUnit selects
  int tempMin = 0;
  int humidity = 0;    // percent
  WeatherIconType icon = ICON_UNKNOWN;
};

WeatherInfo todayWeather;
WeatherInfo tomorrowWeather;
bool weatherNeedsRefresh = true;   // forces a fetch the next time STATE_WEATHER is drawn
unsigned long lastWeatherFetch = 0;
const unsigned long WEATHER_CACHE_MS = 10UL * 60UL * 1000UL; // 10 minutes

// ---------------------------------------------------------------------------
// Reset button handling (debounced long-press)
// ---------------------------------------------------------------------------
unsigned long buttonPressStart = 0;
bool buttonIsPressed = false;

// ---------------------------------------------------------------------------
// Touch handling (XPT2046, same SPI bus as the display, driven by TFT_eSPI)
// ---------------------------------------------------------------------------
// Calibration data reused from Chapter 10 (same board/wiring). Re-run
// TFT_eSPI's touch_calibrate example if you switch to a different panel.
uint16_t touchCalibration[5] = { 459, 3291, 510, 3056, 7};

bool touchIsPressed = false; // Used to require a release before the next tap
const unsigned long TOUCH_DEBOUNCE_MS = 300;
unsigned long lastTouchHandledAt = 0;

// ---------------------------------------------------------------------------
// Runtime configuration cache (loaded from Preferences at boot)
// ---------------------------------------------------------------------------
String savedSsid;
String savedPassword;
String savedOwmApiKey;
String savedLat;         // Latitude, already geocoded by the App (OpenWeatherMapExt)
String savedLon;         // Longitude, already geocoded by the App
String savedTempUnit = "C"; // "C" or "F", chosen from the App
String currentCityName;  // Resolved by OpenWeatherMap from lat/lon, not sent by the App

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
void loadConfigFromPreferences();
void startAccessPointMode();
void connectToSavedNetwork();
void setupWebServerAP();
void setupWebServerSTA();
void handleSetWifi();
void handleSetDateTime();
void handleSetConfig();
void drawApConfigScreen();
void drawClockScreen(bool forceFullRedraw);
void drawWeatherScreen();
void checkResetButton();
void factoryResetAndReboot();
void checkTouchAndSwitchState();
void startWifiRadio();
void maintainWifiConnection();
void drawWifiStatusIndicator();
WeatherIconType mapOwmIdToIcon(int id);
const uint16_t* iconArrayFor(WeatherIconType type);
bool fetchOwmJson(const char* endpoint, JsonDocument& doc);
bool fetchWeatherData();
void drawWeatherColumn(int x, const char* label, const WeatherInfo& info);

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, LOW); // keep screen dark during init

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  if (!rtc.begin()) {
    // If the RTC is not found the display still works, but the clock
    // screen will show an error. Continue anyway so AP/STA logic can run.
    Serial.println("DS3231 not found on I2C bus");
  } else if (rtc.lostPower()) {
    // Backup battery did not hold the time. Set a placeholder timestamp;
    // it will be corrected as soon as the App Inventor app sends the real
    // date/time via /setdatetime.
    Serial.println("DS3231 lost power, time is not reliable until synced");
    rtc.adjust(DateTime(2026, 1, 1, 0, 0, 0));
  }

  preferences.begin("cfg", false);
  loadConfigFromPreferences();

  // WiFi is started BEFORE the display: enabling the radio causes a current
  // spike on the 3.3V rail that can corrupt the SPI communication with the
  // TFT if it happens while the display is already initialized. Starting
  // WiFi first, then waiting for the rail to settle, avoids the garbled/
  // noisy screen seen when the two happen in the opposite order.
  if (savedSsid.length() == 0) {
    startAccessPointMode();
  } else {
    connectToSavedNetwork();
  }

  delay(500); // let the 3.3V rail stabilize after the WiFi power spike

  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true); // fixes wrong colors (e.g. green instead of navy) in pushImage()
  tft.setTouch(touchCalibration);
  tft.fillScreen(TFT_BLACK);

  digitalWrite(BACKLIGHT_PIN, HIGH); // display is ready, turn the screen on
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void loop() {
  server.handleClient();
  checkResetButton();
  checkTouchAndSwitchState();
  maintainWifiConnection();

  switch (currentState) {
    case STATE_AP_CONFIG:
      if (lastDrawnState != STATE_AP_CONFIG) {
        drawApConfigScreen();
        lastDrawnState = STATE_AP_CONFIG;
      }
      break;

    case STATE_CLOCK: {
      bool fullRedraw = (lastDrawnState != STATE_CLOCK);
      // Refresh the clock once per second, not on every loop iteration
      if (fullRedraw || millis() - lastClockRefresh >= 1000) {
        drawClockScreen(fullRedraw);
        lastClockRefresh = millis();
        lastDrawnState = STATE_CLOCK;
      }
      break;
    }

    case STATE_WEATHER:
      if (lastDrawnState != STATE_WEATHER) {
        drawWeatherScreen();
        lastDrawnState = STATE_WEATHER;
      }
      break;
  }
}

// ---------------------------------------------------------------------------
// Preferences
// ---------------------------------------------------------------------------
void loadConfigFromPreferences() {
  savedSsid = preferences.getString("ssid", "");
  savedPassword = preferences.getString("pass", "");
  savedOwmApiKey = preferences.getString("owm_key", "");
  savedLat = preferences.getString("lat", "");
  savedLon = preferences.getString("lon", "");
  savedTempUnit = preferences.getString("temp_unit", "C");
}

void factoryResetAndReboot() {
  Serial.println("Factory reset requested, clearing Preferences");
  preferences.clear();
  preferences.end();
  delay(200);
  ESP.restart();
}

// ---------------------------------------------------------------------------
// WiFi: Access Point mode (no credentials saved yet)
// ---------------------------------------------------------------------------
void startAccessPointMode() {
  currentState = STATE_AP_CONFIG;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.print("AP started. IP address: ");
  Serial.println(WiFi.softAPIP());

  setupWebServerAP();
  server.begin();

  // Force the first draw in the main loop
  lastDrawnState = STATE_CLOCK; // any value different from STATE_AP_CONFIG
}

void setupWebServerAP() {
  server.on("/setwifi", HTTP_GET, handleSetWifi);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found. Use /setwifi?ssid=..&pass=..");
  });
}

// Example call from App Inventor:
// http://192.168.4.1/setwifi?ssid=MyHomeWifi&pass=MyPassword
void handleSetWifi() {
  if (!server.hasArg("ssid") || !server.hasArg("pass")) {
    server.send(400, "text/plain", "Missing ssid or pass parameter");
    return;
  }

  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  preferences.putString("ssid", ssid);
  preferences.putString("pass", pass);

  server.send(200, "text/plain", "WiFi credentials saved. Rebooting...");
  delay(500);
  ESP.restart();
}

// ---------------------------------------------------------------------------
// WiFi: Station mode (credentials already saved)
// ---------------------------------------------------------------------------
// Non-blocking: only starts the connection, so the display can init right
// after instead of staying black during the whole handshake. Progress is
// tracked/retried by maintainWifiConnection() in loop().
void connectToSavedNetwork() {
  startWifiRadio();

  setupWebServerSTA();
  server.begin();

  currentState = STATE_CLOCK;
  lastDrawnState = STATE_AP_CONFIG; // force first draw
}

// Fully resets the WiFi radio before each attempt (WIFI_OFF -> WIFI_STA,
// disconnect, no modem sleep): avoids intermittent reconnect failures seen
// on some boards right after a reset. Only kicks off the attempt, doesn't wait.
void startWifiRadio() {
  Serial.println("Starting WiFi radio...");

  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  delay(100);

  WiFi.begin(savedSsid.c_str(), savedPassword.c_str());

  wifiConnected = false;
  lastWifiAttempt = millis();
}

// Called every loop(): tracks connect/disconnect transitions for the status
// indicator, and retries WiFi.begin() periodically while offline.
void maintainWifiConnection() {
  if (currentState == STATE_AP_CONFIG) return;

  bool nowConnected = (WiFi.status() == WL_CONNECTED);

  if (nowConnected != wifiConnected) {
    wifiConnected = nowConnected;
    Serial.println(wifiConnected ? "WiFi connected" : "WiFi connection lost");
    if (currentState == STATE_CLOCK) drawWifiStatusIndicator();
  }

  if (!wifiConnected && millis() - lastWifiAttempt >= WIFI_RETRY_INTERVAL_MS) {
    WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
    lastWifiAttempt = millis();
  }
}

void setupWebServerSTA() {
  server.on("/setdatetime", HTTP_GET, handleSetDateTime);
  server.on("/setconfig", HTTP_GET, handleSetConfig);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
}

// Example call from App Inventor: sends the date/time components already
// read in LOCAL time from the phone (Clock1.Year(Clock1.Now), etc.), so
// there is no epoch math and no timezone offset to worry about anywhere.
// http://<esp32-ip>/setdatetime?y=2026&mo=7&d=29&h=15&mi=30&s=0
void handleSetDateTime() {
  const char* requiredArgs[] = { "y", "mo", "d", "h", "mi", "s" };
  for (int i = 0; i < 6; i++) {
    if (!server.hasArg(requiredArgs[i])) {
      server.send(400, "text/plain", String("Missing parameter: ") + requiredArgs[i]);
      return;
    }
  }

  int y = server.arg("y").toInt();
  int mo = server.arg("mo").toInt();
  int d = server.arg("d").toInt();
  int h = server.arg("h").toInt();
  int mi = server.arg("mi").toInt();
  int s = server.arg("s").toInt();

  rtc.adjust(DateTime(y, mo, d, h, mi, s));

  Serial.printf("RTC updated from App Inventor: %04d-%02d-%02d %02d:%02d:%02d\n",
                y, mo, d, h, mi, s);
  server.send(200, "text/plain", "Date/time synced to DS3231");
}

// Example call (lat/lon are geocoded by the App via the OpenWeatherMapExt
// extension, NOT resolved on the ESP32). MET needs no API key.
// http://<esp32-ip>/setconfig?owmkey=XXXX&lat=45.4408&lon=12.3155
void handleSetConfig() {
  bool updated = false;

  if (server.hasArg("owmkey")) {
    savedOwmApiKey = server.arg("owmkey");
    preferences.putString("owm_key", savedOwmApiKey);
    updated = true;
  }
  if (server.hasArg("lat")) {
    savedLat = server.arg("lat");
    preferences.putString("lat", savedLat);
    updated = true;
  }
  if (server.hasArg("lon")) {
    savedLon = server.arg("lon");
    preferences.putString("lon", savedLon);
    updated = true;
  }
  if (server.hasArg("unit")) {
    savedTempUnit = server.arg("unit"); // "C" or "F"
    preferences.putString("temp_unit", savedTempUnit);
    updated = true;
  }

  if (!updated) {
    server.send(400, "text/plain", "No recognized parameters");
    return;
  }

  Serial.printf("Config saved - owmkey:%s lat:%s lon:%s\n",
                savedOwmApiKey.c_str(), savedLat.c_str(), savedLon.c_str());
  server.send(200, "text/plain", "Configuration saved");
}



// ---------------------------------------------------------------------------
// Display screens
// ---------------------------------------------------------------------------
void drawApConfigScreen() {
  tft.fillScreen(TFT_NAVY);
  tft.setTextColor(TFT_GOLD, TFT_NAVY);
  tft.setTextSize(2);

  tft.setCursor(10, 20);
  tft.println("Configuration mode");

  tft.setTextSize(1);
  tft.setCursor(10, 60);
  tft.print("Connect to WiFi network:");
  tft.setCursor(10, 75);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.println(AP_SSID);

  tft.setTextColor(TFT_GOLD, TFT_NAVY);
  tft.setCursor(10, 95);
  tft.print("Password:");
  tft.setCursor(10, 110);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.println(AP_PASSWORD);

  tft.setTextColor(TFT_GOLD, TFT_NAVY);
  tft.setCursor(10, 130);
  tft.print("Then open the App and send");
  tft.setCursor(10, 145);
  tft.print("your home WiFi credentials.");

  tft.setCursor(10, 175);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.print("Device IP: ");
  tft.println(WiFi.softAPIP());
}

void drawClockScreen(bool forceFullRedraw) {
  DateTime now = rtc.now();

  if (forceFullRedraw) {
    tft.fillScreen(TFT_NAVY);
    drawWifiStatusIndicator();
  }

  // Redraw only the time area to avoid flicker
  tft.fillRect(10, 80, 300, 60, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(4);
  tft.setCursor(20, 90);

  char timeBuffer[9];
  snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d",
           now.hour(), now.minute(), now.second());
  tft.print(timeBuffer);

  tft.setTextSize(2);
  tft.setCursor(20, 150);
  char dateBuffer[16];
  snprintf(dateBuffer, sizeof(dateBuffer), "%02d/%02d/%04d",
           now.day(), now.month(), now.year());
  tft.fillRect(10, 150, 300, 20, TFT_NAVY);
  tft.print(dateBuffer);
}

// Small status indicator in the top-right corner: a colored dot (green =
// connected, red = offline) plus the current IP address, or 0.0.0.0 when
// there is no WiFi connection. Redrawn on full screen redraws and whenever
// the connection state changes (see maintainWifiConnection()).
void drawWifiStatusIndicator() {
  const int areaX = 190, areaY = 0, areaW = 130, areaH = 20;
  tft.fillRect(areaX, areaY, areaW, areaH, TFT_NAVY);

  uint16_t dotColor = wifiConnected ? TFT_GREEN : TFT_RED;
  tft.fillCircle(areaX + 8, areaY + 10, 5, dotColor);

  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(1);
  tft.setCursor(areaX + 20, areaY + 6);

  IPAddress ip = wifiConnected ? WiFi.localIP() : IPAddress(0, 0, 0, 0);
  tft.print(ip);
}

// ---------------------------------------------------------------------------
// Weather: fetch + rendering
// ---------------------------------------------------------------------------
WeatherIconType mapOwmIdToIcon(int id) {
  if (id >= 200 && id < 300) return ICON_THUNDERSTORM;
  if (id >= 300 && id < 600) return ICON_RAIN;   // drizzle + rain
  if (id >= 600 && id < 700) return ICON_SNOW;
  if (id >= 700 && id < 800) return ICON_MIST;   // fog/mist/haze/dust/etc
  if (id == 800) return ICON_CLEAR;
  if (id > 800 && id < 900) return ICON_CLOUDS;
  return ICON_UNKNOWN;
}

const uint16_t* iconArrayFor(WeatherIconType type) {
  switch (type) {
    case ICON_CLEAR:        return weatherIcon_clear;
    case ICON_CLOUDS:       return weatherIcon_clouds;
    case ICON_RAIN:         return weatherIcon_rain;
    case ICON_THUNDERSTORM: return weatherIcon_thunderstorm;
    case ICON_SNOW:         return weatherIcon_snow;
    case ICON_MIST:         return weatherIcon_mist;
    default:                return weatherIcon_clouds; // fallback for unknown codes
  }
}

// Single HTTPS GET + JSON parse, reused for both the current-weather and
// forecast endpoints below (they only differ by URL).
bool fetchOwmJson(const char* endpoint, JsonDocument& doc) {
  WiFiClientSecure client;
  client.setInsecure(); // skips certificate validation - simplification for this project

  const char* units = (savedTempUnit == "F") ? "imperial" : "metric";

  HTTPClient http;
  String url = String("https://api.openweathermap.org/data/2.5/") + endpoint +
               "?lat=" + savedLat + "&lon=" + savedLon +
               "&appid=" + savedOwmApiKey + "&units=" + units;
  http.begin(client, url);
  int httpCode = http.GET();

  bool ok = false;
  if (httpCode == 200) {
    ok = (deserializeJson(doc, http.getString()) == DeserializationError::Ok);
  } else {
    Serial.printf("Weather HTTP error (%s): %d\n", endpoint, httpCode);
  }
  http.end();
  return ok;
}

// Fetches ONLY the forecast endpoint (3h steps) and splits entries into
// "today" and "tomorrow" by date (from the DS3231), computing max/min
// temperature across each day's slots. Icon and humidity for each day come
// from the slot closest to midday. Note: "today" only covers the forecast
// slots from now until midnight (the API has no data for hours already
// past), so its Min may be higher than the real morning low.
bool fetchWeatherData() {
  if (savedOwmApiKey.length() == 0 || savedLat.length() == 0 || savedLon.length() == 0) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  todayWeather.valid = false;
  tomorrowWeather.valid = false;

  JsonDocument doc;
  if (!fetchOwmJson("forecast", doc)) return false;

  currentCityName = doc["city"]["name"].as<String>();

  DateTime now = rtc.now();
  DateTime tomorrowDate = now + TimeSpan(1, 0, 0, 0);
  char todayStr[11], tomorrowStr[11];
  snprintf(todayStr, sizeof(todayStr), "%04d-%02d-%02d", now.year(), now.month(), now.day());
  snprintf(tomorrowStr, sizeof(tomorrowStr), "%04d-%02d-%02d",
           tomorrowDate.year(), tomorrowDate.month(), tomorrowDate.day());

  int bestHourDiffToday = 99, bestHourDiffTomorrow = 99;

  for (JsonObject entry : doc["list"].as<JsonArray>()) {
    String dtTxt = entry["dt_txt"].as<String>(); // "YYYY-MM-DD HH:MM:SS"
    String entryDate = dtTxt.substring(0, 10);
    int hourDiff = abs(dtTxt.substring(11, 13).toInt() - 12); // closest to midday
    float t = entry["main"]["temp"].as<float>();
    int id = entry["weather"][0]["id"].as<int>();
    int hum = entry["main"]["humidity"].as<int>();

    WeatherInfo* target = nullptr;
    int* bestHourDiff = nullptr;
    if (entryDate == todayStr) { target = &todayWeather; bestHourDiff = &bestHourDiffToday; }
    else if (entryDate == tomorrowStr) { target = &tomorrowWeather; bestHourDiff = &bestHourDiffTomorrow; }
    if (!target) continue;

    if (!target->valid || t > target->tempMax) target->tempMax = round(t);
    if (!target->valid || t < target->tempMin) target->tempMin = round(t);
    if (hourDiff < *bestHourDiff) {
      *bestHourDiff = hourDiff;
      target->humidity = hum;
      target->icon = mapOwmIdToIcon(id);
    }
    target->valid = true;
  }

  lastWeatherFetch = millis();
  return todayWeather.valid && tomorrowWeather.valid;
}

// Draws one column (icon + max/min temperature + humidity, or an error
// line) at x. Reused for both "Today" and "Tomorrow".
void drawWeatherColumn(int x, const char* label, const WeatherInfo& info) {
  tft.setTextColor(TFT_GOLD, TFT_NAVY);
  tft.setTextSize(2);
  tft.setCursor(x, 56);
  tft.print(label);

  if (info.valid) {
    tft.pushImage(x, 76, WEATHER_ICON_SIZE, WEATHER_ICON_SIZE, iconArrayFor(info.icon));

    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextSize(2);

    tft.setCursor(x, 138); // extra gap below the icon
    tft.print("Max:");
    tft.print(info.tempMax);
    tft.print((char)247);
    tft.print(savedTempUnit);

    tft.setCursor(x, 162);
    tft.print("Min:");
    tft.print(info.tempMin);
    tft.print((char)247);
    tft.print(savedTempUnit);

    tft.setCursor(x, 186);
    tft.print("Hum:");
    tft.print(info.humidity);
    tft.print("%");
  } else {
    tft.setTextColor(TFT_RED, TFT_NAVY);
    tft.setTextSize(2);
    tft.setCursor(x, 100);
    tft.println("No data");
  }
}

void drawWeatherScreen() {
  tft.fillScreen(TFT_NAVY);
  drawWifiStatusIndicator();

  if (WiFi.status() != WL_CONNECTED) {
    tft.setTextColor(TFT_GOLD, TFT_NAVY);
    tft.setTextSize(2);
    tft.setCursor(10, 100);
    tft.println("No WiFi connection");
    return;
  }

  bool cacheStale = (millis() - lastWeatherFetch > WEATHER_CACHE_MS);
  if (weatherNeedsRefresh || cacheStale || !todayWeather.valid) {
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextSize(2);
    tft.setCursor(50, 110);
    tft.println("Loading weather...");

    fetchWeatherData();
    weatherNeedsRefresh = false;

    tft.fillScreen(TFT_NAVY); // clear the "loading" message before drawing results
    drawWifiStatusIndicator();
  }

  // City name, top-left (resolved by OWM itself from lat/lon, not sent by the App)
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  tft.setCursor(5, 28);
  tft.print(currentCityName);

  drawWeatherColumn(30, "Today", todayWeather);
  drawWeatherColumn(170, "Tomorrow", tomorrowWeather);
}

// ---------------------------------------------------------------------------
// Reset button (long press = factory reset)
// ---------------------------------------------------------------------------
void checkResetButton() {
  bool pressedNow = (digitalRead(RESET_BUTTON_PIN) == LOW);

  if (pressedNow && !buttonIsPressed) {
    // Button just pressed
    buttonIsPressed = true;
    buttonPressStart = millis();
  } else if (pressedNow && buttonIsPressed) {
    // Button still held down, check duration
    if (millis() - buttonPressStart >= RESET_HOLD_MS) {
      factoryResetAndReboot();
    }
  } else if (!pressedNow && buttonIsPressed) {
    // Button released before reaching the hold threshold
    buttonIsPressed = false;
  }
}

// ---------------------------------------------------------------------------
// Touch: cycles CLOCK -> WEATHER -> CLOCK on each tap.
// Ignored while in AP_CONFIG state (no screens to cycle through yet).
// ---------------------------------------------------------------------------
void checkTouchAndSwitchState() {
  if (currentState == STATE_AP_CONFIG) {
    return;
  }

  uint16_t touchX, touchY;
  bool pressedNow = tft.getTouch(&touchX, &touchY);

  if (pressedNow && !touchIsPressed) {
    // Rising edge: a new tap started
    touchIsPressed = true;

    unsigned long nowMs = millis();
    if (nowMs - lastTouchHandledAt >= TOUCH_DEBOUNCE_MS) {
      lastTouchHandledAt = nowMs;

      // Weather screen is skipped if not configured (empty API key or
      // coordinates) - whether it's "configured" is decided by the App,
      // the ESP32 just checks for empty values and falls back to the clock.
      bool weatherConfigured = (savedOwmApiKey.length() > 0 &&
                                 savedLat.length() > 0 &&
                                 savedLon.length() > 0);

      switch (currentState) {
        case STATE_CLOCK:
          if (weatherConfigured) {
            currentState = STATE_WEATHER;
            weatherNeedsRefresh = true; // force a fresh fetch every time we enter this screen
          }
          // else: weather not configured, stay on the clock
          break;
        case STATE_WEATHER:
          currentState = STATE_CLOCK;
          break;
        default: break;
      }
    }
  } else if (!pressedNow && touchIsPressed) {
    // Touch released, ready to detect the next tap
    touchIsPressed = false;
  }
}
