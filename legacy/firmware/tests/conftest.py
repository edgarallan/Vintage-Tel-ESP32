"""Fixture condivise per la suite firmware."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from src.main import VintageTel  # noqa: E402

from fakes import FakeBell, FakeDial, FakeHook, make_config  # noqa: E402


@pytest.fixture
def tel(tmp_path):
    """VintageTel con hardware finto (hook/dial/bell) e display/LED disattivi.

    Il backend BT va impostato dal singolo test (tel.bt = FakeBackend()).
    """
    cfg = make_config()
    cfg["phonebook"]["db_path"] = str(tmp_path / "phonebook.db")
    machine = VintageTel(cfg)
    machine.hook = FakeHook()
    machine.dial = FakeDial()
    machine.bell = FakeBell()
    return machine
