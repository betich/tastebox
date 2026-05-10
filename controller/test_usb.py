#!/usr/bin/env python3
"""USB connectivity test for tastebox nodes.

Discovers all nodes on USB serial ports, pings each one, reads a status
register, and prints a summary.  Run from the controller/ directory:

    python test_usb.py
"""

import sys
import time
import os

sys.path.insert(0, os.path.dirname(__file__))

from lib.usb_discover import discover, NODE_ADDRESSES, BAUD
from lib.node_serial_bus import NodeSerialBus

ADDR_NAMES = {v: k for k, v in NODE_ADDRESSES.items()}  # name → addr, flip it
# REG 0x00 is a valid readable register on every node
PROBE_REG = 0x00


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
    print("── USB node test ─────────────────────────────────")
    print("Scanning ports...")
    found = discover()

    if not found:
        print("No nodes found. Check USB cables and hub.")
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
        status = "OK" if ok else "MISSING"
        print(f"  {name:<12} {status}")
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
