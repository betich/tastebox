"""
Flask HTTP server — exposes all I2C device commands over REST.
The web frontend (CONTROLLER_API_URL, default http://localhost:5000) talks here.
Start via server.start() from master.py; runs in a daemon thread.
"""
import logging
import threading
import time
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

# USB re-discovery callback — set by main_usb_auto() in master.py
_rediscover_fn = None

# ── cook state ────────────────────────────────────────────────────────────────
_cook_lock  = threading.Lock()
_cook_abort = threading.Event()
_cook_state: dict = {
    "running": False, "step": 0, "step_label": "",
    "done": False, "error": None, "cook_end_ms": 0,
}
COOK_DURATION_S = 240
PLATE_ARM_MS    = 5000
PLATE_PAN_STEPS = 500


def set_rediscover_callback(fn):
    global _rediscover_fn
    _rediscover_fn = fn


# RS-485 monitor — optional, populated by init_rs485_monitor()
_rs485_available = False
_rs485_node_status: dict[str, bool] = {
    "cooker": False, "plating": False, "ingredient": False, "cutter": False
}
_RS485_ADDRS = {"cooker": 0x42, "plating": 0x43, "ingredient": 0x44, "cutter": 0x45}


def init(cooker, plater, ingredient, cutter, display: SSD1306Display | None = None):
    global _cooker, _plater, _ingredient, _cutter, _display
    _cooker     = cooker
    _plater     = plater
    _ingredient = ingredient
    _cutter     = cutter
    _display    = display


def init_rs485_monitor(port: str):
    """Open a secondary RS-485 bus used only for admin debug status badges."""
    global _rs485_available
    from lib.rs485_bus import RS485Bus
    try:
        bus = RS485Bus(port)
        bus.open()
        _rs485_available = True
        t = threading.Thread(target=_rs485_probe_loop, args=(bus,),
                             daemon=True, name="rs485-monitor")
        t.start()
        logger.info("RS-485 monitor active on %s", port)
    except Exception as e:
        logger.warning("RS-485 monitor could not open %s: %s", port, e)


def _rs485_probe_loop(bus):
    while True:
        time.sleep(5)
        for name, addr in _RS485_ADDRS.items():
            try:
                _rs485_node_status[name] = bus.probe(addr)
            except Exception:
                _rs485_node_status[name] = False


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
            plater_data = {
                "online":    plater_ok,
                "m1_busy":   _plater.is_pan_busy()      if plater_ok else False,
                "m2_busy":   _plater.is_arm_busy()      if plater_ok else False,
                "m1_pos":    _plater.get_pan_position() if plater_ok else 0,
                "m2_pos":    0,
                "arm":       _plater.get_arm_state()    if plater_ok else 0,
                "lid":       _plater.get_lid_state()    if plater_ok else 0,
                "lid_busy":  _plater.is_lid_busy()      if plater_ok else False,
            }
            ing_data = {
                "online": ing_ok,
                "a_busy": _ingredient.is_busy_a() if ing_ok else False,
                "b_busy": _ingredient.is_busy_b() if ing_ok else False,
                "c_busy": _ingredient.is_busy_c() if ing_ok else False,
            }
            cutter_data = {
                "online": cutter_ok,
                **(_cutter.get_status_flags() if cutter_ok else {
                    "door_busy": False, "clamp_busy": False,
                    "roller_busy": False, "scissor_busy": False,
                    "pepper_busy": False, "salt_busy": False,
                    "pump_a_busy": False, "pump_b_busy": False,
                }),
            }

        rs485_data = {
            "available":   _rs485_available,
            "cooker":      _rs485_node_status["cooker"],
            "plating":     _rs485_node_status["plating"],
            "ingredient":  _rs485_node_status["ingredient"],
            "cutter":      _rs485_node_status["cutter"],
        }

        return jsonify({
            "ok":         True,
            "cooker":     cooker_data,
            "plating":    plater_data,
            "ingredient": ing_data,
            "cutter":     cutter_data,
            "rs485":      rs485_data,
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
                _plater.set_arm_duration(m2 * 50)
                _plater.dispense()
    return _safe(_do)


@app.route("/plating/home", methods=["POST"])
def plating_home():
    return _safe(lambda: _plater.home_pan() or {})


@app.route("/plating/pan/stop", methods=["POST"])
def plating_pan_stop():
    return _safe(lambda: _plater.stop_pan() or {})


@app.route("/plating/arm", methods=["POST"])
def plating_arm():
    data   = request.get_json() or {}
    action = data.get("action", "stop")
    dur    = data.get("duration_ms")
    def _do():
        with _lock:
            if dur is not None:
                _plater.set_arm_duration(int(dur))
            if   action == "dispense":  _plater.dispense()
            elif action == "retract":   _plater.retract()
            elif action == "fwd_cont":  _plater.fwd_cont()
            elif action == "bwd_cont":  _plater.bwd_cont()
            else:                       _plater.stop_arm()
    return _safe(_do)


@app.route("/plating/lid", methods=["POST"])
def plating_lid():
    data   = request.get_json() or {}
    action = data.get("action", "stop")
    dur    = data.get("duration_ms")
    def _do():
        with _lock:
            if dur is not None:
                _plater.set_lid_duration(int(dur))
            if   action == "open":      _plater.open_lid()
            elif action == "close":     _plater.close_lid()
            elif action == "fwd_cont":  _plater.lid_fwd_cont()
            elif action == "bwd_cont":  _plater.lid_bwd_cont()
            else:                       _plater.stop_lid()
    return _safe(_do)


# ── ingredient (0x44) ─────────────────────────────────────────────────────────

@app.route("/ingredient/a/fwd",      methods=["POST"])
def ingredient_a_fwd():      return _safe(lambda: _ingredient.a_fwd()      or {})

@app.route("/ingredient/a/bwd",      methods=["POST"])
def ingredient_a_bwd():      return _safe(lambda: _ingredient.a_bwd()      or {})

@app.route("/ingredient/a/dispense", methods=["POST"])
def ingredient_a_dispense(): return _safe(lambda: _ingredient.dispense()   or {})

@app.route("/ingredient/a/retract",  methods=["POST"])
def ingredient_a_retract():  return _safe(lambda: _ingredient.retract()    or {})

@app.route("/ingredient/a/stop",     methods=["POST"])
def ingredient_a_stop():     return _safe(lambda: _ingredient.stop_a()     or {})

@app.route("/ingredient/b/fwd",      methods=["POST"])
def ingredient_b_fwd():      return _safe(lambda: _ingredient.b_fwd()      or {})

@app.route("/ingredient/b/bwd",      methods=["POST"])
def ingredient_b_bwd():      return _safe(lambda: _ingredient.b_bwd()      or {})

@app.route("/ingredient/b/dispense", methods=["POST"])
def ingredient_b_dispense(): return _safe(lambda: _ingredient.b_dispense() or {})

@app.route("/ingredient/b/retract",  methods=["POST"])
def ingredient_b_retract():  return _safe(lambda: _ingredient.b_retract()  or {})

@app.route("/ingredient/b/stop",     methods=["POST"])
def ingredient_b_stop():     return _safe(lambda: _ingredient.stop_b()     or {})

@app.route("/ingredient/c/fwd",      methods=["POST"])
def ingredient_c_fwd():      return _safe(lambda: _ingredient.c_fwd()      or {})

@app.route("/ingredient/c/bwd",      methods=["POST"])
def ingredient_c_bwd():      return _safe(lambda: _ingredient.c_bwd()      or {})

@app.route("/ingredient/c/dispense", methods=["POST"])
def ingredient_c_dispense(): return _safe(lambda: _ingredient.c_dispense() or {})

@app.route("/ingredient/c/retract",  methods=["POST"])
def ingredient_c_retract():  return _safe(lambda: _ingredient.c_retract()  or {})

@app.route("/ingredient/c/stop",     methods=["POST"])
def ingredient_c_stop():     return _safe(lambda: _ingredient.stop_c()     or {})

@app.route("/ingredient/stop",       methods=["POST"])
def ingredient_stop():       return _safe(lambda: _ingredient.stop()       or {})

@app.route("/ingredient/revolutions", methods=["POST"])
def ingredient_revolutions():
    data  = request.get_json() or {}
    steps = int(data.get("steps", 200))
    return _safe(lambda: _ingredient.set_steps_per_rev(steps) or {})


@app.route("/ingredient/speed", methods=["POST"])
def ingredient_speed():
    half_us = int((request.get_json() or {}).get("half_us", 800))
    return _safe(lambda: _ingredient.set_speed(half_us) or {})


# ── cutter (0x45) ─────────────────────────────────────────────────────────────

@app.route("/cutter/door", methods=["POST"])
def cutter_door():
    action = (request.get_json() or {}).get("action", "close")
    def _do():
        with _lock:
            if action == "open": _cutter.open_door()
            else:                _cutter.close_door()
    return _safe(_do)


@app.route("/cutter/clamp", methods=["POST"])
def cutter_clamp():
    action = (request.get_json() or {}).get("action", "stow")
    def _do():
        with _lock:
            if   action == "hover": _cutter.pinner_hover()
            elif action == "pin":   _cutter.pinner_pin()
            else:                   _cutter.pinner_stow()
    return _safe(_do)


@app.route("/cutter/roller", methods=["POST"])
def cutter_roller():
    action = (request.get_json() or {}).get("action", "stop")
    def _do():
        with _lock:
            if   action == "up":   _cutter.roller_up()
            elif action == "down": _cutter.roller_down()
            else:                  _cutter.roller_stop()
    return _safe(_do)


@app.route("/cutter/cut", methods=["POST"])
def cutter_cut():
    action = (request.get_json() or {}).get("action", "stop")
    def _do():
        with _lock:
            if   action == "close": _cutter.cutter_close()
            elif action == "open":  _cutter.cutter_open()
            else:                   _cutter.cutter_stop()
    return _safe(_do)


@app.route("/cutter/pepper", methods=["POST"])
def cutter_pepper():
    action = (request.get_json() or {}).get("action", "stop")
    def _do():
        with _lock:
            if action == "dispense": _cutter.pepper_dispense()
            else:                    _cutter.pepper_stop()
    return _safe(_do)


@app.route("/cutter/salt", methods=["POST"])
def cutter_salt():
    action = (request.get_json() or {}).get("action", "stop")
    def _do():
        with _lock:
            if action == "dispense": _cutter.salt_dispense()
            else:                    _cutter.salt_stop()
    return _safe(_do)


@app.route("/cutter/pump_a", methods=["POST"])
def cutter_pump_a():
    data = request.get_json() or {}
    pwm  = int(data.get("pwm", 255 if data.get("action") == "on" else 0))
    return _safe(lambda: _cutter.set_pump_a(pwm) or {})


@app.route("/cutter/pump_b", methods=["POST"])
def cutter_pump_b():
    data = request.get_json() or {}
    pwm  = int(data.get("pwm", 255 if data.get("action") == "on" else 0))
    return _safe(lambda: _cutter.set_pump_b(pwm) or {})


@app.route("/cutter/duration", methods=["POST"])
def cutter_duration():
    ms = int((request.get_json() or {}).get("ms", 1000))
    return _safe(lambda: _cutter.set_duration(ms) or {})


# ── USB rescan ────────────────────────────────────────────────────────────────

@app.route("/scan", methods=["POST"])
def scan():
    if _rediscover_fn is None:
        return jsonify({"ok": False, "error": "not in USB auto mode"}), 400
    try:
        _rediscover_fn()
        return jsonify({"ok": True})
    except Exception as e:
        logger.warning("scan error: %s", e)
        return jsonify({"ok": False, "error": str(e)}), 500


# ── cook sequence ────────────────────────────────────────────────────────────

def _cook_wait(busy_fn, timeout_s: float = 30.0, poll_s: float = 0.25):
    """Poll until busy_fn() is falsy, raising on abort or timeout."""
    deadline = time.monotonic() + timeout_s
    while True:
        if _cook_abort.is_set():
            raise RuntimeError("E-STOP")
        try:
            if not busy_fn():
                return
        except Exception:
            pass
        if time.monotonic() > deadline:
            raise TimeoutError(f"device busy after {timeout_s}s")
        time.sleep(poll_s)


def _run_cook_sequence(umami_pos: int, menu_index: int):
    global _cook_state
    try:
        # 1 — dispense ingredients A, B, C simultaneously
        _cook_state.update(step=1, step_label="Dispensing ingredients")
        with _lock:
            _ingredient.dispense()
            _ingredient.b_dispense()
            _ingredient.c_dispense()
        _cook_wait(
            lambda: _ingredient.is_busy_a() or _ingredient.is_busy_b() or _ingredient.is_busy_c(),
            timeout_s=30,
        )

        # 2 — cutter: pin → cut → roll → open door
        _cook_state.update(step=2, step_label="Cutting & prepping")
        with _lock:
            _cutter.pinner_pin()
        time.sleep(0.5)
        with _lock:
            _cutter.cutter_close()
        _cook_wait(lambda: _cutter.get_status_flags()["scissor_busy"], timeout_s=15)
        with _lock:
            _cutter.roller_down()
        _cook_wait(lambda: _cutter.get_status_flags()["roller_busy"], timeout_s=15)
        with _lock:
            _cutter.open_door()

        # 3 — close plating lid
        _cook_state.update(step=3, step_label="Closing lid")
        with _lock:
            _plater.close_lid()
        _cook_wait(lambda: _plater.is_lid_busy(), timeout_s=20)

        # 4 — cooker on: set position + click
        _cook_state.update(step=4, step_label="Starting cooker")
        with _lock:
            _cooker.set_position(umami_pos + 1)
            _cooker.click()

        # 5 — cook 4 min
        _cook_state.update(step=5, step_label="Cooking")
        cook_end_ms = int(time.time() * 1000) + COOK_DURATION_S * 1000
        _cook_state["cook_end_ms"] = cook_end_ms
        deadline = time.monotonic() + COOK_DURATION_S
        while time.monotonic() < deadline:
            if _cook_abort.is_set():
                raise RuntimeError("E-STOP")
            time.sleep(0.5)

        # 6 — cooker off: click again
        _cook_state.update(step=6, step_label="Finishing")
        with _lock:
            _cooker.click()

        # 7 — plate: lid up → arm up → FWD½ → BWD½ → arm down
        _cook_state.update(step=7, step_label="Plating")
        with _lock:
            _plater.open_lid()
        _cook_wait(lambda: _plater.is_lid_busy(), timeout_s=20)
        with _lock:
            _plater.set_arm_duration(PLATE_ARM_MS)
            _plater.retract()                           # arm up
        _cook_wait(lambda: _plater.is_arm_busy(), timeout_s=PLATE_ARM_MS / 1000 + 5)
        with _lock:
            _plater.set_arm_duration(PLATE_ARM_MS // 2)
            _plater.dispense()                          # FWD ½
        _cook_wait(lambda: _plater.is_arm_busy(), timeout_s=PLATE_ARM_MS / 1000 + 5)
        with _lock:
            _plater.set_arm_duration(PLATE_ARM_MS // 2)
            _plater.retract()                           # BWD ½
        _cook_wait(lambda: _plater.is_arm_busy(), timeout_s=PLATE_ARM_MS / 1000 + 5)
        with _lock:
            _plater.set_arm_duration(PLATE_ARM_MS)
            _plater.dispense()                          # arm down
        _cook_wait(lambda: _plater.is_arm_busy(), timeout_s=PLATE_ARM_MS / 1000 + 5)

        _cook_state.update(running=False, done=True, step=0, step_label="Done")

    except Exception as e:
        logger.warning("cook sequence: %s", e)
        _cook_state.update(
            running=False, done=False,
            error=str(e),
            step=0, step_label="Stopped" if _cook_abort.is_set() else "Error",
        )
    finally:
        try:
            with _lock:
                _ingredient.stop()
                _cutter.roller_stop()
                _cutter.cutter_stop()
                _cutter.close_door()
                _cutter.pinner_stow()
                _plater.stop_lid()
                _plater.stop_arm()
        except Exception as ex:
            logger.warning("cook cleanup: %s", ex)


@app.route("/cook/start", methods=["POST"])
def cook_start():
    global _cook_state
    data       = request.get_json() or {}
    umami_pos  = int(data.get("umami_pos",  2))
    menu_index = int(data.get("menu_index", 0))
    with _cook_lock:
        if _cook_state.get("running"):
            return jsonify({"ok": False, "error": "already running"}), 409
        _cook_abort.clear()
        _cook_state = {
            "running": True, "step": 0, "step_label": "Starting…",
            "done": False, "error": None, "cook_end_ms": 0,
        }
        threading.Thread(
            target=_run_cook_sequence, args=(umami_pos, menu_index),
            daemon=True, name="cook-seq",
        ).start()
    return jsonify({"ok": True})


@app.route("/cook/status")
def cook_status():
    return jsonify({"ok": True, **_cook_state})


@app.route("/cook/estop", methods=["POST"])
def cook_estop():
    _cook_abort.set()
    try:
        with _lock:
            _ingredient.stop()
            _cutter.roller_stop()
            _cutter.cutter_stop()
            _cutter.close_door()
            _cutter.pinner_stow()
            _plater.stop_lid()
            _plater.stop_arm()
    except Exception as e:
        logger.warning("estop error: %s", e)
    _cook_state.update(
        running=False, step=0, step_label="E-STOP",
        error="Emergency stop activated",
    )
    return jsonify({"ok": True})


# ── health ping ───────────────────────────────────────────────────────────────

@app.route("/ping")
def ping():
    devices = [
        ("cooker",     _cooker),
        ("plating",    _plater),
        ("ingredient", _ingredient),
        ("cutter",     _cutter),
    ]
    results = {}
    with _lock:
        for name, dev in devices:
            t0 = time.monotonic()
            ok = dev.ping()
            elapsed_ms = round((time.monotonic() - t0) * 1000)
            results[name] = {"online": ok, "ms": elapsed_ms if ok else None}

    if _rediscover_fn and any(not v["online"] for v in results.values()):
        threading.Thread(target=_rediscover_fn, daemon=True, name="usb-rediscover").start()

    return jsonify({"ok": True, "nodes": results})
