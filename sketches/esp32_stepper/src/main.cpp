#include <Arduino.h>

// ESP32 step/dir stepper driver — serial command interface
// ENA → GPIO16 (active-low), DIR → GPIO17, PUL → GPIO18
// Compatible with TB6600, DRV8825, A4988, and similar drivers.
//
// Serial commands (115200, newline-terminated):
//   ENABLE           — energise driver (ENA low)
//   DISABLE          — release driver  (ENA high)
//   MOVE <steps>     — move N steps; negative = reverse direction
//   SPEED <us>       — set full-step interval in microseconds (default 1000)
//   STATUS           — print enabled/position/speed
//   RESET            — zero the position counter
//   X                — abort a move in progress

#define PIN_ENA  16
#define PIN_DIR  17
#define PIN_PUL  18

#define DIR_SETUP_US  10    // DIR → PUL setup time (µs); TB6600 needs 5 µs
#define MIN_SPEED_US  50    // floor to avoid stalling the ESP32

static long  pos         = 0;
static long  stepDelayUs = 1000;
static bool  enabled     = false;
static bool  abortMove   = false;

// ── Driver helpers ─────────────────────────────────────────────────────────────

void setEnabled(bool en) {
  enabled = en;
  digitalWrite(PIN_ENA, en ? LOW : HIGH);   // active-low
}

void doMove(long steps) {
  if (!enabled) { Serial.println("ERR not_enabled"); return; }
  if (steps == 0) { Serial.println("OK pos=0"); return; }

  abortMove = false;
  bool forward = steps > 0;
  long count   = abs(steps);

  digitalWrite(PIN_DIR, forward ? HIGH : LOW);
  delayMicroseconds(DIR_SETUP_US);

  for (long i = 0; i < count; i++) {
    if (abortMove) {
      Serial.print("ABORTED pos=");
      Serial.println(pos);
      return;
    }
    digitalWrite(PIN_PUL, HIGH);
    delayMicroseconds(stepDelayUs / 2);
    digitalWrite(PIN_PUL, LOW);
    delayMicroseconds(stepDelayUs / 2);
    pos += forward ? 1 : -1;
  }

  Serial.print("OK pos=");
  Serial.println(pos);
}

// ── Serial command parser ──────────────────────────────────────────────────────

void handleLine(const String& line) {
  if (line.length() == 1 && toupper(line[0]) == 'X') {
    abortMove = true;
    return;
  }

  String upper = line;
  upper.toUpperCase();

  if (upper == "ENABLE") {
    setEnabled(true);
    Serial.println("OK enabled");

  } else if (upper == "DISABLE") {
    setEnabled(false);
    Serial.println("OK disabled");

  } else if (upper == "STATUS") {
    Serial.print("enabled="); Serial.print(enabled ? "1" : "0");
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
    Serial.println("Commands: ENABLE | DISABLE | MOVE <steps> | SPEED <us> | STATUS | RESET | X");
  }
}

// ── Setup / Loop ───────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  pinMode(PIN_ENA, OUTPUT);
  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_PUL, OUTPUT);
  digitalWrite(PIN_ENA, HIGH);   // disabled at startup
  digitalWrite(PIN_DIR, LOW);
  digitalWrite(PIN_PUL, LOW);
  Serial.println("[stepper] ready — ENABLE to start");
  Serial.println("Commands: ENABLE | DISABLE | MOVE <steps> | SPEED <us> | STATUS | RESET | X");
}

void loop() {
  // Check for abort byte mid-move without waiting for newline
  while (Serial.available()) {
    char peek = (char)Serial.peek();
    if (peek == 'X' || peek == 'x') {
      Serial.read();
      abortMove = true;
      return;
    }

    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length()) handleLine(line);
  }
}
