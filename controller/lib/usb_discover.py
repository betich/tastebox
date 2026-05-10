"""USB serial auto-discovery for tastebox nodes.

Scans /dev/ttyUSB* and /dev/ttyACM* ports at 115200 baud,
probes each known node address using the ASCII register protocol,
and returns {address: port_path} for every found node.
"""

import glob
import logging
import time

import serial

from .protocol import encode_read, decode_response

logger = logging.getLogger(__name__)

NODE_ADDRESSES: dict[int, str] = {
    0x42: "cooker",
    0x43: "plating",
    0x44: "ingredient",
    0x45: "cutter",
}

BAUD = 115200
PROBE_TIMEOUT = 0.5
BOOT_WAIT = 2.5  # seconds to wait after port open for bootloader + sketch startup


def discover_open() -> dict[int, serial.Serial]:
    """Scan all USB serial ports and return {node_address: open_serial}.

    Opens all ports at once (all Arduinos reset simultaneously), waits a single
    BOOT_WAIT, then probes each port sequentially.  The serial connections are
    kept open and ready for immediate use — no second open() call, no second
    Arduino reset.  Caller is responsible for closing the serials when done.
    """
    ports = sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
    if not ports:
        logger.warning("usb-discover: no serial ports found")
        return {}

    logger.info("usb-discover: scanning %d port(s): %s", len(ports), ", ".join(ports))

    # Open all ports quickly so all Arduinos reset at the same time.
    open_ports: list[tuple[str, serial.Serial]] = []
    for path in ports:
        try:
            ser = serial.Serial(path, BAUD, timeout=PROBE_TIMEOUT)
            open_ports.append((path, ser))
        except serial.SerialException as e:
            logger.warning("usb-discover: cannot open %s: %s", path, e)

    if not open_ports:
        return {}

    time.sleep(BOOT_WAIT)  # one wait covers all ports

    found: dict[int, serial.Serial] = {}
    for path, ser in open_ports:
        ser.reset_input_buffer()
        matched = False
        for addr in NODE_ADDRESSES:
            ser.reset_input_buffer()
            ser.write(encode_read(addr, 0x00))
            raw = ser.readline().decode(errors="ignore")
            if not raw.startswith(f"@{addr:02X} "):
                continue
            parts = raw.split()
            if len(parts) >= 2 and parts[1] not in ("R", "W"):
                name = NODE_ADDRESSES[addr]
                logger.info("usb-discover: 0x%02X (%s) → %s", addr, name, path)
                found[addr] = ser
                matched = True
                break
        if not matched:
            logger.debug("usb-discover: no node identified on %s", path)
            ser.close()

    missing = [NODE_ADDRESSES[a] for a in NODE_ADDRESSES if a not in found]
    if missing:
        logger.warning("usb-discover: nodes not found: %s", ", ".join(missing))
    return found


def discover() -> dict[int, str]:
    """Scan all USB serial ports and return {node_address: port_path}.

    Thin wrapper around discover_open() that closes the serials after mapping
    addresses to port paths.
    """
    result = discover_open()
    paths: dict[int, str] = {}
    for addr, ser in result.items():
        paths[addr] = ser.port
        ser.close()
    return paths
