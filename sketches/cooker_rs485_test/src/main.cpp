#include <Arduino.h>
#include <SoftwareSerial.h>

// RS-485 pins
#define PIN_RO   2
#define PIN_DI   3
#define PIN_REDE 4

SoftwareSerial rs485(PIN_RO, PIN_DI);

void rs485Send(const char* msg) {
  digitalWrite(PIN_REDE, HIGH);
  rs485.print(msg);
  rs485.flush();
  delayMicroseconds(500);
  digitalWrite(PIN_REDE, LOW);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_REDE, OUTPUT);
  digitalWrite(PIN_REDE, LOW);
  rs485.begin(9600);
  Serial.println("[cooker_rs485_test] RO=D2 DI=D3 DE/RE=D4");
  Serial.println("Type a frame to send, e.g.: @42 R 00");
}

void loop() {
  // RS-485 → USB serial
  while (rs485.available()) {
    char c = (char)rs485.read();
    Serial.write(c);
  }

  // USB serial → RS-485
  static char buf[64];
  static uint8_t bufLen = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      if (bufLen > 0 && buf[bufLen - 1] == '\r') bufLen--;
      buf[bufLen++] = '\n';
      buf[bufLen]   = '\0';
      rs485Send(buf);
      bufLen = 0;
    } else if (bufLen < 63) {
      buf[bufLen++] = c;
    }
  }
}
