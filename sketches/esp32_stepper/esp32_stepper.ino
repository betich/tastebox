#include <Arduino.h>

// ESP32 stepper driver — tuned for DM556, serial command interface
// ENA → GPIO16 (active-low), DIR → GPIO17, PUL → GPIO18
//
// DM556 timing requirements:
//   ENA  active-low; hold ≥5 µs before first pulse
//   DIR  setup time ≥5 µs before PUL rising edge
//   PUL  HIGH ≥2.5 µs, LOW ≥2.5 µs
//   Note: ESP32 is 3.3 V logic — works with DM556 but a level shifter
//         (e.g. 74HCT245) is recommended for long cable runs.
//
// Serial commands (115200, newline-terminated):
//   ENABLE           — energise driver (ENA low)
//   DISABLE          — release driver  (ENA high)
//   F                — run forward continuously
//   R                — run reverse continuously
//   S  (or X)        — stop continuous run / abort MOVE
//   MOVE <steps>     — move N steps precisely; negative = reverse
//   SPEED <us>       — set step interval in microseconds (default 1000)
//   STATUS           — print mode/position/speed
//   RESET            — zero the position counter

#define PIN_ENA  16
#define PIN_DIR  17
#define PIN_PUL  18

// DM556 minimums (datasheet §4): PUL HIGH/LOW ≥2.5 µs, DIR setup ≥5 µs
#define DIR_SETUP_US    5   // µs — DIR stable before PUL rising edge
#define PULSE_WIDTH_US  3   // µs — PUL HIGH width (≥2.5 µs per DM556)
#define ENA_SETUP_US    5   // µs — ENA stable before first pulse
#define MIN_SPEED_US   50   // floor: LOW half must also be ≥2.5 µs → full step ≥5 µs

static long  pos         = 0;
static long  stepDelayUs = 1000;
static bool  enabled     = false;

// ── Run mode (non-blocking continuous drive) ───────────────────────────────────

enum RunMode : uint8_t { IDLE, FWD, REV };
static RunMode       runMode    = IDLE;
static unsigned long nextStepUs = 0;
static volatile bool abortFlag  = false;

// ── Helpers ────────────────────────────────────────────────────────────────────

void setEnabled(bool en) {
  enabled = en;
  digitalWrite(PIN_ENA, en ? LOW : HIGH);   // active-low
  if (en) delayMicroseconds(ENA_SETUP_US);  // DM556: ENA must settle before first pulse
  if (!en && runMode != IDLE) {
    runMode = IDLE;
    Serial.println("OK disabled — run stopped");
  }
}

void setRunMode(RunMode mode) {
  if (mode == runMode) return;
  if (mode != IDLE) {
    if (!enabled) { Serial.println("ERR not_enabled"); return; }
    digitalWrite(PIN_DIR, mode == FWD ? HIGH : LOW);
    delayMicroseconds(DIR_SETUP_US);
    nextStepUs = micros();
  }
  runMode = mode;
  if (mode == FWD)  Serial.println("OK fwd");
  if (mode == REV)  Serial.println("OK rev");
  if (mode == IDLE) Serial.println("OK stopped");
}

// Blocking precise move — exits run mode first.
void doMove(long steps) {
  if (!enabled) { Serial.println("ERR not_enabled"); return; }
  if (steps == 0) { Serial.print("OK pos="); Serial.println(pos); return; }

  if (runMode != IDLE) { runMode = IDLE; }

  abortFlag = false;
  bool forward = steps > 0;
  long count   = abs(steps);

  digitalWrite(PIN_DIR, forward ? HIGH : LOW);
  delayMicroseconds(DIR_SETUP_US);

  for (long i = 0; i < count; i++) {
    if (abortFlag) {
      Serial.print("ABORTED pos="); Serial.println(pos);
      return;
    }
    digitalWrite(PIN_PUL, HIGH);
    delayMicroseconds(stepDelayUs / 2);
    digitalWrite(PIN_PUL, LOW);
    delayMicroseconds(stepDelayUs / 2);
    pos += forward ? 1 : -1;
  }

  Serial.print("OK pos="); Serial.println(pos);
}

// ── Serial command parser ──────────────────────────────────────────────────────

void handleLine(const String& line) {
  String upper = line;
  upper.toUpperCase();
  upper.trim();

  if (upper == "F") {
    setRunMode(FWD);
  } else if (upper == "R") {
    setRunMode(REV);
  } else if (upper == "S" || upper == "X") {
    abortFlag = true;
    setRunMode(IDLE);

  } else if (upper == "ENABLE") {
    setEnabled(true);
    Serial.println("OK enabled");
  } else if (upper == "DISABLE") {
    setEnabled(false);

  } else if (upper == "STATUS") {
    const char* modeStr = runMode == FWD ? "fwd" : runMode == REV ? "rev" : "idle";
    Serial.print("enabled="); Serial.print(enabled ? "1" : "0");
    Serial.print(" mode=");   Serial.print(modeStr);
    Serial.print(" pos=");    Serial.print(pos);
    Serial.print(" speed=");  Serial.print(stepDelayUs);
    Serial.println("us");

  } else if (upper == "RESET") {
    pos = 0;
    Serial.println("OK pos=0");

  } else if (upper.startsWith("MOVE ")) {
    long steps = line.substring(5).toInt();
    doMove(steps);

  } else if (upper.startsWith("SPEED ")) {
    long us = line.substring(6).toInt();
    if (us < MIN_SPEED_US) {
      Serial.print("ERR min="); Serial.print(MIN_SPEED_US); Serial.println("us");
      return;
    }
    stepDelayUs = us;
    Serial.print("OK speed="); Serial.print(stepDelayUs); Serial.println("us");

  } else {
    Serial.print("ERR unknown: "); Serial.println(line);
    Serial.println("F | R | S | MOVE <steps> | SPEED <us> | ENABLE | DISABLE | STATUS | RESET");
  }
}

// ── Setup / Loop ───────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  pinMode(PIN_ENA, OUTPUT);
  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_PUL, OUTPUT);
  digitalWrite(PIN_ENA, HIGH);
  digitalWrite(PIN_DIR, LOW);
  digitalWrite(PIN_PUL, LOW);
  Serial.println("[stepper] ready");
  Serial.println("F | R | S | MOVE <steps> | SPEED <us> | ENABLE | DISABLE | STATUS | RESET");
}

void loop() {
  // ── Non-blocking continuous stepping ──────────────────────────────────────
  if (runMode != IDLE && enabled) {
    unsigned long now = micros();
    if ((long)(now - nextStepUs) >= 0) {
      digitalWrite(PIN_PUL, HIGH);
      delayMicroseconds(PULSE_WIDTH_US);
      digitalWrite(PIN_PUL, LOW);
      nextStepUs = micros() + stepDelayUs;
      pos += (runMode == FWD) ? 1 : -1;
    }
  }

  // ── Serial input ───────────────────────────────────────────────────────────
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length()) handleLine(line);
  }
}
