// ============================================================
// Chapter 9 - ESP32 as a Web Server (Access Point only)
// Fixed SSID/password, no provisioning, no router connection.
// Hardware: Relay + OLED SH1106 (U8g2)
//
// Flow:
// - ESP32 starts as a fixed Access Point (192.168.4.1).
// - OLED shows the network name, IP address and a WiFi icon.
// - App Inventor connects directly to this network and controls
//   the relay via HTTP GET (state=ON / state=OFF).
// - The OLED reflects the current relay state.
// ============================================================

#include <WiFi.h>
#include <WebServer.h>
#include <U8g2lib.h>
#include <Wire.h>

// ---------------- Pin configuration ----------------
const int RELAY_PIN = 26; // active-HIGH relay module

// ---------------- OLED (SH1106, I2C, hardware) ----------------
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// ---------------- Access Point settings (fixed) ----------------
const char* AP_SSID = "ESP32_Relay";
const char* AP_PASSWORD = "Relay_123";
IPAddress apIP(192, 168, 4, 1);
IPAddress apSubnet(255, 255, 255, 0);

// ---------------- Web server ----------------
WebServer server(80);

// ---------------- Runtime state ----------------
String relayState = "OFF";

// ============================================================
// OLED drawing helpers
// ============================================================

// Simple signal-bars icon to represent the active Access Point
void drawWifiIcon(int x, int y) {
  display.drawBox(x, y + 8, 3, 4);
  display.drawBox(x + 5, y + 5, 3, 7);
  display.drawBox(x + 10, y + 2, 3, 10);
}

void drawStatusScreen() {
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(0, 10, "Network:");
  display.drawStr(0, 22, AP_SSID);
  display.drawStr(0, 36, "IP:");
  display.drawStr(20, 36, apIP.toString().c_str());

  display.setFont(u8g2_font_7x14B_tf);
  String stateLine = "Relay: " + relayState;
  display.drawStr(0, 56, stateLine.c_str());

  drawWifiIcon(112, 0);
  display.sendBuffer();
}

// ============================================================
// HTTP handlers
// ============================================================

// Called via WEB GET from App Inventor: /relay?state=ON or /relay?state=OFF
void handleRelay() {
  if (!server.hasArg("state")) {
    server.send(400, "text/plain", "Missing state parameter");
    return;
  }

  String state = server.arg("state");

  if (state == "ON") {
    digitalWrite(RELAY_PIN, HIGH);
    relayState = "ON";
    server.send(200, "text/plain", "Relay ON");
  } else if (state == "OFF") {
    digitalWrite(RELAY_PIN, LOW);
    relayState = "OFF";
    server.send(200, "text/plain", "Relay OFF");
  } else {
    server.send(400, "text/plain", "Invalid state value");
    return;
  }

  Serial.println("Relay state changed to: " + relayState);
  drawStatusScreen();
}

// Called via WEB GET from App Inventor to check if the ESP32 is reachable
void handlePing() {
  server.send(200, "text/plain", "PONG");
}

// ============================================================
// Setup / Loop
// ============================================================

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // relay off at boot (active-HIGH)

  display.begin();

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, apSubnet);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.println("Access Point started.");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("IP address: ");
  Serial.println(apIP);

  server.on("/relay", HTTP_GET, handleRelay);
  server.on("/ping", HTTP_GET, handlePing);
  server.begin();
  Serial.println("Web server ready. Use GET /relay?state=ON or OFF");

  drawStatusScreen();
}

void loop() {
  server.handleClient();
}
