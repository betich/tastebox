"""UART loopback test — no MAX485, no GPIO. Jumper TX (Pin 8) → RX (Pin 10)."""

import serial

UART_PORT = "/dev/ttyAMA0"
BAUD = 9600

def run():
    ser = serial.Serial(UART_PORT, BAUD, timeout=1.0)
    print(f"UART loopback test on {UART_PORT} @ {BAUD} baud")
    print("Jumper GPIO14 (Pin 8, TX) → GPIO15 (Pin 10, RX)")
    print("Ctrl-C to stop.\n")

    try:
        for i in range(5):
            msg = f"HELLO{i}\n"
            ser.reset_input_buffer()
            ser.write(msg.encode())
            ser.flush()
            echo = ser.readline().decode(errors="replace")
            if echo.strip() == msg.strip():
                print(f"[OK]  sent '{msg.strip()}' — echoed back correctly")
            elif echo:
                print(f"[ERR] sent '{msg.strip()}' — got '{echo.strip()}'")
            else:
                print(f"[TMO] sent '{msg.strip()}' — no echo (check TX→RX jumper)")
    finally:
        ser.close()

if __name__ == "__main__":
    run()
