/*
  Chapter 5 - Servo (Part 1: local)
  Moves a servo motor with two push-buttons, 2 degrees every 15 ms
  while a button is held down.
  Board:     ESP32 Dev Module
  Hardware:  servo GPIO 18, buttons GPIO 25 and GPIO 26
  Libraries: ESP32Servo
*/
#include <ESP32Servo.h>

const int SERVO_PIN = 18;
const int BTN_LEFT_PIN = 25;
const int BTN_RIGHT_PIN = 26;
const int ANGLE_MIN = 0;
const int ANGLE_MAX = 180;
const int STEP = 2;          // degrees per update
const int INTERVAL = 15;     // ms between movements

Servo servo;
int angle = 90;              // starts centered
unsigned long lastMove = 0;

void setup() {
  pinMode(BTN_LEFT_PIN, INPUT_PULLUP);
  pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);
  servo.attach(SERVO_PIN);
  servo.write(angle);
}

void loop() {
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