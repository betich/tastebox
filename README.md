# Robo Cooker

Senior Project for Robotics and Artificial Intelligence Engineering.

## System Architecture

```mermaid
graph TD
    A["Kiosk UI<br/>(Raspberry Pi Browser)"] -->|HTTP| B["Controller Backend<br/>(Python Flask)"]
    B -->|I2C SMBus| C["Cooker<br/>(0x42)"]
    B -->|I2C SMBus| D["Plating<br/>(0x43)"]
    B -->|I2C SMBus| E["Ingredient<br/>(0x44)"]
    B -->|I2C SMBus| F["Cutter<br/>(TBD)"]

    C --> C1["Electric Cooker Module"]
    D --> D1["DC Motor +<br/>Stepper Motor"]
    E --> E1["Stepper Motors"]
    F --> F1["Servo Motor +<br/>Linear Pistons +<br/>DC Motor"]

    style A fill:#e1f5ff
    style B fill:#fff3e0
    style C fill:#f3e5f5
    style D fill:#f3e5f5
    style E fill:#f3e5f5
    style F fill:#f3e5f5
```

### Components

| Layer        | Technology                  | Role                                      |
| ------------ | --------------------------- | ----------------------------------------- |
| **UI**       | React Router 7 (TypeScript) | Web interface for order customization     |
| **Backend**  | Python Flask                | I2C controller API; device communication  |
| **Devices**  | Arduino Nano × 4            | I2C-enabled firmware for hardware control |
| **Hardware** | Motors, sensors, relays     | Physical cooking and plating apparatus    |

## Arduino Firmware (nodes)

Uses [PlatformIO](https://platformio.org/). Install it once with:

```bash
pipx install platformio
```

Then for any node (`cooker`, `plating`, `ingredient`, `cutter`):

```bash
cd nodes/<node>
pio run                  # compile
pio run -t upload        # flash to Arduino Nano via USB
pio device monitor       # serial monitor at 115200 baud
```

---

## Deployment

Run once to install systemd services and kiosk mode on the Pi:

```bash
./deploy.sh --setup [rpi-host]   # default host: rpi
```

Subsequent deploys (sync + build + restart):

```bash
./deploy.sh [rpi-host]
```

**Pi prerequisites (done once manually):**

```bash
sudo apt install -y python3-venv nodejs npm chromium-browser unclutter
sudo raspi-config   # Interface Options → I2C → Enable
```

### Services

| Service          | What it runs                               | Port |
| ---------------- | ------------------------------------------ | ---- |
| `tastebox-api`   | Flask controller API (`controller/api.py`) | 5000 |
| `tastebox-web`   | React Router web UI (`react-router-serve`) | 3000 |
| `tastebox-kiosk` | Chromium in kiosk mode → `localhost:3000`  | —    |

### Log tailing

```bash
ssh rpi 'journalctl -u tastebox-api    -f'
ssh rpi 'journalctl -u tastebox-web    -f'
ssh rpi 'journalctl -u tastebox-kiosk  -f'
```

---

## Hardware

### System Diagram

![](docs/diagram.jpg)
_Full system wiring and control architecture_

### Cutting Mechanism

![](docs/cutting.jpg)
_Cutter assembly with lid opener and linear pistons_

### Ingredient Coil

![](docs/integredients-coil.jpg)
_Stepper motor-driven coil for ingredient dispensing_

### Serving

![](docs/serving.jpg)
_Plating arm delivering food to the serving position_

![](docs/serving-2.jpg)
_Alternate view of the serving sequence_
