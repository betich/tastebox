#include <Arduino.h>

// skipped: D0/D1 (serial), D13 (built-in LED pulldown), A4/A5 (I2C)
int digitalPins[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
int analogPins[]  = {A0, A1, A2, A3};

void testPin(int pin, const char* label) {
  pinMode(pin, INPUT_PULLUP);
  delay(5);
  int val = digitalRead(pin);
  Serial.print(label);
  Serial.print(": ");
  Serial.print(val);
  if (val == 0) Serial.print("  <-- FAILED/FRIED?");
  Serial.println();
}

void setup() {
  Serial.begin(9600);
  Serial.println("Pin INPUT_PULLUP test — HIGH(1) expected when unconnected");
  Serial.println("Skipped: D0 D1 (serial), D13 (LED), A4 A5 (I2C)");
  Serial.println("---");
}

void loop() {
  for (int i = 0; i < 11; i++) {
    char label[4];
    snprintf(label, sizeof(label), "D%d", digitalPins[i]);
    testPin(digitalPins[i], label);
  }
  for (int i = 0; i < 4; i++) {
    char label[4];
    snprintf(label, sizeof(label), "A%d", i);
    testPin(analogPins[i], label);
  }
  Serial.println("---");
  delay(2000);
}
