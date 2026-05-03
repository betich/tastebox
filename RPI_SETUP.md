# Raspberry Pi Setup

## Prerequisites

- Raspberry Pi OS (Bookworm or Bullseye) flashed and booted
- SSH or direct terminal access
- Internet connection

---

## Steps

1. Clone the repo
   ```bash
   git clone https://github.com/betich/tastebox.git
   cd tastebox
   ```

2. Run the setup script
   ```bash
   bash setup_rpi.sh
   ```
   - Installs system packages, Python venv, Node.js, Docker, PlatformIO
   - Enables I2C and SPI in `/boot/firmware/config.txt`
   - Adds your user to `i2c`, `spi`, `gpio`, `dialout` groups
   - Creates and enables `tastebox-controller` and `tastebox-web` systemd services

3. Reboot
   ```bash
   sudo reboot
   ```

4. Verify I2C devices are detected (should see 0x42, 0x43, 0x44, 0x45)
   ```bash
   i2cdetect -y 1
   ```

5. Start the controller API
   ```bash
   sudo systemctl start tastebox-controller
   sudo systemctl status tastebox-controller
   ```

6. Start the web UI
   ```bash
   sudo systemctl start tastebox-web
   sudo systemctl status tastebox-web
   ```
   Web UI is available at `http://<pi-ip>:3000`

7. Flash Arduino firmware (repeat for each node)
   ```bash
   .venv/bin/pio run --target upload -d nodes/cooker
   .venv/bin/pio run --target upload -d nodes/plating
   .venv/bin/pio run --target upload -d nodes/ingredient
   .venv/bin/pio run --target upload -d nodes/cutter
   ```

---

## Wiring

### SSD1306 OLED (0.96") → RPi I2C1

| SSD1306 | RPi GPIO        |
|---------|-----------------|
| SDA     | GPIO 2 (SDA1)   |
| SCL     | GPIO 3 (SCL1)   |
| VCC     | 3.3V            |
| GND     | GND             |

I2C address: `0x3C`

### Arduino Nodes → RPi I2C

| Node       | SDA     | SCL     | I2C Address |
|------------|---------|---------|-------------|
| cooker     | GPIO 2  | GPIO 3  | 0x42        |
| plating    | GPIO 2  | GPIO 3  | 0x43        |
| ingredient | GPIO 2  | GPIO 3  | 0x44        |
| cutter     | GPIO 2  | GPIO 3  | 0x45        |

All four nodes share the same I2C bus (GPIO 2/3).

---

## Useful commands

```bash
# View controller logs
journalctl -u tastebox-controller -f

# View web logs
journalctl -u tastebox-web -f

# Restart services
sudo systemctl restart tastebox-controller
sudo systemctl restart tastebox-web

# Run controller manually (without systemd)
cd controller && ../.venv/bin/python api.py

# Open serial monitor for a node
.venv/bin/pio device monitor -d nodes/cooker --baud 115200
```
