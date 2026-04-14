#include <Wire.h>

#define SLAVE_ADDR 0x42

// Registers
#define REG_POS_HI  0x00
#define REG_POS_LO  0x01
#define REG_SW      0x02  // cooktop on/off state
#define REG_EVT     0x03
#define REG_CMD     0x10
#define REG_SET_POS 0x11

// Commands
#define CMD_RESET 0x01
#define CMD_CLICK 0x04

bool slavePresent = false;

uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(SLAVE_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    slavePresent = false;
    return 0xFF;
  }
  slavePresent = true;
  Wire.requestFrom(SLAVE_ADDR, 1);
  return Wire.available() ? Wire.read() : 0xFF;
}

void writeCmd(uint8_t cmd) {
  Wire.beginTransmission(SLAVE_ADDR);
  Wire.write(REG_CMD);
  Wire.write(cmd);
  Wire.endTransmission();
}

int16_t readPosition() {
  uint8_t hi = readReg(REG_POS_HI);
  uint8_t lo = readReg(REG_POS_LO);
  return (int16_t)((hi << 8) | lo);
}

void setPosition(int16_t pos) {
  Wire.beginTransmission(SLAVE_ADDR);
  Wire.write(REG_SET_POS);
  Wire.write((uint8_t)(pos >> 8));
  Wire.write((uint8_t)(pos & 0xFF));
  Wire.endTransmission();
}

void printHelp() {
  Serial.println("--- Commands ---");
  Serial.println("  ON           click (toggle cooktop on)");
  Serial.println("  OFF          click (toggle cooktop off)");
  Serial.println("  CW <n>       step clockwise n ticks");
  Serial.println("  CCW <n>      step counter-clockwise n ticks");
  Serial.println("  STATUS       show cooktop on/off state");
  Serial.println("  RESET        reset tracked position to 0");
  Serial.println("----------------");
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  delay(500);
  Serial.println("=== I2C Master ===");
  printHelp();
}

void loop() {
  int16_t pos = readPosition();
  uint8_t sw  = readReg(REG_SW);
  uint8_t evt = readReg(REG_EVT);

  if (slavePresent) {
    if (evt & 0x01) { Serial.print("[EVT] CW    -> pos="); Serial.println(pos); }
    if (evt & 0x02) { Serial.print("[EVT] CCW   -> pos="); Serial.println(pos); }
    if (evt & 0x04) { Serial.println("[EVT] CLICK"); }
  }

  static unsigned long lastStatus = 0;
  if (millis() - lastStatus >= 1000) {
    if (!slavePresent) {
      Serial.println("[STATUS] no slave");
    } else {
      Serial.print("[STATUS] pos="); Serial.print(pos);
      Serial.print("  cooktop="); Serial.print(sw ? "ON" : "OFF");
      Serial.print("  evt=0x"); Serial.println(evt, HEX);
    }
    lastStatus = millis();
  }

  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    input.toUpperCase();

    if (input == "ON" || input == "OFF") {
      writeCmd(CMD_CLICK);
      Serial.println("[CMD] Clicking power...");

    } else if (input.startsWith("CW")) {
      int steps = 1;
      if (input.length() > 3) steps = input.substring(3).toInt();
      int16_t cur = readPosition();
      setPosition(cur + steps);
      Serial.print("[CMD] CW x"); Serial.println(steps);

    } else if (input.startsWith("CCW")) {
      int steps = 1;
      if (input.length() > 4) steps = input.substring(4).toInt();
      int16_t cur = readPosition();
      setPosition(cur - steps);
      Serial.print("[CMD] CCW x"); Serial.println(steps);

    } else if (input == "STATUS") {
      uint8_t sw = readReg(REG_SW);
      Serial.print("[STATUS] Cooktop is ");
      Serial.println((slavePresent && sw) ? "ON" : "OFF");

    } else if (input == "RESET") {
      writeCmd(CMD_RESET);
      Serial.println("[CMD] Reset position to 0");

    } else {
      Serial.print("[ERR] Unknown command: "); Serial.println(input);
      printHelp();
    }
  }

  delay(20);
}
