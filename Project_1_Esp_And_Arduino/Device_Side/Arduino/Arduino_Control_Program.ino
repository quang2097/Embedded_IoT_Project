#include <Stepper.h>

// Define the number of steps per full 360-degree rotation
const int stepsPerRevolution = 2048; 

// Initialize the stepper library on pins 8 through 11:
// Note the pin order is 8, 10, 9, 11 to match the ULN2003 sequence
Stepper myStepper(stepsPerRevolution, 8, 10, 9, 11);

void setup() {
  // Set the motor speed (10 to 15 RPM is safe for this specific motor)
  myStepper.setSpeed(15);
  
  // Initialize the serial port for debugging
  Serial.begin(9600);
}

void loop() {
  Serial.println("Rotating 90 degrees clockwise...");
  // 2048 steps = 360 degrees. 2048 / 4 = 512 steps (90 degrees)
  myStepper.step(512);
  
  delay(1000); // Wait one second
  
  Serial.println("Rotating 90 degrees counter-clockwise...");
  // Use a negative number to reverse direction
  myStepper.step(-512); 
  
  delay(1000); // Wait one second
}