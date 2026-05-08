#include <Arduino.h>
#include <RS485Node.h>
#include <SerialFrameHandler.h>

// Encoder spoofer / cooktop I/O
#define ENC_E  2   // orange - encoder A
#define ENC_D  3   // blue   - encoder B
#define ENC_C  9   // yellow - click/button  (moved from D4 to free D9)
#define BUZ    8   // buzzer output

// RS-485 (SoftwareSerial)
#define PIN_RS485_RX    4
#define PIN_RS485_TX    5
#define PIN_RS485_DE_RE 6

#define NODE_ADDR  0x42
#define STEP_DELAY 5  // ms between quadrature steps

// Registers
#define REG_POS_HI  0x00
#define REG_POS_LO  0x01
#define REG_SW      0x02
#define REG_EVT     0x03
#define REG_CMD     0x10
#define REG_SET_POS 0x11

RS485Node          node(NODE_ADDR, PIN_RS485_RX, PIN_RS485_TX, PIN_RS485_DE_RE);
SerialFrameHandler serial_handler(NODE_ADDR);

// ── Encoder spoofer ──────────────────────────────────────────

void stepCW(int steps) {
  int pattern[4][2] = {{0,1},{1,1},{1,0},{0,0}};
  for (int s = 0; s < steps; s++) {
    for (int i = 0; i < 4; i++) {
      digitalWrite(ENC_E, pattern[i][0]);
      digitalWrite(ENC_D, pattern[i][1]);
      delay(STEP_DELAY);
    }
  }
}

void stepCCW(int steps) {
  int pattern[4][2] = {{0,0},{1,0},{1,1},{0,1}};
  for (int s = 0; s < steps; s++) {
    for (int i = 0; i < 4; i++) {
      digitalWrite(ENC_E, pattern[i][0]);
      digitalWrite(ENC_D, pattern[i][1]);
      delay(STEP_DELAY);
    }
  }
}

void click() {
  digitalWrite(ENC_C, HIGH);
  delay(100);
  digitalWrite(ENC_C, LOW);
  delay(100);
}

void beep() {
  digitalWrite(BUZ, LOW);
  delay(50);
  digitalWrite(BUZ, HIGH);
}

// ── State ────────────────────────────────────────────────────

int16_t  currentPos   = 0;
int16_t  targetPos    = 0;
bool     pendingClick = false;
bool     pendingBeep  = false;
uint8_t  eventFlags   = 0;

// ── Register handlers ────────────────────────────────────────

uint8_t processRead(uint8_t reg) {
  switch (reg) {
    case REG_POS_HI: return (uint8_t)(currentPos >> 8);
    case REG_POS_LO: return (uint8_t)(currentPos & 0xFF);
    case REG_SW:     return 0;
    case REG_EVT: {
      uint8_t f = eventFlags;
      eventFlags = 0;
      return f;
    }
    default: return 0xFF;
  }
}

void processWrite(uint8_t reg, uint8_t* data, uint8_t len) {
  if (len < 1) return;
  if (reg == REG_CMD) {
    pendingBeep = true;
    if (data[0] == 0x01) { targetPos = 0; currentPos = 0; }
    else if (data[0] == 0x04) pendingClick = true;
  } else if (reg == REG_SET_POS && len >= 2) {
    pendingBeep = true;
    targetPos = (int16_t)((data[0] << 8) | data[1]);
  }
}

// ── Setup / Loop ─────────────────────────────────────────────

void setup() {
  Serial.begin(115200);

  pinMode(ENC_E, OUTPUT); digitalWrite(ENC_E, LOW);
  pinMode(ENC_D, OUTPUT); digitalWrite(ENC_D, LOW);
  pinMode(ENC_C, OUTPUT); digitalWrite(ENC_C, LOW);
  pinMode(BUZ, OUTPUT); digitalWrite(BUZ, HIGH);  // idle HIGH (active-low)

  node.begin();
  node.setDefaultReadHandler(processRead);
  node.setDefaultWriteHandler(processWrite);

  serial_handler.setDefaultReadHandler(processRead);
  serial_handler.setDefaultWriteHandler(processWrite);

  beep();
  Serial.println("[cooker] RS485 node 0x42 ready");
}

void loop() {
  node.poll();
  serial_handler.poll(Serial);

  if (pendingBeep)  { pendingBeep = false; beep(); }

  if (pendingClick) {
    pendingClick = false;
    click();
    eventFlags |= 0x04;
    Serial.println("[CMD] click");
  }

  if (currentPos < targetPos) {
    int steps = targetPos - currentPos;
    stepCW(steps);
    currentPos = targetPos;
    eventFlags |= 0x01;
    Serial.print("[CMD] CW x"); Serial.print(steps);
    Serial.print(" -> pos="); Serial.println(currentPos);
  } else if (currentPos > targetPos) {
    int steps = currentPos - targetPos;
    stepCCW(steps);
    currentPos = targetPos;
    eventFlags |= 0x02;
    Serial.print("[CMD] CCW x"); Serial.print(steps);
    Serial.print(" -> pos="); Serial.println(currentPos);
  }

}
