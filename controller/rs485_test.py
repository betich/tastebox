"""RS-485 connectivity test: RPi master pings Arduino Nano slave."""

import signal
import sys
import time

import RPi.GPIO as GPIO
import serial

UART_PORT = "/dev/ttyS0"
BAUD = 9600
PIN_DE = 23
PIN_RE = 24
PING_INTERVAL = 2.0
READ_TIMEOUT = 1.0


def _setup():
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(PIN_DE, GPIO.OUT, initial=GPIO.LOW)
    GPIO.setup(PIN_RE, GPIO.OUT, initial=GPIO.LOW)
    ser = serial.Serial(UART_PORT, BAUD, timeout=READ_TIMEOUT)
    return ser


def _tx_enable(enabled: bool):
    level = GPIO.HIGH if enabled else GPIO.LOW
    GPIO.output(PIN_DE, level)
    GPIO.output(PIN_RE, level)


def _send(ser: serial.Serial, message: str):
    _tx_enable(True)
    ser.write(message.encode())
    ser.flush()
    # Wait for last byte to leave the shift register before releasing the bus.
    # At 9600 baud, one byte ≈ 1.04 ms; a small margin is enough.
    time.sleep(0.002)
    _tx_enable(False)


def run():
    ser = _setup()

    def _cleanup(_sig=None, _frame=None):
        GPIO.cleanup()
        ser.close()
        print("\nCleaned up. Exiting.")
        sys.exit(0)

    signal.signal(signal.SIGINT, _cleanup)

    print(f"RS-485 test: pinging Nano every {PING_INTERVAL}s on {UART_PORT} @ {BAUD} baud")
    print("Ctrl-C to stop.\n")

    while True:
        ser.reset_input_buffer()
        t0 = time.monotonic()
        _send(ser, "PING\n")

        response = ser.readline().decode(errors="replace").strip()
        rtt_ms = (time.monotonic() - t0) * 1000

        if response == "PONG":
            print(f"[OK]  RTT {rtt_ms:.1f} ms — got '{response}'")
        elif response:
            print(f"[ERR] RTT {rtt_ms:.1f} ms — unexpected '{response}'")
        else:
            print(f"[TMO] No response after {READ_TIMEOUT}s")

        time.sleep(PING_INTERVAL)


if __name__ == "__main__":
    run()
