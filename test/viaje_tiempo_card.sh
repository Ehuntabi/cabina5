#!/usr/bin/env bash
# El tiempo conduciendo de la tarjeta VIAJE (27-oct-2026)
#
# La tarjeta del viaje de la pantalla de 5" pinta el tiempo conduciendo con sus
# unidades ("2h 35m" y, pasado un dia, "1d 2h 35m"); antes era "2:35" y una "h"
# suelta, que de un vistazo se confunde con una hora del reloj. Aqui se saca la
# funcion REAL (view_info.c, conduccion_texto) y se comprueban los cortes y que
# quepa en el rotulo.
#
# POR QUE EN SU PROPIO FICHERO Y NO EN viaje_contadores.sh (27-oct-2026): la
# tarjeta con los km y el tiempo SOLO existe en la pantalla de 5"; la de 3,5"
# tiene el mismo ciclo de viaje pero no esa tarjeta. viaje_contadores.sh es de
# los ficheros que el CI del contrato compara BYTE A BYTE entre las dos cabinas
# (job "contrato", paso "El ciclo del viaje, identico en las dos cabinas"), asi
# que meter ahi una prueba que solo tiene sentido en la 5" rompia el contrato y
# puso el CI en rojo el 27-oct. Lo compartido se queda compartido; lo propio de
# una pantalla, en un fichero de esa pantalla.
#
# Uso: bash test/viaje_tiempo_card.sh
set -euo pipefail

AQUI=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "== el tiempo conduciendo de la tarjeta VIAJE =="
python3 - "$AQUI/.." "$TMP/cond.c" <<'PYEXT5'
import re, sys, pathlib
sys.path.insert(0, str(pathlib.Path(sys.argv[1]).resolve() / 'test'))
from extrae_cuerpo import desde
repo = pathlib.Path(sys.argv[1]).resolve()
fuente = (repo / 'main/ui/view_info.c').read_text()
m = re.search(r'static void conduccion_texto\(unsigned min, char \*b, size_t n\)\s*\n\{', fuente)
if not m:
    raise SystemExit('no encuentro conduccion_texto() en main/ui/view_info.c')
fn = desde(fuente, m)
plantilla = '''#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

__FN__

int main(int argc, char **argv)
{
    (void)argc;
    char b[24];
    conduccion_texto((unsigned)strtoul(argv[1], NULL, 10), b, sizeof(b));
    printf("%s\\n", b);
    return 0;
}
'''
pathlib.Path(sys.argv[2]).write_text(plantilla.replace('__FN__', fn))
PYEXT5
gcc -std=gnu17 -Wall -Wextra -o "$TMP/cond" "$TMP/cond.c"

fallos=0
comprueba_cond() {   # $1 minutos  $2 texto esperado
    local got
    got=$("$TMP/cond" "$1")
    if [ "$got" = "$2" ]; then
        echo "ok     $1 min -> $got"
    else
        echo "FALLO  $1 min -> '$got' (esperado '$2')"; fallos=1
    fi
}
comprueba_cond 0      "0h 00m"
comprueba_cond 1      "0h 01m"
comprueba_cond 59     "0h 59m"
comprueba_cond 60     "1h 00m"
comprueba_cond 155    "2h 35m"
comprueba_cond 1439   "23h 59m"
comprueba_cond 1440   "1d 0h 00m"
comprueba_cond 1505   "1d 1h 05m"
comprueba_cond 10000  "6d 22h 40m"
# Y que quepa en el bufer de 24 del rotulo (peor caso: 99 dias).
comprueba_cond 143999 "99d 23h 59m"

[ "$fallos" = 0 ] || { echo; echo "FALLOS: $fallos"; exit 1; }
echo
echo "todo ok"
