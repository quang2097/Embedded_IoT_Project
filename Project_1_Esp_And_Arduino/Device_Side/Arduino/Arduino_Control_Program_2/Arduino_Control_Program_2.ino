#include <Stepper.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>

// Set up a secondary serial port to talk to the ESP32
// Arduino RX = Pin 10, TX = Pin 11
SoftwareSerial espSerial(10, 11);

// Sensor Pins
const int lightPin = A0;
const int moisture1Pin = A1;
const int moisture2Pin = A2;
const int temp1Pin = A3; 
const int temp2Pin = A4;

// Actuator Pins
const int pump1Pin = 2;
const int pump2Pin = 8;

// Stepper Motor Configuration
const int stepsPerRevolution = 2048;
// Initializing on pins 4, 6, 5, 7 to account for ULN2003 wiring sequence
Stepper myStepper(stepsPerRevolution, 4, 6, 5, 7); 

void setup() {
  // Initialize hardware serial (PC debugging) and software serial (ESP32)
  Serial.begin(9600);
  espSerial.begin(9600);

  // Configure Pump Pins
  pinMode(pump1Pin, OUTPUT);
  pinMode(pump2Pin, OUTPUT);
  
  // Ensure pumps start in the OFF position
  digitalWrite(pump1Pin, LOW);
  digitalWrite(pump2Pin, LOW);
  
  // Set stepper speed
  myStepper.setSpeed(10);
}

void loop() {
  // 1. Read all sensors
  int lightVal = analogRead(lightPin);
  int moist1Val = analogRead(moisture1Pin);
  int moist2Val = analogRead(moisture2Pin);
  int temp1Raw = analogRead(temp1Pin);
  int temp2Raw = analogRead(temp2Pin);

  // 2. Average the redundant sensors
  int avgMoisture = (moist1Val + moist2Val) / 2;
  int avgTempRaw = (temp1Raw + temp2Raw) / 2;

  // Optional: Convert raw analog temp reading to Celsius
  // (Assuming standard LM35 or TMP36 analog sensors. Adjust math as needed.)
  float avgTempC = (avgTempRaw * 5.0 * 100.0) / 1024.0; 

  // 3. Construct the JSON payload
  // Allocating 200 bytes for the document is sufficient for this structure
  StaticJsonDocument<200> doc;
  
  doc["light"] = lightVal;
  doc["moisture"] = avgMoisture;
  doc["temperature"] = avgTempC;
  doc["date"] = "TIMESTAMP_INSERTED_BY_ESP32";
  doc["time"] = "TIMESTAMP_INSERTED_BY_ESP32";

  // 4. Send the JSON to the ESP32 over SoftwareSerial
  serializeJson(doc, espSerial);
  espSerial.println(); // Send a newline character to terminate the message
  
  // Mirror the output to the PC Serial monitor for your own debugging
  serializeJson(doc, Serial);
  Serial.println();

  // Wait 5 seconds before compiling and sending the next reading
  delay(5000); 
}