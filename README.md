# Vintage-Tel-ESP32

**A 1970s Italian rotary telephone that places and receives calls through your mobile, over
Bluetooth Hands-Free.** The original handset, pulse dial and electromechanical bell all
still work — the dial really dials, and the bell is rung by its own coils.

Firmware in C on ESP-IDF, running on a plain ESP32.

The phone is a **Siemens/FATME S62**, SIP-issue, stamped *Ed. IX*. Everything the user
touches is original; everything behind the faceplate is new.

> **Docs are in Italian.** This README is the exception, so the findings below are useful to
> people outside Italy. Code comments, commit messages and `docs/` are Italian — it's a
> restoration of an Italian phone, and that felt right.

---

## What works today

Each of these has been **verified on the actual telephone**, not just in tests:

- **Incoming calls.** The mobile rings, and the phone's **original bell** rings with it —
  Italian Telecom cadence, 1 s on / 4 s off, measured to the millisecond. Lift the handset
  and you're connected; the bell stops 90 ms *before* the call goes live.
- **Caller ID.** The name from the phonebook appears on an OLED, large enough to read
  across a room. Unknown callers get the number.
- **Outgoing calls.** Lift the handset, **dial with the rotary dial**, wait — a pulse phone
  has no send button, so silence is the only signal the number is finished.
- **Hang up from either end.** Cradle the handset, or let the other side hang up.
- **Status at a glance.** A single addressable LED gives each state a colour *and a
  motion* — steady, breathing, pulsing, blinking. Colour alone isn't enough: ringing and
  fault are both red.
- **Reconnects by itself** after a power cut, with no touching of the mobile.
- **Wideband audio negotiated** — the link comes up as mSBC at 16 kHz, not 8 kHz CVSD.

## What doesn't work yet

**Voice.** The audio chain is half-built: the codec is configured, I2S runs, and tones come
out of it — but the handset capsules aren't wired to it yet, and the audio isn't bridged
into the Bluetooth call. That's the remaining work.

A **phonebook loader** is also missing: the lookup and quick-dial logic are written and
tested, but there's no way to get contacts into the device yet.

---

## Findings you may be looking for

Several of these cost days to establish and are hard to find written down. If one of them
saves you an evening, this repo has paid for itself.

**Raspberry Pi Pico W cannot do Bluetooth voice calls.** SCO connections — the audio
channel — don't establish on the CYW43439, because SCO traffic shares the single 3-wire SPI
bus with the WiFi firmware. Still open: [pico-sdk #1461](https://github.com/raspberrypi/pico-sdk/issues/1461).
Without SCO there is no call audio, so the Pico is out for this application.

**Only the original ESP32 has Bluetooth Classic.** The S3, C3, C6 and H2 are BLE-only, and
HFP lives on BR/EDR.

**Wideband speech requires the vHCI audio path.** From Espressif's own HFP docs: *"mSBC is
unavailable with PCM data path (CVSD only)."* Route SCO through the host and you get 16 kHz
voice plus a natural place to mix in local tones.

**The PCNT glitch filter cannot debounce a rotary dial.** It saturates around 12.8 µs, while
the measured spurious edges on this dial reach 797 µs. The hardware counter looks perfect
for pulse dialling and is a trap. Debouncing is done in software instead, per contact —
and the per-contact part matters: the dial's pulse contact settles in **1.3 ms**, the hook
switch takes **247 ms**.

**On ESP32, I2S MCLK can only leave via GPIO 0, 1 or 3** — a silicon constraint, and 1 and 3
are the serial console. GPIO 0 is a strapping pin but is only sampled at reset, so it works.

**On the Waveshare WM8960 board, `TX` and `RX` are named from the board's point of view.**
`RXSDA` is the codec's `DACDAT` *input*. Get it backwards and you still hear something —
the digital signal couples capacitively into the analogue output, giving weak, dirty audio
that nevertheless tracks the frequency. Convincing enough to send you hunting through codec
registers for an hour.

More in [`hardware/guasti.md`](hardware/guasti.md) — symptoms, and the reasoning that found
the cause each time.

---

## How it's built

**Every threshold in this firmware was measured, not guessed.** Debounce windows, the bell's
resonant frequency, the inter-digit timeout — each one came from putting an instrument on
the actual telephone. The reasoning sits next to the constant in the source, because a bare
number looks arbitrary six months later.

The bell frequency is a good example. The documentation says a telephone ringer resonates
between 20 and 25 Hz. On *this* fifty-year-old mechanism the best sound is at **11 Hz**,
found by sweeping frequencies and listening — and the sweep was designed so that "louder"
couldn't be mistaken for "resonant", since a coil passes more current at lower frequencies
regardless.

```
firmware/core/       Pure C. No ESP-IDF headers at all — compiles and runs on a PC
firmware/phone_hal/  ESP-IDF drivers: GPIO, I2S, I2C, RMT, HFP, NVS
firmware/tests/      Unity tests, no ESP32 required
firmware/tools/      Serial capture and log extraction used during bring-up
hardware/            GPIO map, bell driver, and the fault log
legacy/              The abandoned Raspberry Pi version, in Python
```

The split is the point: `core/` never includes `esp_*.h`, so the state machine, the dial
decoder, the phonebook and the tone generator are all testable on a laptop. Hardware
reaches them only through a struct of function pointers.

There is **no mutex on the phone state**. Everything — GPIO interrupts, Bluetooth
callbacks, timers — posts events to one queue consumed by one task, so there is nothing to
protect.

### Running the tests

No ESP32, no ESP-IDF. CMake and a C compiler:

```bash
cd firmware
cmake -S tests -B tests/build && cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
./tests/coverage.sh
```

**75 tests, 98.8% line coverage** over `core/`.

---

## Hardware

| | |
|---|---|
| ESP32 DevKit | plain ESP32 — **not** S3/C3/C6 |
| WM8960 | audio codec, I2S + I2C |
| SSD1306 | 128×64 OLED, I2C |
| WS2812 | one addressable LED |
| L298N + XL6009 | boost to ~27 V and H-bridge, to drive the original bell coils |
| From the phone | dial, hook switch, bell coils (≈1690 Ω), handset |

Full pin map and wiring in [`hardware/pinout.md`](hardware/pinout.md). The bell runs on
real 27 V — [`hardware/bell_driver.md`](hardware/bell_driver.md) covers it, including two
jumpers on the L298N that will destroy the module if left as shipped.

---

## Status

| Phase | |
|---|---|
| Portable core, tested on PC | ✅ 75 tests, 98.8% |
| Dial, hook, bell on real hardware | ✅ measured and working |
| LED and display | ✅ |
| Bluetooth call control | ✅ in, out, both hang-up directions, auto-reconnect |
| Audio playback through the codec | ✅ tones audible |
| Voice through the handset | 🚧 in progress |
| Phonebook loading | 🚧 not started |

## License

MIT.
