"""RS-485 self-echo test — USB adapter handles DE/RE automatically."""

import serial

UART_PORT = "/dev/ttyUSB0"
BAUD = 9600

ser = serial.Serial(UART_PORT, BAUD, timeout=1.0)
print(f"RS-485 self-echo test on {UART_PORT} @ {BAUD} baud\n")

try:
    for i in range(5):
        msg = f"HELLO{i}\n"
        data = msg.encode()
        ser.reset_input_buffer()
        ser.write(data)
        ser.flush()
        echo = ser.read(len(data)).decode(errors="replace")
        if echo == msg:
            print(f"[OK]  '{msg.strip()}' echoed — TX chain works")
        elif echo:
            print(f"[ERR] sent '{msg.strip()}' got '{echo.strip()}'")
        else:
            print(f"[TMO] no echo — check A/B wiring")
finally:
    ser.close()
