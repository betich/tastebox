import threading
import time
from enum import Enum

I2C_ADDRESS = 0x3C


class MachineState(Enum):
    IDLE          = "IDLE"
    PERSONALIZING = "PERSONALIZING"
    COOKING       = "COOKING"
    FINISHED      = "FINISHED"


_LABELS = {
    MachineState.IDLE:          "READY",
    MachineState.PERSONALIZING: "PERSONALIZE",
    MachineState.COOKING:       "COOKING",
    MachineState.FINISHED:      "FINISHED",
}


class SSD1306Display:
    def __init__(self):
        self._device = None
        self._lock   = threading.Lock()
        self._state  = MachineState.IDLE
        self._cook_start: float | None = None
        self._timer_stop   = threading.Event()
        self._timer_thread: threading.Thread | None = None
        self._init_device()

    def _init_device(self):
        try:
            from luma.core.interface.serial import i2c
            from luma.oled.device import ssd1306
            serial = i2c(port=1, address=I2C_ADDRESS)
            self._device = ssd1306(serial, width=128, height=64)
            print("[display] SSD1306 initialised")
        except Exception as e:
            print(f"[display] init skipped (headless): {e}")

    # ── public API ────────────────────────────────────────────

    def set_state(self, state: MachineState, subtitle: str = ""):
        self._state = state
        if state == MachineState.COOKING:
            if self._cook_start is None:
                self._cook_start = time.monotonic()
                self._start_timer()
        else:
            self._cook_start = None
            self._stop_timer()
            self._render(state, subtitle)

    # ── cooking elapsed-time ticker ───────────────────────────

    def _start_timer(self):
        self._stop_timer()
        self._timer_stop.clear()
        self._timer_thread = threading.Thread(
            target=self._timer_loop, daemon=True, name="display-timer"
        )
        self._timer_thread.start()

    def _stop_timer(self):
        if self._timer_thread and self._timer_thread.is_alive():
            self._timer_stop.set()
            self._timer_thread.join(timeout=2)
        self._timer_thread = None
        self._timer_stop.clear()

    def _timer_loop(self):
        while not self._timer_stop.wait(1.0):
            if self._cook_start is not None:
                elapsed = int(time.monotonic() - self._cook_start)
                h, rem  = divmod(elapsed, 3600)
                m, s    = divmod(rem, 60)
                self._render(MachineState.COOKING, f"{h:02d}:{m:02d}:{s:02d}")

    # ── rendering ─────────────────────────────────────────────

    def _render(self, state: MachineState, subtitle: str = ""):
        if not self._device:
            tag = f" [{subtitle}]" if subtitle else ""
            print(f"[display] {state.value}{tag}")
            return

        from luma.core.render import canvas
        from PIL import ImageFont

        label = _LABELS[state]
        try:
            font_big = ImageFont.truetype(
                "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16
            )
            font_sm = ImageFont.truetype(
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 10
            )
        except Exception:
            font_big = ImageFont.load_default()
            font_sm  = ImageFont.load_default()

        with self._lock:
            with canvas(self._device) as draw:
                W, H = self._device.width, self._device.height
                draw.rectangle((0, 0, W, H), fill="black")

                lb = draw.textbbox((0, 0), label, font=font_big)
                lw = lb[2] - lb[0]
                lh = lb[3] - lb[1]
                y_label = (H // 2 - lh) // 2 if subtitle else (H - lh) // 2
                draw.text(((W - lw) // 2, y_label), label, font=font_big, fill="white")

                if subtitle:
                    sb = draw.textbbox((0, 0), subtitle, font=font_sm)
                    sw = sb[2] - sb[0]
                    draw.text(
                        ((W - sw) // 2, H // 2 + 4),
                        subtitle, font=font_sm, fill="#888888",
                    )
