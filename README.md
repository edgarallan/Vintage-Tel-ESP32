# Vintage-Tel-ESP32

Un telefono da scrivania **Siemens/FATME S62** degli anni '70 trasformato in **vivavoce
Bluetooth HFP** per il cellulare, mantenendo intatti cornetta, disco combinatore e
campanello elettromeccanico originali.

Firmware in **C/ESP-IDF** su **ESP32**.

> **Versione 2 del progetto.** La v1 girava su Raspberry Pi Zero 2 W in Python:
> [Vintage-Tel-with-Bluetooth](https://github.com/edgarallan/Vintage-Tel-with-Bluetooth).
> È stata riscritta da zero perché il Pi Zero 2 W è diventato costoso e difficile da
> reperire — e perché per un telefono un microcontrollore è semplicemente più adatto:
> si accende in un secondo, non ha una scheda SD da corrompere quando stacchi la corrente
> e non ha bisogno di essere spento con un rituale.

## Cosa fa

- **Chiamate in entrata**: il cellulare squilla, il **campanello originale a bobine** suona
  col ritmo Telecom italiano (1 s on / 4 s off). Sollevi la cornetta e rispondi.
- **Chiamate in uscita**: sollevi, senti il **tono di libero italiano a 425 Hz**, componi
  **col disco** e parte la chiamata.
- **Quick-dial**: una cifra sola sul disco, una pausa, e chiama il contatto associato.
- **DTMF**: il disco funziona anche dentro la chiamata, per i menu vocali.
- **Rubrica**: chi chiama appare sul display OLED col nome, non col numero.
- **Richiamo**: un pulsante nascosto ricompone l'ultimo numero.
- **Configurazione senza aprire**: tieni premuto il pulsante all'accensione e il telefono
  alza un access point WiFi con una pagina per contatti, volumi e **aggiornamento firmware**.

## Perché ESP32 e non un Pico

Le connessioni **SCO** — il canale audio delle chiamate Bluetooth — non si stabiliscono
sul CYW43439 di Pico W e Pico 2 W: è un difetto noto e ancora aperto
([pico-sdk #1461](https://github.com/raspberrypi/pico-sdk/issues/1461)). Senza SCO non c'è
audio in chiamata. Inoltre **solo l'ESP32 originale ha il Bluetooth Classic** — S3, C3, C6
e H2 sono solo BLE — ed ESP-IDF fornisce nativamente il ruolo
[HFP Client](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_hf_client.html),
cioè esattamente il ruolo "vivavoce d'auto" che serve qui.

## Struttura

| Cartella | Contenuto |
|---|---|
| `firmware/core/` | Logica pura in C, **zero dipendenze ESP-IDF** — testabile sul PC |
| `firmware/hal/` | Driver ESP-IDF: PCNT, I2S, HFP, RMT, NVS |
| `firmware/tests/` | Test Unity che girano **senza ESP32 collegato** |
| `hardware/` | `pinout.md` (mappa GPIO), `bell_driver.md` (campanello) |
| `docs/` | Guide in italiano, numerate `01`–`07` |
| `assets/` | Diagrammi di montaggio e foto annotate del retrofit |

## Stato

🚧 **In costruzione.** Fase 1 (BOM e mappa GPIO) completata; il firmware è in sviluppo.

## Documentazione in italiano

Il progetto è documentato interamente in italiano, commenti del codice inclusi.
Parti da [`docs/01_overview.md`](docs/01_overview.md).
