"""
led.py — LED RGB di stato (montato discretamente)

Mappa di colori per stato:
- IDLE:    blu fisso pulsante (heartbeat)
- DIALING: verde fisso
- CALLING: giallo pulsante
- RINGING: rosso lampeggiante veloce
- IN_CALL: verde pulsante lento
- ERROR:   rosso fisso
"""

import asyncio
import logging

try:
    from gpiozero import RGBLED
except ImportError:
    RGBLED = None


log = logging.getLogger("led")


class StatusLed:
    def __init__(self, config: dict):
        self.config = config
        self._led = None
        self._brightness = config.get("brightness", 50) / 100.0
        self._pulse_task: asyncio.Task | None = None

    async def start(self):
        if RGBLED is None:
            log.warning("gpiozero non disponibile — LED disabilitato")
            return
        self._led = RGBLED(red=25, green=8, blue=7, active_high=True)
        log.info("StatusLed pronto")

    async def stop(self):
        await self._cancel_pulse()
        if self._led:
            self._led.off()
            self._led.close()

    async def _cancel_pulse(self):
        if self._pulse_task:
            self._pulse_task.cancel()
            try:
                await self._pulse_task
            except asyncio.CancelledError:
                pass
            self._pulse_task = None

    def _set(self, r: float, g: float, b: float):
        if self._led:
            b_scale = self._brightness
            self._led.color = (r * b_scale, g * b_scale, b * b_scale)

    async def _pulse(self, r: float, g: float, b: float, period_s: float = 2.0):
        await self._cancel_pulse()
        async def loop():
            try:
                while True:
                    for i in range(50):
                        v = (i / 49)
                        self._set(r * v, g * v, b * v)
                        await asyncio.sleep(period_s / 100)
                    for i in range(50):
                        v = 1 - (i / 49)
                        self._set(r * v, g * v, b * v)
                        await asyncio.sleep(period_s / 100)
            except asyncio.CancelledError:
                self._set(0, 0, 0)
                raise
        self._pulse_task = asyncio.create_task(loop())

    async def _blink(self, r: float, g: float, b: float, period_s: float = 0.5):
        await self._cancel_pulse()
        async def loop():
            try:
                state = True
                while True:
                    self._set(r if state else 0, g if state else 0, b if state else 0)
                    state = not state
                    await asyncio.sleep(period_s)
            except asyncio.CancelledError:
                self._set(0, 0, 0)
                raise
        self._pulse_task = asyncio.create_task(loop())

    async def idle(self):
        await self._pulse(0, 0, 1, period_s=4.0)  # blu lento

    async def dialing(self):
        await self._cancel_pulse()
        self._set(0, 1, 0)  # verde fisso

    async def calling(self):
        await self._pulse(1, 1, 0, period_s=1.5)  # giallo

    async def ringing(self):
        await self._blink(1, 0, 0, period_s=0.3)  # rosso lampeggiante

    async def in_call(self):
        await self._pulse(0, 1, 0, period_s=3.0)  # verde lento

    async def error(self):
        await self._cancel_pulse()
        self._set(1, 0, 0)  # rosso fisso
