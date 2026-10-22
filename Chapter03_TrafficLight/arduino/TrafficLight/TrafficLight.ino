/*
  Chapter 3 - Traffic Light (Part 1: local)
  Traffic light cycle (green 4 s, yellow 2 s, red 3 s). Pressing the
  pedestrian button extends the next red light to 5 s.
  Board:    ESP32 Dev Module
  Hardware: red LED GPIO 4, yellow LED GPIO 2, green LED GPIO 15,
            pedestrian button GPIO 5
*/
const int pinRed = 4;
const int pinYellow = 2;
const int pinGreen = 15;
const int pinButton = 5;

// Variable to store if a pedestrian has requested to cross
bool pedestrianCallActive = false;

void setup() {
  Serial.begin(115200);

  Serial.println("--- ESP32 TRAFFIC LIGHT SYSTEM START ---");

  // Configure LED pins as OUTPUT
  pinMode(pinRed, OUTPUT);
  pinMode(pinYellow, OUTPUT);
  pinMode(pinGreen, OUTPUT);

  // Configure button pin as INPUT with internal pull-up resistor
  pinMode(pinButton, INPUT_PULLUP);
}

void loop() {
  // --- STATE 1: GREEN LIGHT ACTIVE ---
  Serial.println("Status: GREEN Light Active.");
  digitalWrite(pinGreen, HIGH);
  digitalWrite(pinYellow, LOW);
  digitalWrite(pinRed, LOW);

  // Scan the button periodically during the green light duration (4 seconds)
  for (int i = 0; i < 40; i++) {
    checkButton();
    delay(100);
  }

  // --- STATE 2: YELLOW LIGHT ACTIVE ---
  Serial.println("Status: YELLOW Light Active.");
  digitalWrite(pinGreen, LOW);
  digitalWrite(pinYellow, HIGH);
  digitalWrite(pinRed, LOW);

  // Scan the button periodically during the yellow light duration (2 seconds)
  for (int i = 0; i < 20; i++) {
    checkButton();
    delay(100);
  }

  // --- STATE 3: RED LIGHT ACTIVE ---
  Serial.println("Status: RED Light Active.");
  digitalWrite(pinGreen, LOW);
  digitalWrite(pinYellow, LOW);
  digitalWrite(pinRed, HIGH);

  // If a pedestrian called, extend the red light window
  if (pedestrianCallActive) {
    Serial.println("-> ALERT: Pedestrian Crossing Safe Window Active! <-");
    delay(5000); // 5 seconds safe crossing time
    pedestrianCallActive = false; // Reset the request after crossing
    Serial.println("-> Pedestrian crossing completed. Resetting call.");
  } else {
    delay(3000); // Standard red light duration
  }
}

// Checks the physical pedestrian button
void checkButton() {
  if (digitalRead(pinButton) == LOW && !pedestrianCallActive) {
    pedestrianCallActive = true;
    Serial.println("-> Input Detected: Physical Button Pressed!");
  }
}
