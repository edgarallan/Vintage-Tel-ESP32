"""
bell_driver.py — Driver del campanello elettromeccanico originale

Genera un'onda quadra a 20-25 Hz applicata alle bobine del campanello tramite un
H-bridge **DRV8871** (breakout Adafruit, regge fino a 45 V), alimentato da un
boost 5V→~24V. Due GPIO pilotano gli ingressi IN1/IN2 del DRV8871:
  - IN1=1, IN2=0 → corrente in un senso
  - IN1=0, IN2=1 → corrente nel senso opposto
  - IN1=IN2=0   → uscite in coast (silenzio, nessuna corrente nella bobina)
Alternando IN1/IN2 alla frequenza di squillo si ottiene l'AC che fa oscillare
il martelletto. Nessun inverter/snubber esterni (il DRV8871 ha protezione interna).

Pattern italiano standard: 1s squillo, 4s pausa.
"""

import asyncio
import logging

try:
    from gpiozero import OutputDevice
except ImportError:
    OutputDevice = None


log = logging.getLogger("bell_driver")


class BellDriver:
    """Pilota il campanello tramite H-bridge DRV8871 (IN1/IN2)."""

    def __init__(self, config: dict):
        self.config = config
        self._in1_gpio = 22      # BCM, DRV8871 IN1
        self._in2_gpio = 23      # BCM, DRV8871 IN2

        freq = max(1, int(config["frequency_hz"]))
        self._half_period = 1.0 / (2 * freq)   # mezzo ciclo dell'onda quadra
        self._freq = freq

        self._in1 = None
        self._in2 = None
        self._ringing = False
        self._ring_task: asyncio.Task | None = None

    async def start(self):
        if OutputDevice is None:
            log.warning("gpiozero non disponibile — modalità simulazione")
            return

        self._in1 = OutputDevice(self._in1_gpio, initial_value=False)
        self._in2 = OutputDevice(self._in2_gpio, initial_value=False)
        log.info("BellDriver pronto (DRV8871 IN1=GPIO%d, IN2=GPIO%d, freq=%dHz)",
                 self._in1_gpio, self._in2_gpio, self._freq)

    async def stop(self):
        await self.stop_ringing()
        if self._in1:
            self._in1.close()
        if self._in2:
            self._in2.close()

    async def start_ringing(self):
        """Avvia pattern di squillo (loop fino a stop_ringing)."""
        if self._ringing:
            return
        self._ringing = True
        log.info("🔔 Inizio squillo")
        self._ring_task = asyncio.create_task(self._ring_loop())

    async def stop_ringing(self):
        """Ferma lo squillo immediatamente."""
        self._ringing = False
        if self._ring_task:
            self._ring_task.cancel()
            try:
                await self._ring_task
            except asyncio.CancelledError:
                pass
            self._ring_task = None
        self._coast()
        log.info("🔕 Stop squillo")

    async def _ring_loop(self):
        """Pattern italiano: on/off ciclico."""
        on_s = self.config["pattern_on_ms"] / 1000.0
        off_s = self.config["pattern_off_ms"] / 1000.0
        max_rings = self.config.get("max_rings", 30)
        count = 0

        try:
            while self._ringing and count < max_rings:
                await self._ac_burst(on_s)
                self._coast()
                if not self._ringing:
                    break
                await asyncio.sleep(off_s)
                count += 1
        finally:
            self._coast()

    async def _ac_burst(self, duration_s: float):
        """Genera onda quadra AC sulle bobine per ~duration_s alternando IN1/IN2."""
        if not self._in1 or not self._in2:
            # Off-Pi: simula soltanto la durata
            await asyncio.sleep(duration_s)
            return

        cycles = max(1, int(duration_s * self._freq))
        for _ in range(cycles):
            if not self._ringing:
                break
            self._in1.on()
            self._in2.off()
            await asyncio.sleep(self._half_period)
            self._in1.off()
            self._in2.on()
            await asyncio.sleep(self._half_period)
        self._coast()

    def _coast(self):
        """Mette le uscite in coast (silenzio): nessuna corrente nella bobina."""
        if self._in1:
            self._in1.off()
        if self._in2:
            self._in2.off()


async def _test():
    logging.basicConfig(level=logging.DEBUG)
    cfg = {
        "frequency_hz": 22,
        "pattern_on_ms": 1000,
        "pattern_off_ms": 2000,
        "max_rings": 3,
    }
    bell = BellDriver(cfg)
    await bell.start()
    print("Test campanello: 3 squilli")
    await bell.start_ringing()
    await asyncio.sleep(15)
    await bell.stop_ringing()
    await bell.stop()


if __name__ == "__main__":
    asyncio.run(_test())
