import time
from bus import I2CBus
from devices import CookerDevice, PlatingArmDevice, IngredientDevice, CutterDevice


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

    print("[demo] setting arm servo to 45°...")
    plater.set_servo(45)
    time.sleep(0.5)

    print("[demo] dispensing ingredient (default duration)...")
    ingredient.dispense()
    time.sleep(0.5)

    print("[demo] opening lid...")
    cutter.open_lid()
    time.sleep(0.5)

    print("[demo] homing plater pan...")
    plater.home()
    print("[demo] resetting cooker position...")
    cooker.reset()
    print("[demo] stopping ingredient...")
    ingredient.stop()
    print("[demo] closing lid...")
    cutter.close_lid()


def main():
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

        demo(cooker, plater, ingredient, cutter)


if __name__ == "__main__":
    main()
