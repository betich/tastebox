#include <Arduino.h>
#include <Wire.h>

// Motor 1: PUL→D7,  DIR→D8,  ENA→D4  — Joystick Y (A1)
// Motor 2: PUL→D13, DIR→D12, ENA→D11 — Joystick X (A0)
//
// Control strategy: set CONTROL_MODE below.
//   MODE_JOYSTICK — stand-alone joystick control
//   MODE_I2C      — I2C slave at I2C_ADDRESS (0x43), register-based

#define MODE_JOYSTICK 0
#define MODE_I2C      1
#define CONTROL_MODE  MODE_I2C   // ← change here to switch strategy

// ── Hardware constants ─────────────────────────────────────
#define MICROSTEPPING     16
#define MOTOR_STEPS_REV   200
#define GEAR_RATIO        1.0f
#define STEPS_PER_DEG     (MOTOR_STEPS_REV * MICROSTEPPING * GEAR_RATIO / 360.0f)

#define JOY_CENTER    512
#define JOY_DEADZONE   30
#define MAX_SPEED_DEG  60.0f
#define LOOP_MS        50

// ── I2C constants ──────────────────────────────────────────
#define I2C_ADDRESS   0x43

// Registers
#define REG_M1_POS_HI 0x00
#define REG_M1_POS_LO 0x01
#define REG_M2_POS_HI 0x02
#define REG_M2_POS_LO 0x03
#define REG_STATUS    0x04  // bit0=M1_busy, bit1=M2_busy
#define REG_CMD       0x10  // 0x01=stop, 0x02=home
#define REG_SET_M1_HI 0x11  // int16 motor 1 target (hi, lo)
#define REG_SET_M2_HI 0x13  // int16 motor 2 target (hi, lo)

// Commands
#define CMD_STOP 0x01
#define CMD_HOME 0x02

// ── Stepper ────────────────────────────────────────────────
struct Stepper {
  uint8_t pin_step, pin_dir, pin_ena;
  bool ena_active_high;

  void begin() {
    pinMode(pin_step, OUTPUT);
    pinMode(pin_dir,  OUTPUT);
    pinMode(pin_ena,  OUTPUT);
    digitalWrite(pin_step, LOW);
    digitalWrite(pin_dir,  LOW);
    digitalWrite(pin_ena,  ena_active_high ? HIGH : LOW);
  }

  void step(long steps, float speed_deg) {
    if (steps == 0) return;
    digitalWrite(pin_dir, steps > 0 ? HIGH : LOW);
    delayMicroseconds(5);
    float steps_per_sec = speed_deg * STEPS_PER_DEG;
    long delay_us = (long)(500000.0f / steps_per_sec);
    long n = abs(steps);
    for (long i = 0; i < n; i++) {
      digitalWrite(pin_step, HIGH);
      delayMicroseconds(delay_us);
      digitalWrite(pin_step, LOW);
      delayMicroseconds(delay_us);
    }
  }
};

// ── JoyAxis ────────────────────────────────────────────────
struct JoyAxis {
  uint8_t pin;

  float readNorm() {
    int raw = analogRead(pin);
    int deflection = raw - JOY_CENTER;
    if (abs(deflection) <= JOY_DEADZONE) return 0.0f;
    float norm = (float)(deflection - (deflection > 0 ? JOY_DEADZONE : -JOY_DEADZONE))
                 / (float)(512 - JOY_DEADZONE);
    return constrain(norm, -1.0f, 1.0f);
  }

  int readRaw() { return analogRead(pin); }
};

// ── Instances ──────────────────────────────────────────────
Stepper motor1 = { 7,  8,  4,  false };
Stepper motor2 = { 13, 12, 11, false };
JoyAxis joyY   = { A1 };
JoyAxis joyX   = { A0 };

// ── Strategy: Joystick ─────────────────────────────────────
#if CONTROL_MODE == MODE_JOYSTICK

void strategySetup() {
  Serial.println("[MODE] Joystick control");
}

void strategyLoop() {
  auto drive = [](Stepper& motor, JoyAxis& axis) {
    float norm = axis.readNorm();
    if (norm == 0.0f) return;
    float delta_deg = norm * MAX_SPEED_DEG * (LOOP_MS / 1000.0f);
    long steps = (long)(delta_deg * STEPS_PER_DEG);
    motor.step(steps, MAX_SPEED_DEG);
  };

  drive(motor1, joyY);
  drive(motor2, joyX);

  Serial.print("Y: "); Serial.print(joyY.readRaw());
  Serial.print("\tX: "); Serial.println(joyX.readRaw());

  delay(LOOP_MS);
}

// ── Strategy: I2C ─────────────────────────────────────────
#elif CONTROL_MODE == MODE_I2C

volatile int16_t m1_pos     = 0, m2_pos     = 0;
volatile int16_t m1_target  = 0, m2_target  = 0;
volatile uint8_t i2c_status = 0;
volatile uint8_t selected_reg = 0;
volatile bool    pending_stop = false;
volatile bool    pending_home = false;

void onReceive(int numBytes) {
  if (numBytes < 1) return;
  selected_reg = Wire.read();

  if (numBytes > 1) {
    uint8_t hi = Wire.read();
    if (selected_reg == REG_CMD) {
      if (hi == CMD_STOP) pending_stop = true;
      else if (hi == CMD_HOME) pending_home = true;
    } else if ((selected_reg == REG_SET_M1_HI || selected_reg == REG_SET_M2_HI) && numBytes >= 3) {
      uint8_t lo = Wire.read();
      int16_t val = (int16_t)((hi << 8) | lo);
      if (selected_reg == REG_SET_M1_HI) m1_target = val;
      else                               m2_target = val;
    }
  }
}

void onRequest() {
  switch (selected_reg) {
    case REG_M1_POS_HI: Wire.write((uint8_t)(m1_pos >> 8));    break;
    case REG_M1_POS_LO: Wire.write((uint8_t)(m1_pos & 0xFF)); break;
    case REG_M2_POS_HI: Wire.write((uint8_t)(m2_pos >> 8));    break;
    case REG_M2_POS_LO: Wire.write((uint8_t)(m2_pos & 0xFF)); break;
    case REG_STATUS:    Wire.write(i2c_status);                break;
    default:            Wire.write(0xFF);
  }
}

void strategySetup() {
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
  Serial.print("[MODE] I2C slave at 0x");
  Serial.println(I2C_ADDRESS, HEX);
}

void strategyLoop() {
  if (pending_stop) {
    pending_stop = false;
    m1_target = m1_pos;
    m2_target = m2_pos;
    Serial.println("[CMD] stop");
  }

  if (pending_home) {
    pending_home = false;
    m1_target = 0;
    m2_target = 0;
    Serial.println("[CMD] home");
  }

  if (m1_pos != m1_target) {
    i2c_status |= 0x01;
    long steps = m1_target - m1_pos;
    motor1.step(steps, MAX_SPEED_DEG);
    m1_pos = m1_target;
    i2c_status &= ~0x01;
    Serial.print("[M1] pos="); Serial.println(m1_pos);
  }

  if (m2_pos != m2_target) {
    i2c_status |= 0x02;
    long steps = m2_target - m2_pos;
    motor2.step(steps, MAX_SPEED_DEG);
    m2_pos = m2_target;
    i2c_status &= ~0x02;
    Serial.print("[M2] pos="); Serial.println(m2_pos);
  }
}

#endif  // CONTROL_MODE

// ── Setup / Loop ───────────────────────────────────────────
void setup() {
  motor1.begin();
  motor2.begin();
  Serial.begin(115200);
  strategySetup();
}

void loop() {
  strategyLoop();
}
