/*
  Chapter 8 - Parking Sensor (Part 2: BLE)
  Same as Part 1, plus the distance in millimetres is sent to the
  App Inventor app over BLE (device name "ESP32_ParkingSensor").
  Board:     ESP32 Dev Module
  Hardware:  VL53L1X (I2C SDA 21, SCL 22), RGB LED GPIO 25 (R), 26 (G), 27 (B)
  Libraries: SparkFun VL53L1X 4m Laser Distance Sensor
*/
#include <Wire.h>
#include <SparkFun_VL53L1X.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

SFEVL53L1X sensor;

const int LED_PIN_R = 25;
const int LED_PIN_G = 26;
const int LED_PIN_B = 27;

const int DIST_NEAR = 100;   // mm - full red
const int DIST_FAR  = 1000;  // mm - full green

// Standard UUIDs for this chapter's BLE service
#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer* bleServer = NULL;
BLECharacteristic* distanceCharacteristic = NULL;
bool deviceConnected = false;

class ServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* server) {
    deviceConnected = true;
    Serial.println("BLE client connected");
  }

  void onDisconnect(BLEServer* server) {
    deviceConnected = false;
    Serial.println("BLE client disconnected, restarting advertising");
    server->getAdvertising()->start();
  }
};

void setup() {
  Serial.begin(115200);
  Wire.begin();

  if (sensor.begin() != 0) {
    Serial.println("Error: VL53L1X not detected");
    while (1) delay(1000);
  }

  sensor.startRanging();

  pinMode(LED_PIN_R, OUTPUT);
  pinMode(LED_PIN_G, OUTPUT);
  pinMode(LED_PIN_B, OUTPUT);

  // BLE setup
  BLEDevice::init("ESP32_ParkingSensor");
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());

  BLEService* bleService = bleServer->createService(SERVICE_UUID);

  distanceCharacteristic = bleService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  distanceCharacteristic->addDescriptor(new BLE2902());

  bleService->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("BLE started, waiting for connection...");
}

void loop() {
  if (sensor.checkForDataReady()) {
    int distance = sensor.getDistance(); // mm
    sensor.clearInterrupt();

    // Clamp the value inside the usable range
    int d = constrain(distance, DIST_NEAR, DIST_FAR);

    // Map: near=0 -> full red, far=255 -> full green
    int red = map(d, DIST_NEAR, DIST_FAR, 255, 0);
    int green = map(d, DIST_NEAR, DIST_FAR, 0, 255);

    analogWrite(LED_PIN_R, red);
    analogWrite(LED_PIN_G, green);
    analogWrite(LED_PIN_B, 0);

    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.print(" mm  R=");
    Serial.print(red);
    Serial.print(" G=");
    Serial.println(green);

    // Send raw distance as text over BLE notify, only if a client is connected
    if (deviceConnected) {
      char value[8];
      snprintf(value, sizeof(value), "%d", distance);
      distanceCharacteristic->setValue(value);
      distanceCharacteristic->notify();
    }
  }

  delay(50);
}