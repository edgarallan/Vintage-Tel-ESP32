# Mappa GPIO — ESP32-WROVER-E

**Fonte di verità per l'assegnazione dei pin.** Se un modulo del firmware usa un pin
diverso da questa tabella, è un bug — oppure va aggiornata questa tabella nello stesso commit.

## Pin inutilizzabili, e perché

Prima della mappa serve capire cosa **non** si può usare, perché è ciò che rende
l'assegnazione obbligata invece che arbitraria.

| GPIO | Perché è escluso |
|---|---|
| 6, 7, 8, 9, 10, 11 | Collegati alla flash SPI interna. Usarli manda in crash il chip |
| 16, 17 | Usati dalla **PSRAM** sui moduli WROVER. Su un WROOM sarebbero liberi |
| 1, 3 | UART0 TX/RX: console seriale e flashing |
| 0, 2, 5, 12, 15 | **Strapping pin**: il loro livello all'accensione decide modalità di boot e tensione della flash. Se un contatto esterno li tiene bassi o alti al momento sbagliato, **il telefono non si avvia** |
| 34, 35, 36, 39 | **Solo input e senza pull-up interno.** Inutilizzabili per i contatti puliti di disco e gancio senza resistenze esterne saldate |

Restano **13 pin** utilizzabili: 4, 13, 14, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33.
Servono **13 segnali**. Margine: **zero**.

## Assegnazione

| Funzione | GPIO | Direzione | Peripheral | Note |
|---|---|---|---|---|
| Disco — impulsi | **4** | IN, pull-up | PCNT | Filtro anti-glitch hardware: niente debounce software |
| Disco — NSI (fuori-normale) | **32** | IN, pull-up | GPIO | Abilita il conteggio mentre il disco ruota |
| Gancio (cornetta) | **18** | IN, pull-up | GPIO + ISR | `xQueueSendFromISR` verso il task telefono |
| Campanello — IN1 | **13** | OUT | esp_timer | DRV8871 |
| Campanello — IN2 | **14** | OUT | esp_timer | In **antifase** con IN1, ~22 Hz |
| Pulsante rubrica | **23** | IN, pull-up | GPIO | All'avvio: config mode. In esercizio: richiama ultimo numero |
| LED di stato WS2812 | **27** | OUT | RMT | Un pixel indirizzabile |
| I2S — BCLK | **26** | OUT | I2S0 | **Condiviso** INMP441 + MAX98357A |
| I2S — WS / LRCLK | **25** | OUT | I2S0 | **Condiviso** INMP441 + MAX98357A |
| I2S — DIN (dal mic) | **33** | IN | I2S0 | `SD` dell'INMP441 |
| I2S — DOUT (all'ampli) | **22** | OUT | I2S0 | `DIN` del MAX98357A |
| I2C — SDA | **21** | I/O | I2C0 | SSD1306 |
| I2C — SCL | **19** | OUT | I2C0 | SSD1306 |

## Riserva

Non c'è un pin libero. Se in corso d'opera ne servisse uno, l'unica manovra possibile è
spostare **gancio** o **NSI** su un pin solo-input (34-39) aggiungendo una **resistenza di
pull-up esterna da 10 kΩ** verso 3V3. È l'unica saldatura di riserva prevista dal progetto.

## Collegamenti dei moduli

### INMP441 (microfono, nella cornetta)
| Pin modulo | Va a |
|---|---|
| VDD | 3V3 |
| GND | GND |
| SCK | GPIO 26 |
| WS | GPIO 25 |
| SD | GPIO 33 |
| L/R | GND (canale sinistro) — ponticello **sul modulo**, non fino alla base |

Sono **5 fili** fino alla cornetta. Contali sul cordone originale prima di ordinare:
se sono 4, il cablaggio interno al cordone va rifatto.

### MAX98357A (ampli, nella base)
| Pin modulo | Va a |
|---|---|
| VIN | 5V |
| GND | GND |
| BCLK | GPIO 26 |
| LRC | GPIO 25 |
| DIN | GPIO 22 |
| GAIN | libero (+9 dB) |
| Morsetto a vite | **Capsula della cornetta** |

### DRV8871 (campanello)
| Pin modulo | Va a |
|---|---|
| IN1 / IN2 | GPIO 13 / 14 |
| VM | Uscita boost XL6009 (~24 V) |
| GND | GND |
| Morsetti OUT1/OUT2 | **Bobine del campanello originale** |

⚠️ Regola dal progetto originale, ancora valida: le bobine immagazzinano energia.
Porta IN1=IN2=0 (coast) e togli alimentazione **prima** di scollegare i cavi.

### SSD1306 e WS2812
OLED: VCC 3V3, GND, SDA 21, SCL 19 — indirizzo I2C `0x3C`.
WS2812: VCC 5V, GND, DIN 27.
