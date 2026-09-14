"""Test della conversione impulsi → cifra del disco combinatore.

Off-Pi gpiozero.Button è None, quindi piloto direttamente i callback
(_on_nsi_start / _on_pulse / _on_nsi_end) come farebbero le interruzioni GPIO.
"""

import asyncio
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from src.dial_reader import DialReader


async def _read_one(reader, timeout=1.0):
    agen = reader.digits()
    return await asyncio.wait_for(agen.__anext__(), timeout)


async def _dial(reader, pulses: int):
    reader._on_nsi_start()
    for _ in range(pulses):
        reader._on_pulse()
    reader._on_nsi_end()
    # gap inter-cifra: lascia che il loop finalizzi prima della cifra seguente
    # (sull'hardware reale tra due cifre passano >100ms)
    await asyncio.sleep(0.01)


@pytest.mark.parametrize("pulses,expected", [
    (1, 1), (5, 5), (9, 9),
    (10, 0),   # convenzione italiana: 10 impulsi = 0
    (11, 0),   # oltre 10 → comunque 0
])
async def test_pulse_count_to_digit(pulses, expected):
    reader = DialReader({"pulse_bouncetime_ms": 0, "zero_pulses": 10})
    await reader.start()        # Button None off-Pi → cattura solo il loop
    try:
        await _dial(reader, pulses)
        assert await _read_one(reader) == expected
    finally:
        await reader.stop()


async def test_zero_pulses_emits_nothing():
    reader = DialReader({"pulse_bouncetime_ms": 0, "zero_pulses": 10})
    await reader.start()
    try:
        await _dial(reader, 0)   # disco mosso ma nessun impulso
        with pytest.raises(asyncio.TimeoutError):
            await _read_one(reader, timeout=0.1)
    finally:
        await reader.stop()


async def test_consecutive_digits():
    reader = DialReader({"pulse_bouncetime_ms": 0, "zero_pulses": 10})
    await reader.start()
    try:
        await _dial(reader, 3)
        await _dial(reader, 10)
        assert await _read_one(reader) == 3
        assert await _read_one(reader) == 0
    finally:
        await reader.stop()


async def test_fallback_finalizes_digit_without_nsi_end():
    """Se il rilascio NSI si perde, la cifra esce comunque dopo il timeout."""
    reader = DialReader({"pulse_bouncetime_ms": 0, "zero_pulses": 10,
                         "digit_timeout_s": 0.05})
    await reader.start()
    try:
        reader._on_nsi_start()
        for _ in range(4):
            reader._on_pulse()
        # NESSUN _on_nsi_end: deve scattare il fallback
        assert await _read_one(reader, timeout=1.0) == 4
    finally:
        await reader.stop()


async def test_no_double_emit_when_nsi_end_and_fallback_race():
    """NSI end + fallback non devono produrre due cifre per la stessa rotazione."""
    reader = DialReader({"pulse_bouncetime_ms": 0, "zero_pulses": 10,
                         "digit_timeout_s": 0.05})
    await reader.start()
    try:
        reader._on_nsi_start()
        for _ in range(2):
            reader._on_pulse()
        reader._on_nsi_end()                 # percorso normale
        assert await _read_one(reader) == 2
        # oltre il timeout del fallback: non deve arrivare una seconda cifra
        with pytest.raises(asyncio.TimeoutError):
            await _read_one(reader, timeout=0.2)
    finally:
        await reader.stop()


async def test_fallback_disabled_when_timeout_zero():
    """Con digit_timeout_s=0 il fallback è disattivo: serve il rilascio NSI."""
    reader = DialReader({"pulse_bouncetime_ms": 0, "zero_pulses": 10,
                         "digit_timeout_s": 0})
    await reader.start()
    try:
        reader._on_nsi_start()
        for _ in range(3):
            reader._on_pulse()
        with pytest.raises(asyncio.TimeoutError):
            await _read_one(reader, timeout=0.15)   # niente fallback
        reader._on_nsi_end()                          # ora finalizza
        assert await _read_one(reader) == 3
    finally:
        await reader.stop()
