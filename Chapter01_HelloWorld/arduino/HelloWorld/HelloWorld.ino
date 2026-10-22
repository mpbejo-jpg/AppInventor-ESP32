// Step 1: Local Control Configuration (Local Button only)
// Target Pins: LED on GPIO 4, Push-Button on GPIO 5

const int pinLED = 4;       // LED connected to GPIO 4
const int pinButton = 5;    // Push-button connected to GPIO 5

void setup() {
  // Initialize serial communication at 115200 baud for local debugging
  Serial.begin(115200);
  while (!Serial) {
    ; // Wait for serial port to connect
  }

  Serial.println("--- ESP32 LOCAL SYSTEM START ---");

  // Configure local hardware pins
  pinMode(pinLED, OUTPUT);
  pinMode(pinButton, INPUT_PULLUP);
}

void loop() {
  // 1. READ LOCAL HARDWARE STATUS
  // Read the physical button: HIGH (1) when released, LOW (0) when pressed
  int buttonState = digitalRead(pinButton);

  Serial.print("Button State: ");
  Serial.println(buttonState);

  // 2. LOCAL CONTROL LOGIC
  // The LED stays ON only while the physical button is pressed
  if (buttonState == LOW) {
    digitalWrite(pinLED, HIGH);
  } else {
    digitalWrite(pinLED, LOW);
  }

  // Small delay to stabilize execution and keep the Serial Monitor readable
  delay(200);
}
