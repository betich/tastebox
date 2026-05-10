#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <RS485Node.h>
#include <SerialFrameHandler.h>

// Cutter node — RS485 node 0x45
// RS485:            RO=D2,  DI=D3,  DE/RE=D4
// PCA9685:          SDA=A4, SCL=A5, OE=D5,  I2C addr=0x40
//   Ch  0: Pinner servo
//   Ch  4: Door servo
//   Ch  9: L Salt & Pepper dispenser
//   Ch 15: R Salt & Pepper dispenser
// Pump A MOSFET:    PWM=D6   (0-255)
// Pump B MOSFET:    PWM=D11  (0-255)
// Roller piston:    IN1=D7,  IN2=D8   OUT1=positive
// Cutting piston:   IN3=D9,  IN4=D10  OUT3=positive
//   (ENA/ENB jumpered to 5V — direction only)

// ── Pins ───────────────────────────────────────────────────
#define PIN_RS485_RX    2
#define PIN_RS485_TX    3
#define PIN_RS485_DE_RE 4

#define PIN_PCA_OE      5

#define PIN_PUMP_A      6   // PWM → MOSFET
#define PIN_PIST_R_IN1  7   // Roller piston IN1
#define PIN_PIST_R_IN2  8   // Roller piston IN2
#define PIN_PIST_C_IN3  9   // Cutting piston IN3
#define PIN_PIST_C_IN4  10  // Cutting piston IN4
#define PIN_PUMP_B      11  // PWM → MOSFET

// ── PCA9685 ────────────────────────────────────────────────
#define PCA_ADDR        0x40
#define PCA_CH_PINNER    0
#define PCA_CH_DOOR      4
#define PCA_CH_L_DISP    9
#define PCA_CH_R_DISP   15

// Servo pulse ticks at 50 Hz (4096 ticks / 20 ms)
#define SERVO_0    150
#define SERVO_90   375
#define SERVO_180  600

// ── Registers ──────────────────────────────────────────────
#define REG_STATUS       0x00
#define REG_DOOR_CMD     0x11
#define REG_PINNER_CMD   0x12
#define REG_CUTTER_CMD   0x13  // cutter:  0x01=close 0x02=open 0x03=stop
#define REG_ROLLER_CMD   0x14  // roller:  0x01=up    0x02=down  0x03=stop
#define REG_L_DISP_CMD   0x15
#define REG_R_DISP_CMD   0x16
#define REG_DUR_HI       0x18
#define REG_PUMP_A       0x19  // raw PWM 0-255
#define REG_PUMP_B       0x1A  // raw PWM 0-255
#define REG_PINNER_ANG   0x20  // pinner servo angle 0-180
#define REG_DOOR_ANG     0x21  // door servo angle 0-180
#define REG_L_DISP_ANG   0x22  // L dispenser servo angle 0-180
#define REG_R_DISP_ANG   0x23  // R dispenser servo angle 0-180

// STATUS bits
#define BIT_DOOR    0x01
#define BIT_PINNER  0x02
#define BIT_ROLLER  0x04
#define BIT_CUTTING 0x08
#define BIT_L_DISP  0x10
#define BIT_R_DISP  0x20
#define BIT_PUMP_A  0x40
#define BIT_PUMP_B  0x80

// ── State ──────────────────────────────────────────────────
uint8_t  status_reg = 0;
uint16_t op_dur     = 1000;

unsigned long l_disp_start = 0; bool l_disp_timed = false;
unsigned long r_disp_start = 0; bool r_disp_timed = false;

Adafruit_PWMServoDriver pca(PCA_ADDR);
RS485Node          node(0x45, PIN_RS485_RX, PIN_RS485_TX, PIN_RS485_DE_RE);
SerialFrameHandler serial_handler(0x45);

void handlePlainText(const char* line, Stream& s) {
  if (strcmp(line, "help") != 0) return;
  s.println(F("Cutter node 0x45 — serial commands:"));
  s.println(F("  @45 R 00      status  b0=door b1=pinner b2=roller b3=cutting"));
  s.println(F("                        b4=L_disp b5=R_disp b6=pump_A b7=pump_B"));
  s.println(F("  @45 W 11 01   door open    00=close"));
  s.println(F("  @45 W 12 01   pinner clamp 00=release"));
  s.println(F("  @45 W 13 01   cutter close 02=open 03=stop"));
  s.println(F("  @45 W 14 01   roller up    02=down 03=stop"));
  s.println(F("  @45 W 15 01   L dispenser on  00=off"));
  s.println(F("  @45 W 16 01   R dispenser on  00=off"));
  s.println(F("  @45 W 18 HH LL  dispenser duration ms (default 1000)"));
  s.println(F("  @45 W 19 NN   pump A PWM (0-FF)"));
  s.println(F("  @45 W 1A NN   pump B PWM (0-FF)"));
  s.println(F("  @45 W 20 NN   pinner angle 0-B4 (0-180 deg)"));
  s.println(F("  @45 W 21 NN   door angle   0-B4 (0-180 deg)"));
  s.println(F("  @45 W 22 NN   L disp angle 0-B4 (0-180 deg)"));
  s.println(F("  @45 W 23 NN   R disp angle 0-B4 (0-180 deg)"));
  s.println(F("Pins: RS485(2nd) RO=D2 DI=D3 DE/RE=D4 | PCA9685 OE=D5 SDA=A4 SCL=A5"));
  s.println(F("      PumpA=D6 PumpB=D11 | Roller IN1=D7 IN2=D8 | Cutting IN3=D9 IN4=D10"));
  s.println(F("PCA ch: 0=pinner 4=door 8=L_disp 15=R_disp"));
}

// ── Helpers ────────────────────────────────────────────────
void setServo(uint8_t ch, uint16_t ticks) { pca.setPWM(ch, 0, ticks); }
void setServoAngle(uint8_t ch, uint8_t deg) {
  uint16_t ticks = map(constrain(deg, 0, 180), 0, 180, SERVO_0, SERVO_180);
  pca.setPWM(ch, 0, ticks);
}

void setPumpA(uint8_t val) {
  analogWrite(PIN_PUMP_A, val);
  if (val) status_reg |=  BIT_PUMP_A;
  else     status_reg &= ~BIT_PUMP_A;
}

void setPumpB(uint8_t val) {
  analogWrite(PIN_PUMP_B, val);
  if (val) status_reg |=  BIT_PUMP_B;
  else     status_reg &= ~BIT_PUMP_B;
}

// ── Register handlers ──────────────────────────────────────
uint8_t processRead(uint8_t reg) {
  if (reg == REG_STATUS) return status_reg;
  return 0xFF;
}

void processWrite(uint8_t reg, uint8_t* data, uint8_t len) {
  if (len < 1) return;
  switch (reg) {

    case REG_DOOR_CMD:
      if (data[0] == 0x01) { setServo(PCA_CH_DOOR, SERVO_90); status_reg |=  BIT_DOOR; Serial.println(F("[door] open"));   }
      else                  { setServo(PCA_CH_DOOR, SERVO_0);  status_reg &= ~BIT_DOOR; Serial.println(F("[door] close"));  }
      break;

    case REG_PINNER_CMD:
      if (data[0] == 0x01) { setServo(PCA_CH_PINNER, SERVO_90); status_reg |=  BIT_PINNER; Serial.println(F("[pin] clamp"));   }
      else                  { setServo(PCA_CH_PINNER, SERVO_0);  status_reg &= ~BIT_PINNER; Serial.println(F("[pin] release")); }
      break;

    case REG_CUTTER_CMD:
      if      (data[0] == 0x01) { digitalWrite(PIN_PIST_R_IN1, HIGH); digitalWrite(PIN_PIST_R_IN2, LOW);  status_reg |=  BIT_ROLLER;  Serial.println(F("[cutter] close")); }
      else if (data[0] == 0x02) { digitalWrite(PIN_PIST_R_IN1, LOW);  digitalWrite(PIN_PIST_R_IN2, HIGH); status_reg |=  BIT_ROLLER;  Serial.println(F("[cutter] open"));  }
      else                      { digitalWrite(PIN_PIST_R_IN1, LOW);  digitalWrite(PIN_PIST_R_IN2, LOW);  status_reg &= ~BIT_ROLLER;  Serial.println(F("[cutter] stop")); }
      break;

    case REG_ROLLER_CMD:
      if      (data[0] == 0x01) { digitalWrite(PIN_PIST_C_IN3, HIGH); digitalWrite(PIN_PIST_C_IN4, LOW);  status_reg |=  BIT_CUTTING; Serial.println(F("[roller] up"));   }
      else if (data[0] == 0x02) { digitalWrite(PIN_PIST_C_IN3, LOW);  digitalWrite(PIN_PIST_C_IN4, HIGH); status_reg |=  BIT_CUTTING; Serial.println(F("[roller] down")); }
      else                      { digitalWrite(PIN_PIST_C_IN3, LOW);  digitalWrite(PIN_PIST_C_IN4, LOW);  status_reg &= ~BIT_CUTTING; Serial.println(F("[roller] stop")); }
      break;

    case REG_L_DISP_CMD:
      if (data[0] == 0x01) { setServo(PCA_CH_L_DISP, SERVO_180); l_disp_start = millis(); l_disp_timed = true;  status_reg |=  BIT_L_DISP; Serial.println(F("[L-disp] on"));   }
      else                  { setServo(PCA_CH_L_DISP, SERVO_0);   l_disp_timed = false;                          status_reg &= ~BIT_L_DISP; Serial.println(F("[L-disp] stop")); }
      break;

    case REG_R_DISP_CMD:
      if (data[0] == 0x01) { setServo(PCA_CH_R_DISP, SERVO_180); r_disp_start = millis(); r_disp_timed = true;  status_reg |=  BIT_R_DISP; Serial.println(F("[R-disp] on"));   }
      else                  { setServo(PCA_CH_R_DISP, SERVO_0);   r_disp_timed = false;                          status_reg &= ~BIT_R_DISP; Serial.println(F("[R-disp] stop")); }
      break;

    case REG_DUR_HI:
      if (len >= 2) op_dur = (uint16_t)((data[0] << 8) | data[1]);
      break;

    case REG_PUMP_A:
      setPumpA(data[0]);
      Serial.print(F("[pumpA] ")); Serial.println(data[0]);
      break;

    case REG_PUMP_B:
      setPumpB(data[0]);
      Serial.print(F("[pumpB] ")); Serial.println(data[0]);
      break;

    case REG_PINNER_ANG:
      setServoAngle(PCA_CH_PINNER, data[0]);
      Serial.print(F("[pinner] ")); Serial.print(data[0]); Serial.println(F(" deg"));
      break;

    case REG_DOOR_ANG:
      setServoAngle(PCA_CH_DOOR, data[0]);
      Serial.print(F("[door] ")); Serial.print(data[0]); Serial.println(F(" deg"));
      break;

    case REG_L_DISP_ANG:
      setServoAngle(PCA_CH_L_DISP, data[0]);
      Serial.print(F("[L-disp] ")); Serial.print(data[0]); Serial.println(F(" deg"));
      break;

    case REG_R_DISP_ANG:
      setServoAngle(PCA_CH_R_DISP, data[0]);
      Serial.print(F("[R-disp] ")); Serial.print(data[0]); Serial.println(F(" deg"));
      break;
  }
}

// ── Timed auto-return ──────────────────────────────────────
void updateTimers() {
  if (l_disp_timed && millis() - l_disp_start >= op_dur) {
    setServo(PCA_CH_L_DISP, SERVO_0);
    l_disp_timed = false; status_reg &= ~BIT_L_DISP;
    Serial.println(F("[L-disp] done"));
  }
  if (r_disp_timed && millis() - r_disp_start >= op_dur) {
    setServo(PCA_CH_R_DISP, SERVO_0);
    r_disp_timed = false; status_reg &= ~BIT_R_DISP;
    Serial.println(F("[R-disp] done"));
  }
}

// ── Setup / Loop ───────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Hold OE high (outputs disabled) while PCA initialises
  pinMode(PIN_PCA_OE, OUTPUT);
  digitalWrite(PIN_PCA_OE, HIGH);

  pca.begin();
  pca.setPWMFreq(50);

  setServo(PCA_CH_PINNER, SERVO_0);
  setServo(PCA_CH_DOOR,   SERVO_0);
  setServo(PCA_CH_L_DISP, SERVO_0);
  setServo(PCA_CH_R_DISP, SERVO_0);
  delay(100);
  digitalWrite(PIN_PCA_OE, LOW);  // enable outputs

  // Pumps off
  pinMode(PIN_PUMP_A, OUTPUT); analogWrite(PIN_PUMP_A, 0);
  pinMode(PIN_PUMP_B, OUTPUT); analogWrite(PIN_PUMP_B, 0);

  // Pistons stopped
  pinMode(PIN_PIST_R_IN1, OUTPUT); digitalWrite(PIN_PIST_R_IN1, LOW);
  pinMode(PIN_PIST_R_IN2, OUTPUT); digitalWrite(PIN_PIST_R_IN2, LOW);
  pinMode(PIN_PIST_C_IN3, OUTPUT); digitalWrite(PIN_PIST_C_IN3, LOW);
  pinMode(PIN_PIST_C_IN4, OUTPUT); digitalWrite(PIN_PIST_C_IN4, LOW);

  node.begin();
  node.setDefaultReadHandler(processRead);
  node.setDefaultWriteHandler(processWrite);

  serial_handler.setDefaultReadHandler(processRead);
  serial_handler.setDefaultWriteHandler(processWrite);
  serial_handler.setPlainTextHandler(handlePlainText);

  pinMode(LED_BUILTIN, OUTPUT);

  Serial.println(F("[cutter] USB node 0x45 ready (RS485 secondary)"));
}

static uint32_t led_last = 0;
static bool     led_state = false;

void loop() {
  uint32_t now = millis();
  if (now - led_last >= 500) { led_last = now; led_state = !led_state; digitalWrite(LED_BUILTIN, led_state); }

  serial_handler.poll(Serial);
  node.poll();
  updateTimers();
}
