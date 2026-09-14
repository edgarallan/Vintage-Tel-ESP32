"""
test_hardware.py — Test interattivo dei componenti hardware

Eseguibile direttamente per verificare il corretto cablaggio
prima di chiudere il telefono e avviare il servizio finale.

Uso:
    python3 -m src.test_hardware             # esegue tutti i test in sequenza
    python3 -m src.test_hardware --monitor   # monitora GPIO in tempo reale
"""

import argparse
import asyncio
import logging
import sys

from .hook_switch import HookSwitch
from .dial_reader import DialReader
from .bell_driver import BellDriver
from .led import StatusLed
from .display import Display


async def test_led():
    print("\n=== TEST 1: LED RGB ===")
    led = StatusLed({"brightness": 80})
    await led.start()
    for state, fn in [
        ("IDLE (blu lento)", led.idle),
        ("DIALING (verde)", led.dialing),
        ("CALLING (giallo)", led.calling),
        ("RINGING (rosso blink)", led.ringing),
        ("ERROR (rosso fisso)", led.error),
    ]:
        print(f"  → {state}")
        await fn()
        await asyncio.sleep(2)
    await led.stop()
    print("  ✅ LED test completato")


async def test_display():
    print("\n=== TEST 2: Display OLED ===")
    disp = Display({"enabled": True, "i2c_address": 0x3C, "rotation": 0,
                    "show_clock_when_idle": False})
    await disp.start()
    await disp.show_state("HARDWARE TEST OK")
    print("  ✅ Verifica visivamente che il display mostri 'HARDWARE TEST OK'")
    await asyncio.sleep(3)
    await disp.stop()


async def test_hook():
    print("\n=== TEST 3: Hook switch ===")
    print("  Solleva e abbassa la cornetta 3 volte...")
    hs = HookSwitch({"inverted": False, "debounce_ms": 100})
    await hs.start()
    count = 0
    async for up in hs.events():
        print(f"  → Cornetta: {'⬆️  SU' if up else '⬇️  GIÙ'}")
        count += 1
        if count >= 6:
            break
    await hs.stop()
    print("  ✅ Hook test completato")


async def test_dial():
    print("\n=== TEST 4: Disco combinatore ===")
    print("  Componi 3 cifre con il disco...")
    reader = DialReader({"pulse_bouncetime_ms": 50, "zero_pulses": 10, "dial_timeout_s": 8})
    await reader.start()
    count = 0
    async for digit in reader.digits():
        print(f"  → Cifra: {digit}")
        count += 1
        if count >= 3:
            break
    await reader.stop()
    print("  ✅ Disco test completato")


async def test_bell():
    print("\n=== TEST 5: Campanello ===")
    print("  ⚠️  Allontana l'orecchio — sta per suonare!")
    await asyncio.sleep(2)
    bell = BellDriver({
        "frequency_hz": 22,
        "pattern_on_ms": 800,
        "pattern_off_ms": 1200,
        "max_rings": 3,
    })
    await bell.start()
    await bell.start_ringing()
    await asyncio.sleep(8)
    await bell.stop_ringing()
    await bell.stop()
    print("  ✅ Campanello test completato")


async def test_audio():
    print("\n=== TEST 6: Audio loopback ===")
    print("  Parla nella cornetta per 3 secondi...")
    import subprocess
    try:
        # Registra 3 secondi
        subprocess.run([
            "arecord", "-D", "plughw:CARD=sndrpigooglevoi",
            "-f", "S16_LE", "-r", "16000", "-c", "1", "-d", "3", "/tmp/test.wav"
        ], check=True, timeout=10)
        print("  Riproduco ora dalla cornetta...")
        subprocess.run([
            "aplay", "-D", "plughw:CARD=sndrpigooglevoi", "/tmp/test.wav"
        ], check=True, timeout=10)
        print("  ✅ Audio loopback completato")
    except Exception as e:
        print(f"  ⚠️  Audio test fallito: {e}")
        print("     Probabile causa: ALSA non configurata o I2S non funzionante")


async def monitor_mode():
    """Modalità di monitoraggio continuo di tutti i GPIO."""
    print("\n=== MODALITÀ MONITOR ===")
    print("Premi Ctrl+C per uscire\n")

    hs = HookSwitch({"inverted": False, "debounce_ms": 100})
    reader = DialReader({"pulse_bouncetime_ms": 50, "zero_pulses": 10, "dial_timeout_s": 8})

    await hs.start()
    await reader.start()

    async def watch_hook():
        async for up in hs.events():
            print(f"[HOOK]  {'⬆️  SU' if up else '⬇️  GIÙ'}")

    async def watch_dial():
        async for digit in reader.digits():
            print(f"[DIAL]  Cifra: {digit}")

    try:
        await asyncio.gather(watch_hook(), watch_dial())
    except KeyboardInterrupt:
        pass
    finally:
        await hs.stop()
        await reader.stop()


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--monitor", action="store_true", help="Monitor mode")
    parser.add_argument("--skip", nargs="*", default=[], help="Test da saltare (led/display/hook/dial/bell/audio)")
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")

    if args.monitor:
        await monitor_mode()
        return

    tests = [
        ("led", test_led),
        ("display", test_display),
        ("hook", test_hook),
        ("dial", test_dial),
        ("bell", test_bell),
        ("audio", test_audio),
    ]

    for name, fn in tests:
        if name in args.skip:
            print(f"\n=== Skip: {name} ===")
            continue
        try:
            await fn()
        except KeyboardInterrupt:
            print("\nInterrotto dall'utente")
            return
        except Exception as e:
            print(f"  ❌ ERRORE in test {name}: {e}")

    print("\n🎉 Tutti i test completati! Sei pronto a chiudere il telefono.")


if __name__ == "__main__":
    asyncio.run(main())
