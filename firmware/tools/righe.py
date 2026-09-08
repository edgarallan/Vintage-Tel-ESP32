#!/usr/bin/env python3
"""Estrae le righe di log vere da una cattura seriale.

I file prodotti da cattura.py contengono spesso byte non testuali — coda della
sessione di programmazione, o un flusso anomalo mai spiegato che supera la
banda della seriale. Le righe di ESP_LOG hanno pero' una forma riconoscibile,
e questa la usa per pescarle dal rumore.

    tools/righe.py FILE [FILTRO]

Con FILTRO tiene solo le righe il cui tag o messaggio lo contiene.
Le righe identiche consecutive vengono unite: la duplicazione e' un artefatto
noto della cattura, non un comportamento del firmware.
"""
import re
import sys

RIGA = re.compile(r'([IWE]) \((\d+)\) ([A-Za-z_0-9]+): ([^\r\n]{0,160})')


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    testo = open(sys.argv[1], 'rb').read().decode('latin-1')
    filtro = sys.argv[2] if len(sys.argv) > 2 else None

    precedente = None
    for livello, ms, tag, msg in RIGA.findall(testo):
        if filtro and filtro not in tag and filtro not in msg:
            continue
        chiave = (ms, tag, msg)
        if chiave == precedente:
            continue
        precedente = chiave
        print("%s %9s  %-10s %s" % (livello, ms, tag, msg))
    return 0


if __name__ == "__main__":
    sys.exit(main())
