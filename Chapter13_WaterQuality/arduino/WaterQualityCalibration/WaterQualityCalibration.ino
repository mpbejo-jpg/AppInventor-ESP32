/*
  Chapter 13 - Water Quality: Calibration sketch (interactive)

  Standalone sketch, no Wi-Fi and no ThingSpeak. It walks you through
  calibrating TDS and turbidity step by step: you dip the probe in a
  reference sample and send a single-letter command from the Serial
  Monitor, the sketch captures the reading and does all the math -
  no manual calculation needed. At the end of each pair of readings it
  prints the finished constant values, ready to copy-paste as-is into
  WaterQuality.ino (same constant names are used in both sketches).

  Note: this chapter originally also included a PH-4502C pH sensor, but
  that module was dropped after its analog output turned out to be
  stuck at full scale regardless of the pH of the sample (likely a
  faulty board on that specific unit), so pH calibration is not part of
  this sketch.

  How to use it:
    1. Upload this sketch, open the Serial Monitor at 115200 baud, set
       the line ending dropdown to "Newline".
    2. Follow the printed menu: for each step, dip the probe in the
       reference sample, wait ~30-60s for the raw voltage (printed every
       2s) to settle, then type the indicated letter and press Enter.
    3. Once both readings of a pair are captured, the sketch prints the
       ready-to-copy constant lines for that sensor.
    4. Copy those lines into WaterQuality.ino, replacing the placeholder
       constants there.

  Reference samples used in this chapter (homemade, approximate - not
  certified lab buffers, see the calibration section of the chapter):
    TDS:       distilled water + a known amount of table salt (NaCl)
    Turbidity: clear water (reference "zero")
               a turbid sample (e.g. water with a bit of flour stirred in)

  Hardware (same wiring as WaterQuality.ino):
    TDS Meter  -> AOUT -> GPIO35, powered at 3.3V
    JD23-2     -> OUT  -> GPIO32, powered at 3.3V
    DS18B20    -> DATA -> GPIO4  (OneWire), powered at 3.3V
*/

#include <OneWire.h>
#include <DallasTemperature.h>

// ---------------- Pin configuration (same as WaterQuality.ino) ----------------
#define TDS_PIN             35
#define TURBIDITY_PIN       32
#define ONE_WIRE_PIN        4

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature tempSensor(&oneWire);

const unsigned long PRINT_INTERVAL_MS = 2000;
unsigned long lastPrintMs = 0;

// Captured readings, filled in as the user sends commands. NAN means
// "not captured yet".
float turbidityClearVoltage = NAN;
float turbidityTurbidVoltage = NAN;

float adcToVoltage(int rawAdc) {
  return (rawAdc / 4095.0f) * 3.3f;
}

float readTemperatureC() {
  tempSensor.requestTemperatures();
  float t = tempSensor.getTempCByIndex(0);
  return (t > -100.0f && t < 125.0f) ? t : 25.0f; // fallback if DS18B20 not responding
}

void printMenu() {
  Serial.println();
  Serial.println("=================================================");
  Serial.println(" Water Quality - Calibration sketch");
  Serial.println("=================================================");
  Serial.println("Raw voltages are printed below every 2s so you can");
  Serial.println("watch them settle. When ready, send ONE of these");
  Serial.println("letters (Serial Monitor line ending: Newline):");
  Serial.println();
  Serial.println("  T = capture TDS reading (probe in a known-ppm NaCl sample)");
  Serial.println("  A = capture turbidity reading (probe in CLEAR water)");
  Serial.println("  D = capture turbidity reading (probe in TURBID sample)");
  Serial.println("  M = show this menu again");
  Serial.println("=================================================");
  Serial.println();
}

void printLiveReadings() {
  float tdsVoltage = adcToVoltage(analogRead(TDS_PIN));
  float turbidityVoltage = adcToVoltage(analogRead(TURBIDITY_PIN));
  float tempC = readTemperatureC();

  Serial.print("Temperature: ");
  Serial.print(tempC, 1);
  Serial.print(" C   |  TDS raw: ");
  Serial.print(tdsVoltage, 3);
  Serial.print(" V   |  Turbidity raw: ");
  Serial.print(turbidityVoltage, 3);
  Serial.println(" V");
}

// Blocks until a full line is typed on Serial, returns it parsed as a
// float. Only used right after a 'T' command, to ask for the known ppm
// of the sample just captured.
float promptForFloat(const char *prompt) {
  while (Serial.available()) Serial.read(); // flush leftover newline from the previous command
  Serial.println(prompt);
  while (!Serial.available()) {
    delay(50);
  }
  float value = Serial.parseFloat();
  while (Serial.available()) Serial.read(); // flush the rest of the line
  return value;
}

void maybeComputeTurbidityConstants() {
  if (isnan(turbidityClearVoltage) || isnan(turbidityTurbidVoltage)) return;

  // Arbitrary reference scale (this chapter has no certified NTU sample,
  // so 500 "NTU units" is just a fixed, reproducible reference point for
  // the turbid sample - see the chapter's calibration section).
  const float TURBID_SAMPLE_NTU = 500.0f;
  float ntuPerVolt = TURBID_SAMPLE_NTU / (turbidityClearVoltage - turbidityTurbidVoltage);

  Serial.println();
  Serial.println(">>> Turbidity calibration complete. Copy these two lines into WaterQuality.ino:");
  Serial.print("const float TURBIDITY_CLEAR_VOLTAGE = ");
  Serial.print(turbidityClearVoltage, 4);
  Serial.println("f;");
  Serial.print("const float TURBIDITY_NTU_PER_VOLT = ");
  Serial.print(ntuPerVolt, 4);
  Serial.println("f;");
  Serial.println();
}

void handleCommand(char c) {
  switch (c) {
    case 'T': case 't': {
      float tdsVoltage = adcToVoltage(analogRead(TDS_PIN));
      float tempC = readTemperatureC();
      float compensationCoefficient = 1.0f + 0.02f * (tempC - 25.0f);
      float compensatedVoltage = tdsVoltage / compensationCoefficient;

      Serial.print(">>> Captured TDS voltage: ");
      Serial.print(tdsVoltage, 3);
      Serial.print(" V (compensated: ");
      Serial.print(compensatedVoltage, 3);
      Serial.println(" V)");

      float knownPpm = promptForFloat("Type the known ppm value of this sample (e.g. 500) and press Enter:");
      float tdsPpmPerVolt = knownPpm / compensatedVoltage;

      Serial.println();
      Serial.println(">>> TDS calibration complete. Copy this line into WaterQuality.ino:");
      Serial.print("const float TDS_PPM_PER_VOLT = ");
      Serial.print(tdsPpmPerVolt, 4);
      Serial.println("f;");
      Serial.println();
      break;
    }
    case 'A': case 'a': {
      turbidityClearVoltage = adcToVoltage(analogRead(TURBIDITY_PIN));
      Serial.print(">>> Captured turbidity (clear water) voltage: ");
      Serial.print(turbidityClearVoltage, 3);
      Serial.println(" V");
      maybeComputeTurbidityConstants();
      break;
    }
    case 'D': case 'd': {
      turbidityTurbidVoltage = adcToVoltage(analogRead(TURBIDITY_PIN));
      Serial.print(">>> Captured turbidity (turbid sample) voltage: ");
      Serial.print(turbidityTurbidVoltage, 3);
      Serial.println(" V");
      maybeComputeTurbidityConstants();
      break;
    }
    case 'M': case 'm': {
      printMenu();
      break;
    }
    default:
      break; // ignore stray characters (e.g. the newline itself)
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  tempSensor.begin();

  printMenu();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    handleCommand(c);
  }

  if (millis() - lastPrintMs >= PRINT_INTERVAL_MS) {
    lastPrintMs = millis();
    printLiveReadings();
  }
}
