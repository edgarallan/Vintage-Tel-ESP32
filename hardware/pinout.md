# Mappa GPIO — ESP32-WROOM-32E

**Fonte di verità per l'assegnazione dei pin.** Se un modulo del firmware usa un pin
diverso da questa tabella, è un bug — oppure va aggiornata questa tabella nello stesso commit.

## Pin inutilizzabili, e perché

Prima della mappa serve capire cosa **non** si può usare, perché è ciò che rende
l'assegnazione obbligata invece che arbitraria.

| GPIO | Perché è escluso |
|---|---|
| 6, 7, 8, 9, 10, 11 | Collegati alla flash SPI interna. Usarli manda in crash il chip |
| 1, 3 | UART0 TX/RX: console seriale e flashing |
| 0, 2, 5, 12, 15 | **Strapping pin**: il loro livello all'accensione decide modalità di boot e tensione della flash. Se un contatto esterno li tiene bassi o alti al momento sbagliato, **il telefono non si avvia** |
| 34, 35, 36, 39 | **Solo input e senza pull-up interno.** Inutilizzabili per i contatti puliti di disco e gancio senza resistenze esterne saldate |

Restano **15 pin** utilizzabili: 4, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33.
Servono **13 segnali**. Margine: **due pin**.

> **I GPIO 16 e 17 sono liberi perche' il modulo e' un WROOM.** Sui WROVER li
> occupa la PSRAM, e la prima stesura di questo documento li dava per persi. Il
> modulo effettivamente acquistato e verificato il 26/08/2026 e' un
> **ESP32-D0WD-V3 in package WROOM-32E**, senza PSRAM e con 4 MB di flash
> (`esptool flash-id`: `PKG_VERSION=1`, feature `Wi-Fi, BT`, nessuna PSRAM).
> Il progetto ci guadagna: vedi la sezione Riserva.

## Assegnazione

| Funzione | GPIO | Direzione | Peripheral | Note |
|---|---|---|---|---|
| Disco — impulsi | **4** | IN, pull-up | GPIO + ISR | Antirimbalzo software: assestamento 3 ms nel HAL + finestra cieca 8 ms in `core/`. **Niente PCNT**, vedi sotto |
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

## L'antirimbalzo, misurato invece che stimato

La prima stesura di questo documento sosteneva che il debounce fosse risolto in hardware
dal filtro anti-glitch del peripheral PCNT. **È falso**, e il 26/08/2026 è costato una
serata di bring-up: il disco componeva cifre a caso.

### Perché il filtro hardware non poteva bastare

Da `components/esp_driver_pcnt/src/pulse_cnt.c`:

```c
glitch_filter_thres = esp_clk_apb_freq() / 1000000 * config->max_glitch_ns / 1000;
ESP_RETURN_ON_FALSE(glitch_filter_thres <= PCNT_LL_MAX_GLITCH_WIDTH, ...)
```

con `PCNT_LL_MAX_GLITCH_WIDTH` = **1023** (`hal/esp32/include/hal/pcnt_ll.h`). Ad APB
80 MHz il massimo filtrabile è **1023 / 80 MHz ≈ 12,8 µs**.

### Le misure sull'apparecchio reale

Registrando ogni fronte con marca temporale al microsecondo (`firmware/main/diag_dial.c`):

| Grandezza | Misura |
|---|---|
| Durata di un impulso vero | **61 ms** (a norma per un disco a 10 imp/s) |
| Distanza fra impulsi veri | **~100 ms** |
| Durata della raffica di rimbalzo | **1,3 ms**, 15 fronti spuri |
| Fronte di rimbalzo più distante | **797 µs** |

I fronti di rimbalzo misurati distano 19, 24, 35, 57, 61, 74, 77 µs e uno addirittura 797:
il filtro da 12,8 µs ne avrebbe eliminati **due su quindici**.

### La soluzione adottata, su due livelli

**Il PCNT è stato rimosso.** Contava anche i rimbalzi, e servendo comunque la marca
temporale di ogni fronte per filtrarli, il contatore hardware non aggiungeva nulla.

1. **Assestamento di 3 ms in `phone_hal/hal_input.c`.** Ogni fronte fa ripartire un'attesa;
   solo quando la linea è ferma da 3 ms il task legge il livello vero. Una raffica di
   quindici rimbalzi diventa un evento solo. Il valore sta al doppio del rimbalzo misurato
   e a un decimo del tempo di chiusura del contatto, quindi non può mascherare un impulso.
2. **Finestra cieca di 8 ms in `core/dial_decode.c`** (`min_pulse_gap_ms`), come seconda
   linea di difesa e perché è logica pura, testabile sul Mac. Sta sei volte sopra il
   rimbalzo e dodici volte sotto la distanza fra impulsi veri.

### ⚠️ L'ISR non deve leggere il livello del pin

È l'errore che ha causato il guasto, e va evitato in qualunque driver futuro di questo
progetto. La prima stesura leggeva `gpio_get_level()` **dentro** l'interruzione. Nei dati
comparivano fronti consecutivi con lo **stesso livello** — otto `NSI 1` di fila — cosa
impossibile per fronti veri: nei microsecondi fra l'interruzione e la lettura la linea era
già rimbalzata di nuovo.

Il driver deduceva «disco in rotazione» da quel valore, perdeva i rilasci dell'NSI e
accumulava gli impulsi di più cifre in una sola. **Il livello si legge nel task, dopo
l'assestamento.** L'ISR segnala soltanto *dove* e *quando*.

## Morsettiera del disco combinatore

Dallo schema originale AUSO Siemens ([variante a spina](../assets/retrofit/s62_schema.jpg),
[variante a borchia](../assets/retrofit/s62_schema_borchia.jpg) — concordi su questo punto):

| Morsetto | Filo | Contatto | A riposo |
|---|---|---|---|
| 1 | **bi** (bianco) | impulsi (`cid`) | **chiuso** |
| 2 | **rs** (rosso) | impulsi (`cid`) | **chiuso** |
| 3 | **bl** (blu) | NSI / fuori-normale | **aperto** |
| 4 | **ma** (marrone) | NSI / fuori-normale | **aperto** |

Che 1-2 sia il contatto degli impulsi lo dice la nota in calce allo schema:

> *«Se manca il disco combinatore ponticellare 1 con 2»*

Sostituire il disco con un ponticello fisso ha senso solo se a riposo quel contatto è già
un ponticello — cioè se è normalmente chiuso, come dev'essere un contatto che genera
impulsi interrompendo un circuito.

**Verifica col multimetro prima di fidarti dei colori.** Sui telefoni italiani di
quell'epoca le convenzioni cromatiche cambiavano da lotto a lotto e da riparazione a
riparazione. Con i quattro fili svitati dalla morsettiera, in continuità:

1. Misura tutte e sei le combinazioni **a riposo**: una sola coppia legge zero, ed è quella
   degli impulsi. Gli altri due fili sono l'NSI per esclusione.
2. Conferma l'NSI **tenendo il disco fermo a fine corsa**: deve chiudersi e restare chiuso
   per tutta la rotazione. È una misura statica, non serve inseguire fronti veloci.

> **Se leggi tutto aperto, non è guasto: è ossido.** Su questo apparecchio le sei coppie
> risultavano inizialmente tutte aperte. I contatti dei dischi sono **autopulenti per
> progetto** — strisciano a ogni rotazione — e sono bastate cinque o sei composizioni per
> farli tornare a condurre. Se durante il bring-up mancano impulsi, il primo sospetto è di
> nuovo l'ossido; se invece ne arrivano troppi, è il rimbalzo.

### Collegamento all'ESP32

I quattro fili vanno **scollegati dalla morsettiera** e portati diretti al chip. Lasciandoli
collegati, il contatto resterebbe in parallelo alle bobine da 47 e 29, ai condensatori da
2,2 µF e 1 µF e alla rete RC: il pull-up interno non riuscirebbe a portare il pin a un
livello netto. Non è un espediente temporaneo — quella rete analogica va rimossa comunque,
vedi `docs/07_retrofit_layout.md`.

La polarità non conta, sono contatti puliti:

```
bianco  → GPIO 4     rosso    → GND
blu     → GPIO 32    marrone  → GND
```

## Riserva

Restano **due pin liberi: GPIO 16 e 17**, entrambi bidirezionali e con pull-up interno,
quindi utilizzabili senza alcun componente aggiuntivo.

Questo **cancella l'unica saldatura di riserva prevista dal progetto**. La stesura
precedente, che assumeva un WROVER, non aveva pin liberi e prevedeva come ripiego di
spostare **gancio** o **NSI** su un pin solo-input (34-39) con una **resistenza di pull-up
esterna da 10 kΩ** verso 3V3. Con il WROOM quel ripiego non serve piu'.

## Collegamenti dei moduli

### Gancio — misure del commutatore

Coppia di lamelle **più a sinistra** della pila dietro il commutatore
(`assets/retrofit/s62_gancio_retro.jpeg`), collegata diretta a **GPIO 18 + GND**: nessuna
via parallela, non serve isolarla.

| | |
|---|---|
| cornetta **appoggiata** | contatto aperto → pull-up → livello **1** |
| cornetta **sollevata** | contatto chiuso → livello **0** |

Verificato il 04/09/2026 su cinque cicli: il livello a riposo con la cornetta giù è `1`, e
i cicli alternano `0,1,0,1…` chiudendo su `1`. Il verso non è deducibile a priori e va
misurato: invertirlo produce un telefono che risponde quando riagganci e riaggancia quando
rispondi — un guasto simmetrico che sembra un problema di Bluetooth e non lo è.

**Il contatto produce due fenomeni distinti**, ed è facile confonderli:

| | |
|---|---|
| rimbalzo vero | 3-10 fronti in **meno di 1 ms** |
| chiacchiera di corsa | fino a 6 fronti su **247 ms**, intervalli di 20, 19, **140**, 21, 47 ms |

Il secondo è quello che determina l'antirimbalzo — ma il numero che conta **non è la durata
totale**, bensì l'**intervallo massimo fra due fronti**: l'attesa in `hal_input.c` è
ritriggerabile e ogni fronte fa ripartire il conto. Con 140 ms di intervallo peggiore, i
150 ms stimati inizialmente stavano appena sopra la soglia, e una cornetta sollevata un filo
più adagio spezzava la raffica in due transizioni. **L'assestamento è quindi 400 ms**:
molto sopra i 140 ms di chiacchiera e molto sotto il tempo minimo fra due gesti umani
distinti, che non potrebbero mai essere fusi in uno.

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

**Misure rilevate su questo apparecchio:**

| Coppia | Valore |
|---|---|
| bianco – rosso | **261 Ω** ← la maggiore |
| bianco – blu | 220 Ω |
| rosso – blu | 41 Ω |

La somma torna al singolo ohm (220 + 41 = 261), e questo dice due cose. La prima è che il
**blu è la massa comune**. La seconda, meno ovvia: nella cornetta **non c'è nessun
componente in parallelo** alle capsule — nessun varistore, nessun condensatore, come si
trova spesso sui telefoni d'epoca. Se ci fosse, aggiungerebbe un percorso e la somma non
tornerebbe esatta.

I 220 Ω sul microfono sono alti per una capsula a carbone: è il sintomo dei granelli
**compattati e ossidati** dopo cinquant'anni. La capsula funziona ancora, ma suona
impastata — motivo in più per sostituirla con un electret.

**Conferma del verso, 04/09/2026.** La somma degli ohm dimostra che il blu è il comune,
ma non dice quale fra bianco e rosso sia il microfono: per quello serve una prova
dinamica. Misurando la coppia **bianco – blu** e parlando nella cornetta, **la lettura
sale** — è la modulazione dei granelli di carbone sotto la pressione sonora. Solo un
microfono fa variare la propria resistenza col suono; una bobina d'ascolto resta immobile.

La tabella dei fili qui sopra è quindi **misurata, non dedotta**, ed è la mappa da seguire
per cablare il codec.

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
