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

// ── Register access (shared by I2C and serial) ─────────────

uint8_t processRead(uint8_t reg) {
  switch (reg) {
    case REG_STATUS: return i2c_status;
    default:         return 0xFF;
  }
}

void processWrite(uint8_t reg, uint8_t* data, uint8_t len) {
  if (len < 1) return;
  switch (reg) {
    case REG_CMD:
      pending_cmd = data[0];
      break;
    case REG_SERVO1_ANGLE:
      servo1.write((data[0] > 180) ? 180 : data[0]);
      break;
    case REG_SERVO2_ANGLE:
      servo2.write((data[0] > 180) ? 180 : data[0]);
      break;
    case REG_LID_DUR_HI:
      if (len >= 2) lid_dur = (uint16_t)((data[0] << 8) | data[1]);
      break;
    case REG_PST_DUR_HI:
      if (len >= 2) pst_dur = (uint16_t)((data[0] << 8) | data[1]);
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
  Serial.println("[cutter] ready (I2C 0x45 | serial 115200)");
}

void loop() {
  handleSerial();

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
