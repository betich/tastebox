import threading
import serial


class SerialBus:
    """
    Drop-in replacement for I2CBus that talks to a single Arduino node over
    a USB serial port.  The `addr` parameter on every method is accepted but
    ignored — each serial port connects to exactly one device.

    Protocol (115200 8N1, newline-terminated ASCII):
        master → node : "R HH\n"           read register 0xHH
        node   → master: "!HH\n"           value (2 hex digits)

        master → node : "W HH DD...\n"     write register 0xHH with data bytes
        node   → master: "!OK\n"           acknowledgement

    Log lines emitted by the node start with '[' and are skipped silently.
    """

    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 2.0):
        self._ser = serial.Serial(port, baudrate, timeout=timeout)
        self._lock = threading.Lock()

    # ── internal ──────────────────────────────────────────────

    def _send(self, line: str) -> str:
        """Send a command line and return the payload after '!'."""
        with self._lock:
            self._ser.write((line + '\n').encode())
            self._ser.flush()
            while True:
                raw = self._ser.readline().decode(errors='replace').strip()
                if raw.startswith('!'):
                    return raw[1:]

    # ── public interface (matches I2CBus) ─────────────────────

    def read_byte(self, addr: int, reg: int) -> int:
        return int(self._send(f'R {reg:02X}'), 16)

    def write_bytes(self, addr: int, reg: int, *data: int):
        payload = ' '.join(f'{b:02X}' for b in data)
        self._send(f'W {reg:02X} {payload}')

    def read_int16(self, addr: int, hi_reg: int, lo_reg: int) -> int:
        hi = self.read_byte(addr, hi_reg)
        lo = self.read_byte(addr, lo_reg)
        val = (hi << 8) | lo
        return val if val < 0x8000 else val - 0x10000

    def probe(self, addr: int) -> bool:
        try:
            self._send('R 00')
            return True
        except Exception:
            return False

    def close(self):
        self._ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
