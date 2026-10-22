// Step 2: Hybrid Control Configuration (Local Button + Wireless Bluetooth)
// Target Pins: LED on GPIO 4, Push-Button on GPIO 5

#include "BluetoothSerial.h"

// Check if Bluetooth is properly enabled in the ESP32 core architecture
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run 'make menuconfig' to enable it
#endif

BluetoothSerial SerialBT; // Create the Bluetooth Serial communication object

const int pinLED = 4;       // LED connected to GPIO 4
const int pinButton = 5;    // Push-button connected to GPIO 5

// Variable to store the wireless command state received from the mobile app
bool appCommandState = false;

void setup() {
  // Initialize serial communication at 115200 baud for local debugging
  Serial.begin(115200);
  while (!Serial) {
    ; // Wait for serial port to connect
  }
  
  Serial.println("--- ESP32 HYBRID BLUETOOTH SYSTEM START ---");
  
  // Configure local hardware pins
  pinMode(pinLED, OUTPUT);
  pinMode(pinButton, INPUT_PULLUP);
  
  // Initialize Bluetooth and set the local broadcast name for smartphone pairing
  SerialBT.begin("ESP32_HelloWorld");
  
  Serial.println("Bluetooth Device Ready! Pair your phone with: 'ESP32_MIT_Project'");
}

void loop() {
  // 1. READ LOCAL HARDWARE STATUS
  // Check if the physical button on the breadboard is currently pressed (LOW)
  bool isPhysicalButtonPressed = (digitalRead(pinButton) == LOW);

  // 2. READ INCOMING BLUETOOTH DATA (INTERFACING LAYER)
  // Check if the smartphone app has transmitted any characters over the air
  if (SerialBT.available()) {
    char receivedChar = SerialBT.read(); // Read the single incoming byte
    
    Serial.print("Bluetooth Received: ");
    Serial.println(receivedChar);
    
    // Process the wireless character command
    if (receivedChar == '1') {
      appCommandState = true;  // App sent '1' -> request to latch the LED ON
      Serial.println("-> Wireless Command: LED ON Request");
    } 
    else if (receivedChar == '0') {
      appCommandState = false; // App sent '0' -> request to latch the LED OFF
      Serial.println("-> Wireless Command: LED OFF Request");
    }
  }

  // 3. HYBRID CONTROL LOGIC OVERLAY
  // The LED turns ON if the physical button IS pressed OR if the App latched it ON
  if (isPhysicalButtonPressed || appCommandState) {
    digitalWrite(pinLED, HIGH);
    
    // Debug print only if the physical button is overriding the state
    if (isPhysicalButtonPressed) {
      Serial.println("-> Hardware Override: Physical Button is currently pressed!");
    }
  } else {
    digitalWrite(pinLED, LOW);
  }

  // Small delay to stabilize execution and not stress the CPU loop
  delay(50);
}