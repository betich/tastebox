"""Central logging setup for the Tastebox controller.

Call log.setup() once at program entry (master.py).  Every other module
then does the standard `logger = logging.getLogger(__name__)`.

Output goes to both stdout and a rotating file (tastebox.log, 5 MB × 3).
"""
import logging
import logging.handlers
import sys
from pathlib import Path

LOG_FILE = Path(__file__).parent / "tastebox.log"
_FMT     = "%(asctime)s.%(msecs)03d %(levelname)-5s [%(name)-12s] %(message)s"
_DATEFMT = "%Y-%m-%d %H:%M:%S"


def setup(level: int = logging.DEBUG) -> None:
    root = logging.getLogger()
    if root.handlers:
        return
    root.setLevel(level)

    fmt = logging.Formatter(_FMT, datefmt=_DATEFMT)

    ch = logging.StreamHandler(sys.stdout)
    ch.setFormatter(fmt)
    root.addHandler(ch)

    fh = logging.handlers.RotatingFileHandler(
        LOG_FILE, maxBytes=5 * 1024 * 1024, backupCount=3, encoding="utf-8"
    )
    fh.setFormatter(fmt)
    root.addHandler(fh)

    # Suppress per-request noise from werkzeug; we log our own summaries
    logging.getLogger("werkzeug").setLevel(logging.WARNING)
