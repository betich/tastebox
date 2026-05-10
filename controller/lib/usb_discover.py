"""USB serial auto-discovery for tastebox nodes.

Scans /dev/ttyUSB* and /dev/ttyACM* ports at 115200 baud,
probes each known node address using the ASCII register protocol,
and returns {address: port_path} for every found node.
"""

import concurrent.futures
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


def _probe_port(path: str) -> int | None:
    """Open one serial port and return the node address that responds, or None."""
    try:
        with serial.Serial(path, BAUD, timeout=PROBE_TIMEOUT,
                           dsrdtr=False, rtscts=False) as ser:
            time.sleep(0.1)  # let any startup noise drain
            for addr in NODE_ADDRESSES:
                ser.reset_input_buffer()
                ser.write(encode_read(addr, 0x00))
                raw = ser.readline().decode(errors="ignore")
                if not raw.startswith(f"@{addr:02X} "):
                    continue
                parts = raw.split()
                if len(parts) >= 2 and parts[1] not in ("R", "W"):
                    return addr
    except serial.SerialException:
        pass
    return None


def discover() -> dict[int, str]:
    """Scan all USB serial ports and return {node_address: port_path}.

    Ports are probed in parallel to keep total scan time short.
    Nodes not found are logged as warnings.
    """
    ports = sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
    if not ports:
        logger.warning("usb-discover: no serial ports found")
        return {}

    logger.info("usb-discover: scanning %d port(s): %s", len(ports), ", ".join(ports))
    found: dict[int, str] = {}

    with concurrent.futures.ThreadPoolExecutor(max_workers=len(ports)) as pool:
        future_to_port = {pool.submit(_probe_port, p): p for p in ports}
        for future in concurrent.futures.as_completed(future_to_port):
            path = future_to_port[future]
            addr = future.result()
            if addr is not None:
                name = NODE_ADDRESSES[addr]
                logger.info("usb-discover: 0x%02X (%s) → %s", addr, name, path)
                found[addr] = path
            else:
                logger.debug("usb-discover: no node on %s", path)

    missing = [NODE_ADDRESSES[a] for a in NODE_ADDRESSES if a not in found]
    if missing:
        logger.warning("usb-discover: nodes not found: %s", ", ".join(missing))
    return found
