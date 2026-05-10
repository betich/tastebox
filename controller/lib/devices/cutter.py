from .base import BaseDevice

# Register map — mirrors cutter/src/main.cpp (RS485 node at 0x45)
REG_STATUS      = 0x00
REG_DOOR_CMD    = 0x11
REG_PINNER_CMD  = 0x12
REG_CUTTER_CMD  = 0x13  # cutter: 0x01=close 0x02=open 0x03=stop
REG_ROLLER_CMD  = 0x14  # roller: 0x01=up   0x02=down  0x03=stop
REG_L_DISP_CMD  = 0x15
REG_R_DISP_CMD  = 0x16
REG_DUR_HI      = 0x18
REG_PUMP_A      = 0x19  # raw PWM 0-255
REG_PUMP_B      = 0x1A  # raw PWM 0-255

# STATUS bits
BIT_DOOR    = 0x01
BIT_PINNER  = 0x02
BIT_ROLLER  = 0x04
BIT_CUTTING = 0x08
BIT_L_DISP  = 0x10
BIT_R_DISP  = 0x20
BIT_PUMP_A  = 0x40
BIT_PUMP_B  = 0x80

DEFAULT_ADDRESS = 0x45


class CutterDevice(BaseDevice):
    def __init__(self, bus, address: int = DEFAULT_ADDRESS, name: str = "cutter"):
        super().__init__(bus, address, name)

    def get_status_flags(self) -> dict:
        flags = self.bus.read_byte(self.address, REG_STATUS)
        return {
            "door_busy":    bool(flags & BIT_DOOR),
            "clamp_busy":   bool(flags & BIT_PINNER),
            "roller_busy":  bool(flags & BIT_ROLLER),
            "scissor_busy": bool(flags & BIT_CUTTING),
            "pepper_busy":  bool(flags & BIT_L_DISP),
            "salt_busy":    bool(flags & BIT_R_DISP),
            "pump_a_busy":  bool(flags & BIT_PUMP_A),
            "pump_b_busy":  bool(flags & BIT_PUMP_B),
        }

    def set_duration(self, ms: int):
        val = max(0, min(65535, int(ms)))
        self.bus.write_bytes(self.address, REG_DUR_HI, val >> 8, val & 0xFF)

    # Door (angle-based: close=0°, open=60°)
    REG_DOOR_ANG = 0x21
    def open_door(self):    self.bus.write_bytes(self.address, 0x21, 60)
    def close_door(self):   self.bus.write_bytes(self.address, 0x21,  0)

    # Pinner (angle-based: stow=0°, pin=30°, hover=60°)
    def pinner_hover(self): self.bus.write_bytes(self.address, 0x20, 60)
    def pinner_pin(self):   self.bus.write_bytes(self.address, 0x20, 30)
    def pinner_stow(self):  self.bus.write_bytes(self.address, 0x20,  0)
    # keep old names as aliases
    def clamp(self):        self.pinner_pin()
    def release(self):      self.pinner_stow()

    # Cutter (register 0x13)
    def cutter_close(self): self.bus.write_bytes(self.address, REG_CUTTER_CMD, 0x01)
    def cutter_open(self):  self.bus.write_bytes(self.address, REG_CUTTER_CMD, 0x02)
    def cutter_stop(self):  self.bus.write_bytes(self.address, REG_CUTTER_CMD, 0x03)

    # Roller piston (register 0x14)
    def roller_up(self):    self.bus.write_bytes(self.address, REG_ROLLER_CMD,  0x01)
    def roller_down(self):  self.bus.write_bytes(self.address, REG_ROLLER_CMD,  0x02)
    def roller_stop(self):  self.bus.write_bytes(self.address, REG_ROLLER_CMD,  0x03)

    # L/R dispensers (salt & pepper servos)
    def pepper_dispense(self): self.bus.write_bytes(self.address, REG_L_DISP_CMD, 0x01)
    def pepper_stop(self):     self.bus.write_bytes(self.address, REG_L_DISP_CMD, 0x02)
    def salt_dispense(self):   self.bus.write_bytes(self.address, REG_R_DISP_CMD, 0x01)
    def salt_stop(self):       self.bus.write_bytes(self.address, REG_R_DISP_CMD, 0x02)

    # Pumps (0 = off, 1-255 = PWM duty)
    def set_pump_a(self, pwm: int): self.bus.write_bytes(self.address, REG_PUMP_A, max(0, min(255, int(pwm))))
    def set_pump_b(self, pwm: int): self.bus.write_bytes(self.address, REG_PUMP_B, max(0, min(255, int(pwm))))
    def pump_a_on(self):   self.set_pump_a(255)
    def pump_a_off(self):  self.set_pump_a(0)
    def pump_b_on(self):   self.set_pump_b(255)
    def pump_b_off(self):  self.set_pump_b(0)

    def status(self) -> dict:
        flags = self.get_status_flags() if self.ping() else {
            "door_busy": False, "clamp_busy": False,
            "roller_busy": False, "scissor_busy": False,
            "pepper_busy": False, "salt_busy": False,
            "pump_a_busy": False, "pump_b_busy": False,
        }
        return {
            "device":  self.name,
            "address": hex(self.address),
            "online":  self.ping(),
            **flags,
        }
