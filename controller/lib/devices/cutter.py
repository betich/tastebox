from .base import BaseDevice

# Register map — mirrors cutter/src/main.cpp (RS485 node at 0x45)
REG_STATUS      = 0x00
REG_DOOR_CMD    = 0x11
REG_CLAMP_CMD   = 0x12
REG_ROLLER_CMD  = 0x13
REG_SCISSOR_CMD = 0x14
REG_PEPPER_CMD  = 0x15
REG_PUMP_CMD    = 0x16
REG_SALT_CMD    = 0x17
REG_DUR_HI      = 0x18

# STATUS bits
BIT_DOOR    = 0x01
BIT_CLAMP   = 0x02
BIT_ROLLER  = 0x04
BIT_SCISSOR = 0x08
BIT_PEPPER  = 0x10
BIT_PUMP    = 0x20
BIT_SALT    = 0x40

DEFAULT_ADDRESS = 0x45


class CutterDevice(BaseDevice):
    def __init__(self, bus, address: int = DEFAULT_ADDRESS, name: str = "cutter"):
        super().__init__(bus, address, name)

    def get_status_flags(self) -> dict:
        flags = self.bus.read_byte(self.address, REG_STATUS)
        return {
            "door_busy":    bool(flags & BIT_DOOR),
            "clamp_busy":   bool(flags & BIT_CLAMP),
            "roller_busy":  bool(flags & BIT_ROLLER),
            "scissor_busy": bool(flags & BIT_SCISSOR),
            "pepper_busy":  bool(flags & BIT_PEPPER),
            "pump_busy":    bool(flags & BIT_PUMP),
            "salt_busy":    bool(flags & BIT_SALT),
        }

    def set_duration(self, ms: int):
        val = max(0, min(65535, int(ms)))
        self.bus.write_bytes(self.address, REG_DUR_HI, val >> 8, val & 0xFF)

    def open_door(self):    self.bus.write_bytes(self.address, REG_DOOR_CMD,    0x01)
    def close_door(self):   self.bus.write_bytes(self.address, REG_DOOR_CMD,    0x02)
    def clamp(self):        self.bus.write_bytes(self.address, REG_CLAMP_CMD,   0x01)
    def release(self):      self.bus.write_bytes(self.address, REG_CLAMP_CMD,   0x02)
    def roller_fwd(self):   self.bus.write_bytes(self.address, REG_ROLLER_CMD,  0x01)
    def roller_rev(self):   self.bus.write_bytes(self.address, REG_ROLLER_CMD,  0x02)
    def roller_stop(self):  self.bus.write_bytes(self.address, REG_ROLLER_CMD,  0x03)
    def scissor_fwd(self):  self.bus.write_bytes(self.address, REG_SCISSOR_CMD, 0x01)
    def scissor_rev(self):  self.bus.write_bytes(self.address, REG_SCISSOR_CMD, 0x02)
    def scissor_stop(self): self.bus.write_bytes(self.address, REG_SCISSOR_CMD, 0x03)
    def pepper_dispense(self): self.bus.write_bytes(self.address, REG_PEPPER_CMD, 0x01)
    def pepper_stop(self):     self.bus.write_bytes(self.address, REG_PEPPER_CMD, 0x02)
    def pump_on(self):      self.bus.write_bytes(self.address, REG_PUMP_CMD,    0x01)
    def pump_off(self):     self.bus.write_bytes(self.address, REG_PUMP_CMD,    0x02)
    def salt_dispense(self): self.bus.write_bytes(self.address, REG_SALT_CMD,   0x01)
    def salt_stop(self):     self.bus.write_bytes(self.address, REG_SALT_CMD,   0x02)

    def status(self) -> dict:
        flags = self.get_status_flags() if self.ping() else {
            "door_busy": False, "clamp_busy": False,
            "roller_busy": False, "scissor_busy": False,
            "pepper_busy": False, "pump_busy": False, "salt_busy": False,
        }
        return {
            "device":  self.name,
            "address": hex(self.address),
            "online":  self.ping(),
            **flags,
        }
