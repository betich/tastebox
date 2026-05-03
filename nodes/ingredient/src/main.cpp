#include <Arduino.h>
#include <Wire.h>

// Ingredient stepper node — I2C slave at 0x44
// Motor 1: PUL→D13, DIR→D12, ENA→D11
// Motor 2: PUL→D2,  DIR→D3,  ENA→D4
//
// Commands:
//   0x01 STOP        — stop immediately
//   0x02 FWD_CONT    — non-stop forward (until STOP)
//   0x03 BWD_CONT    — non-stop backward (until STOP)
//   0x04 FWD_BURST   — forward for duration_ms then auto-stop
//   0x05 BWD_BURST   — backward for duration_ms then auto-stop

// ── Hardware pins ──────────────────────────────────────────
#define PIN_PUL   13
#define PIN_DIR   12
#define PIN_ENA   11

#define PIN_PUL2  2
#define PIN_DIR2  3
#define PIN_ENA2  4

#define STEP_HALF_US  1250   // ~400 Hz step rate

// ── I2C / register map ─────────────────────────────────────
#define I2C_ADDRESS     0x44

#define REG_STATUS      0x00  // bit0=busy, bit1=dir(0=fwd/1=bwd) (read)
#define REG_REMAIN_HI   0x01  // remaining ms high byte (burst only, read)
#define REG_REMAIN_LO   0x02  // remaining ms low byte (read)
#define REG_CMD         0x10  // command (write)
#define REG_SET_DUR_HI  0x11  // burst duration ms high byte (write)
#define REG_SET_DUR_LO  0x12  // burst duration ms low byte  (write, sent with 0x11)

#define CMD_STOP       0x01
#define CMD_FWD_CONT   0x02
#define CMD_BWD_CONT   0x03
#define CMD_FWD_BURST  0x04
#define CMD_BWD_BURST  0x05

// ── State ──────────────────────────────────────────────────
enum RunMode : uint8_t { IDLE, CONTINUOUS, BURST };

volatile uint8_t  pending_cmd  = 0;
volatile uint16_t set_duration = 5000;   // default burst: 5 s
volatile uint8_t  selected_reg = 0;

uint8_t       i2c_status = 0;
RunMode       run_mode   = IDLE;
uint16_t      duration_ms = 5000;
uint16_t      remain_ms   = 0;
unsigned long start_ms    = 0;

// ── Motor helpers ──────────────────────────────────────────
void enable()  { digitalWrite(PIN_ENA, LOW);  digitalWrite(PIN_ENA2, LOW);  }
void disable() { digitalWrite(PIN_ENA, HIGH); digitalWrite(PIN_ENA2, HIGH); }

void setDir(bool reverse) {
  digitalWrite(PIN_DIR,  reverse ? LOW  : HIGH);  // Stepper B
  digitalWrite(PIN_DIR2, reverse ? HIGH : LOW);   // Stepper A (dir inverted)
  delayMicroseconds(5);
}

void stepBoth() {
  digitalWrite(PIN_PUL,  HIGH); digitalWrite(PIN_PUL2,  HIGH);
  delayMicroseconds(STEP_HALF_US);
  digitalWrite(PIN_PUL,  LOW);  digitalWrite(PIN_PUL2,  LOW);
  delayMicroseconds(STEP_HALF_US);
}

// ── Command execution ──────────────────────────────────────
void startContinuous(bool reverse) {
  run_mode   = CONTINUOUS;
  remain_ms  = 0;
  i2c_status = 0x01 | (reverse ? 0x02 : 0x00);
  setDir(reverse);
  enable();
  Serial.println(reverse ? "[CMD] bwd continuous" : "[CMD] fwd continuous");
}

void startBurst(bool reverse) {
  run_mode    = BURST;
  duration_ms = set_duration;
  start_ms    = millis();
  remain_ms   = duration_ms;
  i2c_status  = 0x01 | (reverse ? 0x02 : 0x00);
  setDir(reverse);
  enable();
  Serial.print(reverse ? "[CMD] bwd burst " : "[CMD] fwd burst ");
  Serial.print(duration_ms); Serial.println(" ms");
}

void stopRun() {
  disable();
  run_mode   = IDLE;
  i2c_status = 0;
  remain_ms  = 0;
  Serial.println("[CMD] stop");
}

// ── Register access ────────────────────────────────────────
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
// Protocol: "R HH\n" → "!HH\n"  |  "W HH DD...\n" → "!OK\n"  |  "help\n"
void printHelp() {
  Serial.println("── ingredient node (0x44) ───────────────");
  Serial.println("Commands:");
  Serial.println("  W 10 01        STOP");
  Serial.println("  W 10 02        FWD_CONT  (non-stop forward)");
  Serial.println("  W 10 03        BWD_CONT  (non-stop backward)");
  Serial.println("  W 10 04        FWD_BURST (forward for duration_ms)");
  Serial.println("  W 10 05        BWD_BURST (backward for duration_ms)");
  Serial.println("  W 11 HH LL     set burst duration (ms, uint16 hi lo)");
  Serial.println("Registers (read):");
  Serial.println("  R 00           status  (bit0=busy bit1=bwd)");
  Serial.println("  R 01           remain_ms high byte");
  Serial.println("  R 02           remain_ms low byte");
  Serial.println("Pins:");
  Serial.println("  M1  PUL=D13  DIR=D12  ENA=D11");
  Serial.println("  M2  PUL=D2   DIR=D3   ENA=D4");
  Serial.println("─────────────────────────────────────────");
}

void handleSerial() {
  static char buf[48];
  static uint8_t idx = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (idx > 0) {
        buf[idx] = '\0';
        idx = 0;
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
          uint8_t data[8];
          uint8_t len = 0;
          while (*p && len < (uint8_t)sizeof(data)) {
            while (*p == ' ') p++;
            if (*p) data[len++] = (uint8_t)strtoul(p, &p, 16);
          }
          processWrite(reg, data, len);
          Serial.println("!OK");
        } else {
          Serial.println("?  (type 'help')");
        }
      }
    } else if (idx < (uint8_t)sizeof(buf) - 1) {
      buf[idx++] = c;
    }
  }
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

  if (pending_cmd) {
    uint8_t cmd = pending_cmd;
    pending_cmd = 0;
    switch (cmd) {
      case CMD_STOP:      stopRun();              break;
      case CMD_FWD_CONT:  startContinuous(false); break;
      case CMD_BWD_CONT:  startContinuous(true);  break;
      case CMD_FWD_BURST: startBurst(false);      break;
      case CMD_BWD_BURST: startBurst(true);       break;
    }
  }

  switch (run_mode) {
    case CONTINUOUS:
      stepBoth();
      break;
    case BURST:
      if (millis() - start_ms >= duration_ms) {
        stopRun();
      } else {
        remain_ms = (uint16_t)(duration_ms - (millis() - start_ms));
        stepBoth();
      }
      break;
    case IDLE:
      break;
  }
}
