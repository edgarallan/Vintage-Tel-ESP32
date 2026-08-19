# 02 — Bill of Materials (BOM)

Prezzi indicativi 2026 in EUR, IVA inclusa. Per i moduli hobby vanno benissimo Amazon,
PiMoroni, AliExpress; per i componenti critici (alimentazione) meglio Adafruit/Mouser/RS.

> **Criterio guida di questa BOM: minimizzare le saldature.** Dove esiste una variante con
> header già saldati o con morsetto a vite, è quella indicata. L'unica giunzione che resta
> è sui fili del cordone della cornetta, ed è fattibile a WAGO o crimpaggio.

## Cervello

| # | Componente | Q.tà | € | Note |
|---|-----------|------|---|------|
| 1 | **ESP32-DevKitC-VE** (modulo WROVER-E, 8 MB PSRAM) | 1 | 8 | **Deve essere l'ESP32 originale**: S3/C3/C6/H2 non hanno il Bluetooth Classic e quindi non possono fare HFP |
| 2 | **Basetta breakout a morsetti a vite** per ESP32-DevKitC (38 pin) | 1 | 8 | Venduta **assemblata, zero saldature**. Il DevKitC si innesta e ogni GPIO diventa una vite |

> Verifica all'acquisto che la basetta sia per il formato del tuo DevKitC (larghezza 0.9" o 1.0").
> Il WROVER-E è la versione larga.

## Audio

| # | Componente | Q.tà | € | Note |
|---|-----------|------|---|------|
| 3 | Ampli I2S **MAX98357A** (breakout) | 1 | 6 | Capsula della cornetta sul **morsetto a vite**. Pin `GAIN` libero = +9 dB |
| 4 | Mic I2S MEMS **INMP441** (modulo con header pre-saldati) | 1 | 2 | Va **nella cornetta**, al posto della capsula a carbone |

Mic e ampli condividono BCLK e WS su un unico bus I2S full-duplex a 16 kHz, slot a 32 bit.
Nessun codec I2C: l'I2C resta libero per il display.

> **Da verificare al montaggio**: l'INMP441 richiede **5 fili** fino alla cornetta
> (3V3, GND, SCK, WS, SD). I cordoni originali ne hanno spesso solo 4. Se non bastano,
> va sostituito il cablaggio interno al cordone.

## Alimentazione

| # | Componente | Q.tà | € | Note |
|---|-----------|------|---|------|
| 5 | Alimentatore USB **5 V / 3 A** | 1 | 8 | Alimentazione primaria. Il cavo esce dal **foro del cordone di linea originale** |
| 6 | **Adafruit PowerBoost 1000C** | 1 | 22 | Batteria tampone con **vero load-sharing** (TPS61090): commuta su USB quando c'è rete, su batteria quando manca. Pin EN, **nessun auto-spegnimento** |
| 7 | Cella LiPo 1S 2500-3000 mAh con connettore JST-PH | 1 | 15 | Tampone, non alimentazione principale |
| 8 | Interruttore on/off (vintage style) | 1 | 4 | Nascosto sul fondo |

> **Perché non un IP5306 da 4 €**: fa vero pass-through, ma **disabilita l'uscita quando il
> carico scende sotto ~50 mA**. Il telefono a riposo assorbe ~100 mA, quindi il margine è
> solo 2×: basta spegnere il display per rischiare che il modulo stacchi da solo e tu perda
> le chiamate, in silenzio. Se accetti quel rischio è un ripiego valido a 1/5 del prezzo.

> **Budget di corrente**: ESP32 con BT connesso ~80 mA, OLED ~15 mA, picco del campanello
> ~400 mA dal boost. Il PowerBoost eroga 1 A: ci sta, ma tieni i 100 µF sull'ingresso **e**
> sull'uscita dell'XL6009 per assorbire i picchi degli squilli.

## Driver campanello

| # | Componente | Q.tà | € | Note |
|---|-----------|------|---|------|
| 9 | Boost 5V→~24V (**XL6009** col trimmer) | 1 | 4 | Alimenta solo l'H-bridge; +47-100 µF su ingresso e uscita |
| 10 | **H-bridge DRV8871** (breakout) | 1 | 8 | Regge 45 V, **morsetti a vite**. **NON** L9110S/DRV8833 (max ~12 V) |

Vedi `hardware/bell_driver.md` per la fisica delle bobine e le verifiche meccaniche.

## Disco, gancio e interazione

| # | Componente | Q.tà | € | Note |
|---|-----------|------|---|------|
| 11 | Display OLED 0.96" 128×64 I2C (SSD1306) | 1 | 5 | Stato, cifre, chi chiama, IP in modalità config |
| 12 | **LED WS2812** su modulino (3 pin) | 1 | 1 | Stato sistema. **1 GPIO invece di 3** e nessuna resistenza |
| 13 | Microswitch (pulsante rubrica) | 1 | 1 | Nascosto sotto il piano |

> **Cosa NON serve più** rispetto al progetto Raspberry: i **2× PC817**, le **4 resistenze
> di pull-up** e la **perfboard** che li ospitava. Impulsi e gancio sono contatti puliti
> verso massa: vanno diretti su GPIO con pull-up interno, e il debounce lo fa in hardware
> il filtro anti-glitch del peripheral **PCNT**. Sparisce un'intera basetta da saldare.
> Spariscono anche il LED RGB a 4 zampe e le sue 3 resistenze, sostituiti dal WS2812.

## Cablaggio e meccanica

| # | Componente | Q.tà | € | Note |
|---|-----------|------|---|------|
| 14 | **Morsetti a leva WAGO 221 mini** | 10 | 6 | Giunzioni del cordone cornetta **senza stagno** |
| 15 | Cavetti Dupont F-F assortiti | 40 | 4 | Collegamenti tra moduli |
| 16 | Cavetto 26AWG flessibile (vari colori) | 3 m | 4 | Cablaggio interno |
| 17 | Termorestringente vari diametri | - | 3 | |
| 18 | Distanziali in nylon M2.5 | 10 | 3 | Montaggio schede |

## Totale stimato

| Configurazione | Costo |
|----------------|-------|
| **Minimo** (BT + disco + gancio + audio, alimentazione da rete senza tampone) | ~65 € |
| **Medio** (+ campanello + WS2812) | ~80 € |
| **Completo** (+ display + tampone PowerBoost + cella) | ~120 € |

Escluse spedizioni e il telefono stesso (sui mercatini italiani 20-50 €).

## Strumenti

- **Cacciavite piccolo a taglio** — per i morsetti a vite: è lo strumento principale
- Multimetro
- Saldatore **solo se** il cordone della cornetta va rifatto
- Cacciaviti specifici Siemens (testa cilindrica) per aprire l'S62
- Oscilloscopio (opzionale, utile per il debug del disco)

## Fornitori

- **Adafruit / Mouser / RS** — PowerBoost 1000C, MAX98357A, DRV8871
- **AliExpress** — DevKitC, basetta a morsetti, INMP441, WS2812, XL6009, OLED
- **Subito.it / eBay.it** — telefoni SIP vintage italiani
