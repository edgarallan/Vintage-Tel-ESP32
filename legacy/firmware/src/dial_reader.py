"""
dial_reader.py — Lettura del disco combinatore SIP

Il disco genera impulsi a 10Hz contando le interruzioni del contatto "pulse"
durante il ritorno meccanico. Il contatto NSI ("off-normal switch") indica
quando il disco è in movimento e funge da gate per la conta degli impulsi.
"""

import asyncio
import logging
import time
from typing import AsyncIterator

try:
    from gpiozero import Button
except ImportError:
    Button = None  # Per test su macchina non-Pi


log = logging.getLogger("dial_reader")


class DialReader:
    """Legge le cifre dal disco combinatore.

    Algoritmo:
    1. NSI a riposo (HIGH con pull-up, contatto chiuso a GND quindi LOW in pratica)
       quando il disco è fermo. Quando l'utente inizia a ruotare, NSI cambia stato.
    2. Mentre NSI è "in movimento", contiamo i fronti del contatto Pulse.
    3. Quando NSI torna a riposo, il numero di impulsi = cifra composta
       (con 10 impulsi → cifra 0).
    """

    def __init__(self, config: dict):
        self.config = config
        self._pulse_gpio = 4    # BCM
        self._nsi_gpio = 17     # BCM

        self._pulse_count = 0
        self._dialing = False    # True quando NSI indica disco in moto
        self._finalized = False  # cifra corrente già emessa? (anti-doppione)
        self._last_pulse_time = 0.0
        self._bounce_s = config["pulse_bouncetime_ms"] / 1000.0
        # Fallback: se il rilascio NSI di fine-cifra è rumoroso o si perde,
        # finalizza comunque la cifra dopo questo silenzio di impulsi.
        self._digit_end_s = config.get("digit_timeout_s", 0.4)

        self._digit_queue: asyncio.Queue[int] = asyncio.Queue()
        self._new_digit_event = asyncio.Event()
        self._fallback_handle: asyncio.TimerHandle | None = None

        self._pulse_btn = None
        self._nsi_btn = None
        self._loop: asyncio.AbstractEventLoop | None = None

    async def start(self):
        self._loop = asyncio.get_running_loop()

        if Button is None:
            log.warning("gpiozero non disponibile — modalità simulazione")
            return

        # Pulse: quando il contatto si apre (rising edge con pull-up) è un impulso
        self._pulse_btn = Button(self._pulse_gpio, pull_up=True, bounce_time=self._bounce_s)
        self._pulse_btn.when_released = self._on_pulse  # released = contatto si apre

        # NSI: quando si "preme" (disco inizia a girare)
        self._nsi_btn = Button(self._nsi_gpio, pull_up=True, bounce_time=0.02)
        self._nsi_btn.when_pressed = self._on_nsi_start
        self._nsi_btn.when_released = self._on_nsi_end

        log.info("DialReader avviato (pulse=GPIO%d, NSI=GPIO%d)",
                 self._pulse_gpio, self._nsi_gpio)

    async def stop(self):
        self._cancel_fallback()
        if self._pulse_btn:
            self._pulse_btn.close()
        if self._nsi_btn:
            self._nsi_btn.close()

    def _on_nsi_start(self):
        """Disco ha iniziato a ruotare (callback dal thread GPIO)."""
        log.debug("Disco: NSI start (rotazione iniziata)")
        self._pulse_count = 0
        self._dialing = True
        self._finalized = False
        if self._loop:
            self._loop.call_soon_threadsafe(self._cancel_fallback)

    def _on_nsi_end(self):
        """Disco tornato a riposo: finalizza la cifra (percorso normale)."""
        if not self._dialing:
            return
        self._dialing = False
        log.debug("Disco: NSI end con %d impulsi", self._pulse_count)
        if self._loop:
            self._loop.call_soon_threadsafe(self._finalize, "nsi")

    def _on_pulse(self):
        """Un impulso del contatto pulse (solo se in rotazione)."""
        if not self._dialing:
            return
        # Debouncing aggiuntivo software
        now = time.monotonic()
        if now - self._last_pulse_time < self._bounce_s:
            return
        self._last_pulse_time = now
        self._pulse_count += 1
        log.debug("Disco: impulso %d", self._pulse_count)
        # (Ri)arma il fallback di fine-cifra: scatta se gli impulsi si fermano
        # senza che arrivi il rilascio NSI.
        if self._loop and self._digit_end_s > 0:
            self._loop.call_soon_threadsafe(self._arm_fallback)

    # ─── Finalizzazione cifra (sempre sul loop, anti-doppione) ────────────

    def _decode(self, count: int) -> int | None:
        """Converte la conta impulsi in cifra (10+ → 0, convenzione IT)."""
        if count == 0:
            return None
        if count >= self.config.get("zero_pulses", 10):
            return 0
        if 1 <= count <= 9:
            return count
        log.warning("Conta impulsi anomala: %d — scarto", count)
        return None

    def _finalize(self, reason: str):
        """Emette la cifra corrente una sola volta (eseguito sul loop)."""
        if self._finalized:
            return
        self._finalized = True
        self._dialing = False
        self._cancel_fallback()

        count = self._pulse_count
        digit = self._decode(count)
        if digit is None:
            return
        log.debug("Disco: cifra %d (%d impulsi, via %s)", digit, count, reason)
        self._digit_queue.put_nowait(digit)
        self._new_digit_event.set()

    def _arm_fallback(self):
        """Pianifica il timeout di fine-cifra (eseguito sul loop)."""
        self._cancel_fallback()
        self._fallback_handle = self._loop.call_later(
            self._digit_end_s, lambda: self._finalize("timeout")
        )

    def _cancel_fallback(self):
        if self._fallback_handle:
            self._fallback_handle.cancel()
            self._fallback_handle = None

    async def digits(self) -> AsyncIterator[int]:
        """Generator async che emette ogni cifra composta."""
        while True:
            digit = await self._digit_queue.get()
            yield digit

    async def next_digit_event(self):
        """Attende il prossimo evento di cifra (per timeout)."""
        self._new_digit_event.clear()
        await self._new_digit_event.wait()


# ─── Test standalone ──────────────────────────────────────────────────

async def _test():
    logging.basicConfig(level=logging.DEBUG)
    cfg = {"pulse_bouncetime_ms": 50, "zero_pulses": 10, "dial_timeout_s": 8}
    reader = DialReader(cfg)
    await reader.start()
    print("Componi cifre con il disco (Ctrl+C per uscire)...")
    try:
        async for digit in reader.digits():
            print(f"➡️  Cifra: {digit}")
    except KeyboardInterrupt:
        pass
    finally:
        await reader.stop()


if __name__ == "__main__":
    asyncio.run(_test())
