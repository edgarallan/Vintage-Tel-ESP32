"""Test del wiring config in AudioManager: sample_rate + volume softvol ALSA."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from src.audio import AudioManager


def test_sample_rate_from_config():
    assert AudioManager({"sample_rate": 16000}).sample_rate == 16000


def test_sample_rate_default():
    assert AudioManager({}).sample_rate == AudioManager.DEFAULT_SAMPLE_RATE


def test_gains_read_from_config():
    a = AudioManager({"speaker_gain_db": -6, "mic_gain_db": 18})
    assert a._speaker_gain_db == -6
    assert a._mic_gain_db == 18


def test_volume_cmd_uses_db_and_separator():
    cmd = AudioManager._build_volume_cmd("PhoneSoftVol", -6)
    assert cmd == ["amixer", "-D", "default", "--", "sset", "PhoneSoftVol", "-6dB"]


def test_volume_cmd_positive_gain():
    cmd = AudioManager._build_volume_cmd("PhoneCaptureVol", 18)
    assert cmd[-2:] == ["PhoneCaptureVol", "18dB"]


def test_apply_alsa_volume_noop_without_amixer(monkeypatch):
    """Off-Pi (amixer assente) non deve sollevare eccezioni."""
    monkeypatch.setattr("src.audio.shutil.which", lambda _: None)
    AudioManager({"speaker_gain_db": 0})._apply_alsa_volume()
