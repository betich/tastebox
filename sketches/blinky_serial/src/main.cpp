#include <Arduino.h>

// blinky_serial — LED blink controlled over serial
// Commands (115200 baud, newline-terminated):
//   on          start blinking
//   off         stop blinking (LED off)
//   rate <ms>   set blink interval in milliseconds
//   help        show this list

#define LED_PIN LED_BUILTIN

bool     blinking   = true;
uint32_t interval   = 500;
uint32_t lastToggle = 0;
bool     ledState   = false;

char    buf[32];
uint8_t bufLen = 0;

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  on          start blinking");
  Serial.println("  off         stop (LED off)");
  Serial.println("  rate <ms>   set interval");
  Serial.println("  help        show this");
}

void handleLine(const char* line) {
  if (strcmp(line, "on") == 0) {
    blinking = true;
    Serial.println("blinking on");
  } else if (strcmp(line, "off") == 0) {
    blinking = false;
    digitalWrite(LED_PIN, LOW);
    ledState = false;
    Serial.println("blinking off");
  } else if (strncmp(line, "rate ", 5) == 0) {
    int ms = atoi(line + 5);
    if (ms > 0) {
      interval = (uint32_t)ms;
      Serial.print("interval = "); Serial.print(interval); Serial.println(" ms");
    } else {
      Serial.println("usage: rate <ms>");
    }
  } else if (strcmp(line, "help") == 0) {
    printHelp();
  } else {
    Serial.print("unknown: '"); Serial.print(line); Serial.println("'  (type 'help')");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(12, OUTPUT);
  digitalWrite(12, HIGH);
  printHelp();
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      if (bufLen > 0 && buf[bufLen - 1] == '\r') bufLen--;
      buf[bufLen] = '\0';
      if (bufLen > 0) handleLine(buf);
      bufLen = 0;
    } else if (bufLen < (uint8_t)(sizeof(buf) - 1)) {
      buf[bufLen++] = c;
    }
  }

  if (blinking) {
    uint32_t now = millis();
    if (now - lastToggle >= interval) {
      lastToggle = now;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
  }
}
