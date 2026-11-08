#!/usr/bin/env bash
# El MINIMO de brillo de la 5": el rango, el acotado y como alterna el doble
# toque (21-oct-2026).
#
# Hasta hoy los dos niveles estaban fijos en brillo.h (100 y 85) y para cambiar
# el bajo habia que recompilar. Ahora se elige en Configuracion, de 1 en 1, entre
# 5 y 100, y las dos decisiones que pueden salir mal son PUREAS: brillo_min_acota()
# y brillo_alterna_desde(). Se sacan del fuente (ni una copia) y se prueban aqui.
set -euo pipefail
cd "$(dirname "$0")/.."

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# Los valores, del propio brillo.h (si cambian, el test cambia con ellos).
{ grep -E "^#define BRILLO_(ALTO|MIN_SUELO|MIN_TOPE|MIN_PASO|MIN_DEFECTO) " main/brillo.h || true; } \
    > "$TMP/brillo_defs.h"
cat >> "$TMP/brillo_defs.h" <<'EOF2'
#define __FIN_DEFS__
EOF2

python3 - "$TMP/piezas.c" <<'PY'
import re, sys, pathlib

def extrae(src, firma):
    m = re.search(re.escape(firma) + r'\s*\n\{', src)
    if not m:
        raise SystemExit("no encuentro en el fuente: " + firma)
    i = m.start(); k = src.index('{', m.start()); n = 0
    while k < len(src):
        c = src[k]
        if c == '/' and src[k+1:k+2] == '/':
            k = src.find('\n', k); k = len(src) if k < 0 else k
            continue
        if c == '/' and src[k+1:k+2] == '*':
            k = src.find('*/', k); k = len(src) if k < 0 else k + 2
            continue
        if c in ('"', "'"):
            q = c; k += 1
            while k < len(src) and src[k] != q:
                k += 2 if src[k] == '\\' else 1
            k += 1
            continue
        if c == '{': n += 1
        elif c == '}':
            n -= 1
            if n == 0: return src[i:k+1]
        k += 1
    raise SystemExit("llaves sin cerrar: " + firma)

s = pathlib.Path('main/brillo.c').read_text()
piezas = [
    extrae(s, 'int brillo_min_acota(int pct)'),
    extrae(s, 'int brillo_alterna_desde(int actual, int min)'),
]
pathlib.Path(sys.argv[1]).write_text('\n\n'.join(piezas) + '\n')
PY

cat > "$TMP/t.c" <<'EOF3'
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "brillo_defs.h"

/*__PIEZAS__*/

int main(int argc, char **argv)
{
    (void)argc;
    int actual = atoi(argv[1]);
    int min    = atoi(argv[2]);
    printf("%d %d\n", brillo_min_acota(actual), brillo_alterna_desde(actual, min));
    return 0;
}
EOF3
python3 - "$TMP/t.c" "$TMP/piezas.c" <<'PY'
import sys, pathlib
t = pathlib.Path(sys.argv[1]).read_text()
t = t.replace('/*__PIEZAS__*/', pathlib.Path(sys.argv[2]).read_text())
pathlib.Path(sys.argv[1]).write_text(t)
PY

gcc -std=gnu17 -Wall -Wextra -Werror -I "$TMP" -o "$TMP/t" "$TMP/t.c"

fallos=0
comprueba() {   # $1 actual  $2 minimo  $3 acotado esperado  $4 nivel que toca
    local got
    got=$("$TMP/t" "$1" "$2")
    if [ "$got" = "$3 $4" ]; then
        echo "ok     actual=$1 min=$2 -> acota=$3, doble toque va a $4"
    else
        echo "FALLO  actual=$1 min=$2 -> '$got' (esperado '$3 $4')"
        fallos=$((fallos + 1))
    fi
}

# El acotado (de la PRIMERA cifra: lo que llega como nivel) y el doble toque.
comprueba -5   80   5 100
comprueba  0   80   5 100
comprueba  4   80   5 100
comprueba  5   80   5 100
comprueba  40  80  40 100
comprueba  79  80  79 100
comprueba  80  80  80 100
comprueba  99  80  99 100
comprueba 100  80 100  80   # el alto baja al minimo
comprueba 100   0 100   5   # el minimo se acota al suelo (5)
comprueba 100 200 100 100   # ... y al tope (100)
comprueba 100 101 100 100
comprueba  90  90  90 100   # 90 no es el alto: sube
comprueba 100  90 100  90
comprueba 100   5 100   5
comprueba 100 100 100 100
comprueba  80   5  80 100

echo
if [ "$fallos" -eq 0 ]; then
    echo "MINIMO DE BRILLO OK: 5..100, y el doble toque alterna con el 100 %"
else
    echo "MINIMO DE BRILLO: $fallos fallo(s)"
fi
exit $((fallos > 0))
