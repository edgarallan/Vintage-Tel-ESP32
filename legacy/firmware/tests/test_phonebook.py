"""Test della rubrica SQLite (comportamento + nessuna connessione lasciata aperta)."""

import sys
import warnings
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from src.phonebook import Phonebook


@pytest.fixture
def pb(tmp_path):
    return Phonebook({"db_path": str(tmp_path / "pb.db"), "quick_dial": {9: "112"}})


def test_add_and_lookup_fuzzy(pb):
    pb.add("Mario", "+393331234567")
    # lookup confronta gli ultimi 9 digit: il caller-id può arrivare formattato
    assert pb.lookup("0039-333-1234567") == "Mario"
    assert pb.lookup("333 1234567") == "Mario"


def test_lookup_unknown_returns_none(pb):
    assert pb.lookup("+393339999999") is None


def test_lookup_empty_returns_none(pb):
    assert pb.lookup("sconosciuto") is None


def test_duplicate_number_ignored(pb):
    pb.add("Mario", "+393331234567")
    pb.add("Mario bis", "+393331234567")   # stesso numero → IntegrityError gestito
    assert pb.lookup("+393331234567") == "Mario"


def test_quick_dial_db_overrides_config(pb):
    assert pb.quick_dial(9) == "112"            # da config
    pb.add("Emergenza", "118", quick_dial=9)
    assert pb.quick_dial(9) == "118"            # il DB prevale


def test_quick_dial_unknown_returns_none(pb):
    assert pb.quick_dial(7) is None


def test_all_contacts_sorted(pb):
    pb.add("Zoe", "+393330000001")
    pb.add("Anna", "+393330000002")
    names = [c["name"] for c in pb.all_contacts()]
    assert names == ["Anna", "Zoe"]


def test_no_unclosed_connections(tmp_path):
    """Le operazioni non devono lasciare connessioni SQLite aperte."""
    with warnings.catch_warnings():
        warnings.simplefilter("error", ResourceWarning)
        pb = Phonebook({"db_path": str(tmp_path / "pb.db"), "quick_dial": {}})
        pb.add("Mario", "+393331234567", quick_dial=1)
        pb.lookup("+393331234567")
        pb.quick_dial(1)
        pb.all_contacts()
