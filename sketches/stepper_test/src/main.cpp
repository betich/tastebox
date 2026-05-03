#include <Arduino.h>

// ── Pins ───────────────────────────────────────────────────
#define STEP_PIN 2
#define DIR_PIN  3
#define EN_PIN   4

// ── Parameters ─────────────────────────────────────────────
const int stepsPerRevolution = 200;  // 1.8°/step motor
const int stepDelay          = 800;  // µs between pulses (controls speed)

// ── Helpers ────────────────────────────────────────────────
void stepMotor(int steps) {
  for (int i = 0; i < steps; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(stepDelay);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(stepDelay);
  }
}

// ── Setup / Loop ───────────────────────────────────────────
void setup() {
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN,  OUTPUT);
  pinMode(EN_PIN,   OUTPUT);

  digitalWrite(EN_PIN, LOW);  // enable driver (active-low)

  Serial.begin(9600);
  Serial.println("Stepper test ready");
}

void loop() {
  digitalWrite(DIR_PIN, HIGH);
  Serial.println("CW");
  stepMotor(stepsPerRevolution);
  delay(1000);

  digitalWrite(DIR_PIN, LOW);
  Serial.println("CCW");
  stepMotor(stepsPerRevolution);
  delay(1000);
}
