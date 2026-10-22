/*
  Chapter 4: RGB LED Control with Potentiometer (Local Setup)
  Description: Selects color channel via buttons, adjusts intensity via potentiometer.
*/

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

void setup() {
  Serial.begin(115200);
  
  // Configure physical buttons with internal Pull-Up resistors
  pinMode(BTN_RED, INPUT_PULLUP);
  pinMode(BTN_GREEN, INPUT_PULLUP);
  pinMode(BTN_BLUE, INPUT_PULLUP);
  
  // analogRead: Configures the pin to read variable analog voltages (ADC 0-4095)
  pinMode(PIN_POT, INPUT); 

  /* ledcAttachChannel: Configures the hardware PWM properties for an ESP32 pin.
     Parameters: (Pin, Frequency in Hz, Resolution in bits, Hardware channel ID)
     We use 5000Hz frequency to prevent flickering, and 8-bit resolution (0-255 steps). */
  ledcAttachChannel(PIN_RED, 5000, 8, 0);
  ledcAttachChannel(PIN_GREEN, 5000, 8, 1);
  ledcAttachChannel(PIN_BLUE, 5000, 8, 2);
}

void loop() {
  /* digitalRead: Reads the current binary voltage state of a digital pin.
     Returns LOW (0V) if the button is physically pressed down to ground. */
  if (digitalRead(BTN_RED) == LOW) activeColor = 0;
  if (digitalRead(BTN_GREEN) == LOW) activeColor = 1;
  if (digitalRead(BTN_BLUE) == LOW) activeColor = 2;

  /* analogRead: Translates an incoming analog voltage (0V to 3.3V) into a digital 
     number representation ranging from 0 (GND) to 4095 (3.3V). */
  int potValue = analogRead(PIN_POT);
  
  /* map: Linearly scales an integer value from a source range to a target range.
     Here, it converts the 12-bit ADC range (0-4095) to an 8-bit PWM duty cycle range (0-255). */
  int intensity = map(potValue, 0, 4095, 0, 255);

  // Apply potentiometer changes to the currently active selection channel
  if (activeColor == 0) redVal = intensity;
  if (activeColor == 1) greenVal = intensity;
  if (activeColor == 2) blueVal = intensity;

  /* ledcWrite: Commands the specific hardware pin to output a precise PWM signal.
     The second parameter (0 to 255) defines the duty cycle (on/off ratio) to control brightness. */
  ledcWrite(PIN_RED, redVal);
  ledcWrite(PIN_GREEN, greenVal);
  ledcWrite(PIN_BLUE, blueVal);

  // Short processing delay to ensure loop stability and debounce buttons
  delay(30);
}