# 01 — Overview e Architettura

## Visione d'insieme

Il telefono SIP Siemens grigio degli anni '70 è un capolavoro di ingegneria elettromeccanica. L'obiettivo di questo progetto è **non snaturarlo**: il disco gira come prima, il campanello suona come prima, la cornetta pesa come prima. Cambia solo cosa c'è dentro.

## Architettura logica

```
┌─────────────────────────────────────────────────────────────────┐
│                    TELEFONO SIP (scocca)                        │
│                                                                 │
│  ┌──────────────┐    ┌─────────────────┐    ┌───────────────┐ │
│  │   CORNETTA   │    │  DISCO COMBIN.  │    │  CAMPANELLO   │ │
│  │              │    │                 │    │  (24V AC)     │ │
│  │ • Mic I2S    │    │ • Contatto      │    │               │ │
│  │ • Speaker    │    │   pulse (NSI)   │    │               │ │
│  │ • Hook switch│    │ • Contatto      │    │               │ │
│  │              │    │   off-normal    │    │               │ │
│  └──────┬───────┘    └────────┬────────┘    └───────┬───────┘ │
│         │                     │                     │         │
│         │ analog audio        │ pulses              │ AC      │
│         │                     │                     │         │
│  ┌──────▼─────────────────────▼─────────────────────▼───────┐ │
│  │                  CIRCUITO DI INTERFACCIA                  │ │
│  │  ┌──────────┐  ┌─────────┐  ┌────────────┐  ┌─────────┐  │ │
│  │  │ Pre-amp  │  │ Amp     │  │ GPIO       │  │ Driver  │  │ │
│  │  │ mic      │  │ speaker │  │ debouncing │  │ relay + │  │ │
│  │  │ + bias   │  │ I2S DAC │  │            │  │ boost   │  │ │
│  │  └────┬─────┘  └────▲────┘  └─────┬──────┘  └────▲────┘  │ │
│  └───────┼─────────────┼─────────────┼──────────────┼───────┘ │
│          │             │             │              │         │
│  ┌───────▼─────────────┴─────────────▼──────────────┴───────┐ │
│  │              RASPBERRY PI ZERO 2 W                        │ │
│  │                                                           │ │
│  │  ┌─────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │ │
│  │  │ oFono   │  │ Audio    │  │ Dial     │  │ Display  │   │ │
│  │  │ (BT HFP)│  │ I2S      │  │ reader   │  │ OLED I2C │   │ │
│  │  └────┬────┘  └─────┬────┘  └────┬─────┘  └────┬─────┘   │ │
│  │       │             │            │             │         │ │
│  │       └──────┬──────┴────────────┴─────────────┘         │ │
│  │              │                                            │ │
│  │       ┌──────▼──────────┐                                 │ │
│  │       │   main.py       │  ← orchestratore stati          │ │
│  │       │  (state machine)│                                 │ │
│  │       └──────┬──────────┘                                 │ │
│  │              │                                            │ │
│  │       ┌──────▼──────────┐                                 │ │
│  │       │   phonebook     │                                 │ │
│  │       │   (sqlite)      │                                 │ │
│  │       └─────────────────┘                                 │ │
│  └───────────────────┬───────────────────────────────────────┘ │
│                      │                                          │
│              ┌───────▼────────┐                                 │
│              │  ALIMENTAZIONE │                                 │
│              │  DFR0969       │                                 │
│              │  (2x18650+5V)  │                                 │
│              │  + 24VAC gen.  │                                 │
│              └────────────────┘                                 │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ Bluetooth / Wi-Fi
                              ▼
                    ┌──────────────────┐
                    │   SMARTPHONE     │
                    │   (HFP profile)  │
                    └──────────────────┘
```

## State machine

Il telefono ha 5 stati principali:

```
        ┌─────────────┐
        │    IDLE     │  ← cornetta giù, niente chiamate
        └──────┬──────┘
               │ hook up
               ▼
        ┌─────────────┐
        │   DIALING   │  ← cornetta su, in attesa cifre
        │ (collect    │     dal disco
        │  digits)    │
        └──────┬──────┘
               │ timeout dial / # / numero rubrica match
               ▼
        ┌─────────────┐
        │   CALLING   │  ← chiamata in corso
        └──────┬──────┘    via Bluetooth HFP
               │
               │ hook down / remote hangup
               ▼
        ┌─────────────┐
        │    IDLE     │
        └─────────────┘

        ┌─────────────┐
        │  RINGING    │  ← chiamata in arrivo
        │ (bell on)   │     campanello suona
        └──────┬──────┘
               │ hook up
               ▼
        ┌─────────────┐
        │  IN_CALL    │
        └─────────────┘
```

## Scelte tecniche

### Perché Raspberry Pi Zero 2 W e non ESP32?
Il progetto richiede:
- Stack Bluetooth completo con HFP/HSP (oFono + BlueZ)
- Audio I2S full-duplex per le chiamate
- Database rubrica (SQLite)
- Multitasking reale tra audio, GPIO, Bluetooth

L'ESP32 può fare BT audio ma non HFP + audio + rubrica contemporaneamente in modo affidabile.

### Perché I2S e non l'audio analogico del Pi?
Il Pi Zero non ha jack audio. L'I2S dà qualità migliore e bassa latenza — fondamentale per le chiamate. Usiamo due breakout I2S: **MAX98357A** (ampli/speaker) e **SPH0645** (mic MEMS), full-duplex con l'overlay `googlevoicehat-soundcard`.

### Microfono: MEMS I2S nella cornetta
La capsula a carbone viene **rimossa** e sostituita dal breakout **SPH0645** montato nella cornetta: è digitale (niente codec/ADC analogico), si collega solo a jumper sull'I2S e dà un microfono moderno dove serve, cioè nel microtelefono.

### Perché tenere il campanello originale?
Perché è bellissimo. Richiede un boost converter che generi ~24V AC a 20-25Hz, oppure una soluzione più semplice con due bobine pilotate in alternanza da un H-bridge.

## Funzionamento

Il dispositivo è un **vivavoce Bluetooth HFP** per il cellulare accoppiato: la
telefonia avviene sul cellulare, l'apparecchio vintage fa da interfaccia fisica
(disco per comporre, cornetta per parlare/ascoltare, campanello per lo squillo).
