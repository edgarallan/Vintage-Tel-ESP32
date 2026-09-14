"""
bt_phone.py — Bluetooth Hands-Free Profile (HFP) tramite oFono

Il Pi si presenta come un vivavoce auto al cellulare:
- Riceve notifiche di chiamata in arrivo
- Può fare chiamate (componendo numero)
- Audio passa via SCO Bluetooth (CVSD/mSBC)

Implementazione via DBus → oFono. In alternativa più moderna si può usare
direttamente BlueZ DBus API, ma oFono è ancora lo standard più solido.
"""

import asyncio
import logging
from typing import Callable, Optional

try:
    import dbus
    import dbus.mainloop.glib
    from gi.repository import GLib
    DBUS_AVAILABLE = True
except ImportError:
    DBUS_AVAILABLE = False


log = logging.getLogger("bt_phone")


class BluetoothPhone:
    """Wrapper oFono per gestire HFP."""

    def __init__(self, config: dict):
        self.config = config
        self.connected = False
        self._modem = None
        self._voicecall_mgr = None
        self._current_call = None

        self.on_incoming_call: Optional[Callable] = None
        self.on_call_ended: Optional[Callable] = None

        self._bus = None
        self._loop_thread = None
        # Event loop asyncio catturato in start(): i callback DBus girano nel
        # thread GLib, dove asyncio.get_event_loop() fallirebbe (Python 3.12).
        self._loop: asyncio.AbstractEventLoop | None = None

    async def start(self):
        if not DBUS_AVAILABLE:
            log.warning("python-dbus non disponibile — Bluetooth disabilitato")
            return

        log.info("Avvio Bluetooth HFP via oFono…")
        self._loop = asyncio.get_running_loop()
        # Esegui DBus in thread separato (GLib main loop)
        await self._loop.run_in_executor(None, self._init_dbus)

        # Discovery iniziale del modem (cellulare accoppiato)
        await self._find_modem()

    def _init_dbus(self):
        """Init synchrono DBus (chiamato in executor)."""
        dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
        self._bus = dbus.SystemBus()

        # Sottoscrivi a segnali oFono
        self._bus.add_signal_receiver(
            self._on_modem_added,
            signal_name="ModemAdded",
            dbus_interface="org.ofono.Manager",
        )
        self._bus.add_signal_receiver(
            self._on_modem_removed,
            signal_name="ModemRemoved",
            dbus_interface="org.ofono.Manager",
        )

    async def stop(self):
        if self._current_call:
            await self.hangup()
        log.info("BluetoothPhone fermato")

    async def _find_modem(self):
        """Cerca un modem oFono attivo (= cellulare connesso via HFP)."""
        if not self._bus:
            return
        try:
            manager = dbus.Interface(
                self._bus.get_object("org.ofono", "/"),
                "org.ofono.Manager",
            )
            modems = manager.GetModems()
            if modems:
                path, _props = modems[0]
                log.info("Modem BT trovato: %s", path)
                self._modem = self._bus.get_object("org.ofono", path)
                self.connected = True
                self._subscribe_voice_calls(path)
            else:
                log.info("Nessun modem BT (cellulare non accoppiato/non in HFP)")
        except Exception as e:
            log.warning("Errore enumerazione modem: %s", e)

    def _subscribe_voice_calls(self, modem_path: str):
        """Iscrivi ai segnali di chiamata sul modem."""
        self._bus.add_signal_receiver(
            self._on_call_added,
            signal_name="CallAdded",
            dbus_interface="org.ofono.VoiceCallManager",
            path=modem_path,
        )
        self._bus.add_signal_receiver(
            self._on_call_removed,
            signal_name="CallRemoved",
            dbus_interface="org.ofono.VoiceCallManager",
            path=modem_path,
        )

    def _on_modem_added(self, path, properties):
        log.info("Modem aggiunto: %s", path)
        # Re-scan (callback dal thread GLib → usa il loop catturato)
        if self._loop:
            asyncio.run_coroutine_threadsafe(self._find_modem(), self._loop)

    def _on_modem_removed(self, path):
        log.info("Modem rimosso: %s", path)
        self.connected = False
        self._modem = None

    def _on_call_added(self, path, properties):
        caller = str(properties.get("LineIdentification", "Sconosciuto"))
        state = str(properties.get("State", "unknown"))
        log.info("Call event: %s, state=%s, caller=%s", path, state, caller)
        self._current_call = path

        if state == "incoming" and self.on_incoming_call and self._loop:
            asyncio.run_coroutine_threadsafe(
                self.on_incoming_call(caller),
                self._loop,
            )

    def _on_call_removed(self, path):
        log.info("Call removed: %s", path)
        self._current_call = None
        if self.on_call_ended and self._loop:
            asyncio.run_coroutine_threadsafe(
                self.on_call_ended(),
                self._loop,
            )

    # ─── Operazioni di chiamata ───────────────────────────────────────

    async def place_call(self, number: str) -> bool:
        if not self._modem or not self.connected:
            log.error("Modem non disponibile")
            return False
        try:
            vcm = dbus.Interface(self._modem, "org.ofono.VoiceCallManager")
            await self._run_blocking(lambda: vcm.Dial(number, "default"))
            log.info("Chiamata avviata: %s", number)
            return True
        except Exception as e:
            log.error("Errore Dial: %s", e)
            return False

    async def answer(self):
        if self._current_call:
            call = self._bus.get_object("org.ofono", self._current_call)
            iface = dbus.Interface(call, "org.ofono.VoiceCall")
            await self._run_blocking(iface.Answer)

    async def reject(self):
        if self._current_call:
            call = self._bus.get_object("org.ofono", self._current_call)
            iface = dbus.Interface(call, "org.ofono.VoiceCall")
            await self._run_blocking(iface.Hangup)

    async def hangup(self):
        await self.reject()

    async def send_dtmf(self, digit: str):
        """Invia tono DTMF durante chiamata."""
        if self._modem:
            vcm = dbus.Interface(self._modem, "org.ofono.VoiceCallManager")
            await self._run_blocking(lambda: vcm.SendTones(digit))

    async def _run_blocking(self, fn):
        """Esegue una chiamata DBus sincrona fuori dal loop asyncio."""
        loop = self._loop or asyncio.get_running_loop()
        await loop.run_in_executor(None, fn)
