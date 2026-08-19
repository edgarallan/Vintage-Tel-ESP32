# CLAUDE.md

Guida per Claude Code (claude.ai/code) quando lavora su questo repository.

## Progetto

Retrofit hardware/software che trasforma un **Siemens/FATME S62** italiano degli anni '70
(telefono a disco) in un **vivavoce Bluetooth HFP** per il cellulare, conservando cornetta,
disco combinatore e campanello elettromeccanico originali.

Hardware target: **ESP32 originale** (ESP-WROOM-32), firmware in **C/ESP-IDF**.
Documentazione e commenti del codice sono **in italiano**.

> **Versione 2.** La v1 girava su Raspberry Pi Zero 2 W in Python:
> [Vintage-Tel-with-Bluetooth](https://github.com/edgarallan/Vintage-Tel-with-Bluetooth),
> ora archiviata. Riscritta perché il Pi Zero 2 W è diventato costoso e poco reperibile.

## Vincoli non negoziabili

Prima di proporre qualunque modifica hardware o di dipendenze, verifica questi punti:
sono già costati indagini e non vanno riscoperti.

1. **Deve essere l'ESP32 originale.** S3, C3, C6, H2 sono solo BLE: senza Bluetooth
   Classic non esiste HFP. Il Pico W/Pico 2 W è escluso da un difetto aperto del CYW43439
   che impedisce le connessioni SCO, cioè l'audio in chiamata
   ([pico-sdk #1461](https://github.com/raspberrypi/pico-sdk/issues/1461)).
2. **Percorso audio HFP = vHCI + Wide Band Speech.** Il percorso PCM hardware supporta
   solo CVSD 8 kHz; mSBC a 16 kHz esiste soltanto su vHCI, dove Bluedroid decodifica e
   consegna PCM all'applicazione.
3. **Il cordone della cornetta ha TRE conduttori.** Dentro la cornetta viaggiano solo
   segnali analogici: un microfono I2S ne richiederebbe cinque. Da qui il codec WM8960
   nella base e la capsula electret nella cornetta.
4. **Serve la WM8960 Audio Board, non l'Audio HAT.** L'HAT ha il jack da 3,5 mm in sola
   uscita; la Audio Board ha il jack a 4 segmenti con ingresso microfonico, che è ciò
   che permette di collegare la cornetta a 3 fili senza saldature.
5. **WiFi e Bluetooth non convivono mai.** Condividono la radio: il WiFi si accende solo
   in modalità configurazione, mai durante l'esercizio.
6. **Il consumo a riposo non deve scendere sotto ~60 mA.** Il modulo di alimentazione
   scelto disabilita l'uscita sotto ~50 mA: display e LED non vanno mai spenti del tutto.

## Struttura

- `firmware/core/` — **logica pura in C, zero `#include "esp_*"`**. È ciò che rende il
  firmware testabile su un PC. Dipende dall'hardware solo tramite `hw_iface.h`, una struct
  di puntatori a funzione.
- `firmware/hal/` — driver ESP-IDF sottili che implementano quella struct.
- `firmware/main/` — `app_main.c`: crea la coda eventi e avvia i task.
- `firmware/tests/` — Unity + CMake nativo. Gira **senza ESP32 e senza ESP-IDF**.
- `hardware/` — `pinout.md` (fonte di verità dei GPIO), `bell_driver.md` (campanello).
- `docs/` — guide in italiano numerate `01`–`07`, più la lista d'acquisto.
- `assets/` — diagrammi di montaggio e foto annotate del retrofit.

## Comandi

```bash
# Test del nucleo — sul Mac, senza hardware e senza ESP-IDF
cd firmware
cmake -S tests -B tests/build && cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
./tests/coverage.sh          # copertura di core/, soglia 80%

# Firmware — richiede ESP-IDF e l'ESP32 collegato
idf.py set-target esp32
idf.py build flash monitor
```

`coverage.sh` **unisce** i dati gcov di tutti gli eseguibili: un modulo compilato in più
target produce un `.gcda` per oggetto, e guardarne uno solo fa sembrare scoperte righe che
un altro test esercita. Senza l'unione la misura risultava 76% invece del 100% reale.

## Architettura

**Un solo task possiede lo stato.** Ogni sorgente — ISR del gancio, PCNT del disco,
callback HFP, timer del campanello — impacchetta un `phone_ev_t` e lo manda in coda con
`xQueueSend` (o `xQueueSendFromISR`). La macchina a stati gira in un unico task.

```
ISR gancio ─┐
PCNT disco ─┼─→ xQueueSend(phone_evt_q) ─→ phone_task ─→ transition() ─→ LED/OLED/campanello
HFP cb     ─┤                                (unico proprietario dello stato)
esp_timer  ─┘
```

Invarianti da preservare:

1. **Tutte le transizioni passano da `transition()`** in `core/phone_fsm.c`. Non assegnare
   mai `p->state` altrove.
2. **Niente mutex sullo stato.** Non servono: senza concorrenza non c'è cosa proteggere.
   Se ti viene voglia di aggiungerne uno, il disegno si è rotto da qualche altra parte.
3. **`core/` non include mai `esp_*.h`.** È il confine che rende testabile la logica.
   Il tempo arriva sempre dal chiamante (campo `now_ms` dell'evento), mai da un orologio:
   è ciò che rende i test deterministici e istantanei.
4. **GPIO interrupt-driven, mai in polling.** Niente `while (1) { leggi(); }`.
5. **Il debounce è hardware.** Il filtro anti-glitch del peripheral PCNT sostituisce
   optoaccoppiatori e reti RC della v1.

### Disco combinatore

Due contatti: **impulsi** e **NSI** (fuori-normale, chiuso durante la rotazione). Gli
impulsi si contano solo mentre l'NSI indica che il disco gira, altrimenti le vibrazioni
producono cifre fantasma. Convenzione italiana: **10 impulsi = 0**. Esiste un fallback a
tempo se il contatto NSI non riapre (`digit_timeout_ms`), con la garanzia che NSI e
fallback non emettano due cifre per la stessa rotazione.

### Campanello

Onda quadra a ~22 Hz generata alternando IN1/IN2 del ponte H in antifase, con `esp_timer`.
Cadenza italiana 1 s on / 4 s off in `core/ring_pattern.c`. **La linea a 24-30 V è reale**:
le modifiche al cablaggio vanno in `hardware/bell_driver.md`, non fatte a occhio.

## Mappa GPIO

Fonte di verità: `hardware/pinout.md`. Se un modulo usa un pin diverso è un bug — oppure
si aggiorna `pinout.md` nello stesso commit.

| Funzione | GPIO |
|---|---|
| Disco impulsi / NSI | 4 / 32 |
| Gancio | 18 |
| Campanello IN1 / IN2 | 13 / 14 |
| Pulsante | 23 |
| LED WS2812 | 27 |
| I2S BCLK / WS / DIN / DOUT | 26 / 25 / 33 / 22 |
| I2C SDA / SCL | 21 / 19 |

**Restano 13 pin utilizzabili per 13 segnali: margine zero.** Sono esclusi i GPIO 6-11
(flash SPI), 16-17 (PSRAM su WROVER), 1/3 (console UART), 0/2/5/12/15 (strapping: se
caricati impediscono il boot) e 34-39 (solo input e **senza pull-up interno**, quindi
inservibili per i contatti puliti di disco e gancio).

## Configurazione

Tutto in **NVS**: MAC del cellulare accoppiato, contatti, quick-dial, volumi, parametri di
disco e campanello. Si modifica dalla pagina web in **modalità configurazione**, che si
attiva tenendo premuto il pulsante rubrica all'accensione. Da lì passa anche l'OTA.

Il MAC del cellulare non va mai nei sorgenti né stampato per intero nei log.

## Stile

PEP 8 non si applica: qui è C. Segui lo stile già presente — `snake_case`, funzioni corte,
commenti in italiano che spiegano **perché**, non cosa. I commenti che valgono sono quelli
che raccontano un vincolo fisico o una decisione: "le vibrazioni a disco fermo non devono
contare", non "incrementa il contatore".
