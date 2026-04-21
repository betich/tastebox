#include <Arduino.h>
#include <Wire.h>

// Ingredient coil stepper node — I2C slave at 0x44
// PUL→D7, DIR→D8, ENA→D4
// Time-based A/B dispense: run for duration_ms, then stop.

// ── Hardware pins ──────────────────────────────────────────
#define PIN_PUL   7
#define PIN_DIR   8
#define PIN_ENA   4

// Step frequency ~400 Hz → 1250 us per half-period
#define STEP_HALF_US  1250

// ── I2C ────────────────────────────────────────────────────
#define I2C_ADDRESS     0x44

#define REG_STATUS      0x00  // bit0=busy, bit1=direction(0=fwd/1=rev) (read)
#define REG_REMAIN_HI   0x01  // remaining duration ms high byte (read)
#define REG_REMAIN_LO   0x02  // remaining duration ms low byte (read)
#define REG_CMD         0x10  // commands (write)
#define REG_SET_DUR_HI  0x11  // duration ms high byte (write)
#define REG_SET_DUR_LO  0x12  // duration ms low byte (write, sent with 0x11)

#define CMD_STOP      0x01
#define CMD_DISPENSE  0x02
#define CMD_RETRACT   0x03

// ── State ──────────────────────────────────────────────────
volatile uint8_t  pending_cmd   = 0;
volatile uint16_t set_duration  = 10000;  // default 10 s
volatile uint8_t  selected_reg  = 0;

uint8_t  i2c_status  = 0;   // bit0=busy, bit1=direction
uint16_t duration_ms = 10000;
uint16_t remain_ms   = 0;
unsigned long start_ms = 0;

void enable()  { digitalWrite(PIN_ENA, LOW); }   // active-low
void disable() { digitalWrite(PIN_ENA, HIGH); }

void onReceive(int numBytes) {
  if (numBytes < 1) return;
  selected_reg = Wire.read();

  if (numBytes > 1) {
    uint8_t hi = Wire.read();
    if (selected_reg == REG_CMD) {
      pending_cmd = hi;
    } else if (selected_reg == REG_SET_DUR_HI && numBytes >= 3) {
      uint8_t lo   = Wire.read();
      set_duration = (uint16_t)((hi << 8) | lo);
    }
  }
}

void onRequest() {
  switch (selected_reg) {
    case REG_STATUS:    Wire.write(i2c_status);             break;
    case REG_REMAIN_HI: Wire.write((uint8_t)(remain_ms >> 8));   break;
    case REG_REMAIN_LO: Wire.write((uint8_t)(remain_ms & 0xFF)); break;
    default:            Wire.write(0xFF);
  }
}

void startRun(bool reverse) {
  duration_ms  = set_duration;
  start_ms     = millis();
  remain_ms    = duration_ms;
  i2c_status   = 0x01 | (reverse ? 0x02 : 0x00);  // busy + direction
  digitalWrite(PIN_DIR, reverse ? LOW : HIGH);
  delayMicroseconds(5);
  enable();
  Serial.print(reverse ? "[CMD] retract " : "[CMD] dispense ");
  Serial.print(duration_ms); Serial.println(" ms");
}

void stopRun() {
  disable();
  i2c_status = 0;
  remain_ms  = 0;
  Serial.println("[CMD] stop");
}

// ── Setup / Loop ───────────────────────────────────────────
void setup() {
  pinMode(PIN_PUL, OUTPUT);
  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_ENA, OUTPUT);
  digitalWrite(PIN_PUL, LOW);
  digitalWrite(PIN_DIR, LOW);
  disable();

  Serial.begin(115200);
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
  Serial.print("[ingredient] I2C slave at 0x");
  Serial.println(I2C_ADDRESS, HEX);
}

void loop() {
  // Handle pending commands
  if (pending_cmd) {
    uint8_t cmd = pending_cmd;
    pending_cmd = 0;
    if      (cmd == CMD_STOP)     stopRun();
    else if (cmd == CMD_DISPENSE) startRun(false);
    else if (cmd == CMD_RETRACT)  startRun(true);
  }

  // Step if busy
  if (i2c_status & 0x01) {
    unsigned long elapsed = millis() - start_ms;
    if (elapsed >= duration_ms) {
      stopRun();
    } else {
      remain_ms = (uint16_t)(duration_ms - elapsed);
      // Pulse stepper at ~400 Hz
      digitalWrite(PIN_PUL, HIGH);
      delayMicroseconds(STEP_HALF_US);
      digitalWrite(PIN_PUL, LOW);
      delayMicroseconds(STEP_HALF_US);
    }
  }
}
