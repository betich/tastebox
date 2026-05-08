#include <Arduino.h>
#include <RS485Node.h>
#include <SerialFrameHandler.h>

// Ingredient stepper node — RS485 node 0x44
//
// Stepper A: PUL→D2,  DIR→D3,  ENA→D4
// Stepper B: PUL→D13, DIR→D12, ENA→D11
// Stepper C: PUL→D8,  DIR→D9,  ENA→D10
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
//   0x0C  C_FWD_CONT    — C non-stop forward
//   0x0D  C_BWD_CONT    — C non-stop backward
//   0x0E  C_DISPENSE    — C one revolution forward
//   0x0F  C_RETRACT     — C one revolution backward
//   0x10  STOP_C
//
// Registers:
//   R 0x00  status A  (bit0=busy, bit1=bwd)
//   R 0x01  status B  (bit0=busy, bit1=bwd)
//   R 0x02  status C  (bit0=busy, bit1=bwd)
//   W 0x10  command
//   W 0x11+0x12  steps per revolution (uint16 hi-lo, default 1600)

// ── Pins ───────────────────────────────────────────────────
#define A_PUL  2
#define A_DIR  3
#define A_ENA  4

#define B_PUL  13
#define B_DIR  12
#define B_ENA  11

#define C_PUL  8
#define C_DIR  9
#define C_ENA  10

#define STEP_HALF_US  800   // µs per half-pulse (~625 Hz)

// ── RS485 ───────────────────────────────────────────────────
#define PIN_RS485_RX    5
#define PIN_RS485_TX    6
#define PIN_RS485_DE_RE 7

#define REG_STATUS_A  0x00
#define REG_STATUS_B  0x01
#define REG_STATUS_C  0x02
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
#define CMD_C_FWD_CONT  0x0C
#define CMD_C_BWD_CONT  0x0D
#define CMD_C_DISPENSE  0x0E
#define CMD_C_RETRACT   0x0F
#define CMD_STOP_C      0x10

// ── Motor state ────────────────────────────────────────────
enum RunMode : uint8_t { IDLE, CONTINUOUS, REVOLUTION };

struct Motor {
  RunMode  mode;
  uint16_t steps_left;
  uint8_t  status;      // bit0=busy, bit1=bwd
};

Motor motorA = { IDLE, 0, 0 };
Motor motorB = { IDLE, 0, 0 };
Motor motorC = { IDLE, 0, 0 };

uint8_t  pending_cmd   = 0;
uint16_t set_steps_rev = 1600;  // 1/8 microstepping — tweak until one vend = one revolution

RS485Node          node(0x44, PIN_RS485_RX, PIN_RS485_TX, PIN_RS485_DE_RE);
SerialFrameHandler serial_handler(0x44);

// ── Motor helpers ──────────────────────────────────────────
void enableA()  { digitalWrite(A_ENA, LOW); }
void disableA() { digitalWrite(A_ENA, HIGH); }
void enableB()  { digitalWrite(B_ENA, LOW); }
void disableB() { digitalWrite(B_ENA, HIGH); }
void enableC()  { digitalWrite(C_ENA, LOW); }
void disableC() { digitalWrite(C_ENA, HIGH); }

void setDirA(bool bwd) { digitalWrite(A_DIR, bwd ? LOW : HIGH); }
void setDirB(bool bwd) { digitalWrite(B_DIR, bwd ? LOW : HIGH); }
void setDirC(bool bwd) { digitalWrite(C_DIR, bwd ? LOW : HIGH); }

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
    case REG_STATUS_C: return motorC.status;
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

// ── Command dispatch ───────────────────────────────────────
void handleCommand(uint8_t cmd) {
  switch (cmd) {
    case CMD_STOP_ALL:
      stopMotor(motorA, disableA);
      stopMotor(motorB, disableB);
      stopMotor(motorC, disableC);
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
    case CMD_STOP_C:
      stopMotor(motorC, disableC);
      Serial.println("[CMD] stop C");
      break;
    case CMD_C_FWD_CONT:
      startMotor(motorC, false, CONTINUOUS, enableC, setDirC);
      Serial.println("[CMD] C fwd cont");
      break;
    case CMD_C_BWD_CONT:
      startMotor(motorC, true,  CONTINUOUS, enableC, setDirC);
      Serial.println("[CMD] C bwd cont");
      break;
    case CMD_C_DISPENSE:
      startMotor(motorC, true,  REVOLUTION, enableC, setDirC);
      Serial.print("[CMD] C dispense "); Serial.print(set_steps_rev); Serial.println(" steps");
      break;
    case CMD_C_RETRACT:
      startMotor(motorC, false, REVOLUTION, enableC, setDirC);
      Serial.print("[CMD] C retract "); Serial.print(set_steps_rev); Serial.println(" steps");
      break;
  }
}

// ── Setup / Loop ───────────────────────────────────────────
void setup() {
  pinMode(A_PUL, OUTPUT); pinMode(A_DIR, OUTPUT); pinMode(A_ENA, OUTPUT);
  pinMode(B_PUL, OUTPUT); pinMode(B_DIR, OUTPUT); pinMode(B_ENA, OUTPUT);
  pinMode(C_PUL, OUTPUT); pinMode(C_DIR, OUTPUT); pinMode(C_ENA, OUTPUT);
  digitalWrite(A_PUL, LOW); digitalWrite(A_DIR, LOW);
  digitalWrite(B_PUL, LOW); digitalWrite(B_DIR, LOW);
  digitalWrite(C_PUL, LOW); digitalWrite(C_DIR, LOW);
  disableA(); disableB(); disableC();

  Serial.begin(115200);
  node.begin();
  node.setDefaultReadHandler(processRead);
  node.setDefaultWriteHandler(processWrite);

  serial_handler.setDefaultReadHandler(processRead);
  serial_handler.setDefaultWriteHandler(processWrite);

  Serial.println("[ingredient] RS485 node 0x44 ready");
}

void loop() {
  node.poll();
  serial_handler.poll(Serial);

  if (pending_cmd) {
    handleCommand(pending_cmd);
    pending_cmd = 0;
  }

  bool a = (motorA.mode != IDLE);
  bool b = (motorB.mode != IDLE);
  bool c = (motorC.mode != IDLE);

  if (a || b || c) {
    // Pulse all active motors simultaneously in one delay window
    if (a) digitalWrite(A_PUL, HIGH);
    if (b) digitalWrite(B_PUL, HIGH);
    if (c) digitalWrite(C_PUL, HIGH);
    delayMicroseconds(STEP_HALF_US);
    if (a) digitalWrite(A_PUL, LOW);
    if (b) digitalWrite(B_PUL, LOW);
    if (c) digitalWrite(C_PUL, LOW);
    delayMicroseconds(STEP_HALF_US);
    if (a && motorA.mode == REVOLUTION) {
      if (motorA.steps_left > 0) motorA.steps_left--;
      if (motorA.steps_left == 0) stopMotor(motorA, disableA);
    }
    if (b && motorB.mode == REVOLUTION) {
      if (motorB.steps_left > 0) motorB.steps_left--;
      if (motorB.steps_left == 0) stopMotor(motorB, disableB);
    }
    if (c && motorC.mode == REVOLUTION) {
      if (motorC.steps_left > 0) motorC.steps_left--;
      if (motorC.steps_left == 0) stopMotor(motorC, disableC);
    }
  }
}
