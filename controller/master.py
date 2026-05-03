import sys
import time
from bus import I2CBus
from serial_bus import SerialBus
from devices import CookerDevice, PlatingArmDevice, IngredientDevice, CutterDevice
import server
from display import ST7735Display, MachineState


def probe_all(devices):
    print("\n── Device scan ──────────────────────────")
    for dev in devices:
        state = "OK" if dev.ping() else "NOT FOUND"
        print(f"  {dev.name:<16} 0x{dev.address:02X}  [{state}]")
    print("─────────────────────────────────────────\n")


def demo(cooker: CookerDevice, plater: PlatingArmDevice,
         ingredient: IngredientDevice, cutter: CutterDevice):
    print("[demo] cooker status:     ", cooker.status())
    print("[demo] plater status:     ", plater.status())
    print("[demo] ingredient status: ", ingredient.status())
    print("[demo] cutter status:     ", cutter.status())

    print("\n[demo] clicking cooker power...")
    cooker.click()
    time.sleep(0.5)

    print("[demo] moving cooker to position 3...")
    cooker.set_position(3)
    time.sleep(0.5)

    print("[demo] moving pan stepper +10 steps...")
    plater.move_pan(10)
    time.sleep(0.5)

    print("[demo] moving arm to position B...")
    plater.goto_b()
    time.sleep(0.5)

    print("[demo] dispensing ingredient (default duration)...")
    ingredient.dispense()
    time.sleep(0.5)

    print("[demo] opening lid...")
    cutter.open_lid()
    time.sleep(0.5)

    print("[demo] homing arm to position A...")
    plater.goto_a()
    print("[demo] homing plater pan...")
    plater.home_pan()
    print("[demo] resetting cooker position...")
    cooker.reset()
    print("[demo] stopping ingredient...")
    ingredient.stop()
    print("[demo] closing lid...")
    cutter.close_lid()


def main_i2c():
    display = ST7735Display()
    with I2CBus(bus_num=1) as bus:
        cooker     = CookerDevice(bus)
        plater     = PlatingArmDevice(bus)
        ingredient = IngredientDevice(bus)
        cutter     = CutterDevice(bus)

        devices = [cooker, plater, ingredient, cutter]
        probe_all(devices)

        online = [d for d in devices if d.ping()]
        if not online:
            print("No devices found. Check wiring and I2C addresses.")
            return

        server.init(cooker, plater, ingredient, cutter, display)
        server.start()
        display.set_state(MachineState.IDLE)

        demo(cooker, plater, ingredient, cutter)


def main_serial(cooker_port, plater_port, ingredient_port, cutter_port):
    # Each node has its own USB serial connection — one SerialBus per device.
    display = ST7735Display()
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
        probe_all(devices)

        online = [d for d in devices if d.ping()]
        if not online:
            print("No devices found. Check serial ports and baud rate.")
            return

        server.init(cooker, plater, ingredient, cutter, display)
        server.start()
        display.set_state(MachineState.IDLE)

        demo(cooker, plater, ingredient, cutter)
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
