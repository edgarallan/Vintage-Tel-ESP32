"""
hook_switch.py — Gestione del gancio della cornetta

Il gancio è un semplice switch sotto la culla della cornetta.
- Cornetta giù: switch chiuso → GPIO = LOW (con pull-up interno)
- Cornetta su: switch aperto → GPIO = HIGH

La logica può essere invertita su alcuni modelli, gestita via config.
"""

import asyncio
import logging
from typing import AsyncIterator

try:
    from gpiozero import Button
except ImportError:
    Button = None


log = logging.getLogger("hook_switch")


class HookSwitch:
    """Gestisce gli eventi di sollevamento/abbassamento della cornetta."""

    def __init__(self, config: dict):
        self.config = config
        self._gpio = 27
        self._inverted = config.get("inverted", False)
        self._bounce_s = config.get("debounce_ms", 100) / 1000.0

        self._event_queue: asyncio.Queue[bool] = asyncio.Queue()
        self._loop: asyncio.AbstractEventLoop | None = None
        self._button = None
        self._current_state = False  # False = giù, True = su

    async def start(self):
        self._loop = asyncio.get_running_loop()

        if Button is None:
            log.warning("gpiozero non disponibile — modalità simulazione")
            return

        self._button = Button(self._gpio, pull_up=True, bounce_time=self._bounce_s)
        # Configura callback in funzione dell'orientamento
        if not self._inverted:
            # Cornetta su = button "pressed" (LOW) — NO inversione qui
            # Default Siemens: hook switch normalmente chiuso quando cornetta giù
            # Quindi quando cornetta su, switch apre → GPIO HIGH → Button "released"
            self._button.when_pressed = lambda: self._emit(False)  # cornetta giù
            self._button.when_released = lambda: self._emit(True)  # cornetta su
        else:
            self._button.when_pressed = lambda: self._emit(True)
            self._button.when_released = lambda: self._emit(False)

        # Leggi stato iniziale
        self._current_state = self._read_initial()
        log.info("HookSwitch avviato (GPIO%d, inverted=%s, initial=%s)",
                 self._gpio, self._inverted,
                 "UP" if self._current_state else "DOWN")

    def _read_initial(self) -> bool:
        if not self._button:
            return False
        # is_pressed = True quando GPIO è LOW (con pull-up)
        if self._inverted:
            return self._button.is_pressed
        return not self._button.is_pressed

    async def stop(self):
        if self._button:
            self._button.close()

    def _emit(self, hook_up: bool):
        """Push evento nella coda async."""
        self._current_state = hook_up
        if self._loop:
            asyncio.run_coroutine_threadsafe(
                self._event_queue.put(hook_up), self._loop
            )

    async def events(self) -> AsyncIterator[bool]:
        """Generator async: True = sollevata, False = poggiata."""
        while True:
            ev = await self._event_queue.get()
            yield ev

    @property
    def is_up(self) -> bool:
        return self._current_state


async def _test():
    logging.basicConfig(level=logging.DEBUG)
    hs = HookSwitch({"inverted": False, "debounce_ms": 100})
    await hs.start()
    print("Solleva/abbassa la cornetta (Ctrl+C per uscire)...")
    try:
        async for up in hs.events():
            print(f"➡️  Cornetta: {'SU' if up else 'GIÙ'}")
    except KeyboardInterrupt:
        pass
    finally:
        await hs.stop()


if __name__ == "__main__":
    asyncio.run(_test())
