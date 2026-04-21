#include <Arduino.h>
#include <Wire.h>
#include <Servo.h>

// Plater node — I2C slave at 0x43
// Motor (pan): PUL→D13, DIR→D12, ENA→D11
// Servo (arm): D3

// ── Hardware constants ─────────────────────────────────────
#define MICROSTEPPING     16
#define MOTOR_STEPS_REV   200
#define GEAR_RATIO        1.0f
#define STEPS_PER_DEG     (MOTOR_STEPS_REV * MICROSTEPPING * GEAR_RATIO / 360.0f)
#define MAX_SPEED_DEG     60.0f

#define PIN_SERVO_ARM     3

// ── I2C ────────────────────────────────────────────────────
#define I2C_ADDRESS       0x43

#define REG_PAN_POS_HI    0x00  // pan stepper position high byte (read)
#define REG_PAN_POS_LO    0x01  // pan stepper position low byte (read)
#define REG_SERVO_ANGLE   0x02  // servo angle 0-180 (read)
#define REG_STATUS        0x03  // bit0 = stepper busy (read)
#define REG_CMD           0x10  // commands (write)
#define REG_SET_PAN_HI    0x11  // stepper target high byte (write)
#define REG_SET_PAN_LO    0x12  // stepper target low byte (write, sent with 0x11)
#define REG_SET_SERVO     0x13  // servo angle 0-180 (write)

#define CMD_STOP          0x01
#define CMD_HOME          0x02

// ── Stepper ────────────────────────────────────────────────
struct Stepper {
  uint8_t pin_step, pin_dir, pin_ena;

  void begin() {
    pinMode(pin_step, OUTPUT);
    pinMode(pin_dir,  OUTPUT);
    pinMode(pin_ena,  OUTPUT);
    digitalWrite(pin_step, LOW);
    digitalWrite(pin_dir,  LOW);
    digitalWrite(pin_ena,  LOW);  // ENA active-low: LOW = enabled
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

// ── Instances ──────────────────────────────────────────────
Stepper pan = { 13, 12, 11 };
Servo   arm;

// ── I2C state ──────────────────────────────────────────────
volatile int16_t pan_pos      = 0;
volatile int16_t pan_target   = 0;
volatile uint8_t servo_angle  = 90;
volatile uint8_t i2c_status   = 0;
volatile uint8_t selected_reg = 0;
volatile bool    pending_stop = false;
volatile bool    pending_home = false;

void onReceive(int numBytes) {
  if (numBytes < 1) return;
  selected_reg = Wire.read();

  if (numBytes > 1) {
    uint8_t hi = Wire.read();
    if (selected_reg == REG_CMD) {
      if      (hi == CMD_STOP) pending_stop = true;
      else if (hi == CMD_HOME) pending_home = true;
    } else if (selected_reg == REG_SET_PAN_HI && numBytes >= 3) {
      uint8_t lo  = Wire.read();
      pan_target  = (int16_t)((hi << 8) | lo);
    } else if (selected_reg == REG_SET_SERVO) {
      servo_angle = (hi > 180) ? 180 : hi;
      arm.write(servo_angle);
    }
  }
}

void onRequest() {
  switch (selected_reg) {
    case REG_PAN_POS_HI:  Wire.write((uint8_t)(pan_pos >> 8));    break;
    case REG_PAN_POS_LO:  Wire.write((uint8_t)(pan_pos & 0xFF)); break;
    case REG_SERVO_ANGLE: Wire.write(servo_angle);                 break;
    case REG_STATUS:      Wire.write(i2c_status);                  break;
    default:              Wire.write(0xFF);
  }
}

// ── Setup / Loop ───────────────────────────────────────────
void setup() {
  pan.begin();
  arm.attach(PIN_SERVO_ARM);
  arm.write(servo_angle);

  Serial.begin(115200);
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
  Serial.print("[plater] I2C slave at 0x");
  Serial.println(I2C_ADDRESS, HEX);
}

void loop() {
  if (pending_stop) {
    pending_stop = false;
    pan_target   = pan_pos;
    Serial.println("[CMD] stop");
  }

  if (pending_home) {
    pending_home = false;
    pan_target   = 0;
    Serial.println("[CMD] home");
  }

  if (pan_pos != pan_target) {
    i2c_status |= 0x01;
    long steps  = pan_target - pan_pos;
    pan.step(steps, MAX_SPEED_DEG);
    pan_pos = pan_target;
    i2c_status &= ~0x01;
    Serial.print("[pan] pos="); Serial.println(pan_pos);
  }
}
