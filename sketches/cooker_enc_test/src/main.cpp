#include <Arduino.h>

// Cooker encoder test
//
// C (D2) = encoder common — stays LOW, never driven HIGH
// D (D3) = encoder channel — pulse for one direction
// E (D4) = encoder channel — pulse for other direction (confirmed: -30s per pulse)
//
// Commands:
//   d N   pulse D (D3) N times  — try this for +30s
//   e N   pulse E (D4) N times  — confirmed -30s per pulse
//   h     help

#define PIN_C  2   // common — LOW always
#define PIN_D  3
#define PIN_E  4

#define PULSE_MS  80

void pulse(uint8_t pin, uint16_t times = 1) {
  for (uint16_t i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(PULSE_MS);
    digitalWrite(pin, LOW);
    delay(PULSE_MS);
  }
}

void setup() {
  pinMode(PIN_C, OUTPUT); digitalWrite(PIN_C, LOW);  // common — hold LOW
  pinMode(PIN_D, OUTPUT); digitalWrite(PIN_D, LOW);
  pinMode(PIN_E, OUTPUT); digitalWrite(PIN_E, LOW);

  Serial.begin(115200);
  Serial.println("d N | e N | h");
  Serial.println("C=common(LOW)  D=D3  E=D4(-30s confirmed)");
}

void loop() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  if (line == "h") {
    Serial.println("d N   pulse D (D3) N times");
    Serial.println("e N   pulse E (D4) N times — confirmed -30s");
    return;
  }

  if (line.startsWith("d ") || line.startsWith("D ")) {
    int n = line.substring(2).toInt();
    pulse(PIN_D, n);
    Serial.print("[D] x"); Serial.println(n);
    return;
  }

  if (line.startsWith("e ") || line.startsWith("E ")) {
    int n = line.substring(2).toInt();
    pulse(PIN_E, n);
    Serial.print("[E] x"); Serial.println(n);
    return;
  }

  // single e with no number
  if (line == "e" || line == "E") {
    pulse(PIN_E);
    Serial.println("[E] x1");
    return;
  }

  Serial.print("? "); Serial.println(line);
}
