# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Hardware/software retrofit that turns a 1970s Italian Siemens/FATME S62 (or similar pulse-dial) SIP phone into a **Bluetooth HFP handsfree** for a paired mobile, preserving the original handset, rotary dial, and electromechanical bell. Target hardware is **Raspberry Pi Zero 2 W** running Raspberry Pi OS Lite 64-bit (Bookworm). Documentation and code comments are in Italian.

## Repository layout

- `firmware/` — Python application (the actual code you'll edit)
  - `src/` — modules (one per hardware/protocol concern; see Architecture below)
  - `config/config.example.yaml` — user config template; real `config.yaml` is gitignored
  - `config/asound.conf` — ALSA config copied to `/etc/asound.conf` at install
  - `systemd/vintage-tel.service` — runs `python -m src.main` as user `pi`
  - `requirements.txt`
- `hardware/` — wiring docs (`schematic.md`, `pinout.md`, `bell_driver.md`, `retrofit_layout.md` = keep/remove/add map for gutting the S62 chassis)
- `docs/` — install guide (`install.sh`) and operator manuals (Italian, numbered `01`–`07`; `07_retrofit_layout.md` = chassis conversion map)
- `assets/` — architecture notes + SVG assembly diagrams; `assets/retrofit/` holds the S62 schematic photo, the bare-chassis photos, and the annotated conversion overlays (`cassetta_annotata.png`, `sequenza_annotata.png`)

## Commands

All firmware commands run from `firmware/` with the venv active.

```bash
# Install (on the Pi; needs sudo)
sudo bash docs/install.sh

# Run the app manually (instead of via systemd)
cd firmware && source venv/bin/activate
python -m src.main

# Hardware bring-up tests — run BEFORE closing the phone case
python -m src.test_hardware              # sequential LED/display/hook/dial/bell/audio
python -m src.test_hardware --monitor    # live GPIO monitor (hook + dial)
python -m src.test_hardware --skip bell audio   # skip individual tests

# Per-module standalone tests (each module's __main__)
python -m src.dial_reader
python -m src.bell_driver

# Service control
sudo systemctl {start,stop,restart,status} vintage-tel
journalctl -u vintage-tel -f
```

There is a pytest suite under `firmware/tests/` that runs **off-Pi** (the hardware drivers degrade via `try/except ImportError`, so `gpiozero`/`dbus`/`luma` aren't needed). Install `firmware/requirements-dev.txt` and run `python -m pytest` from `firmware/`; pytest/coverage config lives in `firmware/pyproject.toml`. It covers the state machine, dial reader, phonebook, and audio config — the GPIO/DBus drivers themselves still need on-Pi validation via `src/test_hardware.py`. No linter/formatter is wired up. If you add Python, follow PEP 8 + type hints and match the existing style (snake_case, dataclass-light, async-first).

## Architecture

**Single-process asyncio app.** `firmware/src/main.py` defines `VintageTel`, a state machine with states `IDLE → DIALING → CALLING → IN_CALL` and `IDLE → RINGING → IN_CALL`. Module entry point: `python -m src.main` (loads `firmware/config/config.yaml`).

Key invariants — preserve these when changing code:

1. **All state transitions go through `VintageTel._transition()`** which holds `self._state_lock`, logs the change, and updates LED + display side-effects. Never mutate `self.state` directly.
2. **GPIO is interrupt-driven, never polled.** Hardware modules (`hook_switch`, `dial_reader`) use `gpiozero` callbacks, then forward events into asyncio via `asyncio.run_coroutine_threadsafe(...)` against the loop captured in `start()`. Don't add `while True: read()` patterns.
3. **DBus + GLib threading island** for `bt_phone.py` (oFono HFP): i suoi callback girano nel thread GLib e rientrano in asyncio via `run_coroutine_threadsafe` sul loop catturato in `start()`. Le chiamate DBus sincrone dentro coroutine vanno in `loop.run_in_executor(...)`.
4. **Unico backend chiamate: `BluetoothPhone`** (vivavoce Bluetooth HFP via oFono). Espone `connected`, `place_call`, `answer`, `reject`, `hangup`, `send_dtmf`, più i callback `on_incoming_call(caller)` / `on_call_ended()`. `VintageTel` lo usa direttamente (niente selezione di backend / SIP).
5. **Hardware modules degrade gracefully off-Pi.** Imports of `gpiozero`, `dbus`, `luma.oled` are wrapped in `try/except ImportError` so the code can be inspected/run on a dev machine — keep this when adding new hardware dependencies.

### Dial reader specifics

`DialReader` uses two GPIOs: a **pulse** contact and an **NSI** ("off-normal switch") that gates counting. Pulses are only counted while NSI indicates the disc is rotating; on NSI release the count is converted to a digit (10 pulses = `0` per Italian/EU convention, configurable via `dial.zero_pulses`). Quick-dial: a single digit held for ~1.5s while in `DIALING` is matched against `phonebook.quick_dial` before being treated as the first digit of a longer number.

### Bell driver specifics

`BellDriver` generates a 20–25 Hz square wave on GPIO 23 (PWM, 50% duty) gated by an enable line on GPIO 22 that powers a boost converter feeding an H-bridge to the original 24 V AC bell coils. The on/off ring pattern follows Italian Telecom timing (1s on / 4s off by default). **The 24 V AC line is real** — wiring changes belong in `hardware/bell_driver.md`, not casual edits.

## GPIO map (BCM numbering)

Source of truth: `hardware/pinout.md`. Quick lookup:

| Function | GPIO |
|---|---|
| Dial pulse / Dial NSI | 4 / 17 |
| Hook switch | 27 |
| Bell EN / Bell PH (PWM) | 22 / 23 |
| Phonebook button | 24 |
| LED R/G/B (PWM) | 25 / 8 / 7 |
| Display I2C SDA/SCL | 2 / 3 |
| Audio I2S BCK/LRCK/DIN/DOUT | 18 / 19 / 20 / 21 |

If a module hardcodes a different pin, treat it as a bug (or update `pinout.md` to match in the same change).

## Configuration

User config lives at `firmware/config/config.yaml` (gitignored; created from `config.example.yaml` by `docs/install.sh`). It contains the paired phone's MAC, so never commit it and never echo its full contents into logs or chat. Loaded once at startup via `load_config()`; there is no hot-reload.
