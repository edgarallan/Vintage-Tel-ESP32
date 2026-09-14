"""Test della state machine VintageTel con hardware/backend finti."""

from fakes import FakeBackend, Harness, settle

from src.main import State


async def test_incoming_call_transitions_to_ringing(tel):
    tel.bt = FakeBackend()
    async with Harness(tel):
        await tel._on_incoming_call("+393331234567")
        assert tel.state == State.RINGING
        assert tel.bell.ringing is True


async def test_incoming_call_ignored_when_not_idle(tel):
    tel.bt = FakeBackend()
    async with Harness(tel):
        tel.hook.push(True)          # IDLE → DIALING
        await settle()
        assert tel.state == State.DIALING
        await tel._on_incoming_call("x")
        assert tel.state == State.DIALING   # invariato
        assert tel.bell.ringing is False


async def test_hook_up_answers_incoming_call(tel):
    tel.bt = FakeBackend()
    async with Harness(tel):
        await tel._on_incoming_call("x")
        tel.hook.push(True)          # solleva cornetta → risponde
        await settle()
        assert tel.state == State.IN_CALL
        assert ("answer",) in tel.bt.actions
        assert tel.bell.ringing is False


async def test_hook_down_rejects_incoming_call(tel):
    tel.bt = FakeBackend()
    async with Harness(tel):
        await tel._on_incoming_call("x")
        tel.hook.push(False)         # cornetta giù durante squillo → rifiuta
        await settle()
        assert tel.state == State.IDLE
        assert ("reject",) in tel.bt.actions
        assert tel.bell.ringing is False


async def test_dialing_places_call_after_interdigit_timeout(tel):
    tel.bt = FakeBackend(place_ok=True)
    async with Harness(tel):
        tel.hook.push(True)          # IDLE → DIALING
        await settle()
        assert tel.state == State.DIALING
        for d in (1, 2, 3):
            tel.dial.push(d)
            await settle()
        await settle(0.3)            # oltre dial_timeout_s → compone
        assert ("place_call", "123") in tel.bt.actions
        assert tel.state == State.IN_CALL


async def test_quick_dial_on_single_digit(tel):
    tel.bt = FakeBackend()
    async with Harness(tel):
        tel.hook.push(True)
        await settle()
        tel.dial.push(9)             # 9 → quick-dial 112
        await settle(0.2)            # oltre quick_dial_timeout_s
        assert ("place_call", "112") in tel.bt.actions


async def test_hook_down_hangs_up_active_call(tel):
    tel.bt = FakeBackend()
    async with Harness(tel):
        await tel._on_incoming_call("x")
        tel.hook.push(True)          # risponde → IN_CALL
        await settle()
        tel.hook.push(False)         # riaggancia
        await settle()
        assert tel.state == State.IDLE
        assert ("hangup",) in tel.bt.actions


async def test_dial_during_call_sends_dtmf(tel):
    tel.bt = FakeBackend()
    async with Harness(tel):
        await tel._on_incoming_call("x")
        tel.hook.push(True)          # IN_CALL
        await settle()
        tel.dial.push(5)             # disco in chiamata → DTMF
        await settle()
        assert ("dtmf", "5") in tel.bt.actions


async def test_failed_outgoing_returns_to_idle(tel):
    tel.bt = FakeBackend(place_ok=False)
    async with Harness(tel):
        tel.hook.push(True)
        await settle()
        tel.dial.push(1)
        await settle(0.3)            # timeout → place_call fallisce
        # _place_call su fallimento riproduce busy per ~3s, poi torna IDLE
        await settle(3.2)
        assert ("place_call", "1") in tel.bt.actions
        assert tel.state == State.IDLE


async def test_no_bt_connection_busy_then_idle(tel):
    tel.bt = FakeBackend(connected=False)   # Bluetooth non connesso
    async with Harness(tel):
        tel.hook.push(True)
        await settle()
        tel.dial.push(1)
        await settle(0.3)            # timeout → tenta la chiamata
        await settle(3.2)            # busy ~3s, poi IDLE
        assert ("place_call", "1") not in tel.bt.actions   # non chiama senza BT
        assert tel.state == State.IDLE


async def test_remote_hangup_resets_to_idle(tel):
    tel.bt = FakeBackend()
    async with Harness(tel):
        await tel._on_incoming_call("x")
        tel.hook.push(True)
        await settle()
        assert tel.state == State.IN_CALL
        await tel._on_call_ended()   # il remoto chiude
        assert tel.state == State.IDLE
