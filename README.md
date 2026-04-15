# Robo Cooker

Senior Project for Robotics and Artificial Intelligence Engineering.

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

| Service | What it runs | Port |
|---------|-------------|------|
| `tastebox-api` | Flask controller API (`controller/api.py`) | 5000 |
| `tastebox-web` | React Router web UI (`react-router-serve`) | 3000 |
| `tastebox-kiosk` | Chromium in kiosk mode → `localhost:3000` | — |

### Log tailing

```bash
ssh rpi 'journalctl -u tastebox-api    -f'
ssh rpi 'journalctl -u tastebox-web    -f'
ssh rpi 'journalctl -u tastebox-kiosk  -f'
```
