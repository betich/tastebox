# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Tastebox** (Robo Cooker) is a senior robotics project. A web UI lets users pick and customize a menu; a Python controller translates those choices into I2C commands sent to four Arduino Nano firmware nodes that drive physical cooking and plating hardware.

---

## Commands

### Web (`/web`)
```bash
npm run dev        # Dev server with HMR at http://localhost:5173
npm run build      # Production build → build/client + build/server
npm run start      # Serve production build
npm run typecheck  # Type-check + generate route types
```

### Python controller (`/controller`)
```bash
python master.py   # Run the main controller / hardware demo
```

### Arduino firmware (`/nodes/cooker`, `/nodes/plating`)
Uses PlatformIO CLI:
```bash
pio run              # Compile
pio run --target upload  # Flash to Arduino Nano
pio device monitor   # Serial monitor at 115200 baud
```

---

## Architecture

```
Web UI (React Router / TypeScript)
        ↕  HTTP
Python Controller (master.py)
        ↕  I2C (SMBus / smbus2)
Arduino Nano × 2
        ↕  GPIO
Physical hardware (motors, encoders, buzzer)
```

### Web (`/web`)
- **Framework**: React Router 7 with SSR enabled, TailwindCSS 4, Vite
- **Routes**: `home` → `personalize` → `cooking` → `done` (defined in `app/routes.ts`)
- **State**: Local React state only — no global store
- **Menu data**: `app/types.ts` — `MENU` array and `MenuItem` interface are the single source of truth for menu items

### Python Controller (`/controller`)
- `master.py` — entry point; discovers devices and runs demo sequences
- `bus.py` — thin SMBus wrapper (`read_byte`, `write_bytes`, `probe`)
- `devices/base.py` — abstract `I2CDevice` with address and bus
- `devices/cooker.py` — `CookerDevice` at I2C address `0x42`; controls encoder position, power, event flags (CW/CCW/CLICK)
- `devices/plating.py` — `PlatingArmDevice` at I2C address `0x43`; controls pan stepper + swing arm motor
- `devices/ingredient.py` — `IngredientDevice` at I2C address `0x44`; dispenses/retracts ingredient via stepper
- `devices/cutter.py` — `CutterDevice` at I2C address `0x45`; lid servos + linear pistons

### Arduino Firmware (`/nodes/`)
All four nodes share the same PlatformIO config (`atmelavr`, `nanoatmega328`, Arduino framework):

| Node | I2C address | Key hardware |
|------|-------------|--------------|
| cooker | `0x42` | Rotary encoder (pins 2,3), click button (4), buzzer (8) |
| plating | `0x43` | Pan stepper (D7/D8/D4), arm motor (D13/D12/D11), RS-485 RO=A0/DI=A1/DE+RE=A2 |
| ingredient | `0x44` | Coil-feed stepper motor, RS-485 RO=D5/DI=D6/DE+RE=D7 |
| cutter | `0x45` | Servo × 2 (lid), linear pistons × 2, DC motor, RS-485 RO=D2/DI=D3/DE+RE=D4 |

`/sketches/` holds reference Arduino `.ino` files; production firmware lives in `/nodes/*/src/main.cpp`.

### I2C Register Maps (quick reference)
**Cooker (0x42)**
- `0x00–0x01` current position (int16), `0x02` power state, `0x03` event flags (clears on read), `0x10–0x11` command / set-position

**Plating (0x43)**
- `0x00–0x01` pan stepper position (int16, read), `0x02` arm state: 0=at_A/1=at_B/2=moving (read), `0x03` status flags: bit0=pan_busy/bit1=arm_busy (read)
- `0x10` pan command (STOP `0x01`, HOME `0x02`), `0x11–0x12` pan target int16 (write hi→lo)
- `0x13` arm command (GOTO_A `0x01`, GOTO_B `0x02`, STOP `0x03`), `0x14–0x15` arm travel duration ms uint16 (write hi→lo)

**Ingredient (0x44)**
- `0x00` status flags: bit0=busy/bit1=direction (read), `0x01–0x02` remaining ms uint16 (read)
- `0x10` command (STOP `0x01`, DISPENSE `0x02`, RETRACT `0x03`), `0x11–0x12` duration ms uint16 (write hi→lo)

**Cutter (0x45)**
- `0x00` status flags: bit0=lid_busy/bit1=p1_busy/bit2=p2_busy (read)
- `0x10` command (STOP_ALL `0x01`, OPEN_LID `0x02`, CLOSE_LID `0x03`, P1_EXT `0x04`, P1_RET `0x05`, P2_EXT `0x06`, P2_RET `0x07`)
- `0x11` servo1 angle 0-180 (write), `0x12` servo2 angle 0-180 (write)
- `0x13–0x14` lid duration ms uint16 (write hi→lo), `0x15–0x16` piston duration ms uint16 (write hi→lo)

---

## Deployment

The web app is containerised — `Dockerfile` in `/web/` builds and serves via `react-router-serve` on port 3000.
