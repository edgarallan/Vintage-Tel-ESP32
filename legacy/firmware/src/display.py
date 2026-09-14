"""
display.py — Display OLED 128x64 I2C (SSD1306) per stato e rubrica

Mostra:
- Stato corrente del telefono (IDLE, DIALING, ecc.)
- Numero in composizione
- Nome chiamante in arrivo
- Orologio quando in IDLE (opzionale)
"""

import asyncio
import logging
from datetime import datetime

try:
    from luma.core.interface.serial import i2c
    from luma.oled.device import ssd1306
    from luma.core.render import canvas
    from PIL import ImageFont
    LUMA_AVAILABLE = True
except ImportError:
    LUMA_AVAILABLE = False


log = logging.getLogger("display")


class Display:
    def __init__(self, config: dict):
        self.config = config
        self._device = None
        self._font_small = None
        self._font_large = None
        self._idle_task: asyncio.Task | None = None

    async def start(self):
        if not LUMA_AVAILABLE:
            log.warning("luma.oled non disponibile — display disabilitato")
            return

        try:
            serial = i2c(port=1, address=self.config["i2c_address"])
            self._device = ssd1306(serial, rotate=self.config.get("rotation", 0))
            self._font_small = ImageFont.load_default()
            self._font_large = self._font_small  # In produzione caricare TTF più grande
            log.info("Display OLED inizializzato")
        except Exception as e:
            log.error("Errore init display: %s", e)
            self._device = None

    async def stop(self):
        if self._idle_task:
            self._idle_task.cancel()
        if self._device:
            self._device.clear()

    def _draw(self, draw_fn):
        if not self._device:
            return
        with canvas(self._device) as draw:
            draw_fn(draw)

    async def show_idle(self):
        if self._idle_task:
            self._idle_task.cancel()

        if self.config.get("show_clock_when_idle", True):
            self._idle_task = asyncio.create_task(self._clock_loop())
        else:
            self._draw(lambda d: d.text((10, 25), "VINTAGE TEL", fill="white"))

    async def _clock_loop(self):
        while True:
            now = datetime.now().strftime("%H:%M")
            date = datetime.now().strftime("%d %b %Y")
            self._draw(lambda d: (
                d.text((20, 10), now, fill="white"),
                d.text((10, 45), date, fill="white"),
            ))
            await asyncio.sleep(30)

    async def show_state(self, state_name: str, extra: str = ""):
        if self._idle_task:
            self._idle_task.cancel()
            self._idle_task = None
        self._draw(lambda d: (
            d.text((5, 5), state_name, fill="white"),
            d.text((5, 25), extra, fill="white") if extra else None,
        ))

    async def show_dialing(self, digits: str):
        self._draw(lambda d: (
            d.text((5, 5), "Componi...", fill="white"),
            d.text((5, 25), digits, fill="white"),
        ))

    async def show_incoming(self, name: str, number: str):
        if self._idle_task:
            self._idle_task.cancel()
        self._draw(lambda d: (
            d.text((5, 5), "Chiamata da:", fill="white"),
            d.text((5, 25), name, fill="white"),
            d.text((5, 45), number, fill="white"),
        ))
