#include <Arduino.h>

// ── Pins ───────────────────────────────────────────────────
#define STEP_PIN 2
#define DIR_PIN  3
#define EN_PIN   4

// ── Motor: 17HS4401 (NEMA 17, 1.8°/step = 200 steps/rev) ──
// Set MICROSTEP_DIV to match your driver's MS switch setting:
//   1  → full step   (200 steps/rev)
//   2  → half step   (400 steps/rev)
//   8  → 1/8  step   (1600 steps/rev)   ← A4988 default when MS pins float
//   16 → 1/16 step   (3200 steps/rev)
//   32 → 1/32 step   (6400 steps/rev)   ← DRV8825 max
#define MICROSTEP_DIV 1

const int stepsPerRev = 200 * MICROSTEP_DIV;
const int stepDelay   = 800;  // µs per half-pulse — lower = faster

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

  digitalWrite(EN_PIN, LOW);  // active-low: LOW = enabled

  Serial.begin(9600);
  Serial.print("17HS4401 test — ");
  Serial.print(stepsPerRev);
  Serial.println(" steps/rev");
}

void loop() {
  digitalWrite(DIR_PIN, HIGH);
  Serial.println("CW  (1 rev)");
  stepMotor(stepsPerRev);
  delay(1000);

  digitalWrite(DIR_PIN, LOW);
  Serial.println("CCW (1 rev)");
  stepMotor(stepsPerRev);
  delay(1000);
}
