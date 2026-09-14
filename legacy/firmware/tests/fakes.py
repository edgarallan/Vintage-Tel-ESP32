"""
fakes.py — Doppi di test per hardware e backend.

I moduli reali degradano graziosamente off-Pi (try/except ImportError), ma per
testare la state machine ci serve pilotare gli eventi in modo deterministico e
osservare le azioni invocate sui backend. Questi fake espongono la stessa
superficie usata da VintageTel.
"""

import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


class FakeHook:
    """Hook switch pilotabile: push(True)=cornetta su, push(False)=giù."""

    def __init__(self):
        self._q: asyncio.Queue[bool] = asyncio.Queue()
        self.is_up = False

    async def start(self): ...
    async def stop(self): ...

    async def events(self):
        while True:
            yield await self._q.get()

    def push(self, hook_up: bool):
        self.is_up = hook_up
        self._q.put_nowait(hook_up)


class FakeDial:
    """Disco pilotabile: push(n) emette la cifra n."""

    def __init__(self):
        self._q: asyncio.Queue[int] = asyncio.Queue()
        self._new = asyncio.Event()

    async def start(self): ...
    async def stop(self): ...

    async def digits(self):
        while True:
            yield await self._q.get()

    async def next_digit_event(self):
        self._new.clear()
        await self._new.wait()

    def push(self, digit: int):
        self._q.put_nowait(digit)
        self._new.set()


class FakeBell:
    """Campanello che registra start/stop e tiene lo stato 'ringing'."""

    def __init__(self):
        self.ringing = False
        self.calls: list[str] = []

    async def start(self): ...
    async def stop(self): ...

    async def start_ringing(self):
        self.ringing = True
        self.calls.append("start")

    async def stop_ringing(self):
        self.ringing = False
        self.calls.append("stop")


class FakeBackend:
    """Backend Bluetooth che registra le azioni invocate."""

    def __init__(self, *, connected=True, registered=True, place_ok=True):
        self.connected = connected
        self.registered = registered
        self._place_ok = place_ok
        self.on_incoming_call = None
        self.on_call_ended = None
        self.actions: list[tuple] = []

    async def start(self): ...
    async def stop(self): ...

    async def place_call(self, number: str) -> bool:
        self.actions.append(("place_call", number))
        return self._place_ok

    async def answer(self):
        self.actions.append(("answer",))

    async def reject(self):
        self.actions.append(("reject",))

    async def hangup(self):
        self.actions.append(("hangup",))

    async def send_dtmf(self, digit: str):
        self.actions.append(("dtmf", digit))


def make_config() -> dict:
    """Config minima ma completa per costruire VintageTel nei test.

    Timeout volutamente piccoli per test veloci e deterministici.
    """
    return {
        "hook": {"inverted": False, "debounce_ms": 100},
        "dial": {
            "pulse_bouncetime_ms": 0,
            "dial_timeout_s": 0.15,
            "quick_dial_timeout_s": 0.08,
            "zero_pulses": 10,
        },
        "bell": {"enabled": True, "frequency_hz": 22,
                 "pattern_on_ms": 10, "pattern_off_ms": 10, "max_rings": 1},
        "display": {"enabled": False},
        "led": {"enabled": False},
        "audio": {"sample_rate": 16000},
        "phonebook": {"db_path": "phonebook.db", "quick_dial": {9: "112"}},
        "bluetooth": {"enabled": False},
    }


class Harness:
    """Avvia i loop hook/dial della state machine e li chiude a fine test."""

    def __init__(self, tel):
        self.tel = tel
        self._tasks: list[asyncio.Task] = []

    async def __aenter__(self):
        tel = self.tel
        if tel.bt:
            tel.bt.on_incoming_call = tel._on_incoming_call
            tel.bt.on_call_ended = tel._on_call_ended
        self._tasks = [
            asyncio.create_task(tel._hook_loop()),
            asyncio.create_task(tel._dial_loop()),
        ]
        await asyncio.sleep(0)  # lascia partire i loop
        return self

    async def __aexit__(self, *exc):
        for t in self._tasks:
            t.cancel()
        await asyncio.gather(*self._tasks, return_exceptions=True)


async def settle(seconds: float = 0.03):
    """Lascia processare gli eventi accodati ai loop."""
    await asyncio.sleep(seconds)
