"""
Flask HTTP API for Tastebox hardware control.

Endpoints
---------
GET  /status            — probe both devices and return full status
GET  /cooker/status     — cooker full status
GET  /cooker/position   — current encoder position
GET  /cooker/events     — poll & clear event flags (CW/CCW/CLICK)
POST /cooker/position   — set target position  body: {"position": <int>}
POST /cooker/click      — simulate encoder click
POST /cooker/reset      — reset encoder position to 0

GET  /plating/status    — plating arm full status
POST /plating/move      — move steppers  body: {"m1": <int>, "m2": <int>}
POST /plating/stop      — emergency stop both motors
POST /plating/home      — run homing sequence
"""

from flask import Flask, jsonify, request
from bus import I2CBus
from devices import CookerDevice, PlatingArmDevice

app = Flask(__name__)

# ── Hardware singletons (opened once at startup) ──────────────────────────────
_bus    = I2CBus(bus_num=1)
cooker  = CookerDevice(_bus)
plating = PlatingArmDevice(_bus)


# ── helpers ───────────────────────────────────────────────────────────────────

def _ok(data: dict | None = None, **kwargs):
    payload = {"ok": True}
    if data:
        payload.update(data)
    payload.update(kwargs)
    return jsonify(payload)


def _err(message: str, status: int = 400):
    return jsonify({"ok": False, "error": message}), status


def _require_json(*fields):
    """Return parsed body dict or raise ValueError with a helpful message."""
    body = request.get_json(silent=True) or {}
    missing = [f for f in fields if f not in body]
    if missing:
        raise ValueError(f"Missing required fields: {missing}")
    return body


# ── global ────────────────────────────────────────────────────────────────────

@app.get("/status")
def all_status():
    return _ok(
        cooker=cooker.status(),
        plating=plating.status(),
    )


# ── cooker ────────────────────────────────────────────────────────────────────

@app.get("/cooker/status")
def cooker_status():
    return _ok(**cooker.status())


@app.get("/cooker/position")
def cooker_position():
    return _ok(position=cooker.get_position())


@app.get("/cooker/events")
def cooker_events():
    return _ok(**cooker.poll_events())


@app.post("/cooker/position")
def cooker_set_position():
    try:
        body = _require_json("position")
        pos  = int(body["position"])
    except (ValueError, TypeError) as e:
        return _err(str(e))
    cooker.set_position(pos)
    return _ok(position=pos)


@app.post("/cooker/click")
def cooker_click():
    cooker.click()
    return _ok()


@app.post("/cooker/reset")
def cooker_reset():
    cooker.reset()
    return _ok()


# ── plating ───────────────────────────────────────────────────────────────────

@app.get("/plating/status")
def plating_status():
    return _ok(**plating.status())


@app.post("/plating/move")
def plating_move():
    try:
        body = _require_json("m1", "m2")
        m1   = int(body["m1"])
        m2   = int(body["m2"])
    except (ValueError, TypeError) as e:
        return _err(str(e))
    plating.move(m1, m2)
    return _ok(m1=m1, m2=m2)


@app.post("/plating/stop")
def plating_stop():
    plating.stop()
    return _ok()


@app.post("/plating/home")
def plating_home():
    plating.home()
    return _ok()


# ── entrypoint ────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Tastebox hardware API server")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()

    try:
        app.run(host=args.host, port=args.port, debug=args.debug)
    finally:
        _bus.close()
