# Guasti incontrati, e come sono stati trovati

Non un elenco di soluzioni: un elenco di **sintomi**, con il ragionamento che ha portato
alla causa. Serve perché in questo progetto gli stessi sintomi si ripresentano, e la
seconda volta conviene ricordarsi come è andata la prima.

Regola generale emersa da tutti i casi qui sotto: **misurare invece di dedurre**, e
diffidare del componente che si dà per buono perché "ieri funzionava".

---

## L'ESP32 non dice niente, nemmeno il bootloader

**Sintomo.** La porta seriale esiste, ma la cattura raccoglie zero byte. Non un errore, non
un carattere: silenzio.

**Causa trovata il 12/09/2026.** Un filo del codec finito su `RX0`. Nella piedinatura della
DevKit `TX0` e `RX0` — cioè GPIO 1 e 3, la console — stanno **esattamente fra `22` e `21`**:

```
... 23   22   TX0   RX0   21   GND   19   18 ...
                ▲     ▲
          la console seriale
```

Chi mira al 21 e sbaglia di una posizione prende `RX0`. Il chip continua a funzionare
perfettamente, ma **non ha più con cosa parlare**.

**L'altro candidato** per lo stesso sintomo è un filo su `EN`, che è il primo pin dopo
`3V3` sull'altro lato: `EN` è il reset, e tenuto basso lascia il chip fermo per sempre.

**Come distinguerli.** Il silenzio da `RX0` lascia il chip vivo — LED e display continuano a
funzionare. Il silenzio da `EN` spegne tutto.

---

## Ciclo di riavvio: "Brownout detector was triggered"

**Sintomo.** Venti o più riavvii in quindici secondi, sempre nella riga esatta dopo
`phy_init`, cioè quando la radio Bluetooth si accende.

**Cosa NON era**, escluso misurando il 12/09/2026:

| Ipotesi | Prova | Esito |
|---|---|---|
| Sorgente debole | 5 V sotto carico | 5,06 V stabili |
| Regolatore in ginocchio | 3,3 V a regime | 3,25 V stabili |
| Detector ipersensibile | `CONFIG_ESP_BROWNOUT_DET_LVL` | 0, cioè 2,43 V: la soglia più bassa |
| Troppi carichi | display e codec staccati | invariato |
| L298N che si alimenta dai diodi di protezione | GPIO 13/14 staccati | invariato |
| Calibrazione del PHY corrotta in NVS | `idf.py erase-flash` completo | invariato |
| Potenza radio eccessiva | `ESP_PHY_REDUCE_TX_POWER=y` | invariato |

**Causa vera: la DevKit stessa.** Sostituendola con una seconda scheda, stesso firmware e
stessa alimentazione, il Bluetooth è partito **al primo avvio senza un riavvio**.

Il regolatore della prima scheda **tiene la tensione a riposo ma non segue il gradino di
corrente** della radio — che passa da poche decine di milliampere a un picco di 250-300 mA
in microsecondi. Il multimetro legge 3,25 V perfetti perché fa una media e il crollo dura
troppo poco.

**Perché ci è voluta un'ora.** La notte prima quella scheda aveva retto il Bluetooth per
ore, con **più** roba collegata. "Prima funzionava" faceva sembrare la scheda l'unico
componente sopra ogni sospetto, e ha spostato la ricerca su tutto il resto.

> **"Prima funzionava" non prova che qualcosa sia ancora integro: prova solo che lo era
> prima.** Se i sintomi non tornano con nessuna ipotesi, il sospettato scartato va
> sostituito, non riscartato.

**Come si è probabilmente danneggiata.** Nella stessa nottata: un filo su `RX0`, fili
spostati con l'alimentazione attaccata, contatti provati a tentativi sui pin, decine di
interruzioni brusche. Un contatto momentaneo fra `3V3` e massa basta a stressare un AMS1117
senza ucciderlo — e il risultato è esattamente questo.

**Prevenzione:** staccare l'USB **prima** di spostare qualunque filo.

---

## Il telefono va in panico se manca un accessorio

**Sintomo.** Ventuno riavvii in dodici secondi con `abort()`, dopo aver scollegato l'OLED.

**Causa.** `hal_display_init` usava `ESP_ERROR_CHECK` sull'inizializzazione del pannello: un
display assente non risponde, la chiamata fallisce, e `ESP_ERROR_CHECK` fa abortire
**l'intero sistema**.

**Corretto.** Un accessorio mancante ora produce una riga di avviso. Il mestiere di questo
apparecchio è telefonare, e le chiamate non hanno bisogno di uno schermo.

> Vale come regola per i driver futuri: `ESP_ERROR_CHECK` solo su ciò che, mancando, rende
> il telefono inutile. Per tutto il resto, un avviso e si va avanti.

---

## Il campanello riparte da solo a ogni accensione

**Sintomo.** Il campanello suona in ciclo appena si attacca l'USB, di notte.

**Causa.** Il diagnostico `VT_DIAG_BELL` era stato **disattivato nel sorgente ma mai
scritto sul chip**, perché il flash era fallito con l'USB staccato.

> **Un flash non è avvenuto finché non si è visto `Done`.** Disattivare un diagnostico nel
> `CMakeLists.txt` non cambia nulla sul chip: quello che gira è l'ultimo binario scritto.

**Rimedio immediato**, se ricapita: tenere premuto `BOOT` mentre si attacca l'USB. Il chip
entra in modalità programmazione e l'applicazione non parte affatto.

---

## Il microfono che non si trova

**Sintomo.** Il percorso ADC del codec e' configurato, l'I2S legge, ma i numeri non hanno
senso: livelli di 1-3 su 32767, poi centinaia, e rapporti voce/silenzio che passano da 2,48
a 0,88 sullo stesso ingresso a distanza di minuti.

**Prima causa, trovata: mancava il filo dei dati.** `GPIO 33` non era mai stato collegato a
`TXSDA`, perche' costruendo la sola riproduzione era stato dichiarato rimandabile — e poi
dimenticato quando i due fili dati sono stati scambiati. Senza linea dati in ingresso l'ADC
converte benissimo e quello che produce non arriva da nessuna parte. Leggere **esattamente
1-3** e' la firma: un ingresso I2S senza filo, non un microfono debole.

**Seconda causa: il microfono di bordo non e' sul percorso analogico.** Con un colpetto
d'unghia **direttamente sopra** il microfono della scheda — un transitorio circa 40 dB sopra
una voce, impossibile da confondere col rumore — nessuno dei tre ingressi reagisce:

| | Fondo | Colpetti |
|---|---|---|
| INPUT1 (preamp +40 dB) | 20075, cioe' **61% del fondo scala** | 29255 |
| INPUT2 (boost diretto) | 1299 | 1212 |
| INPUT3 (boost diretto) | **32768, saturo** | 7013 |

Il MEMS a bordo e' probabilmente **digitale**, con un'uscita che non passa dall'ADC.

**Terza cosa imparata, sul metodo.** Il guadagno digitale dell'ADC non serve a tirare fuori
un segnale debole: sta **dopo** il convertitore e amplifica segnale e rumore nella stessa
misura. Misurato: portandolo a +30 dB il rumore e' salito da 286 a 9198 — esattamente 32
volte — e la voce non e' emersa. L'unico guadagno che migliora il rapporto e' quello
**analogico**, prima dell'ADC.

> **Non si tara un percorso microfonico senza un microfono noto collegato.** Senza una
> sorgente di riferimento si insegue il rumore, e le misure non sono nemmeno riproducibili.
> Con la capsula della cornetta attaccata la taratura diventa banale: si parla e si guarda
> il livello.

**Confermato il 17/09/2026**, con la capsula collegata: fondo 70, voce 4248, rapporto oltre
60. La stessa misura che il giorno prima era impossibile.

### E il diagnostico misurava il proprio eco

Le prime versioni della prova scandivano le fasi con dei bip: tre corti per "taci", uno
lungo per "parla". Davano risultati **invertiti** — fondo sistematicamente più alto della
voce — e nessun numero poteva rivelare il perché, perché l'errore era nella struttura della
misura.

**I bip escono dalla capsula d'ascolto della cornetta, a pochi centimetri dal microfono
della stessa cornetta.** Il diagnostico registrava la coda del proprio segnale e la
attribuiva al silenzio.

> Quando lo strumento di misura **agisce** sul sistema che misura, i suoi segnali di
> servizio fanno parte del segnale. Su un telefono, altoparlante e microfono sono a dieci
> centimetri: qualunque cosa esca da uno rientra nell'altro.

La versione che funziona non ha fasi né segnali: **stampa il picco ogni secondo, per
sempre**. Si parla quando si vuole, e il profilo dei numeri mostra da solo dove c'era voce —
comprese le pause fra le frasi.

### Più guadagno non vuol dire più segnale

Due volte, in due punti diversi della catena:

| | Effetto |
|---|---|
| Guadagno **digitale** dell'ADC, +30 dB | rumore ×32, voce invariata |
| Preamplificatore da +20 a +30 dB | fondo 70 → 1800, voce ferma a ~4500, e satura |

Il primo caso è strutturale: il guadagno digitale sta **dopo** il convertitore e non può
cambiare il rapporto fra segnale e rumore. Il secondo è il preamplificatore che, oltre un
certo punto, amplifica soprattutto il proprio rumore.

**Il guadagno giusto è il più alto che non satura, scelto guardando il rapporto e non il
livello.**

I guadagni sono stati riportati a 0 dB, che e' un punto di partenza prudente e non un valore
scelto: +40 dB mandava l'ingresso a saturazione da solo, senza nessuna sorgente.

## La seriale sputa megabyte che non possono esistere

**Sintomo.** Catture da 9 MB in 25 secondi, con righe ripetute decine di volte a parità di
marca temporale.

**Il calcolo che risolve.** A 115200 baud passano **11,5 KB al secondo**, cioè 288 KB in 25
secondi. Nove megabyte non possono essere arrivati dal filo.

> **Se i byte non tornano con la banda della linea, il sospettato è lo strumento di misura,
> non il chip.**

Concorrono due cause distinte: uno strumento di cattura scritto a mano con `termios` che
duplicava i buffer parziali, e la coda della sessione di programmazione — il bootloader
parla a 460800 e la ROM d'avvio a 74880, che letti a 115200 sembrano un guasto.

Lo strumento buono è `firmware/tools/cattura.py`, con le trappole documentate in testa.
