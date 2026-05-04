#include <Arduino.h>
#include <Wire.h>

// Ingredient stepper node — I2C slave at 0x44
//
// Stepper A: PUL→D2,  DIR→D3,  ENA→D4   (DIR inverted vs B)
// Stepper B: PUL→D13, DIR→D12, ENA→D11
//
// Commands (write REG_CMD 0x10):
//   0x01  STOP_ALL
//   0x02  A_FWD_CONT    — A non-stop forward
//   0x03  A_BWD_CONT    — A non-stop backward
//   0x04  A_DISPENSE    — A one revolution forward
//   0x05  A_RETRACT     — A one revolution backward
//   0x06  B_FWD_CONT    — B non-stop forward
//   0x07  B_BWD_CONT    — B non-stop backward
//   0x08  B_DISPENSE    — B one revolution forward
//   0x09  B_RETRACT     — B one revolution backward
//   0x0A  STOP_A
//   0x0B  STOP_B
//
// Registers:
//   R 0x00  status A  (bit0=busy, bit1=bwd)
//   R 0x01  status B  (bit0=busy, bit1=bwd)
//   W 0x10  command
//   W 0x11+0x12  steps per revolution (uint16 hi-lo, default 200)

// ── Pins ───────────────────────────────────────────────────
#define A_PUL  2
#define A_DIR  3
#define A_ENA  4

#define B_PUL  13
#define B_DIR  12
#define B_ENA  11

#define STEP_HALF_US  800   // µs per half-pulse (~625 Hz)

// ── I2C ────────────────────────────────────────────────────
#define I2C_ADDRESS   0x44

#define REG_STATUS_A  0x00
#define REG_STATUS_B  0x01
#define REG_CMD       0x10
#define REG_REV_HI    0x11
#define REG_REV_LO    0x12

#define CMD_STOP_ALL    0x01
#define CMD_A_FWD_CONT  0x02
#define CMD_A_BWD_CONT  0x03
#define CMD_A_DISPENSE  0x04
#define CMD_A_RETRACT   0x05
#define CMD_B_FWD_CONT  0x06
#define CMD_B_BWD_CONT  0x07
#define CMD_B_DISPENSE  0x08
#define CMD_B_RETRACT   0x09
#define CMD_STOP_A      0x0A
#define CMD_STOP_B      0x0B

// ── Motor state ────────────────────────────────────────────
enum RunMode : uint8_t { IDLE, CONTINUOUS, REVOLUTION };

struct Motor {
  RunMode  mode;
  uint16_t steps_left;
  uint8_t  status;      // bit0=busy, bit1=bwd
};

Motor motorA = { IDLE, 0, 0 };
Motor motorB = { IDLE, 0, 0 };

volatile uint8_t  pending_cmd    = 0;
volatile uint16_t set_steps_rev  = 1600;  // 1/8 microstepping — tweak until one vend = one revolution
volatile uint8_t  selected_reg   = 0;

// ── Motor helpers ──────────────────────────────────────────
void enableA()  { digitalWrite(A_ENA, LOW); }
void disableA() { digitalWrite(A_ENA, HIGH); }
void enableB()  { digitalWrite(B_ENA, LOW); }
void disableB() { digitalWrite(B_ENA, HIGH); }

void setDirA(bool bwd) {
  digitalWrite(A_DIR, bwd ? LOW : HIGH);
}
void setDirB(bool bwd) {
  digitalWrite(B_DIR, bwd ? LOW : HIGH);
}

void startMotor(Motor& m, bool bwd, RunMode mode,
                void (*ena)(), void (*setDir)(bool)) {
  m.mode       = mode;
  m.steps_left = set_steps_rev;
  m.status     = 0x01 | (bwd ? 0x02 : 0x00);
  setDir(bwd);
  delayMicroseconds(5);
  ena();
}

void stopMotor(Motor& m, void (*dis)()) {
  dis();
  m.mode   = IDLE;
  m.status = 0;
  m.steps_left = 0;
}

// ── Register access ────────────────────────────────────────
uint8_t processRead(uint8_t reg) {
  switch (reg) {
    case REG_STATUS_A: return motorA.status;
    case REG_STATUS_B: return motorB.status;
    default:           return 0xFF;
  }
}

void processWrite(uint8_t reg, uint8_t* data, uint8_t len) {
  if (len < 1) return;
  if (reg == REG_CMD) {
    pending_cmd = data[0];
  } else if (reg == REG_REV_HI && len >= 2) {
    set_steps_rev = (uint16_t)((data[0] << 8) | data[1]);
  }
}

// ── I2C callbacks ──────────────────────────────────────────
void onReceive(int n) {
  if (n < 1) return;
  selected_reg = Wire.read();
  if (n > 1) {
    uint8_t data[8], len = 0;
    while (Wire.available() && len < 8) data[len++] = Wire.read();
    processWrite(selected_reg, data, len);
  }
}

void onRequest() { Wire.write(processRead(selected_reg)); }

// ── Serial ─────────────────────────────────────────────────
void printHelp() {
  Serial.println("── ingredient node (0x44) ───────────────");
  Serial.println("Stepper A: PUL D2 / DIR D3 / ENA D4");
  Serial.println("Stepper B: PUL D13 / DIR D12 / ENA D11");
  Serial.println("-----------------------------------------");
  Serial.println("Commands (W 10 <cmd>):");
  Serial.println("  01  STOP_ALL");
  Serial.println("  02  A FWD_CONT   — non-stop forward");
  Serial.println("  03  A BWD_CONT   — non-stop backward");
  Serial.println("  04  A DISPENSE   — 1 rev backward");
  Serial.println("  05  A RETRACT    — 1 rev forward");
  Serial.println("  06  B FWD_CONT   — non-stop forward");
  Serial.println("  07  B BWD_CONT   — non-stop backward");
  Serial.println("  08  B DISPENSE   — 1 rev backward");
  Serial.println("  09  B RETRACT    — 1 rev forward");
  Serial.println("  0A  STOP_A");
  Serial.println("  0B  STOP_B");
  Serial.println("-----------------------------------------");
  Serial.println("Steps/rev: W 11 HH LL  (uint16)");
  Serial.println("  W 11 00 C8  →  200 steps (full step)");
  Serial.println("  W 11 01 90  →  400 steps (1/2 step)");
  Serial.println("  W 11 03 20  →  800 steps (1/4 step)");
  Serial.println("  W 11 06 40  → 1600 steps (1/8 step)  ← default");
  Serial.println("-----------------------------------------");
  Serial.println("Read:");
  Serial.println("  R 00  status A  (bit0=busy bit1=bwd)");
  Serial.println("  R 01  status B  (bit0=busy bit1=bwd)");
  Serial.print(  "Steps/rev now: "); Serial.println(set_steps_rev);
  Serial.println("-----------------------------------------");
}

void handleSerial() {
  static char    buf[48];
  static uint8_t idx = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (idx > 0) {
        buf[idx] = '\0'; idx = 0;
        if (strncmp(buf, "help", 4) == 0) {
          printHelp();
        } else if (buf[0] == 'R' && buf[1] == ' ') {
          uint8_t reg = (uint8_t)strtoul(buf + 2, nullptr, 16);
          uint8_t val = processRead(reg);
          Serial.print('!');
          if (val < 0x10) Serial.print('0');
          Serial.println(val, HEX);
        } else if (buf[0] == 'W' && buf[1] == ' ') {
          char* p = buf + 2;
          uint8_t reg = (uint8_t)strtoul(p, &p, 16);
          uint8_t data[8], len = 0;
          while (*p && len < 8) {
            while (*p == ' ') p++;
            if (*p) data[len++] = (uint8_t)strtoul(p, &p, 16);
          }
          processWrite(reg, data, len);
          Serial.println("!OK");
        } else {
          Serial.println("?  (type 'help')");
        }
      }
    } else if (idx < 47) {
      buf[idx++] = c;
    }
  }
}

// ── Command dispatch ───────────────────────────────────────
void handleCommand(uint8_t cmd) {
  switch (cmd) {
    case CMD_STOP_ALL:
      stopMotor(motorA, disableA);
      stopMotor(motorB, disableB);
      Serial.println("[CMD] stop all");
      break;
    case CMD_STOP_A:
      stopMotor(motorA, disableA);
      Serial.println("[CMD] stop A");
      break;
    case CMD_STOP_B:
      stopMotor(motorB, disableB);
      Serial.println("[CMD] stop B");
      break;
    case CMD_A_FWD_CONT:
      startMotor(motorA, false, CONTINUOUS, enableA, setDirA);
      Serial.println("[CMD] A fwd cont");
      break;
    case CMD_A_BWD_CONT:
      startMotor(motorA, true,  CONTINUOUS, enableA, setDirA);
      Serial.println("[CMD] A bwd cont");
      break;
    case CMD_A_DISPENSE:
      startMotor(motorA, true,  REVOLUTION, enableA, setDirA);
      Serial.print("[CMD] A dispense "); Serial.print(set_steps_rev); Serial.println(" steps");
      break;
    case CMD_A_RETRACT:
      startMotor(motorA, false, REVOLUTION, enableA, setDirA);
      Serial.print("[CMD] A retract "); Serial.print(set_steps_rev); Serial.println(" steps");
      break;
    case CMD_B_FWD_CONT:
      startMotor(motorB, false, CONTINUOUS, enableB, setDirB);
      Serial.println("[CMD] B fwd cont");
      break;
    case CMD_B_BWD_CONT:
      startMotor(motorB, true,  CONTINUOUS, enableB, setDirB);
      Serial.println("[CMD] B bwd cont");
      break;
    case CMD_B_DISPENSE:
      startMotor(motorB, true,  REVOLUTION, enableB, setDirB);
      Serial.print("[CMD] B dispense "); Serial.print(set_steps_rev); Serial.println(" steps");
      break;
    case CMD_B_RETRACT:
      startMotor(motorB, false, REVOLUTION, enableB, setDirB);
      Serial.print("[CMD] B retract "); Serial.print(set_steps_rev); Serial.println(" steps");
      break;
  }
}

// ── Step one motor (call only when its turn to pulse) ──────
void stepIf(bool active, uint8_t pul_pin, Motor& m, void (*dis)()) {
  if (!active) return;
  digitalWrite(pul_pin, HIGH);
  delayMicroseconds(STEP_HALF_US);
  digitalWrite(pul_pin, LOW);
  delayMicroseconds(STEP_HALF_US);
  if (m.mode == REVOLUTION) {
    if (m.steps_left > 0) m.steps_left--;
    if (m.steps_left == 0) stopMotor(m, dis);
  }
}

// ── Setup / Loop ───────────────────────────────────────────
void setup() {
  pinMode(A_PUL, OUTPUT); pinMode(A_DIR, OUTPUT); pinMode(A_ENA, OUTPUT);
  pinMode(B_PUL, OUTPUT); pinMode(B_DIR, OUTPUT); pinMode(B_ENA, OUTPUT);
  digitalWrite(A_PUL, LOW); digitalWrite(A_DIR, LOW);
  digitalWrite(B_PUL, LOW); digitalWrite(B_DIR, LOW);
  disableA(); disableB();

  Serial.begin(115200);
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
  Serial.println("[ingredient] ready — type 'help'");
}

void loop() {
  handleSerial();

  if (pending_cmd) {
    handleCommand(pending_cmd);
    pending_cmd = 0;
  }

  bool a = (motorA.mode != IDLE);
  bool b = (motorB.mode != IDLE);

  if (a && b) {
    // Both running — pulse simultaneously
    digitalWrite(A_PUL, HIGH); digitalWrite(B_PUL, HIGH);
    delayMicroseconds(STEP_HALF_US);
    digitalWrite(A_PUL, LOW);  digitalWrite(B_PUL, LOW);
    delayMicroseconds(STEP_HALF_US);
    if (motorA.mode == REVOLUTION) {
      if (motorA.steps_left > 0) motorA.steps_left--;
      if (motorA.steps_left == 0) stopMotor(motorA, disableA);
    }
    if (motorB.mode == REVOLUTION) {
      if (motorB.steps_left > 0) motorB.steps_left--;
      if (motorB.steps_left == 0) stopMotor(motorB, disableB);
    }
  } else {
    stepIf(a, A_PUL, motorA, disableA);
    stepIf(b, B_PUL, motorB, disableB);
  }
}
