import logging
import sys
import time

import log
from bus import I2CBus
from serial_bus import SerialBus
from devices import CookerDevice, PlatingArmDevice, IngredientDevice, CutterDevice
import server
from display import SSD1306Display, MachineState

logger = logging.getLogger(__name__)


def probe_all(devices):
    logger.info("── device scan ───────────────────────────")
    results = {}
    for dev in devices:
        ok = dev.ping()
        results[dev] = ok
        logger.info("  %-16s 0x%02X  [%s]", dev.name, dev.address, "OK" if ok else "NOT FOUND")
    logger.info("──────────────────────────────────────────")
    return results


def demo(cooker: CookerDevice, plater: PlatingArmDevice,
         ingredient: IngredientDevice, cutter: CutterDevice, online: set):
    c_ok = cooker     in online
    p_ok = plater     in online
    i_ok = ingredient in online
    x_ok = cutter     in online

    logger.info("cooker status:     %s", cooker.status()     if c_ok else "OFFLINE — skipped")
    logger.info("plater status:     %s", plater.status()     if p_ok else "OFFLINE — skipped")
    logger.info("ingredient status: %s", ingredient.status() if i_ok else "OFFLINE — skipped")
    logger.info("cutter status:     %s", cutter.status()     if x_ok else "OFFLINE — skipped")

    if c_ok:
        logger.info("demo: clicking cooker power")
        cooker.click()
        time.sleep(0.5)
        logger.info("demo: moving cooker to position 3")
        cooker.set_position(3)
        time.sleep(0.5)

    if p_ok:
        logger.info("demo: moving pan stepper +10 steps")
        plater.move_pan(10)
        time.sleep(0.5)
        logger.info("demo: moving arm to position B")
        plater.goto_b()
        time.sleep(0.5)

    if i_ok:
        logger.info("demo: dispensing ingredient (default duration)")
        ingredient.dispense()
        time.sleep(0.5)

    if x_ok:
        logger.info("demo: opening lid")
        cutter.open_lid()
        time.sleep(0.5)

    if p_ok:
        logger.info("demo: homing arm to position A")
        plater.goto_a()
        logger.info("demo: homing plater pan")
        plater.home_pan()
    if c_ok:
        logger.info("demo: resetting cooker position")
        cooker.reset()
    if i_ok:
        logger.info("demo: stopping ingredient")
        ingredient.stop()
    if x_ok:
        logger.info("demo: closing lid")
        cutter.close_lid()

    logger.info("demo: complete")


def main_i2c(run_demo_on_start: bool = False):
    display = SSD1306Display()
    with I2CBus(bus_num=1) as bus:
        cooker     = CookerDevice(bus)
        plater     = PlatingArmDevice(bus)
        ingredient = IngredientDevice(bus)
        cutter     = CutterDevice(bus)

        devices = [cooker, plater, ingredient, cutter]
        results = probe_all(devices)
        online  = {d for d, ok in results.items() if ok}
        offline = set(devices) - online

        if offline:
            names = ", ".join(d.name for d in offline)
            logger.warning("offline devices will be skipped: %s", names)

        # Always start the API server so the web UI can connect
        server.init(cooker, plater, ingredient, cutter, display)
        server.start()
        display.set_state(MachineState.IDLE)

        if online and run_demo_on_start:
            demo(cooker, plater, ingredient, cutter, online)
        elif online:
            logger.info("startup demo disabled — waiting for explicit commands")
        else:
            logger.warning("no devices found — running headless (API still available)")

        logger.info("serving — Ctrl-C to quit")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            logger.info("shutting down")


def main_rs485(port: str = "/dev/ttyUSB0", run_demo_on_start: bool = False):
    from lib.rs485_bus import RS485Bus
    from lib.devices import CookerDevice as _Cooker, PlatingArmDevice as _Plater
    from lib.devices import IngredientDevice as _Ingredient, CutterDevice as _Cutter

    display = SSD1306Display()
    with RS485Bus(port) as bus:
        cooker     = _Cooker(bus)
        plater     = _Plater(bus)
        ingredient = _Ingredient(bus)
        cutter     = _Cutter(bus)

        devices = [cooker, plater, ingredient, cutter]
        results = probe_all(devices)
        online  = {d for d, ok in results.items() if ok}
        offline = set(devices) - online

        if offline:
            names = ", ".join(d.name for d in offline)
            logger.warning("offline devices will be skipped: %s", names)

        server.init(cooker, plater, ingredient, cutter, display)
        server.start()
        display.set_state(MachineState.IDLE)

        if online and run_demo_on_start:
            demo(cooker, plater, ingredient, cutter, online)
        elif online:
            logger.info("startup demo disabled — waiting for explicit commands")
        else:
            logger.warning("no devices found — running headless (API still available)")

        logger.info("serving — Ctrl-C to quit")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            logger.info("shutting down")


class _NullBus:
    """Placeholder for a node that was not found during USB auto-discovery."""
    def read_byte(self, addr, reg):            raise IOError(f"node 0x{addr:02X} not connected")
    def write_bytes(self, addr, reg, *data):   raise IOError(f"node 0x{addr:02X} not connected")
    def read_int16(self, addr, hi, lo):        raise IOError(f"node 0x{addr:02X} not connected")
    def probe(self, addr):                     return False


def main_usb_auto(run_demo_on_start: bool = False, rs485_monitor_port: str | None = None):
    """Default mode: auto-discover nodes on USB serial ports."""
    from lib.usb_discover import discover, NODE_ADDRESSES, BAUD
    from lib.node_serial_bus import NodeSerialBus
    from lib.devices import CookerDevice as _Cooker, PlatingArmDevice as _Plater
    from lib.devices import IngredientDevice as _Ingredient, CutterDevice as _Cutter

    display = SSD1306Display()
    port_map = discover()  # {addr: port_path}

    def _make_bus(addr: int):
        if addr in port_map:
            b = NodeSerialBus(port_map[addr], baud=BAUD)
            b.open()
            return b
        return _NullBus()

    cooker_bus     = _make_bus(0x42)
    plater_bus     = _make_bus(0x43)
    ingredient_bus = _make_bus(0x44)
    cutter_bus     = _make_bus(0x45)

    cooker     = _Cooker(cooker_bus)
    plater     = _Plater(plater_bus)
    ingredient = _Ingredient(ingredient_bus)
    cutter     = _Cutter(cutter_bus)

    devices = [cooker, plater, ingredient, cutter]
    results = probe_all(devices)
    online  = {d for d, ok in results.items() if ok}
    offline = set(devices) - online

    if offline:
        names = ", ".join(d.name for d in offline)
        logger.warning("offline devices will be skipped: %s", names)

    if rs485_monitor_port:
        server.init_rs485_monitor(rs485_monitor_port)

    server.init(cooker, plater, ingredient, cutter, display)
    server.start()
    display.set_state(MachineState.IDLE)

    if online and run_demo_on_start:
        demo(cooker, plater, ingredient, cutter, online)
    elif online:
        logger.info("startup demo disabled — waiting for explicit commands")
    else:
        logger.warning("no devices found — running headless (API still available)")

    logger.info("serving — Ctrl-C to quit")
    opened_buses = [b for b in (cooker_bus, plater_bus, ingredient_bus, cutter_bus)
                    if isinstance(b, NodeSerialBus)]
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        logger.info("shutting down")
    finally:
        for b in opened_buses:
            b.close()


def main_serial_nodes(cooker_port: str, plater_port: str,
                      ingredient_port: str, cutter_port: str,
                      run_demo_on_start: bool = False):
    """One NodeSerialBus (USB serial, same RS485 ASCII protocol) per node."""
    from lib.strategy import open_serial_nodes, close_buses
    from lib.devices import CookerDevice as _Cooker, PlatingArmDevice as _Plater
    from lib.devices import IngredientDevice as _Ingredient, CutterDevice as _Cutter

    display = SSD1306Display()
    buses = open_serial_nodes(cooker_port, plater_port, ingredient_port, cutter_port)
    try:
        cooker     = _Cooker(buses["cooker"])
        plater     = _Plater(buses["plating"])
        ingredient = _Ingredient(buses["ingredient"])
        cutter     = _Cutter(buses["cutter"])

        devices = [cooker, plater, ingredient, cutter]
        results = probe_all(devices)
        online  = {d for d, ok in results.items() if ok}
        offline = set(devices) - online

        if offline:
            names = ", ".join(d.name for d in offline)
            logger.warning("offline devices will be skipped: %s", names)

        server.init(cooker, plater, ingredient, cutter, display)
        server.start()
        display.set_state(MachineState.IDLE)

        if online and run_demo_on_start:
            demo(cooker, plater, ingredient, cutter, online)
        elif online:
            logger.info("startup demo disabled — waiting for explicit commands")
        else:
            logger.warning("no devices found — running headless (API still available)")

        logger.info("serving — Ctrl-C to quit")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            logger.info("shutting down")
    finally:
        close_buses(buses)


def main_serial(cooker_port, plater_port, ingredient_port, cutter_port, run_demo_on_start: bool = False):
    display = SSD1306Display()
    buses = [
        SerialBus(cooker_port),
        SerialBus(plater_port),
        SerialBus(ingredient_port),
        SerialBus(cutter_port),
    ]
    try:
        cooker     = CookerDevice(buses[0])
        plater     = PlatingArmDevice(buses[1])
        ingredient = IngredientDevice(buses[2])
        cutter     = CutterDevice(buses[3])

        devices = [cooker, plater, ingredient, cutter]
        results = probe_all(devices)
        online  = {d for d, ok in results.items() if ok}
        offline = set(devices) - online

        if offline:
            names = ", ".join(d.name for d in offline)
            logger.warning("offline devices will be skipped: %s", names)

        server.init(cooker, plater, ingredient, cutter, display)
        server.start()
        display.set_state(MachineState.IDLE)

        if online and run_demo_on_start:
            demo(cooker, plater, ingredient, cutter, online)
        elif online:
            logger.info("startup demo disabled — waiting for explicit commands")

        if not online:
            logger.warning("no devices found — running headless (API still available)")

        logger.info("serving — Ctrl-C to quit")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            logger.info("shutting down")
    finally:
        for b in buses:
            b.close()


def main():
    log.setup()
    logger.info("Tastebox controller starting")

    run_demo_on_start = "--demo" in sys.argv

    # Optional RS-485 monitor port for admin debug badge
    rs485_monitor_port = None
    if "--rs485-monitor" in sys.argv:
        idx = sys.argv.index("--rs485-monitor")
        if idx + 1 < len(sys.argv) and not sys.argv[idx + 1].startswith("--"):
            rs485_monitor_port = sys.argv[idx + 1]

    if "--i2c" in sys.argv:
        logger.info("mode: I2C  bus=1")
        main_i2c(run_demo_on_start=run_demo_on_start)
    elif "--rs485" in sys.argv:
        idx = sys.argv.index("--rs485")
        port = sys.argv[idx + 1] if idx + 1 < len(sys.argv) and not sys.argv[idx + 1].startswith("--") else "/dev/ttyUSB0"
        logger.info("mode: RS485  port=%s", port)
        main_rs485(port=port, run_demo_on_start=run_demo_on_start)
    elif "--serial-nodes" in sys.argv:
        ports = [a for a in sys.argv if a.startswith('/dev/') or a.startswith('COM')]
        if len(ports) < 4:
            logger.error("serial-nodes mode requires 4 ports: cooker plating ingredient cutter")
            logger.error("  python master.py --serial-nodes /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyUSB2 /dev/ttyUSB3")
            sys.exit(1)
        logger.info("mode: serial-nodes  ports=%s", ports[:4])
        main_serial_nodes(*ports[:4], run_demo_on_start=run_demo_on_start)
    elif "--serial" in sys.argv:
        ports = [a for a in sys.argv if a.startswith('/dev/') or a.startswith('COM')]
        if len(ports) < 4:
            logger.error("serial mode requires 4 ports: cooker plater ingredient cutter")
            logger.error("  python master.py --serial /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyUSB2 /dev/ttyUSB3")
            sys.exit(1)
        logger.info("mode: serial  ports=%s", ports[:4])
        main_serial(*ports[:4], run_demo_on_start=run_demo_on_start)
    else:
        # Default: USB auto-discover nodes across all /dev/ttyUSB* and /dev/ttyACM* ports
        logger.info("mode: USB auto-discover%s",
                    f"  rs485-monitor={rs485_monitor_port}" if rs485_monitor_port else "")
        main_usb_auto(run_demo_on_start=run_demo_on_start,
                      rs485_monitor_port=rs485_monitor_port)


if __name__ == "__main__":
    main()
