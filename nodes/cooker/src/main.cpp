#include <Arduino.h>
#include <Wire.h>

#define ENC_E  2   // orange - encoder A
#define ENC_D  3   // blue   - encoder B
#define ENC_C  4   // yellow - click/button
#define BUZ    5   // green  - buzzer state (input)
#define BEEP   8   // buzzer output

#define I2C_ADDRESS 0x42
#define STEP_DELAY  5  // ms between quadrature steps

// I2C registers
#define REG_POS_HI  0x00
#define REG_POS_LO  0x01
#define REG_SW      0x02  // cooktop on/off (BUZ pin)
#define REG_EVT     0x03  // bit0=CW, bit1=CCW, bit2=CLICK (clears on read)
#define REG_CMD     0x10  // 0x01=reset, 0x04=click
#define REG_SET_POS 0x11  // int16 target position

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
  digitalWrite(BEEP, LOW);
  delay(50);
  digitalWrite(BEEP, HIGH);
}

bool isCooktopOn() {
  return digitalRead(BUZ) == HIGH;
}

// ── State ────────────────────────────────────────────────────

uint8_t          selectedReg   = 0x00;
int16_t          currentPos    = 0;
volatile int16_t targetPos     = 0;
volatile bool    pendingClick  = false;
volatile bool    pendingBeep   = false;
volatile uint8_t eventFlags    = 0;

// ── Register access (shared by I2C and serial) ───────────────

uint8_t processRead(uint8_t reg) {
  switch (reg) {
    case REG_POS_HI: return (uint8_t)(currentPos >> 8);
    case REG_POS_LO: return (uint8_t)(currentPos & 0xFF);
    case REG_SW:     return isCooktopOn() ? 1 : 0;
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

// ── I2C callbacks ────────────────────────────────────────────

void onReceive(int numBytes) {
  if (numBytes < 1) return;
  selectedReg = Wire.read();
  if (numBytes > 1) {
    uint8_t data[8];
    uint8_t len = 0;
    while (Wire.available() && len < (uint8_t)sizeof(data)) data[len++] = Wire.read();
    processWrite(selectedReg, data, len);
  }
}

void onRequest() {
  Wire.write(processRead(selectedReg));
}

// ── Serial handler ───────────────────────────────────────────
// Protocol: "R HH\n" → "!HH\n"  |  "W HH DD...\n" → "!OK\n"

void handleSerial() {
  static char buf[48];
  static uint8_t idx = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (idx > 0) {
        buf[idx] = '\0';
        idx = 0;
        if (buf[0] == 'R' && buf[1] == ' ') {
          uint8_t reg = (uint8_t)strtoul(buf + 2, nullptr, 16);
          uint8_t val = processRead(reg);
          Serial.print('!');
          if (val < 0x10) Serial.print('0');
          Serial.println(val, HEX);
        } else if (buf[0] == 'W' && buf[1] == ' ') {
          char* p = buf + 2;
          uint8_t reg = (uint8_t)strtoul(p, &p, 16);
          uint8_t data[8];
          uint8_t len = 0;
          while (*p && len < (uint8_t)sizeof(data)) {
            while (*p == ' ') p++;
            if (*p) data[len++] = (uint8_t)strtoul(p, &p, 16);
          }
          processWrite(reg, data, len);
          Serial.println("!OK");
        }
      }
    } else if (idx < (uint8_t)sizeof(buf) - 1) {
      buf[idx++] = c;
    }
  }
}

// ── Setup / Loop ─────────────────────────────────────────────

void setup() {
  Serial.begin(115200);

  pinMode(ENC_E, OUTPUT);
  pinMode(ENC_D, OUTPUT);
  pinMode(ENC_C, OUTPUT);
  pinMode(BUZ,   INPUT);
  pinMode(BEEP,  OUTPUT);

  digitalWrite(ENC_E, LOW);
  digitalWrite(ENC_D, LOW);
  digitalWrite(ENC_C, LOW);
  digitalWrite(BEEP,  HIGH);  // idle HIGH (active-low)

  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);

  beep();
  Serial.println("[SYSTEM] ready (I2C 0x42 | serial 115200)");
}

void loop() {
  handleSerial();

  if (pendingBeep) {
    pendingBeep = false;
    beep();
  }

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

  static bool lastBuz = false;
  bool buz = isCooktopOn();
  if (buz != lastBuz) {
    lastBuz = buz;
    Serial.print("[BUZ] "); Serial.println(buz ? "ON" : "OFF");
  }
}
