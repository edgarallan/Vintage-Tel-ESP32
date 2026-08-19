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
| 3 | **Waveshare WM8960 Audio Board** (cod. 15019) | 1 | 16 | Codec stereo: ADC **e** DAC, ingresso mic con bias per electret, uscita cuffia. **Header goldpin già saldati** a 2,54 mm |
| 4 | Capsula **microfonica electret** Ø ~10 mm, 2 fili | 1 | 2 | Sostituisce la capsula a carbone **dentro il suo guscio originale** |

### Perché un codec e non mic + ampli separati

Il cordone della cornetta dell'S62 ha **tre conduttori** (rosso, bianco, blu): una massa
comune, un filo per la capsula d'ascolto, uno per il microfono. È il cablaggio classico
dei telefoni a cornetta.

Tre fili significa che dentro la cornetta possono viaggiare **solo segnali analogici**.
Un microfono digitale I2S come l'INMP441 ne richiederebbe cinque (3V3, GND, SCK, WS, SD)
ed è quindi impossibile senza rifare il cordone — cosa che su un cordone spiralato
d'epoca vuol dire distruggerlo.

Il WM8960 risolve il vincolo e semplifica: **un solo modulo al posto di due**, perché
integra sia l'ADC per il microfono sia il driver per la capsula d'ascolto. In più porta
un **ALC** (controllo automatico di livello) sull'ingresso mic, che su una cornetta vale
parecchio: compensa da solo quanto vicino alla bocca la tieni.

**Non serve un pin in più**: usa gli stessi 4 pin I2S e condivide il bus I2C con il
display, avendo indirizzi diversi (codec `0x1A`, OLED `0x3C`).

> La capsula d'ascolto va sull'**uscita cuffia**, che è single-ended e può quindi
> condividere la massa col microfono. L'uscita speaker del WM8960 è a ponte (BTL) e
> richiederebbe due fili dedicati: con tre conduttori totali non è utilizzabile.

> **Alternative se il Waveshare non si trova**: il modulo **ES8388 di PCB Artists**
> (~15 €, in stock) è equivalente come funzioni, ma **spedisce senza header** — vanno
> richiesti pre-saldati alla conferma d'ordine. Il breakout **SparkFun WM8960** è stato
> **ritirato** e non è più acquistabile.

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
| **Minimo** (BT + disco + gancio + audio, alimentazione da rete senza tampone) | ~75 € |
| **Medio** (+ campanello + WS2812) | ~90 € |
| **Completo** (+ display + tampone PowerBoost + cella) | ~130 € |

Escluse spedizioni e il telefono stesso (sui mercatini italiani 20-50 €).

## Strumenti

- **Cacciavite piccolo a taglio** — per i morsetti a vite: è lo strumento principale
- Multimetro
- Saldatore **solo se** il cordone della cornetta va rifatto
- Cacciaviti specifici Siemens (testa cilindrica) per aprire l'S62
- Oscilloscopio (opzionale, utile per il debug del disco)

## Fornitori

- **Botland / Kamami** — Waveshare WM8960 Audio Board (spediscono in Italia)
- **Adafruit / Mouser / RS** — PowerBoost 1000C, DRV8871
- **AliExpress** — DevKitC, basetta a morsetti, WS2812, XL6009, OLED, capsula electret
- **Subito.it / eBay.it** — telefoni SIP vintage italiani


---

# Lista d'acquisto — due ordini, seguendo le fasi

Comprare tutto insieme costava **183 €**. Comprare *quando serve* costa **~85 €**, e non
rallenta di un giorno: il piano di montaggio e' gia' a tappe, e il campanello e il display
servono settimane dopo il primo bring-up.

Il ragionamento che ha fatto la differenza non riguarda i multipack — quelli sono la parte
sana della lista: XL6009 e jack vengono **2,00 € l'uno**, L298N e WS2812 2,40, interruttore
0,48. Comprarli singoli da un distributore costerebbe di piu', perche' a quei prezzi la
spedizione vale piu' della merce. L'errore era **pagare il prezzo Amazon per pezzi che
useremo fra tre settimane**.

## Ordine A — subito, Amazon.it (66,95 €)

Tutto cio' che serve per far parlare il telefono.

| Articolo | Prezzo | Link |
|---|---|---|
| **WM8960 Audio Board** — il codec | 20,99 € | [B0CNNFWLNV](https://www.amazon.it/dp/B0CNNFWLNV) |
| **ESP32 WROOM 38 pin ×2** — con scorta | 15,99 € | [B0GJTNCWMZ](https://www.amazon.it/dp/B0GJTNCWMZ) |
| **Cavetti Dupont 120 pz** | 11,99 € | [B0H86XQTKW](https://www.amazon.it/dp/B0H86XQTKW) |
| **Jack 3,5 mm 4 poli a morsetti ×5** — la cornetta senza stagno | 9,99 € | [B0C6M851WD](https://www.amazon.it/dp/B0C6M851WD) |
| **Capsule electret 9,5 mm ×2** | 8,99 € | [B08BX48RMR](https://www.amazon.it/dp/B08BX48RMR) |

### Il codec deve essere la scheda nuda, non l'HAT per Raspberry

Su Amazon esistono molte schede WM8960 che arrivano in **1-2 giorni** invece che a fine
mese, ma sono tutte **HAT per Raspberry Pi**, e non vanno bene. La differenza sta in una
riga della descrizione:

| Scheda | Jack da 3,5 mm |
|---|---|
| **Audio Board** (quella giusta) | *«4-segment earphone jack, allows sound **recording** via external earphones **with Mic**»* |
| **Audio HAT** (piu' veloce) | *«3.5mm earphone jack, **play music** via external earphone»* |

L'HAT ha il jack in sola **uscita**. Senza ingresso microfonico sul jack, il microfono
della cornetta non ha piu' un punto d'ingresso: i microfoni MEMS dell'HAT stanno nella
base, non nella cornetta, e i pin `LINPUT` del codec non sono portati fuori. L'unica via
sarebbe saldare sui pad SMD dei MEMS di bordo — delicato, e l'opposto del criterio di
questa BOM.

> **La consegna lenta del codec non e' il collo di bottiglia.** L'Ordine B da AliExpress
> impiega comunque 2-4 settimane, e la sequenza di bring-up mette l'audio per ultimo:
> NVS, LED, display, gancio, disco e campanello vengono prima.

## Ordine B — AliExpress, in parallelo (16,46 €)

Arriva in 2-4 settimane, cioe' quando serve davvero. Prezzi reali verificati sul carrello.

| Articolo | Variante | Prezzo | Serve per |
|---|---|---|---|
| **L298N** (versione grande) | Rosso | 2,54 € | campanello — ponte H |
| **XL6009** boost regolabile | 1 pz, 5-32 V → 5-50 V | 1,90 € | campanello — genera i ~24 V |
| **Modulo UPS 18650** | **5V module** | 1,94 € | tampone anti-blackout |
| **Condensatori elettrolitici** | **50V 100UF, 20 pz** | 1,75 € | campanello — picchi degli squilli |
| **OLED 0,96" I2C SSD1306** | bianco, 1 pz | 2,54 € | display |
| **Modulo breakout WS2812** | 1 pz | 0,90 € | LED di stato |
| **Interruttore KCD11 10×15 mm** | rosso, 10 pz | 1,69 € | on/off sul fondo |
| **Distanziali nylon** | **M2.5, 10 mm, nero, 30 set** | 3,20 € | montaggio schede |

### ⚠️ I dazi di importazione cambiano i conti — leggere prima di ordinare

Su un ordine di **16,46 €** AliExpress addebita **29,35 € di oneri di importazione**:
un sovrapprezzo del **178%**, che porta il totale a **45,81 €**.

Il meccanismo va capito perche' e' controintuitivo:

> Per gli ordini da fuori UE sotto i 150 €, AliExpress applica **3 € di dazio forfettario
> per ogni *tipo* di oggetto diverso**, piu' IVA. Piu' unita' dello stesso oggetto contano
> come un tipo solo.

Quindi il dazio **non dipende da quanto compri, ma da quante voci diverse ci sono**.
Con IVA al 22% ogni voce costa **~3,67 € fissi**, qualunque sia il suo prezzo. Un modulo
WS2812 da 0,90 € ne costa in realta' **4,57**.

**Conseguenza pratica: su AliExpress non conviene mai aggiungere un articolo economico
singolo.** Conviene invece aumentare le quantita' di voci gia' presenti, perche' le unita'
aggiuntive non pagano dazio.

#### Confronto reale, dazi inclusi

| Articolo | AliExpress + dazio | Amazon | Migliore |
|---|---|---|---|
| L298N | 6,21 € | 11,99 € | AliExpress |
| OLED 0,96" | 6,21 € | 13,99 € | AliExpress |
| XL6009 | 5,57 € | 9,99 € | AliExpress |
| Modulo UPS | 5,61 € | 10,72 € | AliExpress |
| Condensatori | 5,42 € | 12,99 € | AliExpress |
| WS2812 | 4,57 € | 11,99 € | AliExpress |
| Distanziali M2.5 | 6,87 € | ~10 € | AliExpress |
| Interruttore KCD11 | 5,36 € | **4,79 €** | **Amazon** |
| **Totale** | **45,81 €** | **~86 €** | |

**Anche con i dazi l'ordine cinese resta conveniente**, ma il margine si dimezza: da ~70 €
di risparmio teorico a ~40 € reali.

#### Variante piu' economica: comprare in Italia cio' che si trova in Italia

Tre voci sono banali da procurare in qualunque negozio di elettronica e pagano dazio come
le altre: **condensatori** (5,42 €, in negozio ~0,60), **interruttore** (5,36 €, ~1 €) e
**distanziali** (6,87 €, ~3 €). Toglierle dall'ordine fa scendere il totale a **28,17 €**
per le cinque voci che dalla Cina convengono davvero — L298N, XL6009, UPS, OLED, WS2812 —
contro ~5 € di spesa locale.

**Totale: ~33 € invece di 45,81**, al costo di un giro in negozio.

### Tre trappole trovate comprando, che valgono per chiunque rifaccia questo ordine

1. **Il L298N esiste in due formati.** La versione **Mini** costa meno ma regge solo ~12 V:
   sul campanello a 24-30 V si distrugge. Serve quella grande, riconoscibile a vista dal
   **dissipatore nero** e dai **morsetti a vite blu**. Le specifiche dei venditori sono
   quasi sempre vuote: va identificata dalla foto.
2. **Il modulo UPS ha il default a 12 V.** Va selezionata a mano la variante **5V module**,
   altrimenti arrivano 12 V sull'ESP32.
3. **I kit assortiti costano piu' dei pezzi mirati.** Un kit condensatori da 391 pezzi
   costava **21,59 €**; la variante mirata `50V 100UF` ne costa **1,75**. Stessa cosa per i
   distanziali: il set da 850 pezzi costava **19,09 €**, quello da 30 set **3,20**.

> **Occhio alla spedizione.** Molti articoli sono **Choice con spedizione gratuita**: basta
> un solo venditore fuori da quel circuito per far comparire ~3,87 € di trasporto, che
> annullano qualunque risparmio. Verifica che tutti gli articoli stiano nel gruppo
> "Spedito da AliExpress" prima di pagare.

## Non serve comprare

- **Alimentatore USB 5 V** — gia' disponibile
- **Basetta a morsetti per il DevKitC** (13,99 €) — si salta saldando a mano. Si perde il
  "zero saldature" sui contatti del telefono, ma **il jack a 4 poli resta**, quindi la
  cornetta — la parte davvero scomoda — continua a non richiedere stagno
- **WAGO 221** — sostituiti dalle saldature

## Da procurare altrove — la cella 18650

**Amazon.it non vende celle 18650 nude**: sono merci pericolose e le tratta solo come
pacchi gia' cablati con connettore JST. Ma il modulo UPS **include il portacelle** e vuole
quindi una **cella nuda**: un pacco con reofori saldati non entra nell'alloggiamento.

Va comprata da un rivenditore specializzato — **18650.it** o equivalente — dove peraltro
le capacita' sono reali invece che dichiarate. Cerca una **18650 protetta, 3000-3500 mAh,
testa piatta**, ~8-10 €.

> **Non e' bloccante.** L'alimentazione primaria e' la rete: senza cella il telefono
> funziona comunque, perde solo la sopravvivenza ai blackout.

## Riepilogo

| | Costo |
|---|---|
| Ordine A (Amazon, subito) | 66,95 € |
| Ordine B (AliExpress, in parallelo) | 16,46 € |
| Cella 18650 (specializzato, quando serve) | ~9 € |
| **Totale** | **~92 €** |

Contro i **183 €** del carrello unico iniziale.
