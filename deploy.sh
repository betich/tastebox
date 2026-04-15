#!/bin/bash
# Deploy Tastebox (web UI + controller API) to Raspberry Pi and restart services.
# Usage: ./deploy.sh [rpi-host]          (default: rpi)
#
# First-time setup on the Pi (installs systemd services + kiosk):
#   ./deploy.sh --setup [rpi-host]
#
# Prerequisites on the Pi (done once manually):
#   sudo apt install -y python3-venv nodejs npm chromium-browser
#   sudo raspi-config  →  Interface Options → I2C → Enable

set -euo pipefail

# ── flags ──────────────────────────────────────────────────────────────────────
SETUP=false
if [[ "${1:-}" == "--setup" ]]; then
  SETUP=true
  shift
fi

HOST=${1:-rpi}
REMOTE_DIR="tastebox"

# ── helpers ────────────────────────────────────────────────────────────────────
info()  { echo "==> $*"; }
die()   { echo "Error: $*" >&2; exit 1; }

# ── probe the Pi ───────────────────────────────────────────────────────────────
info "Probing $HOST …"

REMOTE_USER=$(ssh "$HOST" whoami)
NODE=$(ssh  "$HOST" "bash -li -c 'which node'    2>/dev/null || true")
NPM=$(ssh   "$HOST" "bash -li -c 'which npm'     2>/dev/null || true")
PYTHON=$(ssh "$HOST" "bash -li -c 'which python3' 2>/dev/null || true")
CHROMIUM=$(ssh "$HOST" "bash -li -c 'which chromium-browser || which chromium' 2>/dev/null || true")

[[ -n "$NODE"    ]] || die "node not found on $HOST  — run: sudo apt install nodejs"
[[ -n "$NPM"     ]] || die "npm not found on $HOST   — run: sudo apt install npm"
[[ -n "$PYTHON"  ]] || die "python3 not found on $HOST — run: sudo apt install python3"

info "node:    $NODE"
info "npm:     $NPM"
info "python3: $PYTHON"

# ── sync source ────────────────────────────────────────────────────────────────
info "Syncing source to $HOST:~/$REMOTE_DIR …"
rsync -avz --progress \
  --exclude 'node_modules'   \
  --exclude '.cache'         \
  --exclude 'build'          \
  --exclude '__pycache__'    \
  --exclude '*.pyc'          \
  --exclude 'venv'           \
  --exclude '.env'           \
  --exclude '.DS_Store'      \
  "$(dirname "$0")/" \
  "$HOST:$REMOTE_DIR/"

# ── build web on the Pi ────────────────────────────────────────────────────────
info "Building web on Pi (this may take a minute on first run) …"
ssh "$HOST" "
  set -e
  cd $REMOTE_DIR/web
  $NPM ci
  $NPM run build
"

# ── set up Python venv & install deps ─────────────────────────────────────────
info "Setting up Python venv on Pi …"
ssh "$HOST" "
  set -e
  cd $REMOTE_DIR
  $PYTHON -m venv venv
  venv/bin/pip install -q --upgrade pip
  venv/bin/pip install -q -r requirements.txt
"

# ── first-time setup: install systemd units + kiosk ───────────────────────────
if $SETUP; then
  info "Installing systemd service units …"

  # ── tastebox-api.service ──────────────────────────────────────────────────
  ssh -t "$HOST" "sudo tee /etc/systemd/system/tastebox-api.service > /dev/null" << EOF
[Unit]
Description=Tastebox Controller API (Flask / I2C)
After=network.target

[Service]
Type=simple
User=$REMOTE_USER
WorkingDirectory=/home/$REMOTE_USER/$REMOTE_DIR/controller
ExecStart=/home/$REMOTE_USER/$REMOTE_DIR/venv/bin/python api.py --host 127.0.0.1 --port 5000
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

  # ── tastebox-web.service ─────────────────────────────────────────────────
  ssh -t "$HOST" "sudo tee /etc/systemd/system/tastebox-web.service > /dev/null" << EOF
[Unit]
Description=Tastebox Web UI (React Router)
After=network.target tastebox-api.service

[Service]
Type=simple
User=$REMOTE_USER
WorkingDirectory=/home/$REMOTE_USER/$REMOTE_DIR/web
Environment=NODE_ENV=production
Environment=PORT=3000
Environment=CONTROLLER_API_URL=http://127.0.0.1:5000
ExecStart=$NODE /home/$REMOTE_USER/$REMOTE_DIR/web/node_modules/.bin/react-router-serve ./build/server/index.js
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

  # ── tastebox-kiosk.service ───────────────────────────────────────────────
  [[ -n "$CHROMIUM" ]] || { echo "Warning: chromium not found — kiosk service skipped. Run: sudo apt install chromium-browser" >&2; }

  ssh -t "$HOST" "sudo tee /etc/systemd/system/tastebox-kiosk.service > /dev/null" << EOF
[Unit]
Description=Tastebox Kiosk (Chromium fullscreen)
After=graphical.target tastebox-web.service
Wants=graphical.target

[Service]
Type=simple
User=$REMOTE_USER
Environment=DISPLAY=:0
Environment=XAUTHORITY=/home/$REMOTE_USER/.Xauthority
# Wait for the web server to be ready
ExecStartPre=/bin/sleep 6
ExecStart=${CHROMIUM:-chromium-browser} \\
  --kiosk \\
  --app=http://localhost:3000 \\
  --noerrdialogs \\
  --disable-infobars \\
  --disable-session-crashed-bubble \\
  --disable-component-update \\
  --check-for-update-interval=31536000 \\
  --disable-pinch \\
  --overscroll-history-navigation=0
Restart=on-failure
RestartSec=10

[Install]
WantedBy=graphical.target
EOF

  # ── hide cursor & disable screen blanking ────────────────────────────────
  info "Configuring display (no cursor, no blanking) …"
  ssh -t "$HOST" "
    # Install unclutter if available (hides idle mouse cursor)
    sudo apt-get install -y unclutter 2>/dev/null || true

    # Disable display power management in X
    if ! grep -q 'tastebox-display' /home/$REMOTE_USER/.bashrc 2>/dev/null; then
      echo '# tastebox-display: disable blanking' >> /home/$REMOTE_USER/.bashrc
      echo 'xset s off; xset -dpms; xset s noblank' >> /home/$REMOTE_USER/.bashrc
    fi
  " || true

  # ── enable & start all services ─────────────────────────────────────────
  info "Enabling and starting services …"
  ssh -t "$HOST" "
    sudo systemctl daemon-reload
    sudo systemctl enable tastebox-api tastebox-web tastebox-kiosk
    sudo systemctl start  tastebox-api tastebox-web tastebox-kiosk
  "

  info "Setup complete!"
  echo ""
  echo "    Logs:"
  echo "      ssh $HOST 'journalctl -u tastebox-api -f'"
  echo "      ssh $HOST 'journalctl -u tastebox-web -f'"
  echo "      ssh $HOST 'journalctl -u tastebox-kiosk -f'"

else
  # ── rolling restart ───────────────────────────────────────────────────────
  info "Restarting services …"
  ssh "$HOST" "
    sudo systemctl restart tastebox-api
    sudo systemctl restart tastebox-web
    sudo systemctl restart tastebox-kiosk 2>/dev/null || true
  "

  info "Done! Tastebox is running on $HOST"
  echo ""
  echo "    Web UI:         http://$HOST:3000"
  echo "    Controller API: http://$HOST:5000/status"
  echo "    Logs: ssh $HOST 'journalctl -u tastebox-web -f'"
fi
