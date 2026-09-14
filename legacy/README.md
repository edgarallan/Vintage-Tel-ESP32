# Versione 1 — Raspberry Pi Zero 2 W

⚠️ **Niente qui dentro descrive il telefono attuale.** Questa è la prima versione del
progetto, in Python su Raspberry Pi, conservata quando il repo originale
`Vintage-Tel-with-Bluetooth` è stato cancellato.

**Per il telefono di oggi** — ESP32, firmware in C — guarda `firmware/`, `docs/` e
`hardware/` nella radice del repo. Se un numero qui dentro contraddice quelli, valgono
quelli.

## Perché è stata abbandonata

Il Raspberry Pi Zero 2 W è diventato **costoso e difficile da reperire**. La v1 era
completa e con la suite di test verde, ma **non è mai stata provata su hardware**: la
bring-up `src/test_hardware.py` non è stata eseguita nemmeno una volta.

L'ESP32 ha risolto anche altro: avvio in un secondo invece di quaranta, nessuna microSD da
corrompere a ogni black-out, nessun rituale di spegnimento, e un quarto del consumo.

## Cosa c'è, e cosa vale ancora

### `diagrammi/`

| File | Vale ancora? |
|---|---|
| `06_bell_driver_schematic.svg` | **in parte** — la fisica delle bobine e la forma d'onda non cambiano; il ponte H montato è però un L298N e non un DRV8871 |
| `08_oled_placement_options.svg` | **sì** — la disposizione nella cassetta non dipende dal microcontrollore |
| `04_general_wiring.svg` | **no** — mappa GPIO del Raspberry, tutt'altra cosa |
| `07_audio_wiring.svg` | **no** — catena MAX98357A + SPH0645, sostituita dal codec WM8960 |

I diagrammi ancora validi della versione attuale stanno in `assets/diagrams/`.

### `docs/`

Panoramica, cablaggio, installazione, configurazione software e troubleshooting della
versione Raspberry, più `install.sh` e le note di architettura. **Superati**, ma raccontano
le scelte da cui è nata la versione attuale.

### `firmware/`

L'implementazione Python completa — macchina a stati, lettore del disco, rubrica, driver del
campanello, HFP via oFono — con la sua suite pytest, 35 test verdi.

**È la specifica eseguibile del comportamento** che la versione C riproduce. Quando ci si
chiede "come si comportava la v1 in questo caso", la risposta è qui: `src/main.py` per la
macchina a stati, `tests/` per i casi limite. Diversi test Unity di `firmware/tests/` sono
la traduzione diretta di quelli Python.

### `CLAUDE-v1.md`

Le istruzioni di progetto della v1, con architettura e invarianti di allora. Utile per
capire cosa è stato mantenuto nel passaggio a C — la regola delle transizioni in un punto
solo, i GPIO mai in polling — e cosa è stato lasciato, come il lock sullo stato, che senza
concorrenza non serviva più.
