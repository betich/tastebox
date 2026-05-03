#!/usr/bin/env python3
"""
Interactive serial CLI for tastebox nodes.
Shows live output and accepts friendly commands + raw R/W protocol.

Usage:
    python serial_cli.py [port] [--baud N]
    python serial_cli.py /dev/tty.usbserial-120

Commands (node-agnostic):
    R HH              read register 0xHH
    W HH DD [DD...]   write register 0xHH with data bytes

Plating shortcuts:
    a / goto_a        arm → A (CW)
    b / goto_b        arm → B (CCW)
    stop              arm coast stop
    pan <steps>       move pan stepper (signed int16)
    home              home pan stepper
    dur <ms>          set arm A↔B travel duration

Cooker shortcuts:
    pos <n>           set encoder position
    click             send click
    reset             reset position to 0

Ingredient shortcuts:
    dispense          start dispensing
    retract           start retracting
    idur <ms>         set ingredient duration

Cutter shortcuts:
    open              open lid
    close             close lid
    p1ext / p1ret     piston 1 extend / retract
    p2ext / p2ret     piston 2 extend / retract
    stopall           stop all actuators

General:
    status            read REG_STATUS (0x03) and REG_ARM_STATE (0x02)
    quit / exit       exit
"""

import sys
import readline as _rl
import threading
import serial
import time

PORT    = "/dev/tty.usbserial-120"
BAUD    = 115200
TIMEOUT = 2.0

# ── helpers ───────────────────────────────────────────────────────────────────

def hi(val: int) -> int: return (val >> 8) & 0xFF
def lo(val: int) -> int: return val & 0xFF
def int16(val: int) -> int: return int(val) & 0xFFFF


def send(ser: serial.Serial, line: str) -> str | None:
    """Send a raw R/W command line; return the '!'-prefixed response payload."""
    ser.write((line.strip() + '\n').encode())
    ser.flush()
    deadline = time.time() + TIMEOUT
    while time.time() < deadline:
        raw = ser.readline().decode(errors='replace').strip()
        if raw.startswith('!'):
            return raw[1:]
    return None


def read_reg(ser, reg: int) -> int | None:
    resp = send(ser, f'R {reg:02X}')
    return int(resp, 16) if resp else None


def write_reg(ser, reg: int, *data: int) -> bool:
    payload = ' '.join(f'{b:02X}' for b in data)
    resp = send(ser, f'W {reg:02X} {payload}')
    return resp == 'OK'


# ── register map (plating-centric but covers all nodes) ──────────────────────

REG = {
    # plating
    "PAN_POS_HI": 0x00, "PAN_POS_LO": 0x01,
    "ARM_STATE":  0x02, "STATUS":     0x03,
    "CMD":        0x10, "SET_PAN_HI": 0x11,
    "ARM_CMD":    0x13, "ARM_DUR_HI": 0x14,
    # cooker
    "POS_HI": 0x00, "POS_LO": 0x01, "SW": 0x02,
    "EVT":    0x03, "CCMD":   0x10, "SET_POS": 0x11,
    # ingredient
    "REMAIN_HI": 0x01, "REMAIN_LO": 0x02,
    "ICMD": 0x10, "SET_DUR_HI": 0x11,
}

ARM_STATE = {0: "at_A", 1: "at_B", 2: "moving"}


# ── command parser ────────────────────────────────────────────────────────────

def handle(line: str, ser: serial.Serial) -> bool:
    """Parse a command line. Returns False to quit."""
    parts = line.strip().split()
    if not parts:
        return True
    cmd = parts[0].lower()

    # ── raw protocol pass-through ─────────────────────────────
    if cmd == 'r' and len(parts) == 2:
        resp = send(ser, line)
        print(f"  → {resp}" if resp else "  → (timeout)")
        return True

    if cmd == 'w' and len(parts) >= 3:
        resp = send(ser, line)
        print(f"  → {resp}" if resp else "  → (timeout)")
        return True

    # ── arm ───────────────────────────────────────────────────
    if cmd == 'load' and len(parts) == 2:
        ms  = int(parts[1])
        dur = abs(ms)
        val = int16(dur)
        write_reg(ser, 0x14, hi(val), lo(val))          # set duration
        if ms >= 0:
            write_reg(ser, 0x13, 0x02)                  # CCW (normal, → B)
            print(f"  CCW {dur} ms → return")
            time.sleep(dur / 1000)
            write_reg(ser, 0x13, 0x01)                  # CW back → A
        else:
            write_reg(ser, 0x13, 0x01)                  # CW (abnormal, → A)
            print(f"  CW  {dur} ms → return")
            time.sleep(dur / 1000)
            write_reg(ser, 0x13, 0x02)                  # CCW back → B
        return True

    if cmd in ('a', 'goto_a'):
        write_reg(ser, 0x13, 0x01)
        return True

    if cmd in ('b', 'goto_b'):
        write_reg(ser, 0x13, 0x02)
        return True

    if cmd == 'stop':
        write_reg(ser, 0x13, 0x03)
        return True

    if cmd == 'dur' and len(parts) == 2:
        ms = int16(int(parts[1]))
        write_reg(ser, 0x14, hi(ms), lo(ms))
        print(f"  arm duration set to {int(parts[1])} ms")
        return True

    # ── pan ───────────────────────────────────────────────────
    if cmd == 'pan' and len(parts) == 2:
        steps = int16(int(parts[1]))
        write_reg(ser, 0x11, hi(steps), lo(steps))
        return True

    if cmd == 'home':
        write_reg(ser, 0x10, 0x02)
        return True

    # ── cooker ────────────────────────────────────────────────
    if cmd == 'pos' and len(parts) == 2:
        p = int16(int(parts[1]))
        write_reg(ser, 0x11, hi(p), lo(p))
        return True

    if cmd == 'click':
        write_reg(ser, 0x10, 0x04)
        return True

    if cmd == 'reset':
        write_reg(ser, 0x10, 0x01)
        return True

    # ── ingredient ────────────────────────────────────────────
    if cmd == 'dispense':
        write_reg(ser, 0x10, 0x02)
        return True

    if cmd == 'retract':
        write_reg(ser, 0x10, 0x03)
        return True

    if cmd == 'idur' and len(parts) == 2:
        ms = int16(int(parts[1]))
        write_reg(ser, 0x11, hi(ms), lo(ms))
        print(f"  ingredient duration set to {int(parts[1])} ms")
        return True

    # ── cutter ────────────────────────────────────────────────
    if cmd == 'open':
        write_reg(ser, 0x10, 0x02)
        return True

    if cmd == 'close':
        write_reg(ser, 0x10, 0x03)
        return True

    if cmd == 'p1ext':
        write_reg(ser, 0x10, 0x04)
        return True

    if cmd == 'p1ret':
        write_reg(ser, 0x10, 0x05)
        return True

    if cmd == 'p2ext':
        write_reg(ser, 0x10, 0x06)
        return True

    if cmd == 'p2ret':
        write_reg(ser, 0x10, 0x07)
        return True

    if cmd == 'stopall':
        write_reg(ser, 0x10, 0x01)
        return True

    # ── status ────────────────────────────────────────────────
    if cmd == 'status':
        st  = read_reg(ser, 0x03)
        arm = read_reg(ser, 0x02)
        if st is not None:
            print(f"  STATUS=0x{st:02X}  pan_busy={bool(st&1)}  arm_busy={bool(st&2)}")
        if arm is not None:
            print(f"  ARM_STATE={ARM_STATE.get(arm, arm)}")
        return True

    if cmd == 'help':
        print("""
  ── raw protocol ──────────────────────────────
  R HH                read register 0xHH
  W HH DD [DD...]     write register 0xHH with data bytes

  ── plating: arm ──────────────────────────────
  a  / goto_a         drive CW  (abnormal) → position A
  b  / goto_b         drive CCW (normal)   → position B
  load <ms>           CCW (normal) for ms then return  (neg ms = CW)
  stop                coast motor, reset position to A
  dur <ms>            set A↔B travel duration

  ── plating: pan stepper ──────────────────────
  pan <steps>         move pan by signed steps
  home                home pan stepper

  ── cooker ────────────────────────────────────
  pos <n>             set encoder target position
  click               send click command
  reset               reset position to 0

  ── ingredient ────────────────────────────────
  dispense            start dispensing
  retract             start retracting
  idur <ms>           set ingredient run duration

  ── cutter ────────────────────────────────────
  open / close        open / close lid
  p1ext / p1ret       piston 1 extend / retract
  p2ext / p2ret       piston 2 extend / retract
  stopall             stop all actuators

  ── general ───────────────────────────────────
  status              read STATUS + ARM_STATE registers
  help                show this message
  quit / exit         disconnect and exit
""")
        return True

    if cmd in ('quit', 'exit'):
        return False

    print(f"  unknown: {cmd!r}  (try 'help')")
    return True


# ── reader thread ─────────────────────────────────────────────────────────────

def reader(ser: serial.Serial, stop_event: threading.Event):
    """Continuously print lines from the node that aren't response lines."""
    while not stop_event.is_set():
        try:
            raw = ser.readline()
            if not raw:
                continue
            text = raw.decode(errors='replace').strip()
            if text and not text.startswith('!'):
                # Clear current line, print log message, then restore the
                # readline prompt + any partial input the user has typed.
                sys.stdout.write(f'\r\033[2K{text}\n')
                sys.stdout.flush()
                try:
                    _rl.redisplay()
                except Exception:
                    pass
        except Exception:
            break


# ── main ─────────────────────────────────────────────────────────────────────

def main():
    port = PORT
    baud = BAUD
    for i, arg in enumerate(sys.argv[1:]):
        if arg == '--baud' and i + 2 < len(sys.argv):
            baud = int(sys.argv[i + 2])
        elif not arg.startswith('--'):
            port = arg

    print(f"Connecting to {port} @ {baud} baud …")
    try:
        ser = serial.Serial(port, baud, timeout=0.1)
    except serial.SerialException as e:
        print(f"Error: {e}")
        sys.exit(1)

    time.sleep(2)  # let Arduino reset after DTR toggle
    ser.reset_input_buffer()
    print("Connected. Type commands or R/W protocol. 'quit' to exit.\n")

    stop = threading.Event()
    t = threading.Thread(target=reader, args=(ser, stop), daemon=True)
    t.start()

    try:
        while True:
            try:
                line = input('> ')
            except (EOFError, KeyboardInterrupt):
                break
            if not handle(line, ser):
                break
    finally:
        stop.set()
        ser.close()
        print("\nDisconnected.")


if __name__ == '__main__':
    main()
