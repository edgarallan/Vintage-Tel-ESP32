# Driver Campanello — Riportare in vita le campane

Il campanello del SIP è il dettaglio che fa la differenza. È un meccanismo elettromagnetico originariamente alimentato dalla centrale telefonica con corrente alternata a ~25Hz e ~75V RMS. Per ragioni di sicurezza ed efficienza, generiamo qualcosa di simile ma a tensione ridotta — il funzionamento è perfetto a 20-30V AC.

## Come funziona il campanello originale

Due bobine in serie attorno a un nucleo ferromagnetico. Quando ci passa corrente alternata, il martelletto (un pezzo di ferro magnetizzato) viene spinto alternativamente verso una campana, poi verso l'altra. Risultato: il classico "DRIN-DRIN".

```
        ┌──────────┐         ┌──────────┐
        │  Campana │         │  Campana │
        │ sinistra │         │  destra  │
        └──────────┘         └──────────┘
              ║                   ║
              ╠═══[Martelletto]═══╣
              ║                   ║
        ┌─────╨─────┐       ┌─────╨─────┐
        │  Bobina   │       │  Bobina   │
        │  sinistra │═══════│  destra   │
        └───────────┘       └───────────┘
              │                   │
              └────── 2 fili ─────┘
                     ║   ║
                    AC ~24V @ 22Hz
```

## Opzione A — ponte H L298N + boost (è quella montata)

> **Nota storica.** I primi documenti di questo progetto indicavano il DRV8871, che
> resta la scelta più elegante: regge 45 V, ha morsetti a vite e la protezione è tutta
> interna. In pratica l'ordine ha portato un **L298N**, che funziona altrettanto bene ma
> ha due trappole capaci di bruciarlo al primo collegamento. Sono documentate qui sotto
> perché non sono deducibili guardando la scheda.

### Schema

```
                                          ┌──────────────────────────┐
                                          │      L298N (modulo)      │
  +5V ──┬──────────────┐                  │                          │   ┌──────────┐
        │      ┌───────┴──────┐           │ 12V/VMS ◄── +27V (boost) │   │  Bobine  │
        │      │    XL6009    │─ +27V ───►│ GND     ◄── GND comune   │   │ campan.  │
        │      │  boost 5V→   │           │ 5V      ◄── +5V  (!)     │   │ ≈1700 Ω  │
        │      │   24-30V     │           │ IN1     ◄── GPIO 13      │   │          │
        │      └──────────────┘           │ IN2     ◄── GPIO 14      │   │          │
        │                                 │ ENA     ── ponticello 5V │   │          │
        └─────────────────────────────────┤ OUT1 ───[morsetto]───────┼──►│          │
                                          │ OUT2 ───[morsetto]───────┼──►│          │
                                          └──────────────────────────┘   └──────────┘
```

### ⚠️ Le due trappole dell'L298N

**1. Il ponticello del regolatore 5 V va TOLTO.**

Il modulo ha a bordo un regolatore 78M05 che ricava i 5 V della logica dalla tensione dei
motori. Un ponticello lo abilita, e di fabbrica è **inserito**. Quel regolatore accetta al
massimo **12 V in ingresso**: alimentandolo coi 27 V del boost si distrugge, e prima di
morire può mandare tensione fuori specifica sulla linea a 5 V — cioè verso l'ESP32.

Tolto il ponticello, il pin `5V` del modulo **diventa un ingresso**: va alimentato dai
nostri 5 V, altrimenti la logica del ponte resta morta e non succede niente.

**2. Il ponticello ENA va LASCIATO.**

`ENA` abilita il canale. Il modulo lo tiene alto con un ponticello verso i 5 V, e va bene
così: nella mappa GPIO non avanza un pin da dedicargli, e non serve — il silenzio si
ottiene già portando IN1 e IN2 entrambi bassi.

### Funzionamento

Il software alterna **IN1/IN2** alla frequenza di squillo (~22 Hz):

- `IN1=1, IN2=0` → corrente nella bobina in un senso
- `IN1=0, IN2=1` → corrente nel senso opposto
- `IN1=IN2=0` → **frenata**: entrambi i lati bassi, bobina cortocircuitata, silenzio

Alternando si ottiene l'onda quadra che fa oscillare il martelletto.

Quel terzo caso merita una precisazione, perché con il DRV8871 sarebbe stato diverso. Con
`ENA` tenuto alto l'L298N non va mai in alta impedenza: IN1=IN2=0 accende i due lati bassi
e **cortocircuita la bobina** invece di lasciarla libera. Non è un problema, anzi — la
corrente residua si smorza subito e il martelletto si ferma netto invece di vibrare per
inerzia. Ma è una frenata, non un *coast*, e chi legge il datasheet aspettandosi
l'alta impedenza non la troverà.

> Il boost resta sempre alimentato: il silenzio si ottiene dai due GPIO, non spegnendo
> l'XL6009. Un GPIO in meno e un cablaggio più semplice, al prezzo di pochi mA a riposo.

### Regolazione del boost

L'L298N è un ponte a transistor bipolari e **si mangia un po' di tensione**: circa 1,5-2 V
in totale alla corrente che ci interessa. Le bobine misurano **≈1700 Ω**, quindi a 24 V
scorrono meno di **15 mA** — una frazione dei 2 A che il modulo regge, e a quella corrente
la caduta resta bassa e il chip non scalda affatto.

Regola quindi l'XL6009 su **26-28 V** per averne ~24-26 sulle bobine. Con il trimmer:
misura l'uscita a vuoto col multimetro **prima** di collegare il ponte.

### Componenti

| Componente | Specifica | Note |
|-----------|-----------|------|
| Boost XL6009 | modulo col trimmer, 5 V → 24-30 V | Regolare **prima** di collegare; ~47-100 µF sull'uscita per i picchi |
| **L298N** (modulo) | doppio ponte H, 46 V max, 2 A | Si usa **un solo canale**. Morsetti a vite: zero saldature |

> ⚠️ **Non** usare L9110S o DRV8833: reggono ~11-12 V e a 24 V si distruggono.

### Codice

`firmware/phone_hal/hal_bell.c` alterna IN1 (GPIO 13) e IN2 (GPIO 14) con un `esp_timer`
periodico a mezzo periodo di 22,7 ms, cioè ~22 Hz. Il codice è **identico** a quello
scritto per il DRV8871: dal lato firmware i due ponti si comandano allo stesso modo, e
tutta la differenza sta nei ponticelli.

La cadenza di squillo — 1 s acceso, 4 s spento, numero massimo di squilli — **non sta lì**:
vive in `firmware/core/ring_pattern.c`, senza dipendenze da ESP-IDF, ed è coperta dai test
che girano sul PC.

### Pro e contro

✅ Zero saldature: morsetti a vite per bobine e alimentazione
✅ Regge 27 V con ampio margine, e a 15 mA non scalda
✅ Frequenza regolabile da software
✅ Costa un terzo del DRV8871 e si trova ovunque

⚠️ **Due ponticelli da gestire**, ed è l'unico vero rischio del componente
⚠️ Caduta di tensione da compensare alzando il boost — il DRV8871, a MOSFET, non l'avrebbe
⚠️ Onda quadra invece che sinusoidale: il campanello suona un filo più secco dell'originale
a 75 V, ma resta gradevolissimo

## Opzione B — Trasformatore + Oscillatore (più autentica)

### Schema concettuale

```
   +5V ─── Oscillatore (NE555 o GPIO) ── Driver MOSFET ── Trasformatore 5V:24V
                  22Hz                                          │
                                                                │
                                                          ┌─────┴─────┐
                                                          │  Bobine   │
                                                          │ campanello│
                                                          └───────────┘
```

Un trasformatore EI step-up commerciale (5V → 24V) o un piccolo trasformatore di ferrite pilotato a frequenza variabile genera l'onda AC quasi sinusoidale.

### Pro e contro

✅ Suono identico all'originale (sinusoide pura)  
✅ Isolamento galvanico tra logica e campanello  

❌ Trasformatori 5V:24V a bassa frequenza sono ingombranti (~3×3×4cm)  
❌ Più costoso (~12€)  
❌ Pilotaggio meno preciso  

## Quale scegliere?

**Per il 90% dei casi**: Opzione A.

L'orecchio umano non distingue significativamente tra onda quadra e sinusoidale su un trasduttore meccanico come le bobine del campanello — il sistema è naturalmente "filtrante" per la sua inerzia meccanica.

## Pattern di squillo italiano

Il pattern Telecom Italia tradizionale:

```
ON  ████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░████████████░░░...
    1 sec        4 sec di pausa                 1 sec
```

`firmware/core/ring_pattern.c` implementa questo pattern. I parametri stanno in **NVS**
e si modificano dalla pagina web in modalità configurazione, senza riaprire il telefono:

| Chiave NVS | Default | Significato |
|---|---|---|
| `bell_on_ms` | 1000 | durata dello squillo |
| `bell_off_ms` | 4000 | pausa tra gli squilli |
| `bell_max_rings` | 30 | massimo ~2,5 minuti |
| `bell_freq_hz` | 22 | frequenza di alternanza IN1/IN2 |

## Verifica meccanica del campanello

Prima di pilotarlo elettronicamente, verifica meccanicamente:

1. Il martelletto deve essere libero di oscillare con le dita, senza attriti
2. Le campane devono essere ben fissate ai loro perni
3. La distanza martelletto-campana a riposo deve essere ~1-2mm da entrambi i lati
4. Spruzza un velo di olio penetrante sul perno del martelletto se mostra resistenza

## ⚠️ Sicurezza

- **30V DC non sono pericolosi al tatto** in condizioni normali
- Le bobine immagazzinano energia: porta IN1=IN2=0 (coast) e togli alimentazione prima di scollegare i cavi
- L'H-bridge può scaldare durante squilli prolungati — verifica che non superi 60°C
- **Non far suonare il campanello vicino all'orecchio** — è MOLTO più forte di quanto sembri (~70-75 dB a 30cm)
