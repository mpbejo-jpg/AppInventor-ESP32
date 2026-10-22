/*
  Chapter 4: RGB LED Bidirectional Bluetooth Control
  Description: Selects color channel via buttons, adjusts intensity via potentiometer.
*/

#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please check your IDE board configuration.
#endif

BluetoothSerial SerialBT;

// Pin configuration
const int PIN_RED = 25;    // RGB Red connected to GPIO 25 (G25)
const int PIN_GREEN = 32;  // RGB Green connected to GPIO 32 (G32)
const int PIN_BLUE = 33;   // RGB Blue connected to GPIO 33 (G33)
const int PIN_POT = 34;    // Potentiometer connected to GPIO 34 (G34)

const int BTN_BLUE = 15;   // Blue Button on GPIO 15 (G15)
const int BTN_GREEN = 2;   // Green Button on GPIO 2 (G2)
const int BTN_RED = 4;     // Red Button on GPIO 4 (G4)

// PWM and Color State Variables
int redVal = 0, greenVal = 0, blueVal = 0;
int activeColor = 0; // 0 = Red, 1 = Green, 2 = Blue

// Variable to store the last stable potentiometer value
int lastIntensity = 0;

void setup() {
  Serial.begin(115200);
  
  // Initialize Bluetooth Classic with the official device name
  SerialBT.begin("ESP32_RGB_Ctrl");
  
  // Configure physical buttons with internal Pull-Up resistors
  pinMode(BTN_RED, INPUT_PULLUP);
  pinMode(BTN_GREEN, INPUT_PULLUP);
  pinMode(BTN_BLUE, INPUT_PULLUP);
  
  // analogRead: Configures the pin to read variable analog voltages (ADC 0-4095)
  pinMode(PIN_POT, INPUT); 

  // Configure RGB LED pins as outputs
  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_BLUE, OUTPUT);
  
  // Read initial potentiometer position to baseline the system
  int initialPot = analogRead(PIN_POT);
  lastIntensity = map(initialPot, 0, 4095, 0, 255);
}

void loop() {
  /* digitalRead: Reads the current binary voltage state of a digital pin. */
  if (digitalRead(BTN_RED) == LOW) activeColor = 0;
  if (digitalRead(BTN_GREEN) == LOW) activeColor = 1;
  if (digitalRead(BTN_BLUE) == LOW) activeColor = 2;

  /* analogRead: Translates an incoming analog voltage (0V to 3.3V) into a digital number (0-4095). */
  int potValue = analogRead(PIN_POT);
  
  /* map: Linearly scales an integer value from a source range to a target range (0-255). */
  int currentIntensity = map(potValue, 0, 4095, 0, 255);

  /* ANTILOOP LOGIC: Check if the potentiometer is actually moving. */
  if (abs(currentIntensity - lastIntensity) > 2) {
    if (activeColor == 0) redVal = currentIntensity;
    if (activeColor == 1) greenVal = currentIntensity;
    if (activeColor == 2) blueVal = currentIntensity;
    lastIntensity = currentIntensity;
  }

  // Check for incoming Bluetooth data stream from the smartphone app
  if (SerialBT.available()) {
    char cmd = SerialBT.read();
    
    // Pattern: App sends a Color command starting with 'C' (e.g., "C255,128,0")
    if (cmd == 'C') {
      redVal = SerialBT.parseInt();
      greenVal = SerialBT.parseInt();
      blueVal = SerialBT.parseInt();
      
      // Update lastIntensity to match the new incoming values, preventing
      // the physical potentiometer from overriding the app instantly.
      int forcedPot = analogRead(PIN_POT);
      lastIntensity = map(forcedPot, 0, 4095, 0, 255);
    }
  }

  /* analogWrite: Generates a hardware PWM wave directly on the specified pin. */
  analogWrite(PIN_RED, redVal);
  analogWrite(PIN_GREEN, greenVal);
  analogWrite(PIN_BLUE, blueVal);

  delay(30);
}