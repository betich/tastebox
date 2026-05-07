"""RS-485 connectivity test: RPi master pings Arduino Nano slave."""

import signal
import sys
import time

import serial

UART_PORT = "/dev/ttyUSB0"
BAUD = 9600
PING_INTERVAL = 2.0
READ_TIMEOUT = 1.0


def run():
    ser = serial.Serial(UART_PORT, BAUD, timeout=READ_TIMEOUT)
    ser.rts = True  # assert RTS to enable DE on adapters that require it

    def _cleanup(_sig=None, _frame=None):
        ser.close()
        print("\nCleaned up. Exiting.")
        sys.exit(0)

    signal.signal(signal.SIGINT, _cleanup)

    print(f"RS-485 test: pinging Nano every {PING_INTERVAL}s on {UART_PORT} @ {BAUD} baud")
    print("Ctrl-C to stop.\n")

    while True:
        ser.reset_input_buffer()
        t0 = time.monotonic()
        data = "PING\n".encode()
        ser.write(data)
        ser.flush()

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
