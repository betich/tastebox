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
//   Ch  8: L Salt & Pepper dispenser
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
#define PCA_CH_L_DISP    8
#define PCA_CH_R_DISP   15

// Servo pulse ticks at 50 Hz (4096 ticks / 20 ms)
#define SERVO_0    150
#define SERVO_90   375
#define SERVO_180  600

// ── Registers ──────────────────────────────────────────────
#define REG_STATUS       0x00
#define REG_DOOR_CMD     0x11
#define REG_PINNER_CMD   0x12
#define REG_ROLLER_CMD   0x13  // roller piston:  0x01=ext 0x02=ret 0x03=stop
#define REG_CUTTING_CMD  0x14  // cutting piston: 0x01=ext 0x02=ret 0x03=stop
#define REG_L_DISP_CMD   0x15
#define REG_R_DISP_CMD   0x16
#define REG_DUR_HI       0x18
#define REG_PUMP_A       0x19  // raw PWM 0-255
#define REG_PUMP_B       0x1A  // raw PWM 0-255

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

// ── Helpers ────────────────────────────────────────────────
void setServo(uint8_t ch, uint16_t ticks) { pca.setPWM(ch, 0, ticks); }

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

    case REG_ROLLER_CMD:
      if      (data[0] == 0x01) { digitalWrite(PIN_PIST_R_IN1, HIGH); digitalWrite(PIN_PIST_R_IN2, LOW);  status_reg |=  BIT_ROLLER; Serial.println(F("[roller] ext"));  }
      else if (data[0] == 0x02) { digitalWrite(PIN_PIST_R_IN1, LOW);  digitalWrite(PIN_PIST_R_IN2, HIGH); status_reg |=  BIT_ROLLER; Serial.println(F("[roller] ret"));  }
      else                      { digitalWrite(PIN_PIST_R_IN1, LOW);  digitalWrite(PIN_PIST_R_IN2, LOW);  status_reg &= ~BIT_ROLLER; Serial.println(F("[roller] stop")); }
      break;

    case REG_CUTTING_CMD:
      if      (data[0] == 0x01) { digitalWrite(PIN_PIST_C_IN3, HIGH); digitalWrite(PIN_PIST_C_IN4, LOW);  status_reg |=  BIT_CUTTING; Serial.println(F("[cut] ext"));  }
      else if (data[0] == 0x02) { digitalWrite(PIN_PIST_C_IN3, LOW);  digitalWrite(PIN_PIST_C_IN4, HIGH); status_reg |=  BIT_CUTTING; Serial.println(F("[cut] ret"));  }
      else                      { digitalWrite(PIN_PIST_C_IN3, LOW);  digitalWrite(PIN_PIST_C_IN4, LOW);  status_reg &= ~BIT_CUTTING; Serial.println(F("[cut] stop")); }
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

  Serial.println(F("[cutter] USB node 0x45 ready (RS485 secondary)"));
}

void loop() {
  serial_handler.poll(Serial);
  node.poll();
  updateTimers();
}
