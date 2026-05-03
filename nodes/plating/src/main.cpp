#include <Arduino.h>
#include <Wire.h>

// Plater node — I2C slave at 0x43
// Pan stepper:  PUL→D13, DIR→D12, ENA→D11
// Arm L298N:    IN1→D6 (OUT1, MOTOR+), IN2→D7 (OUT2, MOTOR-)
//   CCW (toward B) = IN1 active, IN2=0
//   CW  (toward A) = IN2 active, IN1=0
//   ENA tied HIGH; never raise both INs simultaneously

// ── Hardware constants ─────────────────────────────────────
#define MICROSTEPPING   16
#define MOTOR_STEPS_REV 200
#define GEAR_RATIO      1.0f
#define STEPS_PER_DEG   (MOTOR_STEPS_REV * MICROSTEPPING * GEAR_RATIO / 360.0f)
#define MAX_SPEED_DEG   60.0f

// L298N arm driver
#define PIN_IN1  6   // OUT1, MOTOR+
#define PIN_IN2  7   // OUT2, MOTOR-
#define ARM_PWM_SPEED 200  // 0–255; tune for desired speed

// Default travel time A↔B in ms — tune to your physical arm length
#define DEFAULT_ARM_DUR_MS  2000

// ── I2C ────────────────────────────────────────────────────
#define I2C_ADDRESS    0x43

#define REG_PAN_POS_HI 0x00  // pan position high byte (read)
#define REG_PAN_POS_LO 0x01  // pan position low byte (read)
#define REG_ARM_STATE  0x02  // arm state: 0=at_A, 1=at_B, 2=moving (read)
#define REG_STATUS     0x03  // bit0=pan busy, bit1=arm busy (read)
#define REG_CMD        0x10  // pan: STOP=0x01, HOME=0x02 (write)
#define REG_SET_PAN_HI 0x11  // pan target high byte (write)
#define REG_SET_PAN_LO 0x12  // pan target low byte (write, with 0x11)
#define REG_ARM_CMD    0x13  // arm: GOTO_A=0x01, GOTO_B=0x02, STOP=0x03 (write)
#define REG_ARM_DUR_HI 0x14  // arm travel duration ms high byte (write)
#define REG_ARM_DUR_LO 0x15  // arm travel duration ms low byte (write, with 0x14)

#define CMD_PAN_STOP   0x01
#define CMD_PAN_HOME   0x02
#define CMD_ARM_GOTO_A 0x01
#define CMD_ARM_GOTO_B 0x02
#define CMD_ARM_STOP   0x03

#define ARM_AT_A   0
#define ARM_AT_B   1
#define ARM_MOVING 2

// ── Stepper ────────────────────────────────────────────────
struct Stepper {
  uint8_t pin_step, pin_dir, pin_ena;

  void begin() {
    pinMode(pin_step, OUTPUT);
    pinMode(pin_dir,  OUTPUT);
    pinMode(pin_ena,  OUTPUT);
    digitalWrite(pin_step, LOW);
    digitalWrite(pin_dir,  LOW);
    digitalWrite(pin_ena,  LOW);  // active-low: LOW = enabled
  }

  void step(long steps, float speed_deg) {
    if (steps == 0) return;
    digitalWrite(pin_dir, steps > 0 ? HIGH : LOW);
    delayMicroseconds(5);
    float steps_per_sec = speed_deg * STEPS_PER_DEG;
    long  delay_us      = (long)(500000.0f / steps_per_sec);
    long  n             = abs(steps);
    for (long i = 0; i < n; i++) {
      digitalWrite(pin_step, HIGH); delayMicroseconds(delay_us);
      digitalWrite(pin_step, LOW);  delayMicroseconds(delay_us);
    }
  }
};

// ── Arm (L298N DC motor) ────────────────────────────────────

void armBegin() {
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  analogWrite(PIN_IN1, 0);
  analogWrite(PIN_IN2, 0);
}

void armCoast() {
  analogWrite(PIN_IN1, 0);
  analogWrite(PIN_IN2, 0);
}

void armDriveCCW() {               // toward B: zero IN2, dead time, then ramp IN1
  analogWrite(PIN_IN2, 0);
  delay(5);
  analogWrite(PIN_IN1, ARM_PWM_SPEED);
}

void armDriveCW() {                // toward A: zero IN1, dead time, then ramp IN2
  analogWrite(PIN_IN1, 0);
  delay(5);
  analogWrite(PIN_IN2, ARM_PWM_SPEED);
}

// ── Instances ──────────────────────────────────────────────
Stepper pan = { 13, 12, 11 };

// ── State ──────────────────────────────────────────────────
volatile int16_t pan_pos          = 0;
volatile int16_t pan_target       = 0;
volatile uint8_t status_reg       = 0;
volatile uint8_t selected_reg     = 0;
volatile bool    pending_pan_stop = false;
volatile bool    pending_pan_home = false;

uint8_t       arm_state    = ARM_AT_A;
uint8_t       arm_target   = ARM_AT_A;  // destination of current move
volatile uint8_t  arm_cmd_next = 0;
uint16_t      arm_dur_ms   = DEFAULT_ARM_DUR_MS;
unsigned long arm_start_ms = 0;

// ── Register access (shared by I2C and serial) ─────────────

uint8_t processRead(uint8_t reg) {
  switch (reg) {
    case REG_PAN_POS_HI: return (uint8_t)(pan_pos >> 8);
    case REG_PAN_POS_LO: return (uint8_t)(pan_pos & 0xFF);
    case REG_ARM_STATE:  return arm_state;
    case REG_STATUS:     return status_reg;
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
      arm_cmd_next = data[0];
      break;
    case REG_ARM_DUR_HI:
      if (len >= 2) arm_dur_ms = (uint16_t)((data[0] << 8) | data[1]);
      break;
  }
}

// ── I2C callbacks ──────────────────────────────────────────

void onReceive(int numBytes) {
  if (numBytes < 1) return;
  selected_reg = Wire.read();
  if (numBytes > 1) {
    uint8_t data[8];
    uint8_t len = 0;
    while (Wire.available() && len < (uint8_t)sizeof(data)) data[len++] = Wire.read();
    processWrite(selected_reg, data, len);
  }
}

void onRequest() {
  Wire.write(processRead(selected_reg));
}

// ── Serial handler ─────────────────────────────────────────
// Protocol: "R HH\n" → "!HH\n"  |  "W HH DD...\n" → "!OK\n"

void handleSerial() {
  static char    buf[48];
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
          char*   p   = buf + 2;
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

// ── Arm state machine (non-blocking) ──────────────────────

void updateArm() {
  if (arm_cmd_next) {
    uint8_t cmd  = arm_cmd_next;
    arm_cmd_next = 0;

    if (cmd == CMD_ARM_GOTO_B) {
      armDriveCCW();
      arm_target   = ARM_AT_B;
      arm_state    = ARM_MOVING;
      arm_start_ms = millis();
      status_reg  |= 0x02;
      Serial.print("[arm] -> B (CCW, "); Serial.print(arm_dur_ms); Serial.println("ms)");
    } else if (cmd == CMD_ARM_GOTO_A) {
      armDriveCW();
      arm_target   = ARM_AT_A;
      arm_state    = ARM_MOVING;
      arm_start_ms = millis();
      status_reg  |= 0x02;
      Serial.print("[arm] -> A (CW, "); Serial.print(arm_dur_ms); Serial.println("ms)");
    } else if (cmd == CMD_ARM_STOP) {
      armCoast();
      arm_state   = ARM_AT_A;  // treat as homed after forced stop
      status_reg &= ~0x02;
      Serial.println("[arm] stop");
    }
  }

  if (arm_state == ARM_MOVING && millis() - arm_start_ms >= arm_dur_ms) {
    armCoast();
    arm_state   = arm_target;
    status_reg &= ~0x02;
    Serial.print("[arm] at "); Serial.println(arm_target == ARM_AT_B ? "B" : "A");
  }
}

// ── Setup / Loop ───────────────────────────────────────────

void setup() {
  pan.begin();
  armBegin();

  Serial.begin(115200);
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
  Serial.println("[plater] ready (I2C 0x43 | serial 115200)");
  Serial.print("[arm] dur="); Serial.print(arm_dur_ms); Serial.println("ms, at A");
}

void loop() {
  handleSerial();
  updateArm();

  if (pending_pan_stop) {
    pending_pan_stop = false;
    pan_target       = pan_pos;
    Serial.println("[pan] stop");
  }

  if (pending_pan_home) {
    pending_pan_home = false;
    pan_target       = 0;
    Serial.println("[pan] home");
  }

  if (pan_pos != pan_target) {
    status_reg |= 0x01;
    long steps  = pan_target - pan_pos;
    pan.step(steps, MAX_SPEED_DEG);
    pan_pos     = pan_target;
    status_reg &= ~0x01;
    Serial.print("[pan] pos="); Serial.println(pan_pos);
  }
}
