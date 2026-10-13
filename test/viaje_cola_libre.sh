#!/usr/bin/env bash
# ¿Cuanto queda en la tarjeta de la P4? (14-oct-2026)
#
# La P4 añade " libre_mb=NNN" al cuerpo de sus respuestas de /api/viaje (que la
# cabina recibia y tiraba). Ese texto es SIN AUTENTICAR: la pantalla solo lo usa
# para AVISAR al usuario, nunca para decidir si un apunte se ha guardado. Aqui se
# prueba el parseo, con la funcion REAL de main/net/viaje_cola.c.
set -euo pipefail

AQUI=$(cd "$(dirname "$0")" && pwd)

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

python3 - "$AQUI/viaje_cola_libre_arnes.c" "$TMP/t.c" <<'PYEXT'
import re, sys, pathlib
arnes_path = pathlib.Path(sys.argv[1]).resolve()
raiz = arnes_path.parent.parent
fuente = (raiz / 'main/net/viaje_cola.c').read_text()
m = re.search(r'static int parsea_libre_mb\(const char \*respuesta\)\s*\n\{', fuente)
if not m:
    raise SystemExit('no encuentro parsea_libre_mb() en main/net/viaje_cola.c')
i = m.start(); k = fuente.index('{', i); n = 0
while True:
    if fuente[k] == '{': n += 1
    elif fuente[k] == '}':
        n -= 1
        if n == 0: break
    k += 1
pathlib.Path(sys.argv[2]).write_text(arnes_path.read_text().replace('__FN__', fuente[i:k+1]))
PYEXT

gcc -std=gnu17 -Wall -Wextra -o "$TMP/t" "$TMP/t.c"
"$TMP/t"
