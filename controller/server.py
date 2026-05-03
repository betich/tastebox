"""
Flask HTTP server — exposes all I2C device commands over REST.
The web frontend (CONTROLLER_API_URL, default http://localhost:5000) talks here.
Start via server.start() from master.py; runs in a daemon thread.
"""
import logging
import threading
from flask import Flask, jsonify, request

from display import SSD1306Display, MachineState

logger = logging.getLogger(__name__)

app  = Flask(__name__)
_lock = threading.Lock()

# Device handles — populated by init()
_cooker     = None
_plater     = None
_ingredient = None
_cutter     = None
_display: SSD1306Display | None = None


def init(cooker, plater, ingredient, cutter, display: SSD1306Display | None = None):
    global _cooker, _plater, _ingredient, _cutter, _display
    _cooker     = cooker
    _plater     = plater
    _ingredient = ingredient
    _cutter     = cutter
    _display    = display


def start(port: int = 5000):
    t = threading.Thread(
        target=lambda: app.run(host="0.0.0.0", port=port,
                               use_reloader=False, threaded=True),
        daemon=True,
        name="flask",
    )
    t.start()
    logger.info("HTTP API listening on port %d", port)


# ── request logging ───────────────────────────────────────────────────────────

@app.after_request
def _log_request(response):
    if request.method == "POST":
        body = request.get_json(silent=True) or {}
        logger.info("POST %-28s %s → %d", request.path, body or "", response.status_code)
    elif response.status_code >= 400:
        logger.warning("GET  %-28s → %d", request.path, response.status_code)
    return response


# ── helpers ───────────────────────────────────────────────────────────────────

def _safe(fn):
    """Wrap a device call; return 500 on any exception."""
    try:
        result = fn()
        return jsonify({"ok": True, **(result or {})})
    except Exception as e:
        logger.warning("device error: %s", e)
        return jsonify({"ok": False, "error": str(e)}), 500


# ── status ────────────────────────────────────────────────────────────────────

@app.route("/status")
def status():
    try:
        with _lock:
            cooker_ok  = _cooker.ping()
            plater_ok  = _plater.ping()
            ing_ok     = _ingredient.ping()
            cutter_ok  = _cutter.ping()

            cooker_data = {
                "online":   cooker_ok,
                "on":       _cooker.is_on()       if cooker_ok else False,
                "position": _cooker.get_position() if cooker_ok else 0,
            }
            # m1/m2 naming kept for cooking.tsx compatibility
            plater_data = {
                "online":   plater_ok,
                "m1_busy":  _plater.is_pan_busy()      if plater_ok else False,
                "m2_busy":  _plater.is_arm_busy()      if plater_ok else False,
                "m1_pos":   _plater.get_pan_position() if plater_ok else 0,
                "m2_pos":   0,
                "arm":      _plater.get_arm_state()    if plater_ok else 0,
            }
            ing_data = {
                "online":       ing_ok,
                "busy":         _ingredient.is_busy()         if ing_ok else False,
                "remaining_ms": _ingredient.get_remaining_ms() if ing_ok else 0,
            }
            cutter_data = {
                "online": cutter_ok,
                **(_cutter.get_status_flags() if cutter_ok else
                   {"lid_busy": False, "piston1_busy": False, "piston2_busy": False}),
            }

        return jsonify({
            "ok":         True,
            "cooker":     cooker_data,
            "plating":    plater_data,
            "ingredient": ing_data,
            "cutter":     cutter_data,
        })
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500


# ── machine state (LCD display) ───────────────────────────────────────────────

@app.route("/state", methods=["POST"])
def set_state():
    data = request.get_json() or {}
    raw  = data.get("state", "idle").upper()
    subtitle = data.get("subtitle", "")
    try:
        state = MachineState[raw]
    except KeyError:
        return jsonify({"ok": False, "error": f"unknown state: {raw}"}), 400
    if _display:
        _display.set_state(state, subtitle=subtitle)
    logger.info("display state → %s%s", state.value, f" [{subtitle}]" if subtitle else "")
    return jsonify({"ok": True, "state": state.value})


# ── cooker (0x42) ─────────────────────────────────────────────────────────────

@app.route("/cooker/click", methods=["POST"])
def cooker_click():
    return _safe(lambda: _cooker.click() or {})


@app.route("/cooker/reset", methods=["POST"])
def cooker_reset():
    return _safe(lambda: _cooker.reset() or {})


@app.route("/cooker/position", methods=["POST"])
def cooker_position():
    data = request.get_json() or {}
    def _do():
        with _lock:
            if "delta" in data:
                pos = _cooker.get_position() + int(data["delta"])
            else:
                pos = int(data.get("position", 0))
            _cooker.set_position(pos)
    return _safe(_do)


# ── plating arm (0x43) ────────────────────────────────────────────────────────

@app.route("/plating/move", methods=["POST"])
def plating_move():
    data = request.get_json() or {}
    m1 = int(data.get("m1", 0))
    m2 = int(data.get("m2", 0))
    def _do():
        with _lock:
            if m1 != 0:
                _plater.move_pan(m1)
            if m2 > 0:
                # m2 treated as oiliness steps → arm duration ms (50 ms per step)
                _plater.set_arm_duration(m2 * 50)
                _plater.goto_b()
    return _safe(_do)


@app.route("/plating/home", methods=["POST"])
def plating_home():
    return _safe(lambda: _plater.home_pan() or {})


@app.route("/plating/arm", methods=["POST"])
def plating_arm():
    data   = request.get_json() or {}
    action = data.get("action", "stop")
    dur    = data.get("duration_ms")
    def _do():
        with _lock:
            if dur is not None:
                _plater.set_arm_duration(int(dur))
            if action == "goto_a":
                _plater.goto_a()
            elif action == "goto_b":
                _plater.goto_b()
            else:
                _plater.stop_arm()
    return _safe(_do)


# ── ingredient (0x44) ─────────────────────────────────────────────────────────

@app.route("/ingredient/fwd", methods=["POST"])
def ingredient_fwd():
    return _safe(lambda: _ingredient.fwd() or {})


@app.route("/ingredient/bwd", methods=["POST"])
def ingredient_bwd():
    return _safe(lambda: _ingredient.bwd() or {})


@app.route("/ingredient/dispense", methods=["POST"])
def ingredient_dispense():
    data = request.get_json() or {}
    ms   = data.get("duration_ms")
    def _do():
        with _lock:
            if ms is not None:
                _ingredient.set_duration(int(ms))
            _ingredient.dispense()
    return _safe(_do)


@app.route("/ingredient/retract", methods=["POST"])
def ingredient_retract():
    data = request.get_json() or {}
    ms   = data.get("duration_ms")
    def _do():
        with _lock:
            if ms is not None:
                _ingredient.set_duration(int(ms))
            _ingredient.retract()
    return _safe(_do)


@app.route("/ingredient/stop", methods=["POST"])
def ingredient_stop():
    return _safe(lambda: _ingredient.stop() or {})


@app.route("/ingredient/duration", methods=["POST"])
def ingredient_duration():
    data = request.get_json() or {}
    ms   = int(data.get("ms", 1000))
    return _safe(lambda: _ingredient.set_duration(ms) or {})


# ── cutter (0x45) ─────────────────────────────────────────────────────────────

@app.route("/cutter/lid", methods=["POST"])
def cutter_lid():
    data   = request.get_json() or {}
    action = data.get("action", "close")
    ms     = data.get("duration_ms")
    def _do():
        with _lock:
            if ms is not None:
                _cutter.set_lid_duration(int(ms))
            if action == "open":
                _cutter.open_lid()
            else:
                _cutter.close_lid()
    return _safe(_do)


@app.route("/cutter/piston", methods=["POST"])
def cutter_piston():
    data   = request.get_json() or {}
    which  = int(data.get("which", 1))
    action = data.get("action", "retract")
    ms     = data.get("duration_ms")
    def _do():
        with _lock:
            if ms is not None:
                _cutter.set_piston_duration(int(ms))
            if which == 1:
                if action == "extend":
                    _cutter.piston1_extend()
                else:
                    _cutter.piston1_retract()
            else:
                if action == "extend":
                    _cutter.piston2_extend()
                else:
                    _cutter.piston2_retract()
    return _safe(_do)


@app.route("/cutter/stop", methods=["POST"])
def cutter_stop():
    return _safe(lambda: _cutter.stop_all() or {})
