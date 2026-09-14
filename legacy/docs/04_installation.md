# 04 — Guida installazione hardware

## ⚠️ Prima di iniziare

1. **Fotografa tutto** prima di smontare. Soprattutto le connessioni originali del telefono.
2. **Stacca eventuali residui di linea telefonica** (se è ancora connesso a un impianto, scollegalo dalla presa).
3. **Lavora su superficie isolante** (tappeto antistatico raccomandato).
4. **Non scartare nessuna vite/molla originale** — usa scatoline etichettate.

## Step 1 — Apertura del telefono SIP

![Apertura del telefono](../assets/diagrams/01_opening_phone.svg)

Il Siemens grigio ha tipicamente 2 viti sul fondo (testa cilindrica, dimensione PH1 o cacciavite piatto 3mm). 

1. Svita le viti sul fondo
2. Solleva il coperchio superiore con delicatezza — è incernierato sul disco
3. Identifica i componenti:
   - Disco combinatore (al centro, con due gruppi di contatti sul retro)
   - Campanello (le due bobine sotto, con il martelletto)
   - Hook switch (sotto la culla della cornetta)
   - Bornier di connessione (dove arrivano i fili della cornetta e della linea)

> 🗺️ **Mappa di conversione**: prima di rimuovere qualsiasi cosa, consulta
> [`07_retrofit_layout.md`](07_retrofit_layout.md) — foto annotata con cosa
> togliere, cosa tenere e dove sistemare i componenti nuovi.

## Step 2 — Mappatura dei contatti originali

![Mappa dei contatti](../assets/diagrams/02_contacts_map.svg)

Usa il multimetro in modalità continuità per identificare:

### Disco combinatore (4 fili)

![Identificazione fili disco](../assets/diagrams/03_dial_wires_identification.svg)

- Premi e tieni un puntale su un filo, scorri l'altro sui 3 rimanenti
- Trova la coppia che è **chiusa con disco fermo, aperta in rotazione** → NSI
- Trova la coppia che **apre/chiude durante il ritorno** → Pulse

Etichetta i fili con strisce di carta + scotch.

### Hook switch (2 fili)
- I contatti sotto la culla della cornetta
- Continuità invertita: chiuso = cornetta giù, aperto = cornetta su (verifica sul tuo esemplare, può variare)

### Cornetta (4 fili)
- 2 fili per il mic (di solito rossi o codificati)
- 2 fili per lo speaker (di solito neri o codificati)
- Identifica con multimetro misurando la resistenza: speaker ~50-200Ω, mic carbone ~30-300Ω a riposo

### Campanello (2 fili)
- I due capi delle bobine in serie
- Resistenza misurata: ~1-2 kΩ

## Step 3 — Pulizia e manutenzione meccanica

Prima di rimontare con l'elettronica nuova, vale la pena:
1. Pulire i contatti del disco con un panno + alcool isopropilico
2. Lubrificare leggermente le parti mobili del disco con olio leggero (orologi)
3. Pulire la cornetta esternamente e internamente con panno umido
4. Verificare che il martelletto del campanello sia libero e ben centrato

## Step 4 — Assemblaggio scheda di interfaccia

Salda su perfboard 7x9cm:
1. Resistori pull-up + condensatori per disco e hook (vedi `03_wiring.md`)
2. Breakout audio **MAX98357A** (ampli/speaker) e **SPH0645** (mic I2S)
3. Connettori JST-XH per i cavi modulari verso il Pi e verso i componenti originali

Crea **almeno 5 JST sul perfboard**:
- JST-2: hook switch
- JST-4: disco (pulse + NSI)
- JST-2: speaker cornetta
- JST-2: mic cornetta
- JST-2: campanello

In questo modo puoi smontare facilmente.

## Step 5 — Montaggio del Raspberry Pi

![Layout fisico interno](../assets/diagrams/05_internal_layout.svg)

1. Salda l'header GPIO 40 pin sul Pi Zero 2 W (se non già presente)
2. Inserisci la microSD con Raspberry Pi OS Lite 64-bit già preparata (vedi `05_software_setup.md`)
3. Fissa il Pi sulla base interna del telefono con distanziali M2.5 + biadesivo industriale
4. Cabla con jumper female-female o piccola scheda di interconnessione

## Step 6 — Montaggio alimentazione (DFRobot DFR0969)

Il **DFR0969** integra portacelle 2×18650 + caricabatterie + protezione + uscita
**5 V regolata**: niente boost separato sul rail logico.

1. Inserisci le due celle 18650 nel modulo (rispetta la polarità).
2. L'uscita **5 V** alimenta il Pi (pin 2/5V) e tutti i moduli a 5 V; da quella stessa 5 V parte anche l'ingresso del boost campanello **XL6009**.
3. Fissa il modulo sul fondo del telefono con biadesivo industriale.
4. Porta il connettore **USB-C / micro-USB** di ricarica sul retro/sotto, in posizione discreta.

> ⚠️ Il DFR0969 **carica oppure scarica**, non in contemporanea (no pass-through): non alimentare il telefono mentre è in carica. Verifica il budget 5V/2A coi picchi del campanello.

## Step 7 — Driver campanello

![Schema driver campanello](../assets/diagrams/06_bell_driver_schematic.svg)

> Nota: lo schema illustra il principio (boost + H-bridge). Il driver attuale è
> **DRV8871** (regge 24-30V, morsetti a vite), pilotato da GPIO 22/23 — vedi
> [`hardware/bell_driver.md`](../hardware/bell_driver.md).

Segui [`hardware/bell_driver.md`](../hardware/bell_driver.md).

Test funzionamento PRIMA di rimontare tutto:
1. Connetti il driver al campanello originale
2. Controlla manualmente con script di test (`firmware/src/bell_driver.py` ha un main di test)
3. Aspettati ~60-70 dB a 30cm di distanza — è forte ma autentico!

## Step 8 — Audio cornetta

![Connessioni audio](../assets/diagrams/07_audio_wiring.svg)

> Il diagramma illustra il principio (DAC/ampli I2S in uscita + mic I2S in
> ingresso). I moduli attuali sono **MAX98357A** (uscita) e **SPH0645** (mic).

Audio I2S full-duplex a due breakout: **MAX98357A** (ampli/speaker) e **SPH0645**
(mic MEMS). Solo collegamenti a jumper, niente SMD.

1. **Rimuovi** la capsula a carbone dalla cornetta (si svita o si estrae).
2. Monta il breakout **SPH0645** nella sede del microfono della cornetta (adattatore stampato 3D se serve); porta i fili al Pi con cavetto schermato. `SEL` a GND.
3. Monta il **MAX98357A** nella base; collega lo **speaker della cornetta** (50-200Ω) al suo **morsetto a vite** (+/−), nessun adattamento.
4. Collega entrambi al Pi **a jumper** condividendo BCLK/LRCK: MAX98357A su DOUT (GPIO21), SPH0645 su DIN (GPIO20) — vedi [`03_wiring.md`](03_wiring.md). Usano solo l'I2S, i GPIO di disco/gancio/campanello/LED restano liberi.
5. Abilita l'overlay `googlevoicehat-soundcard` e verifica la scheda con `aplay -l` (`sndrpigooglevoi`) — vedi [`05_software_setup.md`](05_software_setup.md).

## Step 9 — Display OLED (opzionale)

![Opzioni posizionamento OLED](../assets/diagrams/08_oled_placement_options.svg)

Possibili posizionamenti:
- **Sotto un coperchio rimovibile** sul retro del telefono — si vede solo quando vuoi
- **Dietro un piccolo foro discreto** sulla base, coperto da plexiglass scuro
- **Nel fondo, visibile solo capovolgendo il telefono** — soluzione "easter egg"

Cablaggio I2C:
```
Pi 3V3 ──► VCC display
Pi GND ──► GND display
Pi GPIO 2 (SDA) ──► SDA display
Pi GPIO 3 (SCL) ──► SCL display
```

## Step 10 — Test prima della chiusura

Prima di rimontare il coperchio, testa **tutto**:

```bash
cd ~/vintage_tel_bl/firmware
python3 -m src.test_hardware
```

Lo script `test_hardware.py` esegue:
1. Test LED RGB (ciclo colori)
2. Test display OLED (mostra "VINTAGE TEL BL OK")
3. Test hook switch (mostra stato in tempo reale)
4. Test disco (mostra cifre composte)
5. Test campanello (3 squilli brevi)
6. Test audio loopback (registra 2 sec dal mic, riproduce sullo speaker)

Solo se tutti i 6 test passano, procedi al rimontaggio finale.

## Step 11 — Rimontaggio finale

1. Sistema tutti i cavi con fascette adesive interne
2. Verifica che nessun cavo interferisca con il martelletto del campanello
3. Verifica che il disco giri liberamente
4. Chiudi il coperchio
5. Avvita le viti del fondo
6. Carica la batteria via USB-C fino a fine ciclo (LED di carica del DFR0969)

🎉 Sei pronto!
