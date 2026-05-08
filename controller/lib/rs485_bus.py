import threading
import time
import serial
from .protocol import encode_read, encode_write, decode_response
from .interceptor import CommandInterceptor


class RS485Bus:
    """Thread-safe RS485 bus backed by a single serial port."""

    def __init__(self, port: str = "/dev/ttyUSB0", baud: int = 9600,
                 timeout: float = 0.1, retries: int = 1):
        self._port = port
        self._baud = baud
        self._timeout = timeout
        self._retries = retries
        self._ser: serial.Serial | None = None
        self._lock = threading.Lock()
        self._interceptors: list[CommandInterceptor] = []

    def open(self):
        self._ser = serial.Serial(self._port, self._baud, timeout=self._timeout)

    def close(self):
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, *_):
        self.close()

    def add_interceptor(self, interceptor: CommandInterceptor):
        self._interceptors.append(interceptor)

    def _readline_valid(self, addr: int) -> str:
        """Read lines until we get one addressed to addr that isn't an echo of our own frame."""
        deadline = self._timeout * (self._retries + 1) + 0.05
        t0 = time.monotonic()
        while (time.monotonic() - t0) < deadline:
            raw = self._ser.readline().decode(errors="ignore")
            if not raw:
                break
            prefix = f"@{addr:02X} "
            if not raw.startswith(prefix):
                continue
            # Skip echo: echoed read frames look like "@NN R RR\n"
            # echoed write frames look like "@NN W RR DD\n"
            parts = raw.split()
            if len(parts) >= 2 and parts[1] in ("R", "W"):
                continue
            return raw
        return ""

    def read_byte(self, addr: int, reg: int) -> int:
        for ic in self._interceptors:
            ic.before_read(addr, reg)

        last_err: Exception = IOError(f"no response from 0x{addr:02X} reg 0x{reg:02X}")
        for _ in range(self._retries + 1):
            with self._lock:
                self._ser.reset_input_buffer()
                self._ser.write(encode_read(addr, reg))
                raw = self._readline_valid(addr)
            if raw:
                val = decode_response(raw)
                if isinstance(val, int):
                    for ic in self._interceptors:
                        val = ic.after_read(addr, reg, val)
                    return val
                last_err = IOError(f"unexpected response: {raw.strip()!r}")
        raise last_err

    def write_bytes(self, addr: int, reg: int, *data: int):
        payload = list(data)
        for ic in self._interceptors:
            payload = ic.before_write(addr, reg, payload)

        last_err: Exception = IOError(f"no response from 0x{addr:02X} reg 0x{reg:02X}")
        for _ in range(self._retries + 1):
            with self._lock:
                self._ser.reset_input_buffer()
                self._ser.write(encode_write(addr, reg, *payload))
                raw = self._readline_valid(addr)
            if raw:
                result = decode_response(raw)
                if result == "OK":
                    for ic in self._interceptors:
                        ic.after_write(addr, reg, payload)
                    return
                if result == "ERR":
                    raise IOError(f"device 0x{addr:02X} returned ERR for reg 0x{reg:02X}")
                last_err = IOError(f"unexpected response: {raw.strip()!r}")
        raise last_err

    def read_int16(self, addr: int, hi_reg: int, lo_reg: int) -> int:
        hi = self.read_byte(addr, hi_reg)
        lo = self.read_byte(addr, lo_reg)
        val = (hi << 8) | lo
        return val - 0x10000 if val >= 0x8000 else val

    def probe(self, addr: int) -> bool:
        try:
            self.read_byte(addr, 0x00)
            return True
        except IOError:
            return False
