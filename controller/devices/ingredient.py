from ..bus import I2CBus
from .base import I2CDevice

# Register map — mirrors ingredient/src/main.cpp (I2C slave at 0x44)
REG_STATUS     = 0x00  # bit0=busy, bit1=direction(0=fwd/1=rev) (read)
REG_REMAIN_HI  = 0x01  # remaining duration ms high byte (read, unsigned)
REG_REMAIN_LO  = 0x02  # remaining duration ms low byte (read, unsigned)
REG_CMD        = 0x10
REG_SET_DUR_HI = 0x11  # duration ms high byte (write)

CMD_STOP     = 0x01
CMD_DISPENSE = 0x02
CMD_RETRACT  = 0x03

DEFAULT_ADDRESS = 0x44


class IngredientDevice(I2CDevice):
    def __init__(self, bus: I2CBus, address: int = DEFAULT_ADDRESS, name: str = "ingredient"):
        super().__init__(bus, address, name)

    # ── reads ────────────────────────────────────────────────

    def is_busy(self) -> bool:
        return bool(self.bus.read_byte(self.address, REG_STATUS) & 0x01)

    def is_reversing(self) -> bool:
        return bool(self.bus.read_byte(self.address, REG_STATUS) & 0x02)

    def get_remaining_ms(self) -> int:
        hi = self.bus.read_byte(self.address, REG_REMAIN_HI)
        lo = self.bus.read_byte(self.address, REG_REMAIN_LO)
        return (hi << 8) | lo  # unsigned — do not use read_int16

    # ── commands ─────────────────────────────────────────────

    def set_duration(self, ms: int):
        val = max(0, min(65535, int(ms)))
        self.bus.write_bytes(self.address, REG_SET_DUR_HI, val >> 8, val & 0xFF)

    def dispense(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_DISPENSE)

    def retract(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_RETRACT)

    def stop(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_STOP)

    # ── base ─────────────────────────────────────────────────

    def status(self) -> dict:
        return {
            "device":       self.name,
            "address":      hex(self.address),
            "online":       self.ping(),
            "busy":         self.is_busy(),
            "reversing":    self.is_reversing(),
            "remaining_ms": self.get_remaining_ms(),
        }
