"""
audio.py — Gestione audio: toni di sistema (dial tone, busy, keypress)

I toni vengono generati al volo con NumPy e riprodotti via ALSA/PulseAudio.
Per le chiamate vere, l'audio è gestito da oFono (Bluetooth HFP),
che dialoga col dispositivo audio I2S.
"""

import asyncio
import logging
import shutil
import subprocess

try:
    import numpy as np
    import sounddevice as sd
    AUDIO_OK = True
except ImportError:
    AUDIO_OK = False


log = logging.getLogger("audio")


class AudioManager:
    """Genera e riproduce toni di sistema in stile telefono italiano."""

    DEFAULT_SAMPLE_RATE = 44100
    TONE_AMPLITUDE = 0.3  # ampiezza fissa dei toni; il volume è gestito da ALSA

    # Controlli softvol definiti in config/asound.conf
    SPEAKER_CONTROL = "PhoneSoftVol"
    CAPTURE_CONTROL = "PhoneCaptureVol"

    def __init__(self, config: dict):
        self.config = config
        # Allinea il sample rate al device I2S/ALSA configurato (default 44100).
        self.sample_rate = int(config.get("sample_rate", self.DEFAULT_SAMPLE_RATE))
        # I gain pilotano il softvol ALSA, che agisce sull'intero path della
        # cornetta (toni di sistema + audio della chiamata Bluetooth/SIP).
        self._speaker_gain_db = float(config.get("speaker_gain_db", 0))
        self._mic_gain_db = float(config.get("mic_gain_db", 0))
        self._dial_task: asyncio.Task | None = None
        self._busy_task: asyncio.Task | None = None

    async def start(self):
        self._apply_alsa_volume()
        if not AUDIO_OK:
            log.warning("sounddevice/numpy non disponibili — audio simulato")
            return
        log.info("AudioManager pronto (sample_rate=%d)", self.sample_rate)

    # ─── Volume cornetta via softvol ALSA ─────────────────────────────
    @staticmethod
    def _build_volume_cmd(control: str, gain_db: float) -> list[str]:
        """Comando amixer per impostare un controllo softvol a un livello in dB."""
        return ["amixer", "-D", "default", "--", "sset", control, f"{gain_db}dB"]

    def _apply_alsa_volume(self):
        """Imposta i softvol di altoparlante e microfono dai gain in config."""
        if not shutil.which("amixer"):
            log.info("amixer non disponibile — salto impostazione volume ALSA")
            return
        self._set_softvol(self.SPEAKER_CONTROL, self._speaker_gain_db)
        self._set_softvol(self.CAPTURE_CONTROL, self._mic_gain_db)

    def _set_softvol(self, control: str, gain_db: float):
        try:
            subprocess.run(
                self._build_volume_cmd(control, gain_db),
                check=True, capture_output=True,
            )
            log.info("Volume ALSA %s = %+g dB", control, gain_db)
        except (subprocess.CalledProcessError, OSError) as e:
            log.warning("Impossibile impostare %s: %s", control, e)

    async def stop(self):
        await self.stop_dial_tone()
        await self.stop_busy_tone()

    # ─── Toni italiani ────────────────────────────────────────────────
    # Riferimento: ITU-T E.180 + standard italiano Telecom
    # Dial tone IT: 425Hz continuo
    # Busy tone IT: 425Hz, 500ms on / 500ms off
    # Ring-back IT: 425Hz, 1s on / 4s off

    def _tone(self, freq: float, duration_s: float) -> "np.ndarray":
        t = np.linspace(0, duration_s, int(self.sample_rate * duration_s), False)
        # Fade in/out per evitare click
        wave = np.sin(2 * np.pi * freq * t) * self.TONE_AMPLITUDE
        fade = int(0.01 * self.sample_rate)
        wave[:fade] *= np.linspace(0, 1, fade)
        wave[-fade:] *= np.linspace(1, 0, fade)
        return wave.astype(np.float32)

    async def play_dial_tone(self):
        """Riproduce dial tone continuo (425Hz, fino a stop)."""
        if not AUDIO_OK or self._dial_task:
            return
        self._dial_task = asyncio.create_task(self._dial_loop())

    async def stop_dial_tone(self):
        if self._dial_task:
            self._dial_task.cancel()
            try:
                await self._dial_task
            except asyncio.CancelledError:
                pass
            self._dial_task = None
            if AUDIO_OK:
                sd.stop()

    async def _dial_loop(self):
        wave = self._tone(425, 1.0)
        try:
            while True:
                sd.play(wave, samplerate=self.sample_rate, blocking=False)
                await asyncio.sleep(1.0)
        except asyncio.CancelledError:
            pass

    async def play_busy_tone(self):
        if not AUDIO_OK or self._busy_task:
            return
        self._busy_task = asyncio.create_task(self._busy_loop())

    async def stop_busy_tone(self):
        if self._busy_task:
            self._busy_task.cancel()
            try:
                await self._busy_task
            except asyncio.CancelledError:
                pass
            self._busy_task = None
            if AUDIO_OK:
                sd.stop()

    async def _busy_loop(self):
        wave = self._tone(425, 0.5)
        try:
            while True:
                sd.play(wave, samplerate=self.sample_rate, blocking=False)
                await asyncio.sleep(1.0)
        except asyncio.CancelledError:
            pass

    async def play_keypress(self):
        """Beep breve di feedback alla pressione tasto/cifra."""
        if not AUDIO_OK:
            return
        wave = self._tone(800, 0.05)
        sd.play(wave, samplerate=self.sample_rate, blocking=False)
