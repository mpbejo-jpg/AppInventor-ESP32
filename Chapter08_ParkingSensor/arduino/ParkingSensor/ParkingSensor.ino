/*
  Chapter 8 - Parking Sensor (Part 1: local)
  Measures the distance with the VL53L1X laser sensor and colors the
  RGB LED from green (1000 mm or more) to red (100 mm or less).
  Board:     ESP32 Dev Module
  Hardware:  VL53L1X (I2C SDA 21, SCL 22), RGB LED GPIO 25 (R), 26 (G), 27 (B)
  Libraries: SparkFun VL53L1X 4m Laser Distance Sensor
*/
#include <Wire.h>
#include <SparkFun_VL53L1X.h>

SFEVL53L1X sensor;

const int LED_PIN_R = 25;
const int LED_PIN_G = 26;
const int LED_PIN_B = 27;

const int DIST_NEAR = 100;   // mm - full red
const int DIST_FAR  = 1000;  // mm - full green

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
  }

  delay(50);
}