// IBT-2 Motor Driver control with Arduino Nano
#define R_PWM 4   // Right PWM pin (forward)
#define L_PWM 5   // Left PWM pin (reverse)

void setup() {
  pinMode(R_PWM, OUTPUT);
  pinMode(L_PWM, OUTPUT);
  Serial.begin(9600);
  Serial.println("IBT-2 motor control started!");
}

void loop() {
  // Rotate forward
  Serial.println("Motor forward");
  setMotor(200);  // Speed 200/255
  delay(2000);

  // Stop
  Serial.println("Stop");
  setMotor(0);
  delay(1000);

  // Rotate backward
  Serial.println("Motor backward");
  setMotor(-200);
  delay(2000);

  // Stop
  Serial.println("Stop");
  setMotor(0);
  delay(2000);
}

// Control motor speed and direction
// speed: -255 to 255
void setMotor(int speed) {
  if (speed > 0) {
    analogWrite(R_PWM, speed);
    analogWrite(L_PWM, 0);
  } else if (speed < 0) {
    analogWrite(R_PWM, 0);
    analogWrite(L_PWM, -speed);
  } else {
    // Stop motor
    analogWrite(R_PWM, 0);
    analogWrite(L_PWM, 0);
  }
}
