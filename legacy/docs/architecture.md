# Architettura — note di design

## Perché `asyncio`?

Il sistema deve gestire contemporaneamente:
- Eventi GPIO (hook, disco) → asincroni e imprevedibili
- DBus calls (oFono per BT-HFP) → bloccanti
- Audio I2S → real-time
- Display refresh → periodico
- Stati di chiamata → reattivi

`asyncio` è la scelta giusta: tutti i moduli espongono un'API async e si coordinano nella `main.VintageTel` state machine senza thread espliciti.

## Threading boundaries

Solo due "isole" non-async, isolate in executor:
1. **DBus + GLib main loop** (oFono): gira in thread separato, le callback usano `asyncio.run_coroutine_threadsafe`
2. **PJSIP**: gira in thread interno, anch'esso usa la stessa tecnica per inoltrare eventi all'event loop

## State machine come single source of truth

Tutte le transizioni passano da `VintageTel._transition()` che:
1. Acquisisce un `asyncio.Lock` per evitare race
2. Logga la transizione
3. Aggiorna i side-effect UI (LED, display)

Se si volesse estendere (es. "call waiting"), basta aggiungere uno stato all'enum e definire le transizioni in `_hook_loop` e nei callback dei backend.

## Disaccoppiamento backend

`bt_phone.py` e `sip_client.py` hanno la stessa interfaccia:

```python
class CallBackend:
    @property
    def available(self) -> bool: ...
    async def place_call(self, number: str) -> bool: ...
    async def answer(self): ...
    async def reject(self): ...
    async def hangup(self): ...
    async def send_dtmf(self, digit: str): ...
    on_incoming_call: Callable
    on_call_ended: Callable
```

Questo permette al `main.py` di selezionare il backend dinamicamente in base alla configurazione (`mode: hybrid` sceglie BT se disponibile, altrimenti SIP).

## Estensioni naturali

Cosa aggiungere se vuoi spingere oltre:

### 1. WebUI di configurazione
Un piccolo server FastAPI su porta 80 per:
- Modificare rubrica
- Settare quick-dial
- Vedere log live
- Aggiornare config senza SSH

### 2. Multi-cellulare
oFono supporta più modem accoppiati. La logica HFP può ruotare tra cellulari diversi (es. lavoro/personale).

### 3. Voicemail / segreteria
Quando il telefono è in stato RINGING e nessuno risponde entro N squilli:
- Riproduce un messaggio pre-registrato dal speaker
- Registra audio dal mic per M secondi
- Salva file WAV con timestamp
- Notifica al cellulare via Telegram bot

### 4. Integrazione domotica
Trigger su eventi:
- Chiamata in arrivo → accendi luce
- Cornetta sollevata → pausa Spotify
- Numero specifico → comandi Home Assistant

### 5. Hot reload config
Watcher su `config.yaml` con `watchdog` → ricarica senza riavvio del servizio.

## Anti-pattern evitati

- **Threading + GPIO callbacks**: si usa `gpiozero` con il suo event loop, le callback vengono inoltrate ad asyncio
- **Polling GPIO**: nessun `while True: read()` — sempre interrupt-driven
- **Stato globale mutabile**: lo stato vive nella classe `VintageTel`, protetto da lock
- **Audio bloccante**: i toni sono coroutine cancellabili, non bloccano lo state machine
