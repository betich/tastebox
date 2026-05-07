from .base import BaseDevice

REG_POS_HI  = 0x00
REG_POS_LO  = 0x01
REG_SW      = 0x02
REG_EVT     = 0x03
REG_CMD     = 0x10
REG_SET_POS = 0x11

CMD_RESET = 0x01
CMD_CLICK = 0x04

DEFAULT_ADDRESS = 0x42


class CookerDevice(BaseDevice):
    def __init__(self, bus, address: int = DEFAULT_ADDRESS, name: str = "cooker"):
        super().__init__(bus, address, name)

    def get_position(self) -> int:
        return self.bus.read_int16(self.address, REG_POS_HI, REG_POS_LO)

    def is_on(self) -> bool:
        return bool(self.bus.read_byte(self.address, REG_SW))

    def poll_events(self) -> dict:
        flags = self.bus.read_byte(self.address, REG_EVT)
        return {
            "cw":    bool(flags & 0x01),
            "ccw":   bool(flags & 0x02),
            "click": bool(flags & 0x04),
        }

    def set_position(self, pos: int):
        pos = int(pos) & 0xFFFF
        self.bus.write_bytes(self.address, REG_SET_POS, pos >> 8, pos & 0xFF)

    def click(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_CLICK)

    def reset(self):
        self.bus.write_bytes(self.address, REG_CMD, CMD_RESET)

    def status(self) -> dict:
        return {
            "device":   self.name,
            "address":  hex(self.address),
            "online":   self.ping(),
            "position": self.get_position(),
            "on":       self.is_on(),
        }
