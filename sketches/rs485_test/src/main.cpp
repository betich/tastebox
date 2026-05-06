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

  Serial.println("RS-485 slave ready — waiting for PING");
}

static void sendPong() {
  digitalWrite(PIN_DE_RE, HIGH);  // transmit
  rs485.print("PONG\n");
  rs485.flush();
  // One byte at 9600 baud ≈ 1.04 ms; hold DE/RE a bit longer to let the
  // shift register drain before releasing the bus.
  delayMicroseconds(2000);
  digitalWrite(PIN_DE_RE, LOW);   // back to receive
}

void loop() {
  if (rs485.available()) {
    String msg = rs485.readStringUntil('\n');
    msg.trim();
    Serial.print("Received: ");
    Serial.println(msg);

    if (msg == "PING") {
      digitalWrite(LED_BUILTIN, HIGH);
      sendPong();
      Serial.println("Sent: PONG");
      delay(10);
      digitalWrite(LED_BUILTIN, LOW);
    } else {
      Serial.println("Unknown command — ignored");
    }
  }
}
