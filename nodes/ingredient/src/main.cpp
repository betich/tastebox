#include <Arduino.h>
#include <Wire.h>

// Ingredient coil stepper node — I2C slave at 0x44
// PUL→D7, DIR→D8, ENA→D4
// Time-based A/B dispense: run for duration_ms, then stop.

// ── Hardware pins ──────────────────────────────────────────
#define PIN_PUL   7
#define PIN_DIR   8
#define PIN_ENA   4

#define PIN_PUL2  9
#define PIN_DIR2  10
#define PIN_ENA2  11

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

void enable()  { digitalWrite(PIN_ENA, LOW);  digitalWrite(PIN_ENA2, LOW);  }  // active-low
void disable() { digitalWrite(PIN_ENA, HIGH); digitalWrite(PIN_ENA2, HIGH); }

// ── Register access (shared by I2C and serial) ─────────────

uint8_t processRead(uint8_t reg) {
  switch (reg) {
    case REG_STATUS:    return i2c_status;
    case REG_REMAIN_HI: return (uint8_t)(remain_ms >> 8);
    case REG_REMAIN_LO: return (uint8_t)(remain_ms & 0xFF);
    default:            return 0xFF;
  }
}

void processWrite(uint8_t reg, uint8_t* data, uint8_t len) {
  if (len < 1) return;
  if (reg == REG_CMD) {
    pending_cmd = data[0];
  } else if (reg == REG_SET_DUR_HI && len >= 2) {
    set_duration = (uint16_t)((data[0] << 8) | data[1]);
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

void startRun(bool reverse) {
  duration_ms  = set_duration;
  start_ms     = millis();
  remain_ms    = duration_ms;
  i2c_status   = 0x01 | (reverse ? 0x02 : 0x00);  // busy + direction
  digitalWrite(PIN_DIR,  reverse ? LOW : HIGH);
  digitalWrite(PIN_DIR2, reverse ? LOW : HIGH);
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
  pinMode(PIN_PUL,  OUTPUT); pinMode(PIN_PUL2,  OUTPUT);
  pinMode(PIN_DIR,  OUTPUT); pinMode(PIN_DIR2,  OUTPUT);
  pinMode(PIN_ENA,  OUTPUT); pinMode(PIN_ENA2,  OUTPUT);
  digitalWrite(PIN_PUL,  LOW); digitalWrite(PIN_PUL2,  LOW);
  digitalWrite(PIN_DIR,  LOW); digitalWrite(PIN_DIR2,  LOW);
  disable();

  Serial.begin(115200);
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
  Serial.println("[ingredient] ready (I2C 0x44 | serial 115200)");
}

void loop() {
  handleSerial();

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
      // Pulse both steppers at ~400 Hz
      digitalWrite(PIN_PUL,  HIGH); digitalWrite(PIN_PUL2,  HIGH);
      delayMicroseconds(STEP_HALF_US);
      digitalWrite(PIN_PUL,  LOW);  digitalWrite(PIN_PUL2,  LOW);
      delayMicroseconds(STEP_HALF_US);
    }
  }
}
