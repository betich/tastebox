#include <Arduino.h>
#include <Wire.h>

// Plater node — I2C slave at 0x43
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
#define MAX_SPEED_DEG   60.0f

// Arm IBD_2
#define PIN_ARM_L_IS   4
#define PIN_ARM_R_IS   5
#define PIN_ARM_L_PWM  6   // forward/plate (PWM)
#define PIN_ARM_R_PWM  7   // reverse/home  (digital)
#define ARM_PWM_SPEED  200

// Lid IBD_2
#define PIN_LID_L_IS   8
#define PIN_LID_R_IS   9
#define PIN_LID_L_PWM  2   // close (digital — D2 not PWM on Nano)
#define PIN_LID_R_PWM  3   // open  (PWM)
#define LID_PWM_SPEED  40   // duty cycle 0–255; both directions matched via brake-mode PWM

// Limit switch (NC) — e-stop for arm only
#define PIN_LIMIT      10  // INPUT_PULLUP; HIGH = triggered

#define DEFAULT_ARM_DUR_MS      4000
#define DEFAULT_LID_DUR_MS      4700
#define DEFAULT_LID_OPEN_EXTRA   600  // extra ms for open only — accounts for initial tension

// ── I2C ────────────────────────────────────────────────────
#define I2C_ADDRESS    0x43

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
  analogWrite(PIN_ARM_L_PWM, ARM_PWM_SPEED);
}
void armDriveBwd() {
  analogWrite(PIN_ARM_L_PWM, 0); delay(5);
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

volatile int16_t pan_pos          = 0;
volatile int16_t pan_target       = 0;
volatile uint8_t status_reg       = 0;
volatile uint8_t selected_reg     = 0;
volatile bool    pending_pan_stop = false;
volatile bool    pending_pan_home = false;

uint8_t          arm_state     = MOT_AT_A;
uint8_t          arm_target    = MOT_AT_A;
volatile uint8_t arm_cmd_next  = 0;
uint16_t         arm_dur_ms    = DEFAULT_ARM_DUR_MS;
unsigned long    arm_start_ms  = 0;
bool             arm_timed     = false;

uint8_t          lid_state     = MOT_AT_A;  // AT_A = closed
uint8_t          lid_target    = MOT_AT_A;
volatile uint8_t lid_cmd_next  = 0;
uint16_t         lid_dur_ms    = DEFAULT_LID_DUR_MS;
unsigned long    lid_start_ms  = 0;
bool             lid_timed     = false;

// Sequence control
volatile uint8_t sequence_state  = SEQ_IDLE;
volatile uint8_t sequence_target = 0;  // CMD_ARM_SEQ_GOTO_B or CMD_ARM_SEQ_GOTO_A
volatile bool    limit_triggered = false;

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

// ── Serial help ────────────────────────────────────────────
void printHelp() {
  Serial.println("── plating node (0x43) ──────────────────");
  Serial.println("Pan stepper: PUL D13 / DIR D12 / ENA D11");
  Serial.println("Arm IBD_2:   L_IS D4 / R_IS D5 / L_PWM D6 / R_PWM D7");
  Serial.println("Lid IBD_2:   L_IS D8 / R_IS D9 / L_PWM D2 / R_PWM D3");
  Serial.println("Limit (NC):  D10 / GND — e-stop arm only");
  Serial.println("-----------------------------------------");
  Serial.println("Pan (W 10 <cmd>):  01=STOP  02=HOME");
  Serial.println("Pan target: W 11 HH LL  (int16 steps)");
  Serial.println("  W 11 01 90 = 400 steps  W 11 06 40 = 1600 steps");
  Serial.println("-----------------------------------------");
  Serial.println("Arm (W 13 <cmd>):");
  Serial.println("  01 RETRACT       — back to home for dur ms");
  Serial.println("  02 DISPENSE      — to plate for dur ms");
  Serial.println("  03 FWD_CONT  04 BWD_CONT  05 STOP");
  Serial.println("  06 SEQ_GOTO_B    — sequence: open lid, then arm to plate");
  Serial.println("  07 SEQ_GOTO_A    — sequence: arm home, then close lid");
  Serial.println("  *** LIMIT SWITCH STOPS EVERYTHING (arm + lid) ***");
  Serial.println("Arm dur: W 14 HH LL");
  Serial.print(  "  now="); Serial.print(arm_dur_ms); Serial.println("ms");
  Serial.println("-----------------------------------------");
  Serial.println("Sequences (W 13 <cmd>):");
  Serial.println("  06 SEQ_GOTO_B — open lid fully, THEN move arm to plate");
  Serial.println("  07 SEQ_GOTO_A — move arm to home, THEN close lid");
  Serial.println("-----------------------------------------");
  Serial.println("Lid (W 16 <cmd>):");
  Serial.println("  01 CLOSE — rev for dur ms");
  Serial.println("  02 OPEN  — fwd for dur ms");
  Serial.println("  03 FWD_CONT  04 BWD_CONT  05 STOP");
  Serial.println("  *** LIMIT SWITCH STOPS EVERYTHING (arm + lid) ***");
  Serial.println("Lid dur: W 17 HH LL");
  Serial.print(  "  now="); Serial.print(lid_dur_ms); Serial.println("ms");
  Serial.println("-----------------------------------------");
  Serial.println("Duration ref: 01F4=500ms 03E8=1s 07D0=2s 0FA0=4s");
  Serial.println("-----------------------------------------");
  Serial.println("Read:");
  Serial.println("  R 00/01 pan pos hi/lo");
  Serial.println("  R 02  arm (0=home 1=plate 2=moving)");
  Serial.println("  R 03  status (bit0=pan bit1=arm bit2=lid)");
  Serial.println("  R 04  lid (0=closed 1=open 2=moving)");
  Serial.println("-----------------------------------------");
}

// ── Serial handler ─────────────────────────────────────────
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
          Serial.print("!OK: wrote reg 0x");
          if (reg < 0x10) Serial.print('0');
          Serial.print(reg, HEX);
          Serial.print(" with "); Serial.print(len); Serial.print(" byte(s): ");
          for (uint8_t i = 0; i < len; i++) {
            if (data[i] < 0x10) Serial.print('0');
            Serial.print(data[i], HEX);
            if (i < len - 1) Serial.print(' ');
          }
          Serial.println();
        } else {
          Serial.println("?  (type 'help')");
        }
      }
    } else if (idx < (uint8_t)sizeof(buf) - 1) {
      buf[idx++] = c;
    }
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
      // Phase 1: open lid
      if (lid_state != MOT_MOVING) {
        lidDriveFwd(); lid_target = MOT_AT_B; lid_state = MOT_MOVING;
        lid_timed = true; lid_start_ms = millis(); status_reg |= 0x04;
        uint16_t lid_open_dur = lid_dur_ms + DEFAULT_LID_OPEN_EXTRA;
        Serial.print("[seq-A->B] phase 1: open lid ("); Serial.print(lid_dur_ms); Serial.print("ms + ");
        Serial.print(DEFAULT_LID_OPEN_EXTRA); Serial.print("ms extra = "); Serial.print(lid_open_dur); Serial.println("ms)");
      } else if (lid_state == MOT_MOVING) {
        // Wait for lid to finish
        uint16_t effective_lid_dur = lid_dur_ms + DEFAULT_LID_OPEN_EXTRA;
        if (millis() - lid_start_ms >= effective_lid_dur) {
          lidCoast(); lid_state = MOT_AT_B; status_reg &= ~0x04;
          Serial.println("[seq-A->B] phase 1 complete: lid open");
          sequence_state = SEQ_PHASE_2;
        }
      }
    }
    else if (sequence_target == CMD_ARM_SEQ_GOTO_A) {
      // Phase 1: move arm back to home
      if (arm_state != MOT_MOVING) {
        armDriveBwd(); arm_target = MOT_AT_A; arm_state = MOT_MOVING;
        arm_timed = true; arm_start_ms = millis(); status_reg |= 0x02;
        Serial.print("[seq-B->A] phase 1: retract arm ("); Serial.print(arm_dur_ms); Serial.println("ms)");
      } else if (arm_state == MOT_MOVING) {
        // Wait for arm to finish
        if (millis() - arm_start_ms >= arm_dur_ms) {
          armCoast(); arm_state = MOT_AT_A; status_reg &= ~0x02;
          Serial.println("[seq-B->A] phase 1 complete: arm home");
          sequence_state = SEQ_PHASE_2;
        }
      }
    }
  }
  else if (sequence_state == SEQ_PHASE_2) {
    if (sequence_target == CMD_ARM_SEQ_GOTO_B) {
      // Phase 2: move arm to plate
      if (arm_state != MOT_MOVING) {
        armDriveFwd(); arm_target = MOT_AT_B; arm_state = MOT_MOVING;
        arm_timed = true; arm_start_ms = millis(); status_reg |= 0x02;
        Serial.print("[seq-A->B] phase 2: move arm to plate ("); Serial.print(arm_dur_ms); Serial.println("ms)");
      } else if (arm_state == MOT_MOVING) {
        // Wait for arm to finish
        if (millis() - arm_start_ms >= arm_dur_ms) {
          armCoast(); arm_state = MOT_AT_B; status_reg &= ~0x02;
          Serial.println("[seq-A->B] complete: at B (open+plate)");
          sequence_state = SEQ_IDLE;
        }
      }
    }
    else if (sequence_target == CMD_ARM_SEQ_GOTO_A) {
      // Phase 2: close lid
      if (lid_state != MOT_MOVING) {
        lidDriveBwd(); lid_target = MOT_AT_A; lid_state = MOT_MOVING;
        lid_timed = true; lid_start_ms = millis(); status_reg |= 0x04;
        Serial.print("[seq-B->A] phase 2: close lid ("); Serial.print(lid_dur_ms); Serial.println("ms)");
      } else if (lid_state == MOT_MOVING) {
        // Wait for lid to finish
        if (millis() - lid_start_ms >= lid_dur_ms) {
          lidCoast(); lid_state = MOT_AT_A; status_reg &= ~0x04;
          Serial.println("[seq-B->A] complete: at A (home+closed)");
          sequence_state = SEQ_IDLE;

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
      case CMD_LID_OPEN:
        lidDriveFwd(); lid_target = MOT_AT_B; lid_state = MOT_MOVING;
        lid_timed = true; lid_start_ms = millis(); status_reg |= 0x04;
        uint16_t lid_open_dur = lid_dur_ms + DEFAULT_LID_OPEN_EXTRA;
        Serial.print("[lid] open ("); Serial.print(lid_dur_ms); Serial.print("ms + ");
        Serial.print(DEFAULT_LID_OPEN_EXTRA); Serial.print("ms extra = "); Serial.print(lid_open_dur); Serial.println("ms)");
        break;
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
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
  Serial.println("[plater] ready — type 'help'");
}

void loop() {
  handleSerial();
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
