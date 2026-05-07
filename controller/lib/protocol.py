"""Pure functions for encoding/decoding the RS485 ASCII protocol.

Read:   @{ADDR:02X} R {REG:02X}\n          → @{ADDR:02X} {VAL:02X}\n
Write:  @{ADDR:02X} W {REG:02X} {B0} ...\n → @{ADDR:02X} OK\n | @{ADDR:02X} ERR\n
"""


def encode_read(addr: int, reg: int) -> bytes:
    return f"@{addr:02X} R {reg:02X}\n".encode()


def encode_write(addr: int, reg: int, *data: int) -> bytes:
    body = " ".join(f"{b:02X}" for b in data)
    return f"@{addr:02X} W {reg:02X} {body}\n".encode()


def decode_response(line: str) -> int | str | None:
    """Parse a response line.  Returns an int (read value), "OK", "ERR", or None."""
    parts = line.strip().split()
    if len(parts) < 2:
        return None
    token = parts[1]
    if token in ("OK", "ERR"):
        return token
    try:
        return int(token, 16)
    except ValueError:
        return None
