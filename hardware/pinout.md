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

| Filo | Ruolo | Va a |
|---|---|---|
| **blu** | massa comune | `AGND` del WM8960 |
| **rosso** | segnale ascolto | `HP_L` del WM8960 — **uscita cuffia**, non speaker (vedi sotto) |
| **bianco** | segnale microfono | `LINPUT1` del WM8960, bias da `MICBIAS` |

> ⚠️ Questi colori valgono **per l'esemplare di questo progetto**, verificati col
> multimetro. Sui telefoni italiani di quell'epoca le convenzioni cromatiche cambiavano
> da lotto a lotto e da riparazione a riparazione: sul tuo apparecchio **rifai le misure**
> con la procedura qui sotto invece di fidarti di questa tabella.

### Come identificare i tre fili col multimetro

I tre fili non sono tre circuiti separati: formano una **stella**. Un filo è la massa
comune, e dagli altri due partono microfono e capsula d'ascolto. Ne segue una relazione
che rende l'identificazione certa invece che a tentativi:

```
        ┌──── microfono ────┐
comune ─┤                   ├── i due segnali
        └──── ascolto ──────┘

R(mic ↔ ascolto)  =  R(comune ↔ mic)  +  R(comune ↔ ascolto)
```

**La misura più alta delle tre è sempre la somma delle altre due**, e il filo che non
compare in quella coppia è il comune.

1. **Preparazione.** Multimetro in ohm, portata 200 Ω o automatica, niente alimentazione
   collegata. Tocca le punte tra loro e annota l'offset dei puntali (0,2-0,5 Ω): va
   sottratto dalle misure basse.
2. **Misura dal lato base**, con la cornetta montata: così il cordone è incluso nella
   misura, ed è la parte che dopo cinquant'anni cede più spesso. Registra tutte e tre le
   coppie.
3. **Trova il comune**: è il filo escluso dalla coppia con la lettura maggiore.
4. **Distingui mic e ascolto** picchiettando la capsula del microfono mentre misuri:
   - **microfono a carbone** — la lettura *balla*, cambia premendo o girando la capsula
     (i granelli di carbone si spostano). Tipico 20-200 Ω, instabile.
   - **capsula d'ascolto** — la lettura è *immobile*, ed è una bobina. Tipico 50-600 Ω;
     alcune capsule magnetiche d'epoca arrivano a 1-2 kΩ.

   Conferma gratuita: toccando e staccando i puntali sulla coppia dell'ascolto si
   **sente un clic nella capsula**, mossa dalla corrente di prova del multimetro.

**Se due letture su tre danno `OL`**: il microfono a carbone è morto — succede spesso, i
granelli si compattano e si ossidano. Non è un problema, va sostituito comunque: la sola
coppia con valore finito è l'ascolto, e il terzo filo è quello del microfono.

**Controllo da fare comunque**: con i puntali su una coppia che legge, **piega e torci il
cordone spiralato** vicino ai due imbocchi. Se la lettura sfarfalla o va a `OL`, un
conduttore si sta spezzando — è il punto in cui questi cordoni cedono sempre, e conviene
scoprirlo prima di richiudere la cassetta.

⚠️ **Va usata l'uscita cuffia, non quella speaker.** L'uscita speaker del WM8960 è a ponte
(BTL): entrambi i terminali sono pilotati, nessuno dei due è a massa, quindi servirebbero
due fili dedicati. Con tre conduttori totali non è utilizzabile. L'uscita cuffia è
single-ended e condivide la massa: 40 mW su 16 Ω sono enormemente più di quanto serva a
una capsula da orecchio.

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
