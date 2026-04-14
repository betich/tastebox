from ..bus import I2CBus
from .base import I2CDevice

# Register map — mirrors plating_arm.ino (I2C strategy, address 0x43)
REG_M1_POS_HI = 0x00
REG_M1_POS_LO = 0x01
REG_M2_POS_HI = 0x02
REG_M2_POS_LO = 0x03
REG_STATUS    = 0x04  # bit0=M1_busy, bit1=M2_busy
REG_CMD       = 0x10
REG_SET_M1_HI = 0x11  # int16 motor 1 target (hi byte first)
REG_SET_M2_HI = 0x13  # int16 motor 2 target (hi byte first)

CMD_STOP = 0x01
CMD_HOME = 0x02

DEFAULT_ADDRESS = 0x43


class PlatingArmDevice(I2CDevice):
    def __init__(self, bus: I2CBus, address: int = DEFAULT_ADDRESS, name: str = "plating_arm"):
        super().__init__(bus, address, name)

    # ── reads ────────────────────────────────────────────────

    def get_positions(self) -> tuple[int, int]:
        m1 = self.bus.read_int16(self.address, REG_M1_POS_HI, REG_M1_POS_LO)
        m2 = self.bus.read_int16(self.address, REG_M2_POS_HI, REG_M2_POS_LO)
        return m1, m2

    def is_busy(self) -> tuple[bool, bool]:
        flags = self.bus.read_byte(self.address, REG_STATUS)
        return bool(flags & 0x01), bool(flags & 0x02)

    # ── commands ─────────────────────────────────────────────

    def move(self, m1_steps: int, m2_steps: int):
        m1 = int(m1_steps) & 0xFFFF
        m2 = int(m2_steps) & 0xFFFF
        self.bus.write_bytes(self.address, REG_SET_M1_HI, m1 >> 8, m1 & 0xFF)
        self.bus.write_bytes(self.address, REG_SET_M2_HI, m2 >> 8, m2 & 0xFF)

    def stop(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_STOP)

    def home(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_HOME)

    # ── base ─────────────────────────────────────────────────

    def status(self) -> dict:
        m1_pos, m2_pos = self.get_positions()
        m1_busy, m2_busy = self.is_busy()
        return {
            "device":   self.name,
            "address":  hex(self.address),
            "online":   self.ping(),
            "m1_pos":   m1_pos,
            "m2_pos":   m2_pos,
            "m1_busy":  m1_busy,
            "m2_busy":  m2_busy,
        }
