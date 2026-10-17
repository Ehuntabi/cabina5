#!/usr/bin/env bash
# El cambio de horario no puede acortar una pernocta (14-oct-2026)
#
# El 'epoch_local' que manda la P4 es un epoch desplazado a hora local: en el
# cambio de horario DA UN SALTO ATRAS de 3600 s, asi que una pernocta que lo
# cruza salia una hora corta (y el cobro por 24 h con ella). La P4 manda su
# desfase horario en el byte que antes era relleno, y la cabina corrige el
# CALCULO (no la hora de pared que enseña).
#
# El test saca la funcion REAL de main/salida.c y le pone un reloj_delta_horario()
# de mentira con el valor que tocaria en cada caso.
set -euo pipefail

AQUI=$(cd "$(dirname "$0")" && pwd)

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

python3 - "$AQUI/viaje_horario_arnes.c" "$TMP/t.c" <<'PYEXT'
import re, sys, pathlib
arnes_path = pathlib.Path(sys.argv[1]).resolve()
raiz = arnes_path.parent.parent
fuente = (raiz / 'main/salida.c').read_text()
m = re.search(r'uint32_t salida_noches\(uint32_t epoch_ini, uint32_t epoch_fin\)\s*\n\{', fuente)
if not m:
    raise SystemExit('no encuentro salida_noches() en main/salida.c')
i = m.start(); k = fuente.index('{', i); n = 0
while True:
    if fuente[k] == '{': n += 1
    elif fuente[k] == '}':
        n -= 1
        if n == 0: break
    k += 1
fn = fuente[i:k+1]
# DOS funciones desde el 17-oct-2026: salida_noches() pregunta
# reloj_delta_horario_desde(epoch_ini), que es la que decide si el tramo empezo
# ANTES del cambio de horario. Las dos devuelven lo mismo aqui: el test simula
# tramos que cruzan el cambio, que es el caso que importa.
stub = ('static int g_delta = 0;\n'
        'int reloj_delta_horario(void) { return g_delta; }\n'
        'int reloj_delta_horario_desde(uint32_t epoch_ini) { (void)epoch_ini; return g_delta; }\n'
        '#define ESP_LOGW(tag, ...) do { } while (0)\n')
# el define del tope, que en salida.c viene del header
extra = '#define SALIDA_NOCHES_MAX 30\n'
texto = arnes_path.read_text().replace('__DELTA__', extra + stub).replace('__FN__', fn)
pathlib.Path(sys.argv[2]).write_text(texto)
PYEXT

gcc -std=gnu17 -Wall -Wextra -o "$TMP/t" "$TMP/t.c"
"$TMP/t"
