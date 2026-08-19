#!/usr/bin/env bash
# Misura la copertura di core/ unendo i dati di tutti gli eseguibili di test.
#
# Serve unire perche' un modulo compilato in piu' target produce un .gcda per
# oggetto: guardarne uno solo fa sembrare scoperte righe che invece un altro
# test esercita. Una riga conta come coperta se ANCHE UN SOLO eseguibile
# l'ha percorsa.
#
#   ./tests/coverage.sh

set -euo pipefail
cd "$(dirname "$0")/.."

rm -rf tests/cov
cmake -S tests -B tests/cov -DCOVERAGE=ON > /dev/null
cmake --build tests/cov > /dev/null
(cd tests/cov && ctest > /dev/null)

python3 tests/coverage.py tests/cov
