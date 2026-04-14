// Arduino Nano — Dual Axis Stepper + Joystick
// Motor 1: PUL→D7,  DIR→D8,  ENA→D4  — Joystick Y (A1)
// Motor 2: PUL→D13, DIR→D12, ENA→D11 — Joystick X (A0)

#define MICROSTEPPING     16
#define MOTOR_STEPS_REV   200
#define GEAR_RATIO        1.0f
#define STEPS_PER_DEG     (MOTOR_STEPS_REV * MICROSTEPPING * GEAR_RATIO / 360.0f)

#define JOY_CENTER    512
#define JOY_DEADZONE   30
#define MAX_SPEED_DEG  60.0f
#define LOOP_MS        50

// ── Stepper ────────────────────────────────────────────────
struct Stepper {
  uint8_t pin_step, pin_dir, pin_ena;
  bool ena_active_high;  // true = HIGH enables, false = LOW enables

  void begin() {
    pinMode(pin_step, OUTPUT);
    pinMode(pin_dir,  OUTPUT);
    pinMode(pin_ena,  OUTPUT);
    digitalWrite(pin_step, LOW);
    digitalWrite(pin_dir,  LOW);
    digitalWrite(pin_ena,  ena_active_high ? HIGH : LOW); // enable on init
  }

  void step(long steps, float speed_deg) {
    if (steps == 0) return;
    digitalWrite(pin_dir, steps > 0 ? HIGH : LOW);
    delayMicroseconds(5);

    float steps_per_sec = speed_deg * STEPS_PER_DEG;
    long delay_us = (long)(500000.0f / steps_per_sec);

    long n = abs(steps);
    for (long i = 0; i < n; i++) {
      digitalWrite(pin_step, HIGH);
      delayMicroseconds(delay_us);
      digitalWrite(pin_step, LOW);
      delayMicroseconds(delay_us);
    }
  }
};

// ── Joystick Axis ──────────────────────────────────────────
struct JoyAxis {
  uint8_t pin;

  float readNorm() {
    int raw = analogRead(pin);
    int deflection = raw - JOY_CENTER;
    if (abs(deflection) <= JOY_DEADZONE) return 0.0f;

    float norm = (float)(deflection - (deflection > 0 ? JOY_DEADZONE : -JOY_DEADZONE))
                 / (float)(512 - JOY_DEADZONE);
    return constrain(norm, -1.0f, 1.0f);
  }

  int readRaw() { return analogRead(pin); }
};

// ── Instances ──────────────────────────────────────────────
Stepper motor1 = { 7,  8,  4, false };  // ENA active LOW
Stepper motor2 = { 13, 12, 11, false };  // ENA active HIGH

JoyAxis joyY = { A1 };
JoyAxis joyX = { A0 };

// ── Drive ──────────────────────────────────────────────────
void driveFromJoy(Stepper& motor, JoyAxis& axis) {
  float norm = axis.readNorm();
  if (norm == 0.0f) return;

  float delta_deg = norm * MAX_SPEED_DEG * (LOOP_MS / 1000.0f);
  long steps = (long)(delta_deg * STEPS_PER_DEG);
  motor.step(steps, MAX_SPEED_DEG);
}

// ──────────────────────────────────────────────────────────
void setup() {
  motor1.begin();
  motor2.begin();
  Serial.begin(115200);
  Serial.println("Dual axis ready. Y→M1  X→M2");
}

void loop() {
  driveFromJoy(motor1, joyY);
  driveFromJoy(motor2, joyX);

  Serial.print("Y: "); Serial.print(joyY.readRaw());
  Serial.print("\tX: "); Serial.println(joyX.readRaw());

  delay(LOOP_MS);
}