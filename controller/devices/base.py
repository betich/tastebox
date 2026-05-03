from abc import ABC, abstractmethod
from bus import I2CBus


class I2CDevice(ABC):
    def __init__(self, bus: I2CBus, address: int, name: str):
        self.bus = bus
        self.address = address
        self.name = name

    def ping(self) -> bool:
        return self.bus.probe(self.address)

    @abstractmethod
    def status(self) -> dict:
        ...

    def __repr__(self):
        return f"<{self.__class__.__name__} name={self.name!r} addr=0x{self.address:02X}>"
