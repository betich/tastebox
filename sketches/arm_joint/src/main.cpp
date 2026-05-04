#include <Arduino.h>

// DC motor arm joint — hard-stop homing via stall detection
//
// Hardware:
//   H-bridge: EN (PWM) + IN1 + IN2 for direction
//   Current sense: analog voltage proportional to motor current
//
// State machine:
//   IDLE           — motor coasting, safe to command
//   MOVING         — traveling toward A or B
//   BRAKING        — active brake holding after stall detected
//   ERROR_TIMEOUT  — travel time exceeded TRAVEL_TIMEOUT_MS; call clearError() to reset


// ── Pins ─────────────────────────────────────────────────────────────────────
#define PIN_EN       9    // H-bridge enable (PWM)
#define PIN_IN1      7    // H-bridge IN1
#define PIN_IN2      8    // H-bridge IN2
#define PIN_CURRENT  A0   // current-sense analog output


// ── Tuning ───────────────────────────────────────────────────────────────────
// Speed
#define MOTOR_SPEED         180   // PWM 0–255 — reduce if arm overshoots hard stop

// Stall detection
//   STALL_THRESHOLD: ADC counts (0–1023 on 5 V Arduino, 0–4095 on 3.3 V ESP32 12-bit)
//   Raise if normal running current false-triggers; lower if hard stop isn't caught.
//   Tip: print analogRead(PIN_CURRENT) while running freely, then while stalled,
//   and pick a value between the two readings.
#define STALL_THRESHOLD     600   // ADC counts
#define STALL_DEBOUNCE_MS    50   // ms current must stay above threshold to confirm stall
                                  // keeps noise spikes from triggering a false stop

// Inrush filter
//   DC motors draw 5–10× rated current the instant they start.
//   Do NOT check for stalls during this window or the arm will never move.
#define INRUSH_FILTER_MS    200   // ms after start before stall detection activates

// Braking
#define BRAKE_DURATION_MS   250   // ms to hold active brake before coasting to save power

// Safety timeout
//   If the arm travels this long without hitting a hard stop, cut power.
//   Prevents motor burnout if the arm jams mid-travel below the stall threshold.
#define TRAVEL_TIMEOUT_MS  5000   // ms


// ── State machine ─────────────────────────────────────────────────────────────
enum State : uint8_t { IDLE, MOVING, BRAKING, ERROR_TIMEOUT };

static State         _state       = IDLE;
static bool          _movingToA   = false;   // tracks direction across BRAKING state
static unsigned long _moveStart   = 0;
static unsigned long _brakeStart  = 0;
static bool          _stallArmed  = false;   // false during inrush window
static bool          _stallSeen   = false;   // first sample above threshold
static unsigned long _stallSince  = 0;       // when debounce window opened


// ── Motor primitives ──────────────────────────────────────────────────────────

static void _drive(bool in1, bool in2, uint8_t speed) {
  digitalWrite(PIN_IN1, in1 ? HIGH : LOW);
  digitalWrite(PIN_IN2, in2 ? HIGH : LOW);
  analogWrite(PIN_EN, speed);
}

// Active brake: both INs same state → H-bridge shorts motor terminals.
// Motor back-EMF fights rotation — stops fast with minimal mechanical shock.
static void _activeBrake() {
  digitalWrite(PIN_IN1, HIGH);
  digitalWrite(PIN_IN2, HIGH);
  analogWrite(PIN_EN, 255);
}

// Coast: EN low → H-bridge outputs float → motor freewheels to a stop.
// Use after braking hold period to save current draw.
static void _coast() {
  analogWrite(PIN_EN, 0);
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
}

static void _beginMove(bool toA) {
  _movingToA  = toA;
  _state      = MOVING;
  _moveStart  = millis();
  _stallArmed = false;
  _stallSeen  = false;
  // IN1=HIGH, IN2=LOW → direction A  |  IN1=LOW, IN2=HIGH → direction B
  _drive(toA ? HIGH : LOW, toA ? LOW : HIGH, MOTOR_SPEED);
  Serial.print("[arm] moving to ");
  Serial.println(toA ? "A" : "B");
}


// ── Public API ────────────────────────────────────────────────────────────────

void armSetup() {
  pinMode(PIN_EN,      OUTPUT);
  pinMode(PIN_IN1,     OUTPUT);
  pinMode(PIN_IN2,     OUTPUT);
  pinMode(PIN_CURRENT, INPUT);
  _coast();
}

// Start moving toward hard stop A. No-op if currently braking.
void moveToA() {
  if (_state == BRAKING) return;
  _beginMove(true);
}

// Start moving toward hard stop B. No-op if currently braking.
void moveToB() {
  if (_state == BRAKING) return;
  _beginMove(false);
}

bool armIsIdle()    { return _state == IDLE; }
bool armIsError()   { return _state == ERROR_TIMEOUT; }

// Acknowledge and clear an error timeout — resets to IDLE.
void armClearError() {
  if (_state == ERROR_TIMEOUT) {
    _coast();
    _state = IDLE;
    Serial.println("[arm] error cleared");
  }
}


// ── Update — call every loop iteration ────────────────────────────────────────

void armUpdate() {
  unsigned long now = millis();

  switch (_state) {

    case MOVING: {
      unsigned long elapsed = now - _moveStart;

      // ── Safety timeout ────────────────────────────────────
      if (elapsed >= TRAVEL_TIMEOUT_MS) {
        _coast();
        _state = ERROR_TIMEOUT;
        Serial.println("[arm] ERROR: travel timeout — check mechanical path");
        return;
      }

      // ── Inrush filter ─────────────────────────────────────
      // Arm stall detection only after the inrush window has passed.
      if (!_stallArmed && elapsed >= INRUSH_FILTER_MS) {
        _stallArmed = true;
      }

      // ── Stall detection with debounce ─────────────────────
      if (_stallArmed) {
        int adc = analogRead(PIN_CURRENT);

        if (adc >= STALL_THRESHOLD) {
          if (!_stallSeen) {
            // First sample above threshold — start debounce window
            _stallSeen  = true;
            _stallSince = now;
          } else if (now - _stallSince >= STALL_DEBOUNCE_MS) {
            // Current sustained above threshold — hard stop confirmed
            _activeBrake();
            _brakeStart = now;
            _state      = BRAKING;
            Serial.print("[arm] stall → braking @ ");
            Serial.println(_movingToA ? "A" : "B");
          }
        } else {
          // Current dropped — was noise, reset debounce
          _stallSeen = false;
        }
      }
      break;
    }

    case BRAKING:
      // Hold active brake for BRAKE_DURATION_MS then coast
      if (now - _brakeStart >= BRAKE_DURATION_MS) {
        _coast();
        _state = IDLE;
        Serial.print("[arm] at ");
        Serial.println(_movingToA ? "A" : "B");
      }
      break;

    case ERROR_TIMEOUT:
    case IDLE:
    default:
      break;
  }
}


// ── Example usage ─────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  armSetup();
  Serial.println("[arm] ready — sending to B");
  moveToB();
}

void loop() {
  armUpdate();

  // Example: bounce between A and B once each is reached
  static bool lastWasA = false;
  if (armIsIdle()) {
    delay(500);
    if (lastWasA) moveToB(); else moveToA();
    lastWasA = !lastWasA;
  }

  if (armIsError()) {
    Serial.println("[arm] halted — fix obstruction then call armClearError()");
    delay(3000);
    armClearError();
    moveToB();
  }
}
