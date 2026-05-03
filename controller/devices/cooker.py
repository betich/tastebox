from bus import I2CBus
from .base import I2CDevice

# Register map — mirrors cooker_controler.ino
REG_POS_HI  = 0x00
REG_POS_LO  = 0x01
REG_SW      = 0x02  # cooktop on/off state
REG_EVT     = 0x03  # bit0=CW, bit1=CCW, bit2=CLICK (clears on read)
REG_CMD     = 0x10
REG_SET_POS = 0x11  # int16 target position (hi byte first)

CMD_RESET = 0x01
CMD_CLICK = 0x04

DEFAULT_ADDRESS = 0x42


class CookerDevice(I2CDevice):
    def __init__(self, bus: I2CBus, address: int = DEFAULT_ADDRESS, name: str = "cooker"):
        super().__init__(bus, address, name)

    # ── reads ────────────────────────────────────────────────

    def get_position(self) -> int:
        return self.bus.read_int16(self.address, REG_POS_HI, REG_POS_LO)

    def is_on(self) -> bool:
        return bool(self.bus.read_byte(self.address, REG_SW))

    def poll_events(self) -> dict:
        """Read and clear the event register. Returns dict with CW/CCW/CLICK flags."""
        flags = self.bus.read_byte(self.address, REG_EVT)
        return {
            "cw":    bool(flags & 0x01),
            "ccw":   bool(flags & 0x02),
            "click": bool(flags & 0x04),
        }

    # ── commands ─────────────────────────────────────────────

    def set_position(self, pos: int):
        pos = int(pos) & 0xFFFF
        self.bus.write_bytes(self.address, REG_SET_POS, pos >> 8, pos & 0xFF)

    def click(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_CLICK)

    def reset(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_RESET)

    # ── base ─────────────────────────────────────────────────

    def status(self) -> dict:
        return {
            "device":   self.name,
            "address":  hex(self.address),
            "online":   self.ping(),
            "position": self.get_position(),
            "on":       self.is_on(),
        }
