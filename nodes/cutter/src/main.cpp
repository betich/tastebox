#include <Arduino.h>
#include <RS485Node.h>
#include <Servo.h>

// Cutter node — RS485 node 0x45
// RS485:         RO=D2, DI=D3, DE/RE=D4
// Door servo:    D5  (0°=closed, 90°=open)
// Clamp servo:   D6  (0°=release, 90°=clamp)
// Roller L298N:  IN1=D7, IN2=D8
// Scissor L298N: IN3=D9, IN4=D10
// Pepper servo:  D11 (0°=idle, 180°=dispense)
// Pump servo:    D12 (0°=off, 90°=on)
// Salt servo:    A0  (0°=idle, 180°=dispense)

// ── Pins ───────────────────────────────────────────────────
#define PIN_RS485_RX    2
#define PIN_RS485_TX    3
#define PIN_RS485_DE_RE 4

#define PIN_DOOR    5
#define PIN_CLAMP   6
#define PIN_ROLL_1  7
#define PIN_ROLL_2  8
#define PIN_SCIS_1  9
#define PIN_SCIS_2  10
#define PIN_PEPPER  11
#define PIN_PUMP    12
#define PIN_SALT    A0

// ── Registers ──────────────────────────────────────────────
#define REG_STATUS      0x00
#define REG_DOOR_CMD    0x11
#define REG_CLAMP_CMD   0x12
#define REG_ROLLER_CMD  0x13
#define REG_SCISSOR_CMD 0x14
#define REG_PEPPER_CMD  0x15
#define REG_PUMP_CMD    0x16
#define REG_SALT_CMD    0x17
#define REG_DUR_HI      0x18

// STATUS bits
#define BIT_DOOR    0x01
#define BIT_CLAMP   0x02
#define BIT_ROLLER  0x04
#define BIT_SCISSOR 0x08
#define BIT_PEPPER  0x10
#define BIT_PUMP    0x20
#define BIT_SALT    0x40

// ── State ──────────────────────────────────────────────────
uint8_t  status_reg = 0;
uint16_t op_dur     = 1000;

unsigned long pepper_start = 0;
bool          pepper_timed = false;
unsigned long salt_start   = 0;
bool          salt_timed   = false;

Servo door_srv, clamp_srv, pepper_srv, pump_srv, salt_srv;
RS485Node node(0x45, PIN_RS485_RX, PIN_RS485_TX, PIN_RS485_DE_RE);

// ── Register handlers ──────────────────────────────────────
uint8_t processRead(uint8_t reg) {
  if (reg == REG_STATUS) return status_reg;
  return 0xFF;
}

void processWrite(uint8_t reg, uint8_t* data, uint8_t len) {
  if (len < 1) return;
  switch (reg) {
    case REG_DOOR_CMD:
      if (data[0] == 0x01) { door_srv.write(90);  status_reg |= BIT_DOOR;   Serial.println("[door] open");    }
      else                  { door_srv.write(0);   status_reg &= ~BIT_DOOR;  Serial.println("[door] close");   }
      break;
    case REG_CLAMP_CMD:
      if (data[0] == 0x01) { clamp_srv.write(90); status_reg |= BIT_CLAMP;  Serial.println("[clamp] clamp");  }
      else                  { clamp_srv.write(0);  status_reg &= ~BIT_CLAMP; Serial.println("[clamp] release");}
      break;
    case REG_ROLLER_CMD:
      if      (data[0] == 0x01) { digitalWrite(PIN_ROLL_1, HIGH); digitalWrite(PIN_ROLL_2, LOW);  status_reg |= BIT_ROLLER;  Serial.println("[roller] fwd"); }
      else if (data[0] == 0x02) { digitalWrite(PIN_ROLL_1, LOW);  digitalWrite(PIN_ROLL_2, HIGH); status_reg |= BIT_ROLLER;  Serial.println("[roller] rev"); }
      else                      { digitalWrite(PIN_ROLL_1, LOW);  digitalWrite(PIN_ROLL_2, LOW);  status_reg &= ~BIT_ROLLER; Serial.println("[roller] stop"); }
      break;
    case REG_SCISSOR_CMD:
      if      (data[0] == 0x01) { digitalWrite(PIN_SCIS_1, HIGH); digitalWrite(PIN_SCIS_2, LOW);  status_reg |= BIT_SCISSOR;  Serial.println("[scissor] fwd"); }
      else if (data[0] == 0x02) { digitalWrite(PIN_SCIS_1, LOW);  digitalWrite(PIN_SCIS_2, HIGH); status_reg |= BIT_SCISSOR;  Serial.println("[scissor] rev"); }
      else                      { digitalWrite(PIN_SCIS_1, LOW);  digitalWrite(PIN_SCIS_2, LOW);  status_reg &= ~BIT_SCISSOR; Serial.println("[scissor] stop"); }
      break;
    case REG_PEPPER_CMD:
      if (data[0] == 0x01) { pepper_srv.write(180); pepper_start = millis(); pepper_timed = true;  status_reg |= BIT_PEPPER;  Serial.println("[pepper] dispense"); }
      else                  { pepper_srv.write(0);   pepper_timed = false;                          status_reg &= ~BIT_PEPPER; Serial.println("[pepper] stop");     }
      break;
    case REG_PUMP_CMD:
      if (data[0] == 0x01) { pump_srv.write(90); status_reg |= BIT_PUMP;  Serial.println("[pump] on");  }
      else                  { pump_srv.write(0);  status_reg &= ~BIT_PUMP; Serial.println("[pump] off"); }
      break;
    case REG_SALT_CMD:
      if (data[0] == 0x01) { salt_srv.write(180); salt_start = millis(); salt_timed = true;  status_reg |= BIT_SALT;  Serial.println("[salt] dispense"); }
      else                  { salt_srv.write(0);   salt_timed = false;                        status_reg &= ~BIT_SALT; Serial.println("[salt] stop");     }
      break;
    case REG_DUR_HI:
      if (len >= 2) op_dur = (uint16_t)((data[0] << 8) | data[1]);
      break;
  }
}

// ── Timed dispenser update ──────────────────────────────────
void updateTimers() {
  if (pepper_timed && millis() - pepper_start >= op_dur) {
    pepper_srv.write(0); pepper_timed = false;
    status_reg &= ~BIT_PEPPER;
    Serial.println("[pepper] done");
  }
  if (salt_timed && millis() - salt_start >= op_dur) {
    salt_srv.write(0); salt_timed = false;
    status_reg &= ~BIT_SALT;
    Serial.println("[salt] done");
  }
}

// ── Setup / Loop ───────────────────────────────────────────
void setup() {
  door_srv.attach(PIN_DOOR);     door_srv.write(0);
  clamp_srv.attach(PIN_CLAMP);   clamp_srv.write(0);
  pepper_srv.attach(PIN_PEPPER); pepper_srv.write(0);
  pump_srv.attach(PIN_PUMP);     pump_srv.write(0);
  salt_srv.attach(PIN_SALT);     salt_srv.write(0);

  pinMode(PIN_ROLL_1, OUTPUT); digitalWrite(PIN_ROLL_1, LOW);
  pinMode(PIN_ROLL_2, OUTPUT); digitalWrite(PIN_ROLL_2, LOW);
  pinMode(PIN_SCIS_1, OUTPUT); digitalWrite(PIN_SCIS_1, LOW);
  pinMode(PIN_SCIS_2, OUTPUT); digitalWrite(PIN_SCIS_2, LOW);

  Serial.begin(115200);
  node.begin();
  node.setDefaultReadHandler(processRead);
  node.setDefaultWriteHandler(processWrite);
  Serial.println("[cutter] RS485 node 0x45 ready");
}

void loop() {
  node.poll();
  updateTimers();
}
