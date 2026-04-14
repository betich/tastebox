from smbus2 import SMBus


class I2CBus:
    def __init__(self, bus_num: int = 1):
        self._bus = SMBus(bus_num)

    def read_byte(self, addr: int, reg: int) -> int:
        return self._bus.read_byte_data(addr, reg)

    def write_bytes(self, addr: int, reg: int, *data: int):
        self._bus.write_i2c_block_data(addr, reg, list(data))

    def read_int16(self, addr: int, hi_reg: int, lo_reg: int) -> int:
        hi = self.read_byte(addr, hi_reg)
        lo = self.read_byte(addr, lo_reg)
        val = (hi << 8) | lo
        return val if val < 0x8000 else val - 0x10000

    def probe(self, addr: int) -> bool:
        try:
            self._bus.read_byte(addr)
            return True
        except OSError:
            return False

    def close(self):
        self._bus.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
