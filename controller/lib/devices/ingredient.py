from .base import BaseDevice

REG_STATUS_A = 0x00
REG_STATUS_B = 0x01
REG_STATUS_C = 0x02
REG_CMD      = 0x10
REG_REV_HI   = 0x11
REG_REV_LO   = 0x12
REG_SPD_HI   = 0x13
REG_SPD_LO   = 0x14

CMD_STOP_ALL   = 0x01
CMD_A_FWD_CONT = 0x02
CMD_A_BWD_CONT = 0x03
CMD_A_DISPENSE = 0x04
CMD_A_RETRACT  = 0x05
CMD_B_FWD_CONT = 0x06
CMD_B_BWD_CONT = 0x07
CMD_B_DISPENSE = 0x08
CMD_B_RETRACT  = 0x09
CMD_STOP_A     = 0x0A
CMD_STOP_B     = 0x0B
CMD_C_FWD_CONT = 0x0C
CMD_C_BWD_CONT = 0x0D
CMD_C_DISPENSE = 0x0E
CMD_C_RETRACT  = 0x0F
CMD_STOP_C     = 0x10

DEFAULT_ADDRESS = 0x44


class IngredientDevice(BaseDevice):
    def __init__(self, bus, address: int = DEFAULT_ADDRESS, name: str = "ingredient"):
        super().__init__(bus, address, name)

    def is_busy(self) -> bool:
        a = self.bus.read_byte(self.address, REG_STATUS_A) & 0x01
        b = self.bus.read_byte(self.address, REG_STATUS_B) & 0x01
        c = self.bus.read_byte(self.address, REG_STATUS_C) & 0x01
        return bool(a or b or c)

    def is_busy_a(self) -> bool:
        return bool(self.bus.read_byte(self.address, REG_STATUS_A) & 0x01)

    def is_busy_b(self) -> bool:
        return bool(self.bus.read_byte(self.address, REG_STATUS_B) & 0x01)

    def is_busy_c(self) -> bool:
        return bool(self.bus.read_byte(self.address, REG_STATUS_C) & 0x01)

    def set_steps_per_rev(self, steps: int):
        val = max(1, min(65535, int(steps)))
        self.bus.write_bytes(self.address, REG_REV_HI, val >> 8, val & 0xFF)

    def set_speed(self, half_us: int):
        """Set step half-delay in µs (lower = faster). Min 50, default 800."""
        val = max(50, min(65535, int(half_us)))
        self.bus.write_bytes(self.address, REG_SPD_HI, val >> 8, val & 0xFF)

    def a_fwd(self):    self.bus.write_bytes(self.address, REG_CMD, CMD_A_FWD_CONT)
    def a_bwd(self):    self.bus.write_bytes(self.address, REG_CMD, CMD_A_BWD_CONT)
    def dispense(self): self.bus.write_bytes(self.address, REG_CMD, CMD_A_DISPENSE)
    def retract(self):  self.bus.write_bytes(self.address, REG_CMD, CMD_A_RETRACT)
    def stop_a(self):   self.bus.write_bytes(self.address, REG_CMD, CMD_STOP_A)

    def b_fwd(self):      self.bus.write_bytes(self.address, REG_CMD, CMD_B_FWD_CONT)
    def b_bwd(self):      self.bus.write_bytes(self.address, REG_CMD, CMD_B_BWD_CONT)
    def b_dispense(self): self.bus.write_bytes(self.address, REG_CMD, CMD_B_DISPENSE)
    def b_retract(self):  self.bus.write_bytes(self.address, REG_CMD, CMD_B_RETRACT)
    def stop_b(self):     self.bus.write_bytes(self.address, REG_CMD, CMD_STOP_B)

    def c_fwd(self):      self.bus.write_bytes(self.address, REG_CMD, CMD_C_FWD_CONT)
    def c_bwd(self):      self.bus.write_bytes(self.address, REG_CMD, CMD_C_BWD_CONT)
    def c_dispense(self): self.bus.write_bytes(self.address, REG_CMD, CMD_C_DISPENSE)
    def c_retract(self):  self.bus.write_bytes(self.address, REG_CMD, CMD_C_RETRACT)
    def stop_c(self):     self.bus.write_bytes(self.address, REG_CMD, CMD_STOP_C)

    def stop(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_STOP_ALL)

    def status(self) -> dict:
        online = self.ping()
        sa = self.bus.read_byte(self.address, REG_STATUS_A) if online else 0
        sb = self.bus.read_byte(self.address, REG_STATUS_B) if online else 0
        sc = self.bus.read_byte(self.address, REG_STATUS_C) if online else 0
        return {
            "device":  self.name,
            "address": hex(self.address),
            "online":  online,
            "busy":    bool((sa | sb | sc) & 0x01),
            "a_busy":  bool(sa & 0x01),
            "b_busy":  bool(sb & 0x01),
            "c_busy":  bool(sc & 0x01),
        }
