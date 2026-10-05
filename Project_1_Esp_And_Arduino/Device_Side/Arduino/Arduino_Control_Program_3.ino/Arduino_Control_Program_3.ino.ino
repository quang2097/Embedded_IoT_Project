#include <DHT.h>

// --- Pin Definitions ---
#define LIGHT_PIN A5
#define MOISTURE_1_PIN A4
#define MOISTURE_2_PIN A3

#define PUMP_1_PIN 2
#define PUMP_2_PIN 3

#define STEPPER_A0 4
#define STEPPER_A1 5

#define HEAT_SENSOR_PIN 8
#define DHTTYPE DHT11 // Change to DHT22 if necessary

// --- Thresholds ---
const int MOISTURE_THRESHOLD = 200; // Adjust based on your divider
const int LIGHT_THRESHOLD = 1000;    // Water when light is below this (evening/night)
const float TEMP_THRESHOLD = 33.0;  // Celsius 

int openRoof = 0;

DHT dht(HEAT_SENSOR_PIN, DHTTYPE);

// --- State Tracking ---
unsigned long lastSensorCheck = 0;
const unsigned long CHECK_INTERVAL = 10000; // Check every 10 seconds

void setup() {
  // TX/RX to ESP32 via TXS0108E
  Serial.begin(115200);
  
  pinMode(LIGHT_PIN, INPUT);
  pinMode(MOISTURE_1_PIN, INPUT);
  pinMode(MOISTURE_2_PIN, INPUT);
  
  pinMode(PUMP_1_PIN, OUTPUT);
  pinMode(PUMP_2_PIN, OUTPUT);
  pinMode(STEPPER_A0, OUTPUT);
  pinMode(STEPPER_A1, OUTPUT);
  
  digitalWrite(PUMP_1_PIN, LOW);
  digitalWrite(PUMP_2_PIN, LOW);
  
  dht.begin();
}

void loop() {
  // 1. Check for incoming serial commands from ESP32
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    handleESP32Command(command);
  }

  // 2. Periodic automated sensor checks
  if (millis() - lastSensorCheck >= CHECK_INTERVAL) {
    lastSensorCheck = millis();
    
    // Read everything ONCE
    int lgt = analogRead(LIGHT_PIN);
    int m1 = analogRead(MOISTURE_1_PIN);
    int m2 = analogRead(MOISTURE_2_PIN);
    float t = dht.readTemperature();
    if (isnan(t)) t = 0.0;
    
    evaluateEnvironmentMoisture(lgt, m1, m2, t);
    evaluateRoof(lgt, t); // Merged light/heat logic (see below)
  }
}

void evaluateRoof(int light, float temp) {
  bool wantsOpen = (light > 450) || (temp > TEMP_THRESHOLD);
  bool wantsClose = (light < 100) && (temp < 30.0);

  if (wantsOpen && openRoof == 0) {
    transmitData("ROOF_OPENING", light, 0, 0, temp);
    stepMotor(-512);
    openRoof = 1; // Update state!
  } 
  else if (wantsClose && openRoof == 1) {
    transmitData("ROOF_CLOSING", light, 0, 0, temp);
    stepMotor(512);+
    openRoof = 0; // Update state!
  }
}

void evaluateEnvironmentMoisture(int light, int moisture1, int moisture2, float temp) {
  if (isnan(temp)) temp = 0.0; // Fallback for failed read

  // Example Logic: If dry, and it's not the heat of the day (or if it's too hot and needs cooling)
  bool needsWater1 = (moisture1 < MOISTURE_THRESHOLD);
  bool needsWater2 = (moisture2 < MOISTURE_THRESHOLD);

  if(needsWater1 || needsWater2){
    transmitData("AUTO_WATERING", light, moisture1, moisture2, temp);
  }

  if (needsWater1) {
    // Send data to ESP32 *before* wateringtransmitData("AUTO_WATERING", light, moisture1, moisture2, temp);
    triggerPump(PUMP_1_PIN, 5000); // Water for 5 seconds
  }

  if(needsWater2){
    triggerPump(PUMP_2_PIN, 5000); // Water for 5 seconds
  }
}

void handleESP32Command(String cmd) {
  if (cmd == "REQ_DATA") {
    // ESP32 explicitly requested a data dump
    transmitData("DATA_LOG", analogRead(LIGHT_PIN), analogRead(MOISTURE_1_PIN), analogRead(MOISTURE_2_PIN), dht.readTemperature());
  } 
  else if (cmd == "FORCE_WATER_1") {
    transmitData("MANUAL_WATER_1", analogRead(LIGHT_PIN), analogRead(MOISTURE_1_PIN), analogRead(MOISTURE_2_PIN), dht.readTemperature());
    triggerPump(PUMP_1_PIN, 5000);
  }
  else if (cmd == "FORCE_WATER_2") {
    transmitData("MANUAL_WATER_2", analogRead(LIGHT_PIN), analogRead(MOISTURE_1_PIN), analogRead(MOISTURE_2_PIN), dht.readTemperature());
    triggerPump(PUMP_2_PIN, 5000);
  }
  else if (cmd == "STEP_MOTOR") {
    stepMotor(100); // Example: step 100 times
  }
}

void transmitData(String event, int light, int m1, int m2, float temp) {
  // Formats data as CSV: EVENT,LIGHT,M1,M2,TEMP
  Serial.print(event);
  Serial.print(",");
  Serial.print(light);
  Serial.print(",");
  Serial.print(m1);
  Serial.print(",");
  Serial.print(m2);
  Serial.print(",");
  Serial.println(temp);
}

void triggerPump(int pumpPin, unsigned long duration) {
  digitalWrite(pumpPin, HIGH);
  
  // Simple blocking delay for the pump. 
  // For a fully asynchronous system, replace this with a millis() timer.
  delay(duration); 
  
  digitalWrite(pumpPin, LOW);
}

// Drives the 74HC238E by sequencing A0 and A1 to hit Y0, Y1, Y2, Y3 sequentially
void stepMotor(int steps) {
  int absSteps = abs(steps);
  int dir = (steps > 0) ? 1 : -1;
  static int currentPhase = 0; // Remembers position between calls
  
  for (int i = 0; i < absSteps; i++) {
    currentPhase += dir;
    if (currentPhase > 3) currentPhase = 0;
    if (currentPhase < 0) currentPhase = 3;
    
    digitalWrite(STEPPER_A0, bitRead(currentPhase, 0));
    digitalWrite(STEPPER_A1, bitRead(currentPhase, 1));
    delay(10);
  }
}