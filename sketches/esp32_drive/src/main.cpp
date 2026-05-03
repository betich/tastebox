#include <Arduino.h>

// ESP32 IBT-2 drive — serial direction control, constant speed
// R_PWM → GPIO16,  L_PWM → GPIO17
// ENA/ENB tied HIGH (always enabled)
//
// Serial commands (115200, newline-terminated or single char):
//   F  → forward  (L_PWM active)
//   R  → reverse  (R_PWM active)
//   S  → stop / coast

#define PIN_R_PWM  16
#define PIN_L_PWM  17

#define CH_R       0      // LEDC channel for R_PWM
#define CH_L       1      // LEDC channel for L_PWM
#define PWM_FREQ   5000   // Hz
#define PWM_RES    8      // bits (0–255)
#define PWM_SPEED  200    // constant duty; tune to desired current

#define DEAD_TIME_MS 5    // zero active side, wait, then ramp other side

// ── Direction state ────────────────────────────────────────
enum Dir { STOPPED, FORWARD, REVERSE };
Dir current_dir = STOPPED;

// ── Drive helpers ──────────────────────────────────────────

void driveStop() {
  ledcWrite(CH_R, 0);
  ledcWrite(CH_L, 0);
  current_dir = STOPPED;
  Serial.println("[drive] stop");
}

void driveForward() {
  if (current_dir == FORWARD) return;
  ledcWrite(CH_R, 0);
  delay(DEAD_TIME_MS);
  ledcWrite(CH_L, PWM_SPEED);
  current_dir = FORWARD;
  Serial.println("[drive] forward");
}

void driveReverse() {
  if (current_dir == REVERSE) return;
  ledcWrite(CH_L, 0);
  delay(DEAD_TIME_MS);
  ledcWrite(CH_R, PWM_SPEED);
  current_dir = REVERSE;
  Serial.println("[drive] reverse");
}

// ── Serial handler ─────────────────────────────────────────

void handleSerial() {
  static char buf[16];
  static uint8_t idx = 0;

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r' || c == ' ') {
      if (idx > 0) {
        buf[idx] = '\0';
        idx = 0;
        char cmd = toupper(buf[0]);
        if      (cmd == 'F') driveForward();
        else if (cmd == 'R') driveReverse();
        else if (cmd == 'S') driveStop();
        else Serial.println("? F=forward  R=reverse  S=stop");
      }
    } else if (idx < sizeof(buf) - 1) {
      buf[idx++] = c;
    }
  }
}

// ── Setup / Loop ───────────────────────────────────────────

void setup() {
  Serial.begin(115200);

  ledcSetup(CH_R, PWM_FREQ, PWM_RES);
  ledcSetup(CH_L, PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_R_PWM, CH_R);
  ledcAttachPin(PIN_L_PWM, CH_L);
  ledcWrite(CH_R, 0);
  ledcWrite(CH_L, 0);

  Serial.println("[drive] ready — F=forward  R=reverse  S=stop");
}

void loop() {
  handleSerial();
}
