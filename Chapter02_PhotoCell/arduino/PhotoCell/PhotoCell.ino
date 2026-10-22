/*
  Chapter 2: Ambient Light Monitoring
  Description: Reads an LDR (photoresistor) acting as a digital threshold switch.
  Turns on the LED when it's dark and turns it off when there is enough light.
*/

// Pin configuration according to the Fritzing schematic
const int LDR_PIN = 4;  // LDR connected to GPIO 4 (G4)
const int LED_PIN = 2;  // LED connected to GPIO 2 (G2)

// Threshold value to determine light vs dark
const int LIGHT_THRESHOLD = 1500; 

void setup() {
  // Initialize serial communication for debugging at 115200 bps
  Serial.begin(115200);
  
  // Configure LED pin as an output
  pinMode(LED_PIN, OUTPUT);
  
  // ADC pins on ESP32 are configured as inputs by default, 
  // but explicitly setting it keeps the code clear for readers
  pinMode(LDR_PIN, INPUT);
}

void loop() {
  // Read the analog value from the LDR partitor (range: 0 to 4095)
  int ldrValue = analogRead(LDR_PIN);
  
  // Print the raw value to the Serial Monitor/Plotter
  Serial.print("LDR Raw Value: ");
  Serial.println(ldrValue);
  
  // Check if the light level drops below the threshold
  if (ldrValue < LIGHT_THRESHOLD) {
    // It is dark: turn on the LED
    digitalWrite(LED_PIN, HIGH);
    
    // Send status to App Inventor via Bluetooth (to be implemented in the BLE section)
    // Serial.println("LED:ON"); 
  } else {
    // It is bright: turn off the LED
    digitalWrite(LED_PIN, LOW);
    
    // Send status to App Inventor via Bluetooth (to be implemented in the BLE section)
    // Serial.println("LED:OFF");
  }
  
  // Small delay to stabilize readings and avoid flooding the serial port
  delay(100);
}
