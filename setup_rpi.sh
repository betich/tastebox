#!/usr/bin/env bash
# Tastebox RPi setup script
# Run once on a fresh Raspberry Pi OS (Bookworm/Bullseye) installation.
# Usage:  bash setup_rpi.sh
#         bash setup_rpi.sh --no-docker   (skip Docker install)
#         bash setup_rpi.sh --no-pio      (skip PlatformIO install)

set -euo pipefail

# ── Colours ───────────────────────────────────────────────────────────────────
R='\033[0;31m'; G='\033[0;32m'; Y='\033[1;33m'; C='\033[0;36m'; N='\033[0m'
info()  { echo -e "${C}[info]${N}  $*"; }
ok()    { echo -e "${G}[ok]${N}    $*"; }
warn()  { echo -e "${Y}[warn]${N}  $*"; }
die()   { echo -e "${R}[error]${N} $*" >&2; exit 1; }

# ── Args ──────────────────────────────────────────────────────────────────────
SKIP_DOCKER=0
SKIP_PIO=0
for arg in "$@"; do
  case $arg in
    --no-docker) SKIP_DOCKER=1 ;;
    --no-pio)    SKIP_PIO=1    ;;
  esac
done

# ── Must run as non-root (uses sudo internally) ───────────────────────────────
[[ $EUID -eq 0 ]] && die "Run as a regular user, not root. The script will sudo as needed."

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONTROLLER_DIR="$REPO_DIR/controller"
WEB_DIR="$REPO_DIR/web"
VENV="$REPO_DIR/.venv"

echo ""
echo -e "${C}╔═══════════════════════════════════════╗${N}"
echo -e "${C}║      Tastebox RPi Setup Script        ║${N}"
echo -e "${C}╚═══════════════════════════════════════╝${N}"
echo ""
info "Repo:       $REPO_DIR"
info "Controller: $CONTROLLER_DIR"
info "Venv:       $VENV"
echo ""

# ── 1. System packages ────────────────────────────────────────────────────────
info "Updating apt and installing system packages..."
sudo apt-get update -qq
sudo apt-get install -y \
  python3 python3-pip python3-venv python3-dev \
  python3-smbus \
  i2c-tools \
  libjpeg-dev zlib1g-dev libfreetype6-dev \
  git curl wget \
  2>/dev/null
ok "System packages installed"

# ── 2. Enable I2C and SPI in boot config ─────────────────────────────────────
info "Enabling I2C and SPI interfaces..."

# Bookworm uses /boot/firmware/config.txt; Bullseye uses /boot/config.txt
if [[ -f /boot/firmware/config.txt ]]; then
  BOOT_CFG=/boot/firmware/config.txt
elif [[ -f /boot/config.txt ]]; then
  BOOT_CFG=/boot/config.txt
else
  die "Cannot locate /boot/config.txt or /boot/firmware/config.txt"
fi

enable_overlay() {
  local overlay="$1"
  if ! grep -q "^$overlay" "$BOOT_CFG" 2>/dev/null; then
    echo "$overlay" | sudo tee -a "$BOOT_CFG" > /dev/null
    info "Added $overlay to $BOOT_CFG"
  else
    info "$overlay already enabled"
  fi
}

enable_overlay "dtparam=i2c_arm=on"
enable_overlay "dtparam=spi=on"

# Also load via raspi-config if available (no-op if already on)
if command -v raspi-config &>/dev/null; then
  sudo raspi-config nonint do_i2c 0  2>/dev/null || true
  sudo raspi-config nonint do_spi 0  2>/dev/null || true
fi

ok "I2C and SPI enabled (reboot required if this was a first-time change)"

# ── 3. GPIO / hardware groups ─────────────────────────────────────────────────
info "Adding $USER to i2c, spi, gpio groups..."
for grp in i2c spi gpio dialout; do
  if getent group "$grp" &>/dev/null; then
    sudo usermod -aG "$grp" "$USER"
  fi
done
ok "Group memberships updated (effective after re-login)"

# ── 4. Python virtual environment + packages ──────────────────────────────────
info "Creating Python venv at $VENV ..."
python3 -m venv "$VENV"
PIP="$VENV/bin/pip"

info "Upgrading pip..."
"$PIP" install --upgrade pip -q

info "Installing Python packages..."
"$PIP" install \
  smbus2 \
  pyserial \
  flask \
  "luma.lcd" \
  pillow \
  RPi.GPIO \
  -q
ok "Python packages installed"

# ── 5. Node.js 20 (for web dev / typechecking) ───────────────────────────────
if ! command -v node &>/dev/null || [[ "$(node -e 'process.stdout.write(process.version.slice(1).split(".")[0])')" -lt 20 ]]; then
  info "Installing Node.js 20 via NodeSource..."
  curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
  sudo apt-get install -y nodejs -qq
  ok "Node.js $(node --version) installed"
else
  ok "Node.js $(node --version) already installed"
fi

# ── 6. Docker (for containerised web serving) ────────────────────────────────
if [[ $SKIP_DOCKER -eq 0 ]]; then
  if ! command -v docker &>/dev/null; then
    info "Installing Docker..."
    curl -fsSL https://get.docker.com | sudo sh
    sudo usermod -aG docker "$USER"
    ok "Docker installed — re-login to use without sudo"
  else
    ok "Docker $(docker --version) already installed"
  fi
else
  warn "Skipping Docker (--no-docker)"
fi

# ── 7. PlatformIO (for Arduino firmware flashing) ────────────────────────────
if [[ $SKIP_PIO -eq 0 ]]; then
  if ! "$VENV/bin/python" -c "import platformio" 2>/dev/null; then
    info "Installing PlatformIO into venv..."
    "$PIP" install platformio -q
    ok "PlatformIO installed"
  else
    ok "PlatformIO already installed"
  fi
else
  warn "Skipping PlatformIO (--no-pio)"
fi

# ── 8. Systemd service — tastebox-controller ─────────────────────────────────
info "Creating systemd service: tastebox-controller..."

sudo tee /etc/systemd/system/tastebox-controller.service > /dev/null <<EOF
[Unit]
Description=Tastebox Hardware Controller API
After=network.target

[Service]
Type=simple
User=$USER
WorkingDirectory=$CONTROLLER_DIR
Environment="PYTHONUNBUFFERED=1"
ExecStart=$VENV/bin/python api.py --host 0.0.0.0 --port 5000
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable tastebox-controller
ok "tastebox-controller service created and enabled"

# ── 9. Systemd service — tastebox-web (Docker) ───────────────────────────────
if [[ $SKIP_DOCKER -eq 0 ]]; then
  info "Creating systemd service: tastebox-web..."

  sudo tee /etc/systemd/system/tastebox-web.service > /dev/null <<EOF
[Unit]
Description=Tastebox Web UI
After=network.target docker.service
Requires=docker.service

[Service]
Type=simple
User=$USER
WorkingDirectory=$WEB_DIR
Environment="CONTROLLER_API_URL=http://localhost:5000"
ExecStartPre=docker build -t tastebox-web .
ExecStart=docker run --rm -p 3000:3000 -e CONTROLLER_API_URL=http://host.docker.internal:5000 tastebox-web
Restart=on-failure
RestartSec=10
TimeoutStartSec=600

[Install]
WantedBy=multi-user.target
EOF

  sudo systemctl daemon-reload
  sudo systemctl enable tastebox-web
  ok "tastebox-web service created and enabled"
fi

# ── 10. .env file for local dev ───────────────────────────────────────────────
ENV_FILE="$REPO_DIR/.env"
if [[ ! -f "$ENV_FILE" ]]; then
  info "Writing .env for local development..."
  cat > "$ENV_FILE" <<'EOF'
# Python controller API base URL (used by web app SSR)
CONTROLLER_API_URL=http://localhost:5000
EOF
  ok ".env written"
else
  warn ".env already exists, skipping"
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo -e "${G}╔═══════════════════════════════════════════════════════╗${N}"
echo -e "${G}║  Setup complete!                                      ║${N}"
echo -e "${G}╚═══════════════════════════════════════════════════════╝${N}"
echo ""
echo -e "  ${Y}REBOOT REQUIRED${N} to activate I2C/SPI and group changes."
echo ""
echo "  Useful commands after reboot:"
echo ""
echo "    # Check I2C devices:"
echo "    i2cdetect -y 1"
echo ""
echo "    # Start/stop the controller API:"
echo "    sudo systemctl start tastebox-controller"
echo "    sudo systemctl status tastebox-controller"
echo "    journalctl -u tastebox-controller -f"
echo ""
if [[ $SKIP_DOCKER -eq 0 ]]; then
echo "    # Start/stop the web UI:"
echo "    sudo systemctl start tastebox-web"
echo "    sudo systemctl status tastebox-web"
echo ""
fi
echo "    # Flash Arduino firmware (from repo root):"
echo "    $VENV/bin/pio run --target upload -d nodes/cooker"
echo "    $VENV/bin/pio run --target upload -d nodes/plating"
echo "    $VENV/bin/pio run --target upload -d nodes/ingredient"
echo "    $VENV/bin/pio run --target upload -d nodes/cutter"
echo ""
echo "    # Run controller manually:"
echo "    cd $CONTROLLER_DIR && $VENV/bin/python api.py"
echo ""
echo "    # Web dev server:"
echo "    cd $WEB_DIR && npm install && npm run dev"
echo ""
