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
| I2S — BCLK | **26** | OUT | I2S0 | Codec WM8960 |
| I2S — WS / LRCLK | **25** | OUT | I2S0 | Codec WM8960 |
| I2S — DIN (dal codec) | **33** | IN | I2S0 | `ADCDAT`: microfono della cornetta |
| I2S — DOUT (al codec) | **22** | OUT | I2S0 | `DACDAT`: capsula d'ascolto |
| I2C — SDA | **21** | I/O | I2C0 | **Bus condiviso**: WM8960 `0x1A` + SSD1306 `0x3C` |
| I2C — SCL | **19** | OUT | I2C0 | **Bus condiviso**: WM8960 `0x1A` + SSD1306 `0x3C` |

## Riserva

Non c'è un pin libero. Se in corso d'opera ne servisse uno, l'unica manovra possibile è
spostare **gancio** o **NSI** su un pin solo-input (34-39) aggiungendo una **resistenza di
pull-up esterna da 10 kΩ** verso 3V3. È l'unica saldatura di riserva prevista dal progetto.

## Collegamenti dei moduli

### Cornetta — tre conduttori, e perché bastano

Il cordone dell'S62 ha **tre fili**: rosso, bianco, blu. Sono tutti presenti sui morsetti
del microfono, e solo due (rosso e blu) proseguono verso la capsula d'ascolto. È il
cablaggio classico: **una massa comune, un segnale mic, un segnale ascolto**.

Tre conduttori significa che nella cornetta viaggiano **solo segnali analogici**. Un
microfono digitale I2S ne richiederebbe cinque: per questo il progetto usa un codec nella
base e una capsula electret nella cornetta, invece di un microfono I2S.

| Filo | Va a | Nota |
|---|---|---|
| **Massa comune** | `AGND` del WM8960 | condivisa tra mic e capsula d'ascolto |
| **Segnale microfono** | `LINPUT1` del WM8960 | bias fornito da `MICBIAS` del codec |
| **Segnale ascolto** | `HP_L` del WM8960 | **uscita cuffia**, non speaker (vedi sotto) |

⚠️ **Va usata l'uscita cuffia, non quella speaker.** L'uscita speaker del WM8960 è a ponte
(BTL): entrambi i terminali sono pilotati, nessuno dei due è a massa, quindi servirebbero
due fili dedicati. Con tre conduttori totali non è utilizzabile. L'uscita cuffia è
single-ended e condivide la massa: 40 mW su 16 Ω sono enormemente più di quanto serva a
una capsula da orecchio.

> **Identifica i fili col multimetro prima di collegare**: misura la resistenza tra le
> coppie. La coppia che dà qualche decina o centinaia di ohm è la capsula d'ascolto.
> Non fidarti dei colori: dopo cinquant'anni le convenzioni cambiavano da lotto a lotto.

### Codec WM8960 (Waveshare 15019, nella base)

| Pin modulo | Va a |
|---|---|
| VCC | 3V3 |
| GND | GND |
| BCLK | GPIO 26 |
| LRCLK / DACLRC | GPIO 25 |
| ADCDAT | GPIO 33 |
| DACDAT | GPIO 22 |
| SDA / SCL | GPIO 21 / 19 (in parallelo all'OLED) |

Il modulo ha anche microfoni MEMS a bordo e un jack cuffia da 3,5 mm: **non si usano**.
L'ingresso attivo va instradato via I2C su `LINPUT1`.

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
OLED: VCC 3V3, GND, SDA 21, SCL 19 — indirizzo I2C `0x3C`, **stesso bus del codec**.
WS2812: VCC 5V, GND, DIN 27.
