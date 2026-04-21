from ..bus import I2CBus
from .base import I2CDevice

# Register map — mirrors cutter/src/main.cpp (I2C slave at 0x45)
REG_STATUS       = 0x00  # bit0=lid_busy, bit1=p1_busy, bit2=p2_busy (read)
REG_CMD          = 0x10
REG_SERVO1_ANGLE = 0x11  # servo1 (pin 9) angle 0-180 (write)
REG_SERVO2_ANGLE = 0x12  # servo2 (pin 11) angle 0-180 (write)
REG_LID_DUR_HI   = 0x13  # lid duration ms high byte (write)
REG_PST_DUR_HI   = 0x15  # piston duration ms high byte (write)

CMD_STOP_ALL    = 0x01
CMD_OPEN_LID    = 0x02
CMD_CLOSE_LID   = 0x03
CMD_PISTON1_EXT = 0x04
CMD_PISTON1_RET = 0x05
CMD_PISTON2_EXT = 0x06
CMD_PISTON2_RET = 0x07

DEFAULT_ADDRESS = 0x45


class CutterDevice(I2CDevice):
    def __init__(self, bus: I2CBus, address: int = DEFAULT_ADDRESS, name: str = "cutter"):
        super().__init__(bus, address, name)

    # ── reads ────────────────────────────────────────────────

    def get_status_flags(self) -> dict:
        flags = self.bus.read_byte(self.address, REG_STATUS)
        return {
            "lid_busy":     bool(flags & 0x01),
            "piston1_busy": bool(flags & 0x02),
            "piston2_busy": bool(flags & 0x04),
        }

    # ── duration setters ─────────────────────────────────────

    def set_lid_duration(self, ms: int):
        val = max(0, min(65535, int(ms)))
        self.bus.write_bytes(self.address, REG_LID_DUR_HI, val >> 8, val & 0xFF)

    def set_piston_duration(self, ms: int):
        val = max(0, min(65535, int(ms)))
        self.bus.write_bytes(self.address, REG_PST_DUR_HI, val >> 8, val & 0xFF)

    # ── servo setters ────────────────────────────────────────

    def set_servo1(self, angle: int):
        self.bus.write_bytes(self.address, REG_SERVO1_ANGLE, max(0, min(180, int(angle))))

    def set_servo2(self, angle: int):
        self.bus.write_bytes(self.address, REG_SERVO2_ANGLE, max(0, min(180, int(angle))))

    # ── action commands ──────────────────────────────────────

    def open_lid(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_OPEN_LID)

    def close_lid(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_CLOSE_LID)

    def piston1_extend(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_PISTON1_EXT)

    def piston1_retract(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_PISTON1_RET)

    def piston2_extend(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_PISTON2_EXT)

    def piston2_retract(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_PISTON2_RET)

    def stop_all(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_STOP_ALL)

    # ── base ─────────────────────────────────────────────────

    def status(self) -> dict:
        flags = self.get_status_flags()
        return {
            "device":       self.name,
            "address":      hex(self.address),
            "online":       self.ping(),
            **flags,
        }
