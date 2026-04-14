#include <Servo.h>

Servo myServo;
bool servoEnabled = true; // Flag to control servo movement

void setup() {
  Serial.begin(9600);
  myServo.attach(9); // Attach servo to D9
  Serial.println("Enter servo angle (0–180) or type STOP to disable movement:");
}

void loop() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n'); // Read serial line
    input.trim(); // Remove whitespace/newline

    if (input.equalsIgnoreCase("STOP")) {
      servoEnabled = false;
      Serial.println("Servo movement stopped.");
    } else {
      int angle = input.toInt();        // Convert input to integer
      angle = constrain(angle, 0, 180); // Constrain to physical limits
      myServo.write(angle);

      servoEnabled = true; // Re-enable servo if a number is given
      Serial.print("Servo moved to: ");
      Serial.println(angle);
    }

    delay(50); // Small delay to avoid flooding serial
  }
}
