from typing import Protocol, runtime_checkable


@runtime_checkable
class BusProtocol(Protocol):
    def read_byte(self, addr: int, reg: int) -> int: ...
    def write_bytes(self, addr: int, reg: int, *data: int) -> None: ...
    def read_int16(self, addr: int, hi_reg: int, lo_reg: int) -> int: ...
    def probe(self, addr: int) -> bool: ...


class BaseDevice:
    def __init__(self, bus: BusProtocol, address: int, name: str):
        self.bus = bus
        self.address = address
        self.name = name

    def ping(self) -> bool:
        return self.bus.probe(self.address)

    def status(self) -> dict:
        raise NotImplementedError

    def __repr__(self):
        return f"<{self.__class__.__name__} name={self.name!r} addr=0x{self.address:02X}>"
