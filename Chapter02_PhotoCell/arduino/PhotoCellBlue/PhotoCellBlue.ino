/*
  Chapter 2: Ambient Light Monitoring with Bluetooth Classic
  Description: Reads an LDR on GPIO 4 acting as a threshold switch for a LED on GPIO 2.
  It also handles Bluetooth Classic communication: when queried by the app, 
  it returns the current raw LDR reading so the app can manage a pseudo-LED.
*/

#include "BluetoothSerial.h"

// Check if Bluetooth is properly configured in the IDE
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to enable it
#endif

// Object instance for Bluetooth Serial
BluetoothSerial SerialBT;

// Pin configuration according to the Fritzing schematic
const int LDR_PIN = 4;  // LDR connected to GPIO 4 (G4)
const int LED_PIN = 2;  // LED connected to GPIO 2 (G2)

// Threshold value to determine light vs dark locally
const int LIGHT_THRESHOLD = 1500; 

void setup() {
  // Initialize hardware serial communication for debugging
  Serial.begin(115200);
  
  // Initialize Bluetooth Classic with the device name for the book project
  SerialBT.begin("ESP32_Light_Sensor"); 
  Serial.println("Bluetooth device is ready to pair as 'ESP32_Light_Sensor'");
  
  // Configure LED pin as an output and LDR pin as an input
  pinMode(LED_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);
}

void loop() {
  // Read the analog value from the LDR partitor (range: 0 to 4095)
  int ldrValue = analogRead(LDR_PIN);
  
  // Local hardware logic: turn on physical LED if it is dark
  if (ldrValue < LIGHT_THRESHOLD) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
  }
  
  // Check for incoming Bluetooth data from App Inventor
  if (SerialBT.available()) {
    // Read the incoming character command
    char command = SerialBT.read();
    
    // If the app queries the sensor using the '?' character
    if (command == '?') {
      // Send the current raw LDR value back to the app with a newline character
      SerialBT.println(ldrValue);
      
      // Also log the action to the local hardware Serial Monitor for debugging
      Serial.print("Bluetooth query received. Sent LDR value: ");
      Serial.println(ldrValue);
    }
  }
  
  // Small delay to stabilize readings and avoid over-taxing the processor
  delay(50);
}