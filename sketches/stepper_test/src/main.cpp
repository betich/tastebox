#include <Arduino.h>

// ── Pins ───────────────────────────────────────────────────
// A4988: RESET+SLEEP tied together to 5V (external)
//        ENABLE → LOW (hardwired or controlled here)
//        MS1/MS2/MS3 → GND = full step (1.8°, 200 steps/rev)
#define STEP_PIN   2   // ← change if your wiring differs
#define DIR_PIN    3
#define ENABLE_PIN 4

// ── Motor: 17HS4401 coil wiring ───────────────────────────
// 1A/1B → Red(+) / Green(−)   coil A (~20 Ω)
// 2A/2B → Yellow(+) / Blue(−) coil B (~20 Ω)

// ── Parameters ─────────────────────────────────────────────
// Full step: 200 steps/rev. Increase MICROSTEP_DIV if MS pins are set.
#define MICROSTEP_DIV 1
const int stepsPerRev = 200 * MICROSTEP_DIV;
const int stepDelay   = 800;   // µs per half-pulse — raise to 1500 if jitter persists

void setup() {
  pinMode(STEP_PIN,   OUTPUT);
  pinMode(DIR_PIN,    OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);

  digitalWrite(ENABLE_PIN, LOW);   // active-low: LOW = enabled
  digitalWrite(DIR_PIN,    HIGH);  // set direction before first pulse
  digitalWrite(STEP_PIN,   LOW);

  Serial.begin(9600);
  Serial.print("A4988 + 17HS4401  ");
  Serial.print(stepsPerRev); Serial.println(" steps/rev");
  Serial.println("CW continuous — send any char to reverse");
}

void loop() {
  // Reverse direction on any serial input
  if (Serial.available()) {
    Serial.read();
    digitalWrite(DIR_PIN, !digitalRead(DIR_PIN));
    Serial.println(digitalRead(DIR_PIN) ? "DIR → CW" : "DIR → CCW");
  }

  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(stepDelay);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(stepDelay);
}
