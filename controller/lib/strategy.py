"""
Bus strategy — factory helpers for picking a transport.

All returned bus objects implement BusProtocol and can be passed directly
to any device class in lib/devices/.

Modes
-----
RS485        — one shared serial port for all nodes (recommended for production)
SERIAL_NODES — one USB serial port per node (useful for isolated debugging)
I2C          — legacy SMBus/I2C (Raspberry Pi only)

Examples
--------
# RS485 (shared bus):
with open_rs485("/dev/ttyUSB0") as bus:
    cooker = CookerDevice(bus)

# Per-node USB serial:
buses = open_serial_nodes(
    cooker="/dev/ttyUSB0", plating="/dev/ttyUSB1",
    ingredient="/dev/ttyUSB2", cutter="/dev/ttyUSB3",
)
try:
    cooker = CookerDevice(buses["cooker"])
    ...
finally:
    for b in buses.values(): b.close()

# I2C (Raspberry Pi):
with open_i2c() as bus:
    cooker = CookerDevice(bus)
"""

from __future__ import annotations
from contextlib import contextmanager


# ── RS485 (shared) ────────────────────────────────────────────────────────────

def open_rs485(port: str = "/dev/ttyUSB0", baud: int = 9600,
               timeout: float = 0.2, retries: int = 1):
    """Return an open RS485Bus.  Use as a context manager or call .close()."""
    from .rs485_bus import RS485Bus
    bus = RS485Bus(port, baud, timeout, retries)
    bus.open()
    return bus


# ── Per-node USB serial ───────────────────────────────────────────────────────

_NODE_NAMES = ("cooker", "plating", "ingredient", "cutter")


def open_serial_nodes(cooker: str, plating: str, ingredient: str, cutter: str,
                      baud: int = 9600, timeout: float = 0.5,
                      retries: int = 1) -> dict[str, object]:
    """Open one NodeSerialBus per node; returns {name: bus}.

    Call .close() on each bus when done, or pass the dict to close_buses().
    """
    from .node_serial_bus import NodeSerialBus
    ports = dict(zip(_NODE_NAMES, (cooker, plating, ingredient, cutter)))
    buses: dict[str, NodeSerialBus] = {}
    opened = []
    try:
        for name, port in ports.items():
            b = NodeSerialBus(port, baud, timeout, retries)
            b.open()
            opened.append(b)
            buses[name] = b
    except Exception:
        for b in opened:
            b.close()
        raise
    return buses


def close_buses(buses: dict) -> None:
    """Close all buses returned by open_serial_nodes()."""
    for b in buses.values():
        try:
            b.close()
        except Exception:
            pass


# ── I2C (legacy) ─────────────────────────────────────────────────────────────

def open_i2c(bus_num: int = 1):
    """Return an open I2CBus.  Use as a context manager or call .close().

    Requires smbus2 and a Raspberry Pi (or compatible) I2C bus.
    """
    import sys, os
    sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
    from bus import I2CBus
    return I2CBus(bus_num)
