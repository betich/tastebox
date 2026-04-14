import time
from bus import I2CBus
from devices import CookerDevice, PlatingArmDevice


def probe_all(devices):
    print("\n── Device scan ──────────────────────────")
    for dev in devices:
        state = "OK" if dev.ping() else "NOT FOUND"
        print(f"  {dev.name:<16} 0x{dev.address:02X}  [{state}]")
    print("─────────────────────────────────────────\n")


def demo(cooker: CookerDevice, arm: PlatingArmDevice):
    print("[demo] cooker status:", cooker.status())
    print("[demo] arm status:   ", arm.status())

    print("\n[demo] clicking cooker power...")
    cooker.click()
    time.sleep(0.5)

    print("[demo] moving cooker to position 3...")
    cooker.set_position(3)
    time.sleep(0.5)

    print("[demo] moving arm — m1=+10 steps, m2=-5 steps...")
    arm.move(10, -5)
    time.sleep(0.5)

    print("[demo] arm status after move:", arm.status())
    print("[demo] cooker status after move:", cooker.status())

    print("\n[demo] homing arm...")
    arm.home()
    print("[demo] resetting cooker position...")
    cooker.reset()


def main():
    with I2CBus(bus_num=1) as bus:
        cooker = CookerDevice(bus)
        arm    = PlatingArmDevice(bus)

        devices = [cooker, arm]
        # To add more devices: instantiate and append here, no other changes needed.
        # e.g. ingredient = IngredientDevice(bus); devices.append(ingredient)

        probe_all(devices)

        online = [d for d in devices if d.ping()]
        if not online:
            print("No devices found. Check wiring and I2C addresses.")
            return

        demo(cooker, arm)


if __name__ == "__main__":
    main()
