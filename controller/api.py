"""
Flask HTTP API for Tastebox hardware control.

Endpoints
---------
GET  /status                 — full status of all four devices

GET  /cooker/status          — cooker full status
GET  /cooker/position        — current encoder position
GET  /cooker/events          — poll & clear event flags (CW/CCW/CLICK)
POST /cooker/position        — set target position  body: {"position": <int>}
POST /cooker/click           — simulate encoder click
POST /cooker/reset           — reset encoder position to 0

GET  /plating/status         — plater full status
POST /plating/move           — move pan stepper  body: {"steps": <int>}
POST /plating/servo          — set arm servo angle  body: {"angle": <int>}
POST /plating/stop           — stop pan stepper
POST /plating/home           — home pan stepper

GET  /ingredient/status      — ingredient dispenser status
POST /ingredient/dispense    — run coil stepper forward for duration
POST /ingredient/retract     — run coil stepper in reverse for duration
POST /ingredient/stop        — stop coil stepper immediately
POST /ingredient/duration    — set run duration  body: {"ms": <int>}

GET  /cutter/status          — cutter & lid status
POST /cutter/lid/open        — open lid
POST /cutter/lid/close       — close lid
POST /cutter/lid/duration    — set lid hold duration  body: {"ms": <int>}
POST /cutter/piston1/extend  — extend piston 1
POST /cutter/piston1/retract — retract piston 1
POST /cutter/piston2/extend  — extend piston 2
POST /cutter/piston2/retract — retract piston 2
POST /cutter/piston/duration — set piston run duration  body: {"ms": <int>}
POST /cutter/servo1          — set servo1 angle  body: {"angle": <int>}
POST /cutter/servo2          — set servo2 angle  body: {"angle": <int>}
POST /cutter/stop            — stop all cutter actuators
"""

from flask import Flask, jsonify, request
from bus import I2CBus
from devices import CookerDevice, PlatingArmDevice, IngredientDevice, CutterDevice
from display import SSD1306Display, MachineState

app = Flask(__name__)

# ── Hardware singletons (opened once at startup) ──────────────────────────────
_bus       = I2CBus(bus_num=1)
cooker     = CookerDevice(_bus)
plating    = PlatingArmDevice(_bus)
ingredient = IngredientDevice(_bus)
cutter     = CutterDevice(_bus)
display    = SSD1306Display()
display.set_state(MachineState.IDLE)


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


# ── display ───────────────────────────────────────────────────────────────────

@app.post("/state")
def set_state():
    body     = request.get_json(silent=True) or {}
    raw      = body.get("state", "idle").upper()
    subtitle = body.get("subtitle", "")
    try:
        state = MachineState[raw]
    except KeyError:
        return _err(f"unknown state: {raw}")
    display.set_state(state, subtitle=subtitle)
    return _ok(state=state.value)


# ── global ────────────────────────────────────────────────────────────────────

@app.get("/status")
def all_status():
    return _ok(
        cooker=cooker.status(),
        plating=plating.status(),
        ingredient=ingredient.status(),
        cutter=cutter.status(),
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
        body  = _require_json("steps")
        steps = int(body["steps"])
    except (ValueError, TypeError) as e:
        return _err(str(e))
    plating.move_pan(steps)
    return _ok(steps=steps)


@app.post("/plating/servo")
def plating_servo():
    try:
        body  = _require_json("angle")
        angle = int(body["angle"])
    except (ValueError, TypeError) as e:
        return _err(str(e))
    plating.set_servo(angle)
    return _ok(angle=angle)


@app.post("/plating/stop")
def plating_stop():
    plating.stop()
    return _ok()


@app.post("/plating/home")
def plating_home():
    plating.home()
    return _ok()


# ── ingredient ────────────────────────────────────────────────────────────────

@app.get("/ingredient/status")
def ingredient_status():
    return _ok(**ingredient.status())


@app.post("/ingredient/dispense")
def ingredient_dispense():
    ingredient.dispense()
    return _ok()


@app.post("/ingredient/retract")
def ingredient_retract():
    ingredient.retract()
    return _ok()


@app.post("/ingredient/stop")
def ingredient_stop():
    ingredient.stop()
    return _ok()


@app.post("/ingredient/duration")
def ingredient_duration():
    try:
        body = _require_json("ms")
        ms   = int(body["ms"])
    except (ValueError, TypeError) as e:
        return _err(str(e))
    ingredient.set_duration(ms)
    return _ok(ms=ms)


# ── cutter ────────────────────────────────────────────────────────────────────

@app.get("/cutter/status")
def cutter_status():
    return _ok(**cutter.status())


@app.post("/cutter/lid/open")
def cutter_lid_open():
    cutter.open_lid()
    return _ok()


@app.post("/cutter/lid/close")
def cutter_lid_close():
    cutter.close_lid()
    return _ok()


@app.post("/cutter/lid/duration")
def cutter_lid_duration():
    try:
        body = _require_json("ms")
        ms   = int(body["ms"])
    except (ValueError, TypeError) as e:
        return _err(str(e))
    cutter.set_lid_duration(ms)
    return _ok(ms=ms)


@app.post("/cutter/piston1/extend")
def cutter_p1_extend():
    cutter.piston1_extend()
    return _ok()


@app.post("/cutter/piston1/retract")
def cutter_p1_retract():
    cutter.piston1_retract()
    return _ok()


@app.post("/cutter/piston2/extend")
def cutter_p2_extend():
    cutter.piston2_extend()
    return _ok()


@app.post("/cutter/piston2/retract")
def cutter_p2_retract():
    cutter.piston2_retract()
    return _ok()


@app.post("/cutter/piston/duration")
def cutter_piston_duration():
    try:
        body = _require_json("ms")
        ms   = int(body["ms"])
    except (ValueError, TypeError) as e:
        return _err(str(e))
    cutter.set_piston_duration(ms)
    return _ok(ms=ms)


@app.post("/cutter/servo1")
def cutter_servo1():
    try:
        body  = _require_json("angle")
        angle = int(body["angle"])
    except (ValueError, TypeError) as e:
        return _err(str(e))
    cutter.set_servo1(angle)
    return _ok(angle=angle)


@app.post("/cutter/servo2")
def cutter_servo2():
    try:
        body  = _require_json("angle")
        angle = int(body["angle"])
    except (ValueError, TypeError) as e:
        return _err(str(e))
    cutter.set_servo2(angle)
    return _ok(angle=angle)


@app.post("/cutter/stop")
def cutter_stop():
    cutter.stop_all()
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
