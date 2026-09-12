# 07 — Mappa di conversione della cassetta

Da consultare **dopo** l'apertura del telefono (Step 1–2 di
[`04_installation.md`](04_installation.md)) e **prima** di rimuovere qualcosa:
cosa togliere, cosa tenere e dove sistemare i componenti nuovi.

![Cassetta S62 annotata](../assets/retrofit/cassetta_annotata.png)

Vista più chiara (stesso modello, angolazione diversa) con gli stessi numeri:

![Sequenza annotata](../assets/retrofit/sequenza_annotata.png)

> 🟩 verde = tenere · 🟥 rosso = rimuovere · 🟦 blu = nuovo.
> Le posizioni dei marker sono **indicative**: verifica sempre sul tuo esemplare.

Versione **schematica** pulita (stessa numerazione 1–10), comoda come riferimento
rapido: [`../assets/diagrams/09_conversion_map.svg`](../assets/diagrams/09_conversion_map.svg).

## In breve

| Azione | Componenti | Note |
|--------|-----------|------|
| **Tieni** | Disco combinatore, commutatore a gancio, campanello (campane + bobina), morsettiera, altoparlante cornetta | Da ricablare verso l'ESP32 (vedi [`03_wiring.md`](03_wiring.md)) |
| **Rimuovi** | Bobina d'induzione (trasformatore), condensatore + rete analogica (resistori/varistore) | Era il circuito fonia analogico, ora sostituito da ESP32 + I2S |
| **Aggiungi** | ESP32-DevKitC-VE su basetta a morsetti, codec WM8960, boost + H-bridge campanello, capsula electret (in cornetta) | La basetta a morsetti va nello spazio centrale liberato |

## Mappatura completa

Tabelle dettagliate (terminali dello schema S62, valori, GPIO) e identificazione
dei contatti col multimetro: vedi [`../hardware/retrofit_layout.md`](../hardware/retrofit_layout.md).

| # | Tieni / Rimuovi / Aggiungi | Componente | GPIO |
|---|----------------------------|-----------|------|
| 1 | Tieni | Commutatore a gancio | GPIO 18 |
| 6 | Tieni | Disco combinatore (impulsi + NSI) | GPIO 4 / GPIO 32 |
| 5 | Tieni | Campanello (bobina ≈ 1700 Ω) | GPIO 13 / GPIO 14 |
| 4 | Tieni | Morsettiera (nodo di cablaggio) | — |
| 2 | Rimuovi | Bobina d'induzione / trasformatore | — |
| 3 | Rimuovi | Condensatore + rete analogica | — |
| 7 | Aggiungi | ESP32 su basetta a morsetti (zona centrale) | — |
| 8 | Aggiungi | Codec WM8960 (nella base) | I2S + I2C |
| 9 | Aggiungi | Boost + H-bridge (vicino campanello) | — |
| 10 | Aggiungi | Capsula electret (nella cornetta) | analogico — 3 fili sul cordone |

## Procedura consigliata (l'ordine conta)

Idea di fondo: **prima svuoti** la fascia centrale (parti analogiche), **poi
popoli** lo spazio con l'ESP32, cablando e collaudando **un sottosistema alla volta**.
I numeri rimandano ai marker dell'immagine; gli Step a [`04_installation.md`](04_installation.md).

### Fase A — Smontaggio (sicuro, reversibile)

0. **Fotografa ed etichetta** tutti i fili prima di staccare (04 · Step 1–2).
1. Assicurati che il telefono **non sia collegato alla linea**; nessuna tensione presente.
2. Rimuovi **③ condensatore + rete analogica** (resistori/varistore): annota i fili, poi dissalda/scollega.
3. Rimuovi **② bobina d'induzione (trasformatore)**.
4. Scollega il **⑤ campanello dalla linea** (lascia bobina e campane in sede): i due capi della bobina andranno all'H-bridge.
5. **Isola i contatti** di **① gancio** e **⑥ disco** che userai; scollega il resto dal circuito originale.

➡️ Risultato: nello chassis restano solo **disco, gancio, campanello, morsettiera**; la fascia centrale è libera.

### Fase B — Montaggio (un blocco alla volta, con collaudo)

| Ordine | Monta | Cabla | Verifica |
|--------|-------|-------|----------|
| 1 | **⑦ ESP32 su basetta a morsetti** nello spazio centrale | alimentazione 5V | `idf.py monitor` mostra il boot |
| 2 | segnali a bassa tensione | ① gancio→GPIO18, ⑥ impulsi→GPIO4, NSI→GPIO32 — **direttamente sui morsetti**, senza optoaccoppiatori né rete RC | bring-up: gancio a log e ogni cifra 0-9 corretta (10 impulsi → `0`) |
| 3 | **⑩ capsula electret** in cornetta + **⑧ codec WM8960** nella base | 3 fili del cordone: massa, mic, ascolto | test audio loopback: si parla e ci si sente |
| 4 | **⑨ boost + H-bridge** | bobina **⑤ campanello** | test campanello (3 squilli, 1 s on / 4 s off) |
| 5 | alimentazione (rete + tampone) + display/WS2812 | — | LED stato, OLED |
| 6 | — | — | **bring-up completa**, tutti i sottosistemi in sequenza |
| 7 | chiusura | fascette; verifica che i cavi non tocchino il martelletto | il disco gira libero |

> Collauda **prima** di richiudere il coperchio (04 · Step 10): rilavorare a cassetta aperta costa molto meno.

## La rete analogica prima della rimozione

Fotografata il 12/09/2026, **prima** di smontare qualsiasi cosa: è documentazione che dopo
non si può più rifare.

| Foto | Cosa mostra |
|---|---|
| `s62_rete_analogica_completa.jpeg` | la basetta intera con tutti i componenti in sede |
| `s62_rete_analogica_dallalto.jpeg` | la stessa dall'alto, con le bobine del campanello sopra |
| `s62_rete_analogica_di_lato.jpeg` | i condensatori e i morsetti da un'altra angolazione |
| `s62_morsettiera_dettaglio.jpeg` | le viti dei morsetti e la resistenza, da vicino |
| `s62_telaio_sotto.jpeg`, `s62_telaio_sigle.jpeg`, `s62_marchio_auso_siemens.jpeg` | il telaio: `AUSO SIEMENS Telecomunicazioni`, `7002`, `Ed. IX`, `PROPRIETÀ SIP` |

### Cosa c'è sulla basetta

- due condensatori **AUSO**: `1 µF 200 V` (marcato `7002`) e `2 µF 125 V` (marcato `3912`)
- un condensatore assiale azzurro **`0,1 µF 100 V`**
- una **resistenza** con fasce colorate
- un cilindro avvolto in carta, la **bobina d'induzione**

Sono tutti lo stesso circuito fonia, che l'ESP32 sostituisce in blocco: si tolgono insieme.

### ⚠️ La basetta NON si rimuove

È **la morsettiera** — si vedono le viti dei morsetti lungo due bordi — ed è rivettata al
telaio. I componenti ci sono solo appoggiati sopra, con i reofori ai morsetti: liberandoli
la basetta resta in sede, e **non c'è nessun rivetto da trapanare**.

Tenerla non è solo prudenza da restauro: liberata dai componenti diventa il **nodo di
distribuzione** di cui il cablaggio nuovo ha bisogno per `3V3`, `GND`, `SDA` e `SCL` —
linee che vogliono più derivazioni ciascuna. Viti che accettano due o tre fili, già fissate
al telaio, e il cablaggio nuovo passa dallo stesso nodo di quello vecchio.

### Prima di togliere

1. **Fotografa** i collegamenti, se non l'hai già fatto: i colori dei fili non seguono
   nessuno standard su un apparecchio di cinquant'anni
2. **Scarica i condensatori** cortocircuitandone i terminali con un cacciavite
3. Svita i morsetti; se qualche reoforo è saldato, **taglialo a filo del corpo** invece di
   dissaldare — il calore su bachelite d'epoca stacca piste e sbriciola l'isolante
4. **Non buttare i componenti**: in una scatolina, con la foto

## Materiale di riferimento

- Schema originale Siemens S62, due varianti dello stesso apparecchio:
  [a spina](../assets/retrofit/s62_schema.jpg) (`Fg sm 54 S8049 g,h`) e
  [a borchia](../assets/retrofit/s62_schema_borchia.jpg) (`a,b`). Concordano sulla
  morsettiera del disco, trascritta in [`../hardware/pinout.md`](../hardware/pinout.md)
- Interno dell'apparecchio: [completo](../assets/retrofit/s62_interno_completo.jpg),
  [morsettiere](../assets/retrofit/s62_morsettiere.jpg),
  [dettaglio contatti](../assets/retrofit/s62_dettaglio_contatti.jpg)
- Foto cassetta smontata: [`../assets/retrofit/cassetta_smontata.jpg`](../assets/retrofit/cassetta_smontata.jpg)
- Mappa tecnica completa: [`../hardware/retrofit_layout.md`](../hardware/retrofit_layout.md)
