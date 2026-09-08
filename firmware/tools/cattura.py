#!/usr/bin/env python3
"""Registra su file cio' che l'ESP32 scrive sulla seriale.

Serve durante il bring-up: si lancia, si compie il gesto sul telefono
(sollevare la cornetta, comporre un numero, farsi chiamare) e si legge dopo
cosa ha fatto il firmware. E' la controparte del src/test_hardware.py della v1
su Raspberry per la parte di osservazione.

    tools/cattura.py PORTA SECONDI FILE [TESTO_ATTESO [QUANTE_VOLTE]]

Con TESTO_ATTESO la cattura si chiude in anticipo appena quella riga compare il
numero di volte richiesto, invece di aspettare lo scadere del tempo: serve
perche' i gesti sull'apparecchio li fa una persona, e sincronizzarsi con una
finestra fissa non funziona.

Va lanciato con il Python di ESP-IDF, che ha pyserial:

    ~/.espressif/python_env/idf*/bin/python firmware/tools/cattura.py ...

QUATTRO TRAPPOLE, TUTTE GIA' PAGATE.

1. Usare pyserial e non termios+select a mano. Una versione fatta a mano
   duplicava i buffer parziali: la stessa riga compariva decine di volte con la
   stessa marca temporale e i file arrivavano a decine di megabyte, molto oltre
   quello che una seriale a 115200 puo' trasportare. Se i byte non tornano con
   la banda della linea, il sospettato e' lo strumento di misura, non il chip.

2. NORESET=1 quando c'e' una connessione Bluetooth in piedi. Senza, il chip
   viene resettato via DTR/RTS e il collegamento col cellulare cade.

3. Non catturare subito dopo `idf.py flash`. Il bootloader parla a 460800 e la
   ROM d'avvio a 74880: letti a 115200 riempiono il file di spazzatura che
   somiglia a un guasto del firmware. Aspettare qualche secondo.

4. Se il file e' vuoto, prima di sospettare il cavo ricordarsi che a riposo il
   firmware non stampa niente. Un reset (senza NORESET) fa comparire il banner
   d'avvio e toglie l'ambiguita'.
"""
import os
import sys
import time

import serial

BAUD = 115200


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2

    porta, secondi, uscita = sys.argv[1], float(sys.argv[2]), sys.argv[3]
    attesa = sys.argv[4] if len(sys.argv) > 4 else None
    quante = int(sys.argv[5]) if len(sys.argv) > 5 else 0

    s = serial.Serial(porta, BAUD, timeout=0.5)

    if not os.environ.get("NORESET"):
        s.dtr = False      # GPIO0 alto: avvio normale, non bootloader
        s.rts = True       # EN basso: reset tenuto
        time.sleep(0.15)
        s.rts = False

    scadenza = time.time() + secondi
    n = visti = 0
    coda = b""
    with open(uscita, "wb") as f:
        while time.time() < scadenza:
            d = s.read(4096)
            if not d:
                continue
            n += len(d)
            f.write(d)
            f.flush()

            if not attesa:
                continue
            coda += d
            righe = coda.split(b"\n")
            coda = righe.pop()
            visti += sum(1 for r in righe if attesa.encode() in r)
            if quante and visti >= quante:
                print("raccolti %d eventi '%s', chiudo in anticipo"
                      % (visti, attesa))
                break
    s.close()
    print("cattura finita: %d byte in %s (eventi attesi: %d)"
          % (n, uscita, visti))
    return 0


if __name__ == "__main__":
    sys.exit(main())
