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


def main_i2c():
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

        if online:
            demo(cooker, plater, ingredient, cutter, online)
        else:
            logger.warning("no devices found — running headless (API still available)")

        logger.info("serving — Ctrl-C to quit")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            logger.info("shutting down")


def main_serial(cooker_port, plater_port, ingredient_port, cutter_port):
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

        if online:
            demo(cooker, plater, ingredient, cutter, online)
        else:
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

    if "--serial" in sys.argv:
        ports = [a for a in sys.argv if a.startswith('/dev/') or a.startswith('COM')]
        if len(ports) < 4:
            logger.error("serial mode requires 4 ports: cooker plater ingredient cutter")
            logger.error("  python master.py --serial /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyUSB2 /dev/ttyUSB3")
            sys.exit(1)
        logger.info("mode: serial  ports=%s", ports[:4])
        main_serial(*ports[:4])
    else:
        logger.info("mode: I2C  bus=1")
        main_i2c()


if __name__ == "__main__":
    main()
