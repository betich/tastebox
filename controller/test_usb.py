#!/usr/bin/env python3
"""USB connectivity test for tastebox nodes.

Normal run:
    python test_usb.py

Debug mode (shows raw bytes received on every port):
    python test_usb.py --debug
"""

import sys
import time
import os

sys.path.insert(0, os.path.dirname(__file__))

from lib.usb_discover import discover, NODE_ADDRESSES, BAUD, BOOT_WAIT
from lib.node_serial_bus import NodeSerialBus
from lib.protocol import encode_read

PROBE_REG = 0x00


def debug_port(port: str):
    """Open port, wait for boot, dump everything received, then probe all addresses."""
    import serial
    print(f"\n  [{port}] opening at {BAUD} baud ...")
    try:
        with serial.Serial(port, BAUD, timeout=BOOT_WAIT) as ser:
            print(f"  [{port}] waiting {BOOT_WAIT}s for bootloader ...")
            time.sleep(BOOT_WAIT)
            # Drain and show startup noise
            waiting = ser.in_waiting
            if waiting:
                noise = ser.read(waiting).decode(errors="replace")
                print(f"  [{port}] startup output ({waiting} bytes):")
                for line in noise.splitlines():
                    print(f"           > {line}")
            else:
                print(f"  [{port}] no startup output received")
            ser.reset_input_buffer()
            # Probe all addresses
            for addr in NODE_ADDRESSES:
                name = NODE_ADDRESSES[addr]
                ser.reset_input_buffer()
                cmd = encode_read(addr, PROBE_REG)
                print(f"  [{port}] sending {cmd.strip()} ...", end=" ", flush=True)
                ser.write(cmd)
                raw = ser.readline()
                print(repr(raw))
    except serial.SerialException as e:
        print(f"  [{port}] SerialException: {e}")


def test_node(name: str, addr: int, port: str) -> bool:
    print(f"  [{name}] 0x{addr:02X} on {port} ...", end=" ", flush=True)
    try:
        bus = NodeSerialBus(port, baud=BAUD, timeout=1.0)
        bus.open()
        t0 = time.monotonic()
        val = bus.read_byte(addr, PROBE_REG)
        ms = round((time.monotonic() - t0) * 1000)
        bus.close()
        print(f"OK  reg[0x00]=0x{val:02X}  ({ms} ms)")
        return True
    except Exception as e:
        print(f"FAIL  {e}")
        return False


def main():
    debug = "--debug" in sys.argv

    if debug:
        import glob
        ports = sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
        print("── USB debug mode ────────────────────────────────")
        if not ports:
            print("No serial ports found.")
            sys.exit(1)
        print(f"Found ports: {', '.join(ports)}")
        for port in ports:
            debug_port(port)
        sys.exit(0)

    print("── USB node test ─────────────────────────────────")
    print(f"Scanning ports (boot wait: {BOOT_WAIT}s) ...")
    found = discover()

    if not found:
        print("No nodes found. Try --debug to see raw port output.")
        sys.exit(1)

    print(f"Found {len(found)}/4 node(s):\n")

    results = {}
    for addr, port in sorted(found.items()):
        name = NODE_ADDRESSES[addr]
        results[name] = test_node(name, addr, port)

    missing = [NODE_ADDRESSES[a] for a in NODE_ADDRESSES if a not in found]
    for name in missing:
        print(f"  [{name}] NOT FOUND")
        results[name] = False

    print()
    print("── Summary ───────────────────────────────────────")
    all_ok = True
    for name in ("cooker", "plating", "ingredient", "cutter"):
        ok = results.get(name, False)
        print(f"  {name:<12} {'OK' if ok else 'MISSING'}")
        if not ok:
            all_ok = False

    print("──────────────────────────────────────────────────")
    if all_ok:
        print("All nodes connected.")
    else:
        print(f"{sum(1 for v in results.values() if not v)} node(s) missing — check cables.")
        sys.exit(1)


if __name__ == "__main__":
    main()
