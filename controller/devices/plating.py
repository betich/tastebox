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
REG_ARM_DUR_HI = 0x14  # arm duration ms high byte (write, lo follows)
REG_ARM_DUR_LO = 0x15
REG_LID_CMD    = 0x16  # lid commands (write)
REG_LID_DUR_HI = 0x17  # lid duration ms high byte (write, lo follows)
REG_LID_DUR_LO = 0x18
REG_LID_STATE  = 0x04  # lid state: 0=closed, 1=open, 2=moving (read)

CMD_PAN_STOP       = 0x01
CMD_PAN_HOME       = 0x02

CMD_ARM_DISPENSE   = 0x01
CMD_ARM_RETRACT    = 0x02
CMD_ARM_FWD_CONT   = 0x03
CMD_ARM_BWD_CONT   = 0x04
CMD_ARM_STOP       = 0x05

CMD_LID_OPEN       = 0x01
CMD_LID_CLOSE      = 0x02
CMD_LID_FWD_CONT   = 0x03
CMD_LID_BWD_CONT   = 0x04
CMD_LID_STOP       = 0x05

MOT_AT_A   = 0
MOT_AT_B   = 1
MOT_MOVING = 2
ARM_AT_A, ARM_AT_B, ARM_MOVING = MOT_AT_A, MOT_AT_B, MOT_MOVING  # compat

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

    def is_lid_busy(self) -> bool:
        return bool(self.bus.read_byte(self.address, REG_STATUS) & 0x04)

    # ── arm reads ────────────────────────────────────────────

    def get_arm_state(self) -> int:
        return self.bus.read_byte(self.address, REG_ARM_STATE)

    # ── lid reads ────────────────────────────────────────────

    def get_lid_state(self) -> int:
        return self.bus.read_byte(self.address, REG_LID_STATE)

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

    def dispense(self):
        self.bus.write_bytes(self.address, REG_ARM_CMD, CMD_ARM_DISPENSE)

    def retract(self):
        self.bus.write_bytes(self.address, REG_ARM_CMD, CMD_ARM_RETRACT)

    def fwd_cont(self):
        self.bus.write_bytes(self.address, REG_ARM_CMD, CMD_ARM_FWD_CONT)

    def bwd_cont(self):
        self.bus.write_bytes(self.address, REG_ARM_CMD, CMD_ARM_BWD_CONT)

    def stop_arm(self):
        self.bus.write_bytes(self.address, REG_ARM_CMD, CMD_ARM_STOP)

    # ── lid commands ─────────────────────────────────────────

    def set_lid_duration(self, ms: int):
        val = max(0, min(65535, int(ms)))
        self.bus.write_bytes(self.address, REG_LID_DUR_HI, val >> 8, val & 0xFF)

    def open_lid(self):
        self.bus.write_bytes(self.address, REG_LID_CMD, CMD_LID_OPEN)

    def close_lid(self):
        self.bus.write_bytes(self.address, REG_LID_CMD, CMD_LID_CLOSE)

    def lid_fwd_cont(self):
        self.bus.write_bytes(self.address, REG_LID_CMD, CMD_LID_FWD_CONT)

    def lid_bwd_cont(self):
        self.bus.write_bytes(self.address, REG_LID_CMD, CMD_LID_BWD_CONT)

    def stop_lid(self):
        self.bus.write_bytes(self.address, REG_LID_CMD, CMD_LID_STOP)

    # ── kept for compatibility ────────────────────────────────

    def stop(self):
        self.stop_pan()

    def home(self):
        self.home_pan()

    # ── base ─────────────────────────────────────────────────

    def status(self) -> dict:
        labels = {MOT_AT_A: "home", MOT_AT_B: "plate", MOT_MOVING: "moving"}
        lid_labels = {MOT_AT_A: "closed", MOT_AT_B: "open", MOT_MOVING: "moving"}
        return {
            "device":    self.name,
            "address":   hex(self.address),
            "online":    self.ping(),
            "pan_pos":   self.get_pan_position(),
            "pan_busy":  self.is_pan_busy(),
            "arm":       labels.get(self.get_arm_state(), "?"),
            "arm_busy":  self.is_arm_busy(),
            "lid":       lid_labels.get(self.get_lid_state(), "?"),
            "lid_busy":  self.is_lid_busy(),
        }
