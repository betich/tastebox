from ..bus import I2CBus
from .base import I2CDevice

# Register map — mirrors plating/src/main.cpp (I2C slave at 0x43)
REG_PAN_POS_HI  = 0x00  # pan stepper position high byte (read)
REG_PAN_POS_LO  = 0x01  # pan stepper position low byte (read)
REG_SERVO_ANGLE = 0x02  # servo arm angle 0-180 (read)
REG_STATUS      = 0x03  # bit0 = stepper busy (read)
REG_CMD         = 0x10
REG_SET_PAN_HI  = 0x11  # int16 pan target (hi byte, lo byte in one write)
REG_SET_SERVO   = 0x13  # servo angle 0-180 (write)

CMD_STOP = 0x01
CMD_HOME = 0x02

DEFAULT_ADDRESS = 0x43


class PlatingArmDevice(I2CDevice):
    def __init__(self, bus: I2CBus, address: int = DEFAULT_ADDRESS, name: str = "plater"):
        super().__init__(bus, address, name)

    # ── reads ────────────────────────────────────────────────

    def get_pan_position(self) -> int:
        return self.bus.read_int16(self.address, REG_PAN_POS_HI, REG_PAN_POS_LO)

    def get_servo_angle(self) -> int:
        return self.bus.read_byte(self.address, REG_SERVO_ANGLE)

    def is_busy(self) -> bool:
        return bool(self.bus.read_byte(self.address, REG_STATUS) & 0x01)

    # ── commands ─────────────────────────────────────────────

    def move_pan(self, steps: int):
        val = int(steps) & 0xFFFF
        self.bus.write_bytes(self.address, REG_SET_PAN_HI, val >> 8, val & 0xFF)

    def set_servo(self, angle: int):
        self.bus.write_bytes(self.address, REG_SET_SERVO, max(0, min(180, int(angle))))

    def stop(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_STOP)

    def home(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_HOME)

    # ── base ─────────────────────────────────────────────────

    def status(self) -> dict:
        return {
            "device":      self.name,
            "address":     hex(self.address),
            "online":      self.ping(),
            "pan_pos":     self.get_pan_position(),
            "servo_angle": self.get_servo_angle(),
            "busy":        self.is_busy(),
        }
