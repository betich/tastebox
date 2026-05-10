#include <Arduino.h>
#include <RS485Node.h>

// Uno acting as cooker node 0x42
// RS-485 pins
#define PIN_RO   12
#define PIN_DI   13
#define PIN_REDE 2

#define NODE_ADDR 0x42

RS485Node node(NODE_ADDR, PIN_RO, PIN_DI, PIN_REDE);

int16_t  currentPos = 0;
int16_t  targetPos  = 0;
uint8_t  eventFlags = 0;
uint32_t frameCount = 0;

uint8_t processRead(uint8_t reg) {
  uint8_t val;
  switch (reg) {
    case 0x00: val = (uint8_t)(currentPos >> 8);   break;
    case 0x01: val = (uint8_t)(currentPos & 0xFF); break;
    case 0x02: val = 0;                            break;
    case 0x03: val = eventFlags; eventFlags = 0;   break;
    default:   val = 0xFF;                         break;
  }
  frameCount++;
  Serial.print("[RX] R reg=0x"); Serial.print(reg, HEX);
  Serial.print(" -> 0x"); Serial.println(val, HEX);
  Serial.print("[TX] @42 "); Serial.println(val, HEX);
  return val;
}

void processWrite(uint8_t reg, uint8_t* data, uint8_t len) {
  frameCount++;
  Serial.print("[RX] W reg=0x"); Serial.print(reg, HEX);
  Serial.print(" data=");
  for (uint8_t i = 0; i < len; i++) { Serial.print("0x"); Serial.print(data[i], HEX); Serial.print(' '); }
  Serial.println();

  if (len < 1) return;
  if (reg == 0x10) {
    if (data[0] == 0x01)      { currentPos = 0; targetPos = 0; Serial.println("[CMD] home"); }
    else if (data[0] == 0x04) { eventFlags |= 0x04; Serial.println("[CMD] click"); }
  } else if (reg == 0x11 && len >= 2) {
    targetPos  = (int16_t)((data[0] << 8) | data[1]);
    currentPos = targetPos;
    eventFlags |= (targetPos >= 0) ? 0x01 : 0x02;
    Serial.print("[CMD] set_pos "); Serial.println(targetPos);
  }
  Serial.println("[TX] @42 OK");
}

void setup() {
  Serial.begin(115200);
  node.begin();
  node.setDefaultReadHandler(processRead);
  node.setDefaultWriteHandler(processWrite);
  Serial.println("[uno_cooker_spoof] 0x42 ready  RO=D12 DI=D13 DE/RE=D2");
}

void loop() {
  node.poll();

  static uint32_t lastHeartbeat = 0;
  if (millis() - lastHeartbeat >= 2000) {
    lastHeartbeat = millis();
    Serial.print("[alive] uptime="); Serial.print(millis() / 1000);
    Serial.print("s  bytes="); Serial.print(node.bytesReceived());
    Serial.print("  frames="); Serial.print(frameCount);
    Serial.print("  pos="); Serial.println(currentPos);
  }
}
