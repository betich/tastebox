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

// ── I2C state ────────────────────────────────────────────────

uint8_t          selectedReg   = 0x00;
int16_t          currentPos    = 0;
volatile int16_t targetPos     = 0;
volatile bool    pendingClick  = false;
volatile bool    pendingBeep   = false;
volatile uint8_t eventFlags    = 0;

void onReceive(int numBytes) {
  if (numBytes < 1) return;
  selectedReg = Wire.read();

  if (numBytes > 1) {
    uint8_t val = Wire.read();
    if (selectedReg == REG_CMD) {
      pendingBeep = true;
      if (val == 0x01) { targetPos = 0; currentPos = 0; }  // reset
      else if (val == 0x04) pendingClick = true;
    } else if (selectedReg == REG_SET_POS && numBytes >= 3) {
      pendingBeep = true;
      uint8_t lo = Wire.read();
      targetPos = (int16_t)((val << 8) | lo);
    }
  }
}

void onRequest() {
  switch (selectedReg) {
    case REG_POS_HI: Wire.write((uint8_t)(currentPos >> 8));    break;
    case REG_POS_LO: Wire.write((uint8_t)(currentPos & 0xFF)); break;
    case REG_SW:     Wire.write(isCooktopOn() ? 1 : 0);        break;
    case REG_EVT: {
      uint8_t f = eventFlags;
      eventFlags = 0;
      Wire.write(f);
      break;
    }
    default: Wire.write(0xFF);
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
  Serial.println("[SYSTEM] I2C slave ready at 0x42");
}

void loop() {
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
