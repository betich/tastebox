#include <Arduino.h>
#include <Wire.h>
#include <Servo.h>

// Cutter & lid opener node — I2C slave at 0x45
// Lid DC motor (servo signal): D3
// Piston 1 (digital):          D5
// Piston 2 (digital):          D7
// Servo 1 (5V):                D9
// Servo 2 (5V):                D11

// ── Hardware pins ──────────────────────────────────────────
#define PIN_LID       3
#define PIN_PISTON1   5
#define PIN_PISTON2   7
#define PIN_SERVO1    9
#define PIN_SERVO2    11

#define LID_OPEN_ANGLE   90
#define LID_CLOSE_ANGLE   0

// ── I2C ────────────────────────────────────────────────────
#define I2C_ADDRESS       0x45

#define REG_STATUS        0x00  // bit0=lid_busy, bit1=p1_busy, bit2=p2_busy (read)
#define REG_CMD           0x10  // commands (write)
#define REG_SERVO1_ANGLE  0x11  // servo1 angle 0-180 (write)
#define REG_SERVO2_ANGLE  0x12  // servo2 angle 0-180 (write)
#define REG_LID_DUR_HI    0x13  // lid duration ms high byte (write)
#define REG_LID_DUR_LO    0x14  // lid duration ms low byte (write, sent with 0x13)
#define REG_PST_DUR_HI    0x15  // piston duration ms high byte (write)
#define REG_PST_DUR_LO    0x16  // piston duration ms low byte (write, sent with 0x15)

#define CMD_STOP_ALL    0x01
#define CMD_OPEN_LID    0x02
#define CMD_CLOSE_LID   0x03
#define CMD_PISTON1_EXT 0x04
#define CMD_PISTON1_RET 0x05
#define CMD_PISTON2_EXT 0x06
#define CMD_PISTON2_RET 0x07

// ── State ──────────────────────────────────────────────────
volatile uint8_t  pending_cmd  = 0;
volatile uint16_t lid_dur      = 1000;
volatile uint16_t pst_dur      = 1000;
volatile uint8_t  selected_reg = 0;

uint8_t       i2c_status  = 0;

unsigned long lid_start   = 0;
bool          lid_running = false;

unsigned long p1_start    = 0;
bool          p1_running  = false;

unsigned long p2_start    = 0;
bool          p2_running  = false;

Servo lid_servo, servo1, servo2;

void onReceive(int numBytes) {
  if (numBytes < 1) return;
  selected_reg = Wire.read();

  if (numBytes > 1) {
    uint8_t hi = Wire.read();
    switch (selected_reg) {
      case REG_CMD:
        pending_cmd = hi;
        break;
      case REG_SERVO1_ANGLE:
        servo1.write((hi > 180) ? 180 : hi);
        break;
      case REG_SERVO2_ANGLE:
        servo2.write((hi > 180) ? 180 : hi);
        break;
      case REG_LID_DUR_HI:
        if (numBytes >= 3) lid_dur = (uint16_t)((hi << 8) | Wire.read());
        break;
      case REG_PST_DUR_HI:
        if (numBytes >= 3) pst_dur = (uint16_t)((hi << 8) | Wire.read());
        break;
    }
  }
}

void onRequest() {
  switch (selected_reg) {
    case REG_STATUS: Wire.write(i2c_status); break;
    default:         Wire.write(0xFF);
  }
}

// ── Setup / Loop ───────────────────────────────────────────
void setup() {
  lid_servo.attach(PIN_LID);
  servo1.attach(PIN_SERVO1);
  servo2.attach(PIN_SERVO2);
  lid_servo.write(LID_CLOSE_ANGLE);
  servo1.write(90);
  servo2.write(90);

  pinMode(PIN_PISTON1, OUTPUT);
  pinMode(PIN_PISTON2, OUTPUT);
  digitalWrite(PIN_PISTON1, LOW);
  digitalWrite(PIN_PISTON2, LOW);

  Serial.begin(115200);
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
  Serial.print("[cutter] I2C slave at 0x");
  Serial.println(I2C_ADDRESS, HEX);
}

void loop() {
  // Handle pending command
  if (pending_cmd) {
    uint8_t cmd = pending_cmd;
    pending_cmd = 0;

    switch (cmd) {
      case CMD_STOP_ALL:
        lid_servo.write(LID_CLOSE_ANGLE);
        digitalWrite(PIN_PISTON1, LOW);
        digitalWrite(PIN_PISTON2, LOW);
        lid_running = p1_running = p2_running = false;
        i2c_status  = 0;
        Serial.println("[CMD] stop_all");
        break;

      case CMD_OPEN_LID:
        lid_servo.write(LID_OPEN_ANGLE);
        lid_start   = millis();
        lid_running = true;
        i2c_status |= 0x01;
        Serial.println("[CMD] open_lid");
        break;

      case CMD_CLOSE_LID:
        lid_servo.write(LID_CLOSE_ANGLE);
        lid_start   = millis();
        lid_running = true;
        i2c_status |= 0x01;
        Serial.println("[CMD] close_lid");
        break;

      case CMD_PISTON1_EXT:
        digitalWrite(PIN_PISTON1, HIGH);
        p1_start   = millis();
        p1_running = true;
        i2c_status |= 0x02;
        Serial.println("[CMD] piston1_ext");
        break;

      case CMD_PISTON1_RET:
        digitalWrite(PIN_PISTON1, LOW);
        p1_start   = millis();
        p1_running = true;
        i2c_status |= 0x02;
        Serial.println("[CMD] piston1_ret");
        break;

      case CMD_PISTON2_EXT:
        digitalWrite(PIN_PISTON2, HIGH);
        p2_start   = millis();
        p2_running = true;
        i2c_status |= 0x04;
        Serial.println("[CMD] piston2_ext");
        break;

      case CMD_PISTON2_RET:
        digitalWrite(PIN_PISTON2, LOW);
        p2_start   = millis();
        p2_running = true;
        i2c_status |= 0x04;
        Serial.println("[CMD] piston2_ret");
        break;
    }
  }

  // Non-blocking timeout checks
  if (lid_running && millis() - lid_start >= lid_dur) {
    lid_running  = false;
    i2c_status  &= ~0x01;
  }
  if (p1_running && millis() - p1_start >= pst_dur) {
    p1_running   = false;
    digitalWrite(PIN_PISTON1, LOW);
    i2c_status  &= ~0x02;
  }
  if (p2_running && millis() - p2_start >= pst_dur) {
    p2_running   = false;
    digitalWrite(PIN_PISTON2, LOW);
    i2c_status  &= ~0x04;
  }
}
