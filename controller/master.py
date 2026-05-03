import sys
import time
from bus import I2CBus
from serial_bus import SerialBus
from devices import CookerDevice, PlatingArmDevice, IngredientDevice, CutterDevice
import server
from display import SSD1306Display, MachineState


def probe_all(devices):
    print("\n── Device scan ──────────────────────────")
    results = {}
    for dev in devices:
        ok = dev.ping()
        results[dev] = ok
        state = "OK" if ok else "NOT FOUND"
        print(f"  {dev.name:<16} 0x{dev.address:02X}  [{state}]")
    print("─────────────────────────────────────────\n")
    return results


def demo(cooker: CookerDevice, plater: PlatingArmDevice,
         ingredient: IngredientDevice, cutter: CutterDevice, online: set):
    c_ok = cooker     in online
    p_ok = plater     in online
    i_ok = ingredient in online
    x_ok = cutter     in online

    print("[demo] cooker status:     ", cooker.status()     if c_ok else "OFFLINE — skipped")
    print("[demo] plater status:     ", plater.status()     if p_ok else "OFFLINE — skipped")
    print("[demo] ingredient status: ", ingredient.status() if i_ok else "OFFLINE — skipped")
    print("[demo] cutter status:     ", cutter.status()     if x_ok else "OFFLINE — skipped")

    if c_ok:
        print("\n[demo] clicking cooker power...")
        cooker.click()
        time.sleep(0.5)
        print("[demo] moving cooker to position 3...")
        cooker.set_position(3)
        time.sleep(0.5)

    if p_ok:
        print("[demo] moving pan stepper +10 steps...")
        plater.move_pan(10)
        time.sleep(0.5)
        print("[demo] moving arm to position B...")
        plater.goto_b()
        time.sleep(0.5)

    if i_ok:
        print("[demo] dispensing ingredient (default duration)...")
        ingredient.dispense()
        time.sleep(0.5)

    if x_ok:
        print("[demo] opening lid...")
        cutter.open_lid()
        time.sleep(0.5)

    if p_ok:
        print("[demo] homing arm to position A...")
        plater.goto_a()
        print("[demo] homing plater pan...")
        plater.home_pan()
    if c_ok:
        print("[demo] resetting cooker position...")
        cooker.reset()
    if i_ok:
        print("[demo] stopping ingredient...")
        ingredient.stop()
    if x_ok:
        print("[demo] closing lid...")
        cutter.close_lid()


def main_i2c():
    display = SSD1306Display()
    with I2CBus(bus_num=1) as bus:
        cooker     = CookerDevice(bus)
        plater     = PlatingArmDevice(bus)
        ingredient = IngredientDevice(bus)
        cutter     = CutterDevice(bus)

        devices = [cooker, plater, ingredient, cutter]
        results = probe_all(devices)

        online = {d for d, ok in results.items() if ok}
        if not online:
            print("No devices found. Check wiring and I2C addresses.")
            return

        offline = {d for d, ok in results.items() if not ok}
        if offline:
            names = ", ".join(d.name for d in offline)
            print(f"[WARN] offline devices will be skipped: {names}")

        server.init(cooker, plater, ingredient, cutter, display)
        server.start()
        display.set_state(MachineState.IDLE)

        demo(cooker, plater, ingredient, cutter, online)


def main_serial(cooker_port, plater_port, ingredient_port, cutter_port):
    # Each node has its own USB serial connection — one SerialBus per device.
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

        online = {d for d, ok in results.items() if ok}
        if not online:
            print("No devices found. Check serial ports and baud rate.")
            return

        offline = {d for d, ok in results.items() if not ok}
        if offline:
            names = ", ".join(d.name for d in offline)
            print(f"[WARN] offline devices will be skipped: {names}")

        server.init(cooker, plater, ingredient, cutter, display)
        server.start()
        display.set_state(MachineState.IDLE)

        demo(cooker, plater, ingredient, cutter, online)
    finally:
        for b in buses:
            b.close()


def main():
    if "--serial" in sys.argv:
        # Usage: python master.py --serial /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyUSB2 /dev/ttyUSB3
        ports = [a for a in sys.argv if a.startswith('/dev/') or a.startswith('COM')]
        if len(ports) < 4:
            print("Serial mode requires 4 ports: cooker plater ingredient cutter")
            print("  python master.py --serial /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyUSB2 /dev/ttyUSB3")
            sys.exit(1)
        main_serial(*ports[:4])
    else:
        main_i2c()


if __name__ == "__main__":
    main()
