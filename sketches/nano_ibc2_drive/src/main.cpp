#include <Arduino.h>

// Arduino Nano IBD_2/IBC_2 DC motor drive — serial direction control
// L_PWM → D9,  R_PWM → D10
// ENA/ENB tied HIGH (always enabled)
//
// Serial commands (9600, newline-terminated or single char):
//   F  → forward  (L_PWM active)
//   R  → reverse  (R_PWM active)
//   S  → stop / coast

#define PIN_L_PWM  9
#define PIN_R_PWM  10

#define PWM_SPEED  200    // 0–255; tune to desired speed
#define DEAD_TIME_MS 5

enum Dir { STOPPED, FORWARD, REVERSE };
Dir current_dir = STOPPED;

void driveStop() {
  analogWrite(PIN_L_PWM, 0);
  analogWrite(PIN_R_PWM, 0);
  current_dir = STOPPED;
  Serial.println("[drive] stop");
}

void driveForward() {
  if (current_dir == FORWARD) return;
  analogWrite(PIN_R_PWM, 0);
  delay(DEAD_TIME_MS);
  analogWrite(PIN_L_PWM, PWM_SPEED);
  current_dir = FORWARD;
  Serial.println("[drive] forward");
}

void driveReverse() {
  if (current_dir == REVERSE) return;
  analogWrite(PIN_L_PWM, 0);
  delay(DEAD_TIME_MS);
  analogWrite(PIN_R_PWM, PWM_SPEED);
  current_dir = REVERSE;
  Serial.println("[drive] reverse");
}

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

// void setup() {
//   pinMode(PIN_L_PWM, OUTPUT);
//   pinMode(PIN_R_PWM, OUTPUT);
//   analogWrite(PIN_L_PWM, 0);
//   analogWrite(PIN_R_PWM, 0);

//   Serial.begin(9600);
//   Serial.println("[drive] ready — F=forward  R=reverse  S=stop");
// }

void setup() {
  pinMode(PIN_L_PWM, OUTPUT);
  pinMode(PIN_R_PWM, OUTPUT);
  Serial.begin(9600);
  Serial.println("[drive] ready");
  
  // Force forward immediately
  // analogWrite(PIN_R_PWM, 0);
  // delay(5);
  // analogWrite(PIN_L_PWM, 200);
}


void loop() {
  analogWrite(PIN_R_PWM, 0);
  delay(50);
  analogWrite(PIN_L_PWM, 100);
  // handleSerial();
}
