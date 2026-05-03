from bus import I2CBus
from .base import I2CDevice

# Register map — mirrors plating/src/main.cpp (I2C slave at 0x43)
REG_PAN_POS_HI = 0x00  # pan stepper position high byte (read)
REG_PAN_POS_LO = 0x01  # pan stepper position low byte (read)
REG_ARM_STATE  = 0x02  # arm state: 0=at_A, 1=at_B, 2=moving (read)
REG_STATUS     = 0x03  # bit0=pan busy, bit1=arm busy (read)
REG_CMD        = 0x10  # pan commands (write)
REG_SET_PAN_HI = 0x11  # pan target high byte (write, lo byte follows)
REG_SET_PAN_LO = 0x12  # pan target low byte (write, follows hi)
REG_ARM_CMD    = 0x13  # arm commands (write)
REG_ARM_DUR_HI = 0x14  # arm A↔B travel duration ms high byte (write, lo follows)
REG_ARM_DUR_LO = 0x15  # arm A↔B travel duration ms low byte (write, follows hi)

CMD_PAN_STOP   = 0x01
CMD_PAN_HOME   = 0x02

CMD_ARM_GOTO_A = 0x01  # run CW (abnormal) until duration elapses → at A
CMD_ARM_GOTO_B = 0x02  # run CCW (normal)  until duration elapses → at B
CMD_ARM_STOP   = 0x03  # coast immediately (position treated as A after stop)

ARM_AT_A   = 0
ARM_AT_B   = 1
ARM_MOVING = 2

DEFAULT_ADDRESS = 0x43


class PlatingArmDevice(I2CDevice):
    def __init__(self, bus: I2CBus, address: int = DEFAULT_ADDRESS, name: str = "plater"):
        super().__init__(bus, address, name)

    # ── pan reads ────────────────────────────────────────────

    def get_pan_position(self) -> int:
        return self.bus.read_int16(self.address, REG_PAN_POS_HI, REG_PAN_POS_LO)

    def is_pan_busy(self) -> bool:
        return bool(self.bus.read_byte(self.address, REG_STATUS) & 0x01)

    def is_arm_busy(self) -> bool:
        return bool(self.bus.read_byte(self.address, REG_STATUS) & 0x02)

    # ── arm reads ────────────────────────────────────────────

    def get_arm_state(self) -> int:
        """Returns ARM_AT_A (0), ARM_AT_B (1), or ARM_MOVING (2)."""
        return self.bus.read_byte(self.address, REG_ARM_STATE)

    # ── pan commands ─────────────────────────────────────────

    def move_pan(self, steps: int):
        val = int(steps) & 0xFFFF
        self.bus.write_bytes(self.address, REG_SET_PAN_HI, val >> 8, val & 0xFF)

    def stop_pan(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_PAN_STOP)

    def home_pan(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_PAN_HOME)

    # ── arm commands ─────────────────────────────────────────

    def set_arm_duration(self, ms: int):
        """Set the A↔B travel time (same for both directions)."""
        val = max(0, min(65535, int(ms)))
        self.bus.write_bytes(self.address, REG_ARM_DUR_HI, val >> 8, val & 0xFF)

    def goto_a(self):
        """Drive arm CW (abnormal direction) for the programmed duration → position A."""
        self.bus.write_bytes(self.address, REG_ARM_CMD, CMD_ARM_GOTO_A)

    def goto_b(self):
        """Drive arm CCW (normal direction) for the programmed duration → position B."""
        self.bus.write_bytes(self.address, REG_ARM_CMD, CMD_ARM_GOTO_B)

    def stop_arm(self):
        """Coast motor immediately. Arm position is reset to A."""
        self.bus.write_bytes(self.address, REG_ARM_CMD, CMD_ARM_STOP)

    # ── kept for compatibility ────────────────────────────────

    def stop(self):
        self.stop_pan()

    def home(self):
        self.home_pan()

    # ── base ─────────────────────────────────────────────────

    def status(self) -> dict:
        arm_raw = self.get_arm_state()
        arm_str = {ARM_AT_A: "at_A", ARM_AT_B: "at_B", ARM_MOVING: "moving"}.get(arm_raw, "?")
        return {
            "device":    self.name,
            "address":   hex(self.address),
            "online":    self.ping(),
            "pan_pos":   self.get_pan_position(),
            "pan_busy":  self.is_pan_busy(),
            "arm":       arm_str,
            "arm_busy":  self.is_arm_busy(),
        }
