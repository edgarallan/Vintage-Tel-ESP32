# 03 — Schema di cablaggio

![Schema generale di cablaggio](../assets/diagrams/04_general_wiring.svg)

## Pinout Raspberry Pi Zero 2 W

```
                    Raspberry Pi Zero 2 W
                  ┌─────────────────────┐
              3V3 │ 1  ●  ● 2  │ 5V
   I2C SDA (DISP) │ 3  ●  ● 4  │ 5V
   I2C SCL (DISP) │ 5  ●  ● 6  │ GND
       DIAL_PULSE │ 7  ●  ● 8  │ TX (debug)
              GND │ 9  ●  ● 10 │ RX (debug)
       DIAL_NSI   │ 11 ●  ● 12 │ I2S BCK
        HOOK_SW   │ 13 ●  ● 14 │ GND
       BELL_IN1   │ 15 ●  ● 16 │ BELL_IN2
              3V3 │ 17 ●  ● 18 │ BTN_PHONEBOOK
          (MOSI)  │ 19 ●  ● 20 │ GND
          (MISO)  │ 21 ●  ● 22 │ LED_R
          (SCLK)  │ 23 ●  ● 24 │ LED_G
              GND │ 25 ●  ● 26 │ LED_B
              ID  │ 27 ●  ● 28 │ ID
                  │ 29 ●  ● 30 │ GND
                  │ 31 ●  ● 32 │
            I2S LR│ 33 ●  ● 34 │ GND
           I2S DIN│ 35 ●  ● 36 │
                  │ 37 ●  ● 38 │ I2S DOUT
              GND │ 39 ●  ● 40 │
                  └─────────────────────┘
```

| Funzione | GPIO BCM | Pin fisico | Direzione | Note |
|----------|----------|-----------|-----------|------|
| I2C SDA (display OLED) | GPIO 2 | 3 | I/O | OLED 0x3C |
| I2C SCL (display OLED) | GPIO 3 | 5 | I/O | |
| DIAL_PULSE | GPIO 4 | 7 | IN (pull-up) | Impulsi del disco |
| DIAL_NSI | GPIO 17 | 11 | IN (pull-up) | "Off-normal" — disco in movimento |
| HOOK_SW | GPIO 27 | 13 | IN (pull-up) | LOW = cornetta sollevata |
| BELL_IN1 | GPIO 22 | 15 | OUT | DRV8871 IN1 (campanello) |
| BELL_IN2 | GPIO 23 | 16 | OUT | DRV8871 IN2 (campanello) |
| BTN_PHONEBOOK | GPIO 24 | 18 | IN (pull-up) | Pulsante rubrica |
| LED_R | GPIO 25 | 22 | OUT (PWM) | LED stato — rosso |
| LED_G | GPIO 8 | 24 | OUT (PWM) | LED stato — verde |
| LED_B | GPIO 7 | 26 | OUT (PWM) | LED stato — blu |
| I2S BCK | GPIO 18 | 12 | OUT | Bit clock audio |
| I2S LRCK | GPIO 19 | 35 | OUT | LR clock audio |
| I2S DIN | GPIO 20 | 38 | IN | Dal mic SPH0645 (DOUT del mic) |
| I2S DOUT | GPIO 21 | 40 | OUT | All'ampli MAX98357A (DIN dell'ampli) |

## Cablaggio del disco combinatore

Il disco Siemens ha **due contatti** (4 fili totali):
- **NSI / "off-normal"** — chiuso quando il disco è in posizione di riposo, aperto durante la rotazione
- **Pulse / "make-break"** — apre/chiude N volte durante il ritorno (N = cifra composta)

```
            Disco combinatore Siemens
            ┌─────────────────────────┐
            │                         │
   Filo 1 ──┤ Pulse contact  ├── Filo 2
            │                         │
   Filo 3 ──┤ NSI contact    ├── Filo 4
            │                         │
            └─────────────────────────┘

Identificazione fili con multimetro (modalità continuità):
1. Disco a riposo: NSI chiuso, Pulse chiuso
2. Inizio rotazione: NSI apre
3. Ritorno: Pulse apre/chiude N volte
4. Fine: tutto torna chiuso
```

### Circuito di interfaccia disco (con debouncing HW)

```
       +3.3V
         │
        ┌┴┐
        │ │ 10kΩ pull-up
        │ │
        └┬┘
         ├──────────────────────► GPIO 4 (DIAL_PULSE)
         │
         │      ┌──┐
         ├──────┤  ├──── 100nF
         │      └──┘      ↓
         │              GND
         │
   ┌─────┤
   │     │
   │   ┌─┴─┐
   │   │   │ Contatto Pulse del disco
   │   └─┬─┘
   │     │
   └─────┴───── GND

Identico circuito per DIAL_NSI su GPIO 17.
```

**Perché il condensatore 100nF**: il contatto meccanico fa "rimbalzi" (bouncing) di ~1-2ms quando apre/chiude. Il condensatore + pull-up forma un filtro RC con τ = 10kΩ × 100nF = 1ms, che li elimina senza distorcere gli impulsi reali (durata ~50ms).

**Debouncing aggiuntivo via software** in `dial_reader.py` con `bouncetime=50` per sicurezza.

## Cablaggio hook switch (gancio cornetta)

Il gancio è un semplice interruttore meccanico già presente nel telefono. Identico al cablaggio del disco:

```
       +3.3V
         │
        10kΩ
         │
         ├────────► GPIO 27 (HOOK_SW)
         │
         │  ┌──┐
         ├──┤  ├── 100nF ── GND
         │  └──┘
         │
       hook
       switch
         │
        GND

GPIO 27 = LOW → cornetta sollevata (handset up)
GPIO 27 = HIGH → cornetta poggiata (handset down)
```

## Cablaggio audio (MAX98357A + SPH0645)

Audio I2S **full-duplex** con due breakout che condividono i clock I2S:
**MAX98357A** (uscita, sul DOUT del Pi) e **SPH0645** (ingresso, sul DIN del Pi).
Collegamento **a jumper** sugli header — solo saldature through-hole, niente SMD.

### Uscita — MAX98357A → speaker cornetta

```
Pi GPIO 18 (BCK) ───► BCLK   ┐
Pi GPIO 19 (LRCK)───► LRC    │ MAX98357A ─► [+ −] morsetto a vite ─► Speaker cornetta
Pi GPIO 21 (DOUT)───► DIN    ┘
Pi 5V  ─────────────► Vin
Pi GND ─────────────► GND
                       GAIN ── libero = +9 dB (collegalo a GND/Vin per altri livelli)
```

### Ingresso — SPH0645 (mic MEMS) nella cornetta

```
Pi GPIO 18 (BCK) ───► BCLK
Pi GPIO 19 (LRCK)───► LRCL / WS
Pi GPIO 20 (DIN) ◄─── DOUT     (dati mic verso il Pi)
Pi 3V3 ─────────────► 3V
Pi GND ─────────────► GND
                       SEL ── a GND (canale sinistro)
```

Il breakout SPH0645 è piccolo: va montato **nella cornetta**, al posto della vecchia
capsula a carbone (che si rimuove), con cavetto schermato verso il Pi.

Note:
- I due moduli **condividono BCLK (GPIO 18) e LRCK (GPIO 19)**; le linee dati sono
  separate (DOUT del Pi → ampli, DIN del Pi ← mic). Overlay: `googlevoicehat-soundcard`.
- Usano **solo l'I2S**: l'I2C (GPIO 2/3) resta dedicato al display OLED.
- Né MAX98357A né SPH0645 hanno regolazione hardware del volume: il livello si
  imposta dai softvol `PhoneSoftVol` / `PhoneCaptureVol` in
  [`asound.conf`](../firmware/config/asound.conf), pilotati da
  `audio.speaker_gain_db` / `audio.mic_gain_db`.

## Cablaggio campanello

Vedi documento separato: [`hardware/bell_driver.md`](../hardware/bell_driver.md)

Sintesi: il campanello richiede ~24V AC a 20-25Hz. Lo generiamo con:
1. Boost DC-DC: 5V → ~24V (modulo pronto)
2. H-bridge **DRV8871** (regge fino a 45V) con bobina e alimentazione sui morsetti a vite
3. GPIO 22/23 (IN1/IN2) alternati via software a ~22Hz; IN1=IN2=0 → silenzio (coast)

## Alimentazione

Modulo unico **DFRobot DFR0969**: portacelle 2×18650 + caricabatterie + protezione
+ uscita **5 V regolata** (niente boost separato sul rail logico).

```
        USB-C / micro-USB (ricarica)
              │
              ▼
        ┌─────────────────────────────┐
        │  DFRobot DFR0969            │  carica + protezione
        │  2×18650 + charger + 5V/2A  │  (carica O scarica, no UPS)
        └───────────┬─────────────────┘
                    │ 5V regolati
              ┌─────┴────────┐
              │              │
              ▼              ▼
         Pi + moduli    ┌──────────┐
         logic (5V)     │ XL6009   │  5→~30V
                        └─────┬────┘
                              ▼
                        Driver campanello
                        (solo quando squilla)
```

> Budget uscita **2 A**: il Pi Zero 2 W + audio + display stanno larghi; tieni
> margine per lo spunto del campanello (XL6009) se coincide con WiFi/BT attivi.

**Stima autonomia**: 
- In idle (BT advertising): ~50mA → ~140 ore (5+ giorni)
- In chiamata attiva: ~250mA → ~28 ore
- Con il display sempre acceso: scendere di ~30%

## Layout fisico interno

> 🗺️ Per la mappa "cosa togliere / tenere / aggiungere" sovrapposta a una foto
> reale della cassetta S62, vedi [`07_retrofit_layout.md`](07_retrofit_layout.md).

```
        Vista dall'alto (telefono aperto)
        
        ┌───────────────────────────────────┐
        │                                   │
        │   ┌─────────┐                     │
        │   │ Bobine  │       (campanello   │
        │   │ orig.   │        originale)   │
        │   └─────────┘                     │
        │                                   │
        │   ┌─────────────┐  ┌───────────┐  │
        │   │  PCB         │  │  18650 x2 │  │
        │   │  interfacce  │  │           │  │
        │   │  + driver    │  └───────────┘  │
        │   │  campanello  │                 │
        │   └─────────────┘                  │
        │                                    │
        │   ┌─────────────┐                  │
        │   │ Pi Zero 2W  │                  │
        │   │ + DAC I2S   │                  │
        │   └─────────────┘                  │
        │                                    │
        │     ┌────────────────────┐         │
        │     │   Disco originale  │         │
        │     └────────────────────┘         │
        │                                    │
        │   [USB-C ricarica sul retro/sotto] │
        │                                    │
        └────────────────────────────────────┘
```

Il display OLED può essere montato dietro un piccolo foro nascosto sul retro o sotto un coperchio rimovibile. Il LED RGB può essere infilato in un foro discreto sul fondo per indicare lo stato.
