#!/usr/bin/env python3
"""RS485 node diagnostic — tests each node directly without going through master.py.

Usage:
  python rs485_check.py [port]        default: /dev/ttyUSB0
  python rs485_check.py [port] -v     verbose: show raw TX/RX bytes
  python rs485_check.py [port] -r     also read a few key registers per node

Examples:
  python rs485_check.py
  python rs485_check.py /dev/ttyUSB1 -v -r
"""
import sys
import time
import serial

PORT    = next((a for a in sys.argv[1:] if not a.startswith("-")), "/dev/ttyUSB0")
BAUD    = 9600
TIMEOUT = 0.5   # generous — covers slow step-loop nodes
VERBOSE = "-v" in sys.argv
REGS    = "-r" in sys.argv

NODES = [
    ("cooker",     0x42),
    ("plating",    0x43),
    ("ingredient", 0x44),
    ("cutter",     0x45),
]

# Extra registers to read when -r is given (reg, label)
NODE_REGS = {
    0x42: [(0x00, "pos_hi"), (0x01, "pos_lo"), (0x02, "power")],
    0x43: [(0x00, "pan_hi"), (0x01, "pan_lo"), (0x02, "arm"), (0x03, "status")],
    0x44: [(0x00, "status_A"), (0x01, "status_B"), (0x02, "status_C")],
    0x45: [(0x00, "status")],
}


def exchange(ser: serial.Serial, addr: int, reg: int, write_data: list[int] | None = None):
    """Send a read or write frame, return (response_line, elapsed_ms)."""
    if write_data is None:
        frame = f"@{addr:02X} R {reg:02X}\n"
    else:
        body = " ".join(f"{b:02X}" for b in write_data)
        frame = f"@{addr:02X} W {reg:02X} {body}\n"

    ser.reset_input_buffer()
    if VERBOSE:
        print(f"    TX: {frame.encode()!r}")

    ser.write(frame.encode())
    t0 = time.monotonic()

    # Drain lines until we find a valid (non-echo) response for this address
    deadline = t0 + TIMEOUT
    response = ""
    while time.monotonic() < deadline:
        raw = ser.readline().decode(errors="replace")
        if not raw:
            break
        if VERBOSE:
            print(f"    RX: {raw.encode()!r}  ({(time.monotonic()-t0)*1000:.1f} ms)")
        if not raw.startswith(f"@{addr:02X} "):
            continue
        parts = raw.split()
        if len(parts) >= 2 and parts[1] in ("R", "W"):
            continue  # echo of our own frame
        response = raw
        break

    elapsed = (time.monotonic() - t0) * 1000
    return response.strip(), elapsed


def probe(ser: serial.Serial, addr: int):
    """Probe by reading register 0x00. Returns (ok, elapsed_ms, raw_value)."""
    resp, ms = exchange(ser, addr, 0x00)
    if not resp:
        return False, ms, None
    parts = resp.split()
    try:
        val = int(parts[1], 16) if len(parts) >= 2 else None
        return True, ms, val
    except ValueError:
        return False, ms, None


def main():
    print(f"╔══ RS485 Check  port={PORT}  baud={BAUD}  timeout={TIMEOUT}s ══╗")
    print()

    try:
        ser = serial.Serial(PORT, BAUD, timeout=TIMEOUT)
    except serial.SerialException as e:
        print(f"  ERROR: cannot open {PORT}: {e}")
        sys.exit(1)

    time.sleep(0.1)  # let port settle after open

    results = []
    for name, addr in NODES:
        ok, ms, val = probe(ser, addr)
        sym = "✓" if ok else "✗"
        ms_str = f"{ms:.0f} ms" if ok else f">{TIMEOUT*1000:.0f} ms"
        reg_str = f"  reg[0x00]=0x{val:02X}" if val is not None else ""
        print(f"  {sym} [{name}] 0x{addr:02X}  {ms_str}{reg_str}")
        results.append((name, addr, ok))

    if REGS:
        print()
        print("  ── register dump ──────────────────────────")
        for name, addr, ok in results:
            if not ok:
                print(f"  [{name}] skipped (not found)")
                continue
            print(f"  [{name}] 0x{addr:02X}")
            for reg, label in NODE_REGS.get(addr, []):
                resp, ms = exchange(ser, addr, reg)
                parts = resp.split() if resp else []
                try:
                    val = int(parts[1], 16) if len(parts) >= 2 else None
                    print(f"    0x{reg:02X} {label:12s} = 0x{val:02X}  ({ms:.0f} ms)")
                except (ValueError, TypeError):
                    print(f"    0x{reg:02X} {label:12s} = ERR  resp={resp!r}")

    ser.close()

    online = sum(1 for _, _, ok in results if ok)
    print()
    print(f"  {online}/{len(NODES)} nodes responding")
    if online == 0:
        print()
        print("  Checklist:")
        print("    • Is the RS485 adapter connected? (ls /dev/ttyUSB*)")
        print("    • Are all nodes powered?")
        print("    • Is the bus wired correctly? (A/B polarity, termination)")
        print("    • Are all nodes flashed with RS485 firmware?")
        print(f"    • Try: pio device monitor -d nodes/cooker -b 115200")
    print()


if __name__ == "__main__":
    main()
