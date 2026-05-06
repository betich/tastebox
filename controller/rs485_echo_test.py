"""MAX485 self-echo test: DE=HIGH, RE=LOW during TX so the chip reads its own bus drive."""

import time
import RPi.GPIO as GPIO
import serial

UART_PORT = "/dev/ttyAMA0"
BAUD = 9600
PIN_DE = 23
PIN_RE = 24

GPIO.setmode(GPIO.BCM)
GPIO.setup(PIN_DE, GPIO.OUT, initial=GPIO.LOW)
GPIO.setup(PIN_RE, GPIO.OUT, initial=GPIO.LOW)  # RE LOW = receiver always on

ser = serial.Serial(UART_PORT, BAUD, timeout=1.0)
print(f"MAX485 self-echo test on {UART_PORT}")
print("DE=GPIO{PIN_DE}, RE=GPIO{PIN_RE} — RE stays LOW so chip echoes its own TX\n")

try:
    for i in range(5):
        msg = f"HELLO{i}\n"
        ser.reset_input_buffer()

        GPIO.output(PIN_DE, GPIO.HIGH)   # enable driver
        data = msg.encode()
        ser.write(data)
        ser.flush()
        # Hold DE until all bytes have left the UART shift register.
        # 10 bits per byte (start + 8 data + stop) at 9600 baud + 2 ms margin.
        time.sleep(len(data) * 10 / BAUD + 0.002)
        GPIO.output(PIN_DE, GPIO.LOW)    # release bus

        echo = ser.read(len(data)).decode(errors="replace")
        if echo == msg:
            print(f"[OK]  '{msg.strip()}' echoed — TX chain works")
        elif echo:
            print(f"[ERR] sent '{msg.strip()}' got '{echo.strip()}'")
        else:
            print(f"[TMO] no echo — check MAX485 wiring / DE pin / UART TX")

        ser.reset_input_buffer()
        time.sleep(0.5)
finally:
    GPIO.cleanup()
    ser.close()
