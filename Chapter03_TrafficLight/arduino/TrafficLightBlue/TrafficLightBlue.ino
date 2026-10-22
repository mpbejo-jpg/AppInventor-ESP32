// Chapter 3 - Part 2: Hybrid Traffic Light System (Local Button + Wireless Bluetooth)
// Target Pins: Red LED on GPIO 4, Yellow LED on GPIO 2, Green LED on GPIO 15
// Input Pin: Pedestrian Button on GPIO 5

#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run 'make menuconfig' to enable it
#endif

BluetoothSerial SerialBT;

const int pinRed = 4;
const int pinYellow = 2;
const int pinGreen = 15;
const int pinButton = 5;

// Variable to store if a pedestrian has requested to cross (via button or app)
bool pedestrianCallActive = false;

void setup() {
  // Initialize serial communication for local monitoring
  Serial.begin(115200);
  while (!Serial) {
    ; // Wait for serial port to connect
  }
  
  Serial.println("--- ESP32 HYBRID TRAFFIC LIGHT SYSTEM START ---");
  
  // Configure LED pins as OUTPUT
  pinMode(pinRed, OUTPUT);
  pinMode(pinYellow, OUTPUT);
  pinMode(pinGreen, OUTPUT);
  
  // Configure button pin as INPUT with internal pull-up resistor
  pinMode(pinButton, INPUT_PULLUP);
  
  // Initialize Bluetooth and set the local broadcast name
  SerialBT.begin("ESP32_HelloWorld");
  
  Serial.println("Bluetooth Active! Pair your phone with: 'ESP32_HelloWorld'");
}

void loop() {
  // --- STATE 1: GREEN LIGHT ACTIVE ---
  Serial.println("Status: GREEN Light Active.");
  digitalWrite(pinGreen, HIGH);
  digitalWrite(pinYellow, LOW);
  digitalWrite(pinRed, LOW);
  
  // Scan inputs periodically during the green light duration (4 seconds)
  for (int i = 0; i < 40; i++) {
    checkInputs();
    delay(100); 
  }

  // --- STATE 2: YELLOW LIGHT ACTIVE ---
  Serial.println("Status: YELLOW Light Active.");
  digitalWrite(pinGreen, LOW);
  digitalWrite(pinYellow, HIGH);
  digitalWrite(pinRed, LOW);
  
  // Scan inputs periodically during the yellow light duration (2 seconds)
  for (int i = 0; i < 20; i++) {
    checkInputs();
    delay(100); 
  }

  // --- STATE 3: RED LIGHT ACTIVE ---
  Serial.println("Status: RED Light Active.");
  digitalWrite(pinGreen, LOW);
  digitalWrite(pinYellow, LOW);
  digitalWrite(pinRed, HIGH);
  
  // If a pedestrian called (physically or via Bluetooth), extend the red light window
  if (pedestrianCallActive) {
    Serial.println("-> ALERT: Pedestrian Crossing Safe Window Active! <-");
    delay(5000); // 5 seconds safe crossing time
    pedestrianCallActive = false; // Reset the request after crossing
    Serial.println("-> Pedestrian crossing completed. Resetting call.");
  } else {
    delay(3000); // Standard red light duration
  }
}

// Unified function to scan both the physical button and wireless Bluetooth data
void checkInputs() {
  // 1. Check local physical button
  if (digitalRead(pinButton) == LOW && !pedestrianCallActive) {
    pedestrianCallActive = true;
    Serial.println("-> Input Detected: Physical Button Pressed!");
  }

  // 2. Check wireless incoming Bluetooth commands
  if (SerialBT.available()) {
    char receivedChar = SerialBT.read();
    Serial.print("Bluetooth Received: ");
    Serial.println(receivedChar);
    
    // Character 'P' stands for "Pedestrian Call"
    if (receivedChar == 'P' && !pedestrianCallActive) {
      pedestrianCallActive = true;
      Serial.println("-> Input Detected: Wireless Bluetooth Call Registered!");
    }
  }
}