/*
  Chapter 5 - Servo (Part 2: BLE)
  Same as Part 1, plus the servo follows the angle (-90..90) sent by
  the App Inventor app over BLE (device name "ESP32-Servo").
  Board:     ESP32 Dev Module
  Hardware:  servo GPIO 18, buttons GPIO 25 and GPIO 26
  Libraries: ESP32Servo
*/
#include <ESP32Servo.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

const int SERVO_PIN = 18;
const int BTN_LEFT_PIN = 25;
const int BTN_RIGHT_PIN = 26;
const int ANGLE_MIN = 0;
const int ANGLE_MAX = 180;
const int STEP = 2;
const int INTERVAL = 15;

Servo servo;
int angle = 90;
unsigned long lastMove = 0;

BLEServer *pServer;
bool isAdvertising = false;

class WriteCallback: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) override {
    String value = pChar->getValue();
    if (value.length() == 0) return;
    int phoneAngle = value.toInt();
    angle = map(phoneAngle, -90, 90, 0, 180);
    angle = constrain(angle, ANGLE_MIN, ANGLE_MAX);
    servo.write(angle);
  }
};

class ServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override {
    Serial.println("CONNECTED - " + String(millis()));
  }
  void onDisconnect(BLEServer *pServer) override {
    Serial.println("DISCONNECTED - " + String(millis()));
    BLEDevice::startAdvertising();
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BTN_LEFT_PIN, INPUT_PULLUP);
  pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);
  servo.attach(SERVO_PIN);
  servo.write(angle);

  BLEDevice::init("ESP32-Servo");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_WRITE
  );
  pCharacteristic->setCallbacks(new WriteCallback());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  BLEDevice::startAdvertising();
  isAdvertising = true;
  Serial.println("=== ADVERTISING AVVIATO ===");
}

void loop() {
  
  if (pServer->getConnectedCount() == 0 && !isAdvertising) {
    delay(200);
    BLEDevice::startAdvertising();
    isAdvertising = true;
  }
  if (pServer->getConnectedCount() > 0) {
    isAdvertising = false;
  }

  
  if (millis() - lastMove < INTERVAL) return;

  bool leftPressed = digitalRead(BTN_LEFT_PIN) == LOW;
  bool rightPressed = digitalRead(BTN_RIGHT_PIN) == LOW;

  if (leftPressed && angle < ANGLE_MAX) {
    angle += STEP;
    servo.write(angle);
    lastMove = millis();
  } else if (rightPressed && angle > ANGLE_MIN) {
    angle -= STEP;
    servo.write(angle);
    lastMove = millis();
  }
}