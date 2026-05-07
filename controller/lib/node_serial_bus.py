from .rs485_bus import RS485Bus


class NodeSerialBus(RS485Bus):
    """RS485-protocol bus over a direct USB-serial connection to one node.

    Identical to RS485Bus (same @{ADDR} ASCII frame format) but defaults to a
    longer timeout (0.5 s) suited for reliable USB serial rather than the
    potentially-noisy RS485 bus.  One instance per node; addr is still passed
    through to match the BusProtocol interface but only one device is expected
    to respond.

    Usage:
        with NodeSerialBus("/dev/ttyUSB1") as bus:
            cooker = CookerDevice(bus)
            print(cooker.get_position())
    """

    def __init__(self, port: str, baud: int = 9600,
                 timeout: float = 0.5, retries: int = 1):
        super().__init__(port, baud, timeout, retries)
