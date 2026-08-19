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

## Opzione A — H-bridge DRV8871 + Boost (consigliata: semplice, poche saldature)

### Schema

```
                                            ┌──────────────────────┐
                                            │   DRV8871 (breakout)  │
   +5V ──┬───────────┐                      │                      │   ┌─────────┐
         │     ┌──────┴─────┐               │ VM  ◄── +24V         │   │ Bobine  │
         │     │   Boost     │── +24V ──────►│ GND ◄── GND          │   │ campan. │
         │     │ 5V→~24V     │               │ OUT1 ───[morsetto]───┼──►│ (orig.) │
         │     │ (modulo)    │               │ OUT2 ───[morsetto]───┼──►│         │
         │     └─────────────┘               │ IN1  ◄── GPIO 13     │   └─────────┘
         │                                   │ IN2  ◄── GPIO 14     │
        GND                                  └──────────────────────┘
```

Bobina e alimentazione vanno sui **morsetti a vite** del breakout (niente
saldatura). Solo i 3 pin logici (IN1, IN2, GND) richiedono un mini-header.

### Funzionamento

Il software alterna **IN1/IN2** alla frequenza di squillo (~22 Hz):
- IN1=1, IN2=0 → corrente nella bobina in un senso
- IN1=0, IN2=1 → corrente nel senso opposto
- IN1=IN2=0 → **coast**: uscite ad alta impedenza, nessuna corrente → silenzio

Alternando si genera l'onda quadra AC (±24 V) che fa oscillare il martelletto.
Il **DRV8871 regge fino a 45 V** (i 24-30 V del campanello sono ampiamente nei
limiti) e ha **protezione interna** (sovracorrente, sovratemperatura, flyback):
niente inverter, niente diodi/snubber esterni.

> Il boost resta sempre alimentato dai 5 V; il silenzio si ottiene col coast
> (IN1=IN2=0), non spegnendo il boost — una logica/GPIO in meno e un cablaggio
> più semplice. Il consumo a riposo del boost è di pochi mA.

### Componenti

| Componente | Specifica | Note |
|-----------|-----------|------|
| Boost 5V→~24V | modulo pronto (XL6009 col trimmer, o DC-DC fisso 24V) | Regola a ~24-30V; aggiungi ~47-100µF sull'uscita per bufferare i picchi |
| **DRV8871** (breakout, es. Adafruit 3190) | H-bridge, **fino a 45V**, 3.6A picco, protezione interna | Morsetti a vite per bobina + VM (zero saldature); 2 ingressi logici IN1/IN2 |

> ⚠️ **Non** usare L9110S/DRV8833: reggono solo ~11-12 V e si distruggerebbero a 24 V.

### Codice

Vedi `firmware/hal/bell_drv8871.c` — alterna IN1 (GPIO 13) / IN2 (GPIO 14) alla
frequenza di squillo, col pattern italiano (1s on / 4s off, fino a cornetta sollevata
o timeout). Il toggle usa un `esp_timer`, che è agganciato a un timer hardware e ha
precisione al microsecondo: a 22 Hz il jitter è comunque irrilevante, perché l'inerzia
meccanica del martelletto filtra tutto.

Il pattern di squillo (durate e numero massimo) vive in `firmware/core/ring_pattern.c`,
senza dipendenze ESP-IDF, ed è quindi coperto dai test che girano sul PC.

### Pro e contro

✅ Pochissime saldature (morsetti a vite per bobina e alimentazione)  
✅ Nessun inverter/snubber/diodi esterni (protezione interna al DRV8871)  
✅ Regge 24-30V senza problemi (margine fino a 45V)  
✅ Frequenza regolabile da software, silenzio via coast  

⚠️ Onda quadra invece di sinusoidale — il campanello suona leggermente più "secco" dell'originale a 75V sinusoidale, ma rimane gradevolissimo  
⚠️ Il boost resta alimentato a riposo (pochi mA); per azzerare anche quelli servirebbe un GPIO+MOSFET sull'ingresso boost (più saldatura, non necessario)  

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
