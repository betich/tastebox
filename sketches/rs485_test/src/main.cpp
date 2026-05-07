#include <Arduino.h>
#include <SoftwareSerial.h>

#define PIN_RX 4
#define PIN_TX 5
#define PIN_DE_RE 2

SoftwareSerial rs485(PIN_RX, PIN_TX);

void setup() {
  Serial.begin(115200);
  rs485.begin(9600);

  pinMode(PIN_DE_RE, OUTPUT);
  digitalWrite(PIN_DE_RE, LOW);  // receive mode

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  rs485.setTimeout(100);  // don't stall 1s on noise
  Serial.println("RS-485 slave ready — waiting for PING");
}

static void sendMsg(const char* msg) {
  digitalWrite(PIN_DE_RE, HIGH);
  rs485.print(msg);
  delayMicroseconds(500);
  digitalWrite(PIN_DE_RE, LOW);
}

void loop() {
#ifdef BEACON_MODE
  // Continuously transmit so A/B voltages can be measured without needing PING.
  sendMsg("BEACON\n");
  Serial.println("BEACON sent");
  delay(1000);
  return;
#endif

  if (rs485.available()) {
    String msg = rs485.readStringUntil('\n');
    msg.trim();
    Serial.print("Received: ");
    Serial.println(msg);

    if (msg == "PING") {
      digitalWrite(LED_BUILTIN, HIGH);
      sendMsg("PONG\n");
      Serial.println("Sent: PONG");
      delay(10);
      digitalWrite(LED_BUILTIN, LOW);
    } else {
      Serial.println("Unknown command — ignored");
    }
  }
}
