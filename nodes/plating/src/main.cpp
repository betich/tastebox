#include <Arduino.h>
#include <RS485Node.h>
#include <SerialFrameHandler.h>

// Plater node — RS485 node 0x43
// Pan stepper:  PUL→D13, DIR→D12, ENA→D11
// Arm IBD_2:    L_IS→D4, R_IS→D5, L_PWM→D6 (fwd/plate), R_PWM→D7 (rev/home)
// Lid IBD_2:    L_IS→D8, R_IS→D9, L_PWM→D2 (fwd/open),  R_PWM→D3 (rev/close)
// Limit (NC):   D10 / GND — e-stop for both arm and lid
//   Not triggered: switch closed → D10 LOW
//   Triggered:     switch opens  → D10 HIGH (INPUT_PULLUP)

// ── Hardware constants ─────────────────────────────────────
#define MICROSTEPPING   16
#define MOTOR_STEPS_REV 200
#define GEAR_RATIO      1.0f
#define STEPS_PER_DEG   (MOTOR_STEPS_REV * MICROSTEPPING * GEAR_RATIO / 360.0f)
#define MAX_SPEED_DEG   30.0f

// Arm IBD_2
#define PIN_ARM_L_IS   4
#define PIN_ARM_R_IS   5
#define PIN_ARM_L_PWM  6   // forward/plate (PWM)
#define PIN_ARM_R_PWM  7   // reverse/home  (digital)
#define ARM_PWM_FWD    50   // dispense (forward): 75% slower than original 200
#define ARM_PWM_BWD   128   // retract  (backward): 50% slower; brake-mode PWM via L_PWM

// Lid IBD_2
#define PIN_LID_L_IS   8
#define PIN_LID_R_IS   9
#define PIN_LID_L_PWM  2   // close (digital — D2 not PWM on Nano)
#define PIN_LID_R_PWM  3   // open  (PWM)
#define LID_PWM_SPEED  20   // 50% of previous 40; both directions via brake-mode PWM

// Limit switch (NC) — e-stop for arm only
#define PIN_LIMIT      10  // INPUT_PULLUP; HIGH = triggered

#define DEFAULT_ARM_DUR_MS      4000
#define DEFAULT_LID_DUR_MS      4700
#define DEFAULT_LID_OPEN_EXTRA   600  // extra ms for open only — accounts for initial tension

// ── RS485 ───────────────────────────────────────────────────
#define PIN_RS485_RX    A0
#define PIN_RS485_TX    A1
#define PIN_RS485_DE_RE A2

// Read registers
#define REG_PAN_POS_HI 0x00  // pan position high byte
#define REG_PAN_POS_LO 0x01  // pan position low byte
#define REG_ARM_STATE  0x02  // 0=home 1=plate 2=moving
#define REG_STATUS     0x03  // bit0=pan_busy bit1=arm_busy bit2=lid_busy
#define REG_LID_STATE  0x04  // 0=closed 1=open 2=moving

// Write registers
#define REG_CMD        0x10  // pan: STOP=01 HOME=02
#define REG_SET_PAN_HI 0x11  // pan target hi byte (lo follows)
#define REG_SET_PAN_LO 0x12
#define REG_ARM_CMD    0x13  // arm command
#define REG_ARM_DUR_HI 0x14  // arm duration ms (lo follows)
#define REG_ARM_DUR_LO 0x15
#define REG_LID_CMD    0x16  // lid command
#define REG_LID_DUR_HI 0x17  // lid duration ms (lo follows)
#define REG_LID_DUR_LO 0x18

// Pan commands
#define CMD_PAN_STOP       0x01
#define CMD_PAN_HOME       0x02

// Arm commands
#define CMD_ARM_DISPENSE   0x01  // fwd for arm_dur_ms
#define CMD_ARM_RETRACT    0x02  // rev for arm_dur_ms
#define CMD_ARM_FWD_CONT   0x03  // non-stop → plate
#define CMD_ARM_BWD_CONT   0x04  // non-stop → home
#define CMD_ARM_STOP       0x05
#define CMD_ARM_SEQ_GOTO_B 0x06  // sequence: open lid fully, then move arm to plate
#define CMD_ARM_SEQ_GOTO_A 0x07  // sequence: move arm home, then close lid

// Lid commands
#define CMD_LID_CLOSE      0x01  // rev for lid_dur_ms
#define CMD_LID_OPEN       0x02  // fwd for lid_dur_ms
#define CMD_LID_FWD_CONT   0x03  // non-stop open
#define CMD_LID_BWD_CONT   0x04  // non-stop close
#define CMD_LID_STOP       0x05

#define MOT_AT_A   0  // arm=home,   lid=closed
#define MOT_AT_B   1  // arm=plate,  lid=open
#define MOT_MOVING 2

// Sequence states
#define SEQ_IDLE     0
#define SEQ_PHASE_1  1  // first phase of sequence
#define SEQ_PHASE_2  2  // second phase of sequence

// ── Stepper ────────────────────────────────────────────────
struct Stepper {
  uint8_t pin_step, pin_dir, pin_ena;
  void begin() {
    pinMode(pin_step, OUTPUT); pinMode(pin_dir, OUTPUT); pinMode(pin_ena, OUTPUT);
    digitalWrite(pin_step, LOW); digitalWrite(pin_dir, LOW);
    digitalWrite(pin_ena, LOW);  // active-low
  }
  void step(long steps, float speed_deg) {
    if (steps == 0) return;
    digitalWrite(pin_dir, steps > 0 ? HIGH : LOW);
    delayMicroseconds(5);
    long delay_us = (long)(500000.0f / (speed_deg * STEPS_PER_DEG));
    long n = abs(steps);
    for (long i = 0; i < n; i++) {
      digitalWrite(pin_step, HIGH); delayMicroseconds(delay_us);
      digitalWrite(pin_step, LOW);  delayMicroseconds(delay_us);
    }
  }
};

// ── Arm motor ──────────────────────────────────────────────
void armBegin() {
  pinMode(PIN_ARM_L_IS,  OUTPUT); digitalWrite(PIN_ARM_L_IS,  HIGH);
  pinMode(PIN_ARM_R_IS,  OUTPUT); digitalWrite(PIN_ARM_R_IS,  HIGH);
  pinMode(PIN_ARM_L_PWM, OUTPUT); analogWrite(PIN_ARM_L_PWM, 0);
  pinMode(PIN_ARM_R_PWM, OUTPUT); digitalWrite(PIN_ARM_R_PWM, LOW);
}
void armCoast() {
  analogWrite(PIN_ARM_L_PWM, 0);
  digitalWrite(PIN_ARM_R_PWM, LOW);
}
void armDriveFwd() {
  digitalWrite(PIN_ARM_R_PWM, LOW); delay(5);
  analogWrite(PIN_ARM_L_PWM, ARM_PWM_FWD);
}
void armDriveBwd() {
  // brake-mode PWM: L_PWM = 255-BWD → effective backward duty = BWD/255
  analogWrite(PIN_ARM_L_PWM, 255 - ARM_PWM_BWD); delay(5);
  digitalWrite(PIN_ARM_R_PWM, HIGH);
}

// ── Lid motor ──────────────────────────────────────────────
void lidBegin() {
  pinMode(PIN_LID_L_IS,  OUTPUT); digitalWrite(PIN_LID_L_IS,  HIGH);
  pinMode(PIN_LID_R_IS,  OUTPUT); digitalWrite(PIN_LID_R_IS,  HIGH);
  pinMode(PIN_LID_L_PWM, OUTPUT); digitalWrite(PIN_LID_L_PWM, LOW);
  pinMode(PIN_LID_R_PWM, OUTPUT); analogWrite(PIN_LID_R_PWM, 0);
}
void lidCoast() {
  digitalWrite(PIN_LID_L_PWM, LOW);
  analogWrite(PIN_LID_R_PWM, 0);
}
void lidDriveFwd() {                // open — R_PWM (D3) standard PWM
  digitalWrite(PIN_LID_L_PWM , LOW); delay(5);
  analogWrite(PIN_LID_R_PWM, LID_PWM_SPEED);
}
void lidDriveBwd() {                // close — brake-mode PWM: D2=HIGH + D3=PWM(255-speed)
  //   D3 HIGH → brake, D3 LOW → forward; effective duty = LID_PWM_SPEED/255
  analogWrite(PIN_LID_R_PWM, 255 - LID_PWM_SPEED); delay(5);
  digitalWrite(PIN_LID_L_PWM, HIGH);
}

// ── Limit switch ───────────────────────────────────────────
// NC: closed=LOW (not hit), open=HIGH (triggered)
bool limitHit() { return digitalRead(PIN_LIMIT) == HIGH; }

// ── Instances / state ──────────────────────────────────────
Stepper pan = { 13, 12, 11 };
RS485Node          node(0x43, PIN_RS485_RX, PIN_RS485_TX, PIN_RS485_DE_RE);
SerialFrameHandler serial_handler(0x43);

void handlePlainText(const char* line, Stream& s) {
  if (strcmp(line, "help") != 0) return;
  s.println(F("Plater node 0x43 — serial commands:"));
  s.println(F("  @43 R 00/01   pan pos hi/lo (int16)"));
  s.println(F("  @43 R 02      arm state  0=home 1=plate 2=moving"));
  s.println(F("  @43 R 03      status     b0=pan_busy b1=arm_busy b2=lid_busy"));
  s.println(F("  @43 R 04      lid state  0=closed 1=open 2=moving"));
  s.println(F("  @43 W 10 01   pan stop"));
  s.println(F("  @43 W 10 02   pan home"));
  s.println(F("  @43 W 11 HH LL  pan target (int16 hi lo)"));
  s.println(F("  @43 W 13 01   arm dispense  02=retract 03=fwd 04=bwd 05=stop"));
  s.println(F("  @43 W 13 06   seq A->B (open lid then plate)"));
  s.println(F("  @43 W 13 07   seq B->A (retract arm then close lid)"));
  s.println(F("  @43 W 14 HH LL  arm duration ms"));
  s.println(F("  @43 W 16 01   lid close  02=open 03=fwd 04=bwd 05=stop"));
  s.println(F("  @43 W 17 HH LL  lid duration ms"));
  s.println(F("Pins: Pan PUL=D13 DIR=D12 ENA=D11 | Arm L_IS=D4 R_IS=D5 L_PWM=D6 R_PWM=D7"));
  s.println(F("      Lid L_IS=D8 R_IS=D9 L_PWM=D2 R_PWM=D3 | Limit=D10"));
  s.println(F("      RS485(2nd) RO=A0 DI=A1 DE/RE=A2"));
}

int16_t pan_pos          = 0;
int16_t pan_target       = 0;
uint8_t status_reg       = 0;
bool    pending_pan_stop = false;
bool    pending_pan_home = false;

uint8_t          arm_state     = MOT_AT_A;
uint8_t          arm_target    = MOT_AT_A;
uint8_t          arm_cmd_next  = 0;
uint16_t         arm_dur_ms    = DEFAULT_ARM_DUR_MS;
unsigned long    arm_start_ms  = 0;
bool             arm_timed     = false;

uint8_t          lid_state     = MOT_AT_A;  // AT_A = closed
uint8_t          lid_target    = MOT_AT_A;
uint8_t          lid_cmd_next  = 0;
uint16_t         lid_dur_ms    = DEFAULT_LID_DUR_MS;
unsigned long    lid_start_ms  = 0;
bool             lid_timed     = false;

// Sequence control
uint8_t sequence_state  = SEQ_IDLE;
uint8_t sequence_target = 0;  // CMD_ARM_SEQ_GOTO_B or CMD_ARM_SEQ_GOTO_A
bool    limit_triggered = false;

// ── Register access ────────────────────────────────────────
uint8_t processRead(uint8_t reg) {
  switch (reg) {
    case REG_PAN_POS_HI: return (uint8_t)(pan_pos >> 8);
    case REG_PAN_POS_LO: return (uint8_t)(pan_pos & 0xFF);
    case REG_ARM_STATE:  return arm_state;
    case REG_STATUS:     return status_reg;
    case REG_LID_STATE:  return lid_state;
    default:             return 0xFF;
  }
}

void processWrite(uint8_t reg, uint8_t* data, uint8_t len) {
  if (len < 1) return;
  switch (reg) {
    case REG_CMD:
      if      (data[0] == CMD_PAN_STOP) pending_pan_stop = true;
      else if (data[0] == CMD_PAN_HOME) pending_pan_home = true;
      break;
    case REG_SET_PAN_HI:
      if (len >= 2) pan_target = (int16_t)((data[0] << 8) | data[1]);
      break;
    case REG_ARM_CMD:
      if (data[0] == CMD_ARM_SEQ_GOTO_B || data[0] == CMD_ARM_SEQ_GOTO_A) {
        sequence_target = data[0];
        sequence_state = SEQ_PHASE_1;
      } else {
        arm_cmd_next = data[0];
      }
      break;
    case REG_ARM_DUR_HI:
      if (len >= 2) arm_dur_ms = (uint16_t)((data[0] << 8) | data[1]);
      break;
    case REG_LID_CMD:
      lid_cmd_next = data[0];
      break;
    case REG_LID_DUR_HI:
      if (len >= 2) lid_dur_ms = (uint16_t)((data[0] << 8) | data[1]);
      break;
  }
}

// ── Arm state machine ──────────────────────────────────────
void updateArm() {
  if (arm_cmd_next) {
    uint8_t cmd = arm_cmd_next; arm_cmd_next = 0;
    switch (cmd) {
      case CMD_ARM_DISPENSE:
        armDriveFwd(); arm_target = MOT_AT_B; arm_state = MOT_MOVING;
        arm_timed = true; arm_start_ms = millis(); status_reg |= 0x02;
        Serial.print("[arm] dispense ("); Serial.print(arm_dur_ms); Serial.println("ms)");
        break;
      case CMD_ARM_RETRACT:
        armDriveBwd(); arm_target = MOT_AT_A; arm_state = MOT_MOVING;
        arm_timed = true; arm_start_ms = millis(); status_reg |= 0x02;
        Serial.print("[arm] retract ("); Serial.print(arm_dur_ms); Serial.println("ms)");
        break;
      case CMD_ARM_FWD_CONT:
        armDriveFwd(); arm_target = MOT_AT_B; arm_state = MOT_MOVING;
        arm_timed = false; status_reg |= 0x02;
        Serial.println("[arm] fwd cont → plate");
        break;
      case CMD_ARM_BWD_CONT:
        armDriveBwd(); arm_target = MOT_AT_A; arm_state = MOT_MOVING;
        arm_timed = false; status_reg |= 0x02;
        Serial.println("[arm] bwd cont → home");
        break;
      case CMD_ARM_STOP:
        armCoast(); arm_state = MOT_AT_A; arm_timed = false;
        status_reg &= ~0x02; Serial.println("[arm] stop");
        break;
      case CMD_ARM_SEQ_GOTO_B:
        sequence_target = CMD_ARM_SEQ_GOTO_B;
        sequence_state = SEQ_PHASE_1;
        Serial.println("[seq] A->B: open lid first");
        break;
      case CMD_ARM_SEQ_GOTO_A:
        sequence_target = CMD_ARM_SEQ_GOTO_A;
        sequence_state = SEQ_PHASE_1;
        Serial.println("[seq] B->A: retract arm first");
        break;
    }
  }

  if (arm_state == MOT_MOVING) {
    if (limitHit()) {
      limit_triggered = true;
      armCoast(); arm_state = MOT_AT_A; arm_timed = false;
      status_reg &= ~0x02;
      Serial.println("*** [arm] LIMIT HIT — stopped (all systems)");
      return;
    }
    if (arm_timed && millis() - arm_start_ms >= arm_dur_ms) {
      armCoast(); arm_state = arm_target; status_reg &= ~0x02;
      Serial.print("[arm] done → "); Serial.println(arm_target == MOT_AT_B ? "plate" : "home");
    }
  }

  // Handle A↔B sequences
  if (sequence_state == SEQ_PHASE_1) {
    if (sequence_target == CMD_ARM_SEQ_GOTO_B) {
      // Phase 1: open lid, then advance once it reaches open.
      if (lid_state == MOT_AT_B) {
        Serial.println("[seq-A->B] phase 1 complete: lid open");
        sequence_state = SEQ_PHASE_2;
      } else if (lid_state == MOT_AT_A) {
        lidDriveFwd(); lid_target = MOT_AT_B; lid_state = MOT_MOVING;
        lid_timed = true; lid_start_ms = millis(); status_reg |= 0x04;
        uint16_t lid_open_dur = lid_dur_ms + DEFAULT_LID_OPEN_EXTRA;
        Serial.print("[seq-A->B] phase 1: open lid ("); Serial.print(lid_dur_ms); Serial.print("ms + ");
        Serial.print(DEFAULT_LID_OPEN_EXTRA); Serial.print("ms extra = "); Serial.print(lid_open_dur); Serial.println("ms)");
      } else if (lid_state == MOT_MOVING) {
        // Wait for lid to finish.
        uint16_t effective_lid_dur = lid_dur_ms + DEFAULT_LID_OPEN_EXTRA;
        if (millis() - lid_start_ms >= effective_lid_dur) {
          lidCoast(); lid_state = MOT_AT_B; lid_timed = false; status_reg &= ~0x04;
          Serial.println("[seq-A->B] phase 1 complete: lid open");
          sequence_state = SEQ_PHASE_2;
        }
      }
    }
    else if (sequence_target == CMD_ARM_SEQ_GOTO_A) {
      // Phase 1: move arm back home, then advance once it reaches home.
      if (arm_state == MOT_AT_A) {
        Serial.println("[seq-B->A] phase 1 complete: arm home");
        sequence_state = SEQ_PHASE_2;
      } else if (arm_state == MOT_AT_B) {
        armDriveBwd(); arm_target = MOT_AT_A; arm_state = MOT_MOVING;
        arm_timed = true; arm_start_ms = millis(); status_reg |= 0x02;
        Serial.print("[seq-B->A] phase 1: retract arm ("); Serial.print(arm_dur_ms); Serial.println("ms)");
      } else if (arm_state == MOT_MOVING) {
        // Wait for arm to finish.
        if (millis() - arm_start_ms >= arm_dur_ms) {
          armCoast(); arm_state = MOT_AT_A; arm_timed = false; status_reg &= ~0x02;
          Serial.println("[seq-B->A] phase 1 complete: arm home");
          sequence_state = SEQ_PHASE_2;
        }
      }
    }
  }
  else if (sequence_state == SEQ_PHASE_2) {
    if (sequence_target == CMD_ARM_SEQ_GOTO_B) {
      // Phase 2: move arm to plate, then finish once it reaches plate.
      if (arm_state == MOT_AT_B) {
        Serial.println("[seq-A->B] complete: at B (open+plate)");
        sequence_state = SEQ_IDLE;
        sequence_target = 0;
      } else if (arm_state == MOT_AT_A) {
        armDriveFwd(); arm_target = MOT_AT_B; arm_state = MOT_MOVING;
        arm_timed = true; arm_start_ms = millis(); status_reg |= 0x02;
        Serial.print("[seq-A->B] phase 2: move arm to plate ("); Serial.print(arm_dur_ms); Serial.println("ms)");
      } else if (arm_state == MOT_MOVING) {
        // Wait for arm to finish.
        if (millis() - arm_start_ms >= arm_dur_ms) {
          armCoast(); arm_state = MOT_AT_B; arm_timed = false; status_reg &= ~0x02;
          Serial.println("[seq-A->B] complete: at B (open+plate)");
          sequence_state = SEQ_IDLE;
          sequence_target = 0;
        }
      }
    }
    else if (sequence_target == CMD_ARM_SEQ_GOTO_A) {
      // Phase 2: close lid, then finish once it reaches closed.
      if (lid_state == MOT_AT_A) {
        Serial.println("[seq-B->A] complete: at A (home+closed)");
        sequence_state = SEQ_IDLE;
        sequence_target = 0;
      } else if (lid_state == MOT_AT_B) {
        lidDriveBwd(); lid_target = MOT_AT_A; lid_state = MOT_MOVING;
        lid_timed = true; lid_start_ms = millis(); status_reg |= 0x04;
        Serial.print("[seq-B->A] phase 2: close lid ("); Serial.print(lid_dur_ms); Serial.println("ms)");
      } else if (lid_state == MOT_MOVING) {
        // Wait for lid to finish.
        if (millis() - lid_start_ms >= lid_dur_ms) {
          lidCoast(); lid_state = MOT_AT_A; lid_timed = false; status_reg &= ~0x04;
          Serial.println("[seq-B->A] complete: at A (home+closed)");
          sequence_state = SEQ_IDLE;
          sequence_target = 0;
        }
      }
    }
  }
}

// ── Lid state machine ──────────────────────────────────────
void updateLid() {
  if (lid_cmd_next) {
    uint8_t cmd = lid_cmd_next; lid_cmd_next = 0;
    switch (cmd) {
      case CMD_LID_OPEN: {
        lidDriveFwd(); lid_target = MOT_AT_B; lid_state = MOT_MOVING;
        lid_timed = true; lid_start_ms = millis(); status_reg |= 0x04;
        uint16_t lid_open_dur = lid_dur_ms + DEFAULT_LID_OPEN_EXTRA;
        Serial.print("[lid] open ("); Serial.print(lid_dur_ms); Serial.print("ms + ");
        Serial.print(DEFAULT_LID_OPEN_EXTRA); Serial.print("ms extra = "); Serial.print(lid_open_dur); Serial.println("ms)");
        break;
      }
      case CMD_LID_CLOSE:
        lidDriveBwd(); lid_target = MOT_AT_A; lid_state = MOT_MOVING;
        lid_timed = true; lid_start_ms = millis(); status_reg |= 0x04;
        Serial.print("[lid] close ("); Serial.print(lid_dur_ms); Serial.println("ms)");
        break;
      case CMD_LID_FWD_CONT:
        lidDriveFwd(); lid_target = MOT_AT_B; lid_state = MOT_MOVING;
        lid_timed = false; status_reg |= 0x04;
        Serial.println("[lid] fwd cont → open");
        break;
      case CMD_LID_BWD_CONT:
        lidDriveBwd(); lid_target = MOT_AT_A; lid_state = MOT_MOVING;
        lid_timed = false; status_reg |= 0x04;
        Serial.println("[lid] bwd cont → close");
        break;
      case CMD_LID_STOP:
        lidCoast(); lid_state = MOT_AT_A; lid_timed = false;
        status_reg &= ~0x04; Serial.println("[lid] stop");
        break;
    }
  }

  if (lid_state == MOT_MOVING) {
    if (limit_triggered) {
      lidCoast(); lid_state = MOT_AT_A; lid_timed = false;
      status_reg &= ~0x04;
      Serial.println("*** [lid] LIMIT TRIGGERED — emergency stop");
      limit_triggered = false;
      sequence_state = SEQ_IDLE;
      return;
    }
    uint16_t effective_lid_dur = (lid_target == MOT_AT_B) ? (lid_dur_ms + DEFAULT_LID_OPEN_EXTRA) : lid_dur_ms;
    if (lid_timed && millis() - lid_start_ms >= effective_lid_dur) {
      lidCoast(); lid_state = lid_target; status_reg &= ~0x04;
      Serial.print("[lid] done → "); Serial.println(lid_target == MOT_AT_B ? "open" : "closed");
    }
  }
}

// ── Setup / Loop ───────────────────────────────────────────
void setup() {
  pan.begin();
  armBegin();
  lidBegin();
  pinMode(PIN_LIMIT, INPUT_PULLUP);

  Serial.begin(115200);
  node.begin();
  node.setDefaultReadHandler(processRead);
  node.setDefaultWriteHandler(processWrite);

  serial_handler.setDefaultReadHandler(processRead);
  serial_handler.setDefaultWriteHandler(processWrite);
  serial_handler.setPlainTextHandler(handlePlainText);

  Serial.println("[plater] USB node 0x43 ready (RS485 secondary)");
}

void loop() {
  serial_handler.poll(Serial);
  node.poll();
  if (limitHit()) {
    limit_triggered = true;
  }
  updateArm();
  updateLid();

  if (pending_pan_stop) {
    pending_pan_stop = false;
    pan_target = pan_pos;
    Serial.println("[pan] stop");
  }
  if (pending_pan_home) {
    pending_pan_home = false;
    pan_target = 0;
    Serial.println("[pan] home");
  }
  if (pan_pos != pan_target) {
    status_reg |= 0x01;
    long steps = pan_target - pan_pos;
    pan.step(steps, MAX_SPEED_DEG);
    pan_pos = pan_target;
    status_reg &= ~0x01;
    Serial.print("[pan] pos="); Serial.println(pan_pos);
  }
}
