#!/usr/bin/env bash
# Los dos numeros del viaje, sin placa (16-oct-2026)
#
#   1. Los contadores que MANDA la P4 (km recorridos y minutos conduciendo) y que
#      la cabina pinta en la tarjeta del viaje.
#   2. El cuentakilometros del cuadro que la CABINA manda al empezar el viaje
#      (campo km_cuadro del inicio).
#
# La tarjeta del viaje de la pantalla de 5" enseña dos numeros que viajan en la
# telemetria: los km recorridos y los minutos conduciendo. Van partidos en CUATRO
# BYTES SUELTOS (los que eran relleno del struct, ver mini_proto.h) porque
# repartir el struct de nuevo habria cambiado offsets que las cabinas ya grabadas
# estan leyendo.
#
# Ese reparto tiene un fallo posible que no salta a la vista: CRUZAR EL BYTE ALTO
# CON EL BAJO. Los dos numeros siguen pareciendo numeros -- un viaje de 1200 km
# se lee como 4920 -- y no se descubre hasta que uno mira el cuaderno en casa,
# semanas despues. Aqui se comprueba el camino entero SIN PLACA: se saca de
# udp_tx.c de la P4 el trozo que parte los contadores, de data_model.c de la
# cabina el que los recompone, y se les pasan valores.
#
# Uso: bash test/viaje_contadores.sh [ruta del repo victron]
set -euo pipefail

AQUI=$(cd "$(dirname "$0")" && pwd)
VICT="${1:-$(cd "$AQUI/../../victron" 2>/dev/null && pwd || echo "")}"

[ -n "$VICT" ] && [ -f "$VICT/main/net/udp_tx.c" ] || {
    echo "ERROR: no encuentro el repo victron (main/net/udp_tx.c). Pasalo como argumento."; exit 2; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

python3 - "$AQUI/viaje_contadores_arnes.c" "$AQUI/.." "$VICT" "$TMP/t.c" <<'PYEXT'
import re, sys, pathlib

arnes    = pathlib.Path(sys.argv[1]).resolve()
repo_cab = pathlib.Path(sys.argv[2]).resolve()
repo_p4  = pathlib.Path(sys.argv[3]).resolve()
salida   = pathlib.Path(sys.argv[4])

udp_tx = (repo_p4 / 'main/net/udp_tx.c').read_text()
modelo = (repo_cab / 'main/data_model.c').read_text()

# ── Lo que MANDA la P4: el trozo de build_msg() que pasa de km y segundos a los
#    cuatro bytes. Se coge desde la primera linea del calculo hasta la ultima
#    asignacion, y se cambia "out->" por "src." (el struct del arnes).
m = re.search(r'^.*long km_dec = .*$', udp_tx, re.M)
if not m:
    raise SystemExit('no encuentro el calculo de km_dec en udp_tx.c de la P4')
i = m.start()
k = udp_tx.index('out->viaje_min_lo', i)
k = udp_tx.index('\n', k) + 1
encode = udp_tx[i:k].replace('out->', 'src.')
if 'viaje_km_hi' not in encode or 'viaje_min_lo' not in encode:
    raise SystemExit('el trozo sacado de udp_tx.c no tiene los cuatro bytes')

# ── Lo que RECOMPONE la cabina: las dos asignaciones de data_model.c.
m = re.search(r'^.*dst\.viaje_km_decimas.*$|^.*tmp\.viaje_km_decimas.*$', modelo, re.M)
if not m:
    raise SystemExit('no encuentro la recomposicion de viaje_km_decimas en data_model.c')
i = m.start()
k = modelo.index('viaje_min_lo);', i)
k = modelo.index('\n', k) + 1
decode = modelo[i:k].replace('msg->', 'src.').replace('tmp.', 'dst.')
if 'viaje_km_hi' not in decode or 'viaje_min_hi' not in decode:
    raise SystemExit('el trozo sacado de data_model.c no recompone los cuatro bytes')

texto = (arnes.read_text()
         .replace('__ENCODE__', encode)
         .replace('__DECODE__', decode))
salida.write_text(texto)
print(f"P4   : {len(encode.splitlines())} lineas de udp_tx.c")
print(f"cabina: {len(decode.splitlines())} lineas de data_model.c")
PYEXT

gcc -std=gnu17 -Wall -Wextra -I "$AQUI/../main/net" -o "$TMP/t" "$TMP/t.c"
"$TMP/t"

# ── 2. El cuentakilometros del cuadro en el cuerpo del INICIO ───────────────
#
# La cabina pide la lectura del cuadro al empezar un viaje y la manda en el
# inicio. Aqui se saca la funcion REAL que arma ese JSON (p4_api_cuerpo_inicio)
# y se comprueba: que con 0 NO viaje el campo (no se manda un dato que el
# usuario no ha dado), que con lectura viaje tal cual, y que el cuerpo quepa en
# el buffer de verdad (224) hasta con el destino mas largo y 6 cifras.
echo
echo "== el km del cuadro en el cuerpo del inicio =="
python3 - "$AQUI/.." "$TMP/inicio.c" <<'PYEXT2'
import re, sys, pathlib
repo = pathlib.Path(sys.argv[1]).resolve()
fuente = (repo / 'main/net/p4_api.c').read_text()
m = re.search(r'void p4_api_cuerpo_inicio\(char \*out, size_t n, uint32_t id,\s*\n\s*const char \*destino, uint32_t fecha_dias,\s*\n\s*uint32_t km_cuadro\)\s*\n\{', fuente)
if not m:
    raise SystemExit('no encuentro p4_api_cuerpo_inicio() en main/net/p4_api.c')
i = m.start(); k = fuente.index('{', i); n = 0
while True:
    if fuente[k] == '{': n += 1
    elif fuente[k] == '}':
        n -= 1
        if n == 0: break
    k += 1
fn = fuente[i:k+1]
plantilla = '''#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/* La identidad de la pantalla: en la placa es su MAC; aqui da igual cual sea,
 * lo que se comprueba es el resto del cuerpo. */
const char *p4_api_dev_id(void) { return "AABBCCDDEEFF"; }

__FN__

int main(int argc, char **argv)
{
    char cuerpo[224];
    uint32_t km = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 10) : 0;
    p4_api_cuerpo_inicio(cuerpo, sizeof(cuerpo), 4242,
                         "Un destino largo ya", 20739, km);
    printf("%s\\n", cuerpo);
    return 0;
}
'''
pathlib.Path(sys.argv[2]).write_text(plantilla.replace('__FN__', fn))
PYEXT2
gcc -std=gnu17 -Wall -Wextra -I "$AQUI/../main/net" -o "$TMP/inicio" "$TMP/inicio.c"

fallos=0
comprueba() {   # $1 = km, $2 = esperado ("si"/"no" lleva el campo), $3 = valor
    json=$("$TMP/inicio" "$1")
    echo "$json" | python3 -c "
import json, sys
d = json.load(sys.stdin)
assert d['op'] == 'inicio', d
assert d['id'] == 4242, d
assert d['destino'] == 'Un destino largo ya', d
assert d['fecha_dias'] == 20739, d
assert d['dev'] == 'AABBCCDDEEFF', d
lleva = 'km_cuadro' in d
quiere = '$2' == 'si'
assert lleva == quiere, ('km_cuadro' in d, quiere)
if quiere: assert d['km_cuadro'] == int('$3'), d
" 2>/dev/null && { echo "ok     km_cuadro=$1 -> $([ "$2" = si ] && echo "va con $3" || echo "no va (0 = sin lectura)")"; } \
      || { echo "FALLO  km_cuadro=$1 (se esperaba $2)"; echo "       $json"; fallos=1; }
}
comprueba 0 no ""
comprueba 123456 si 123456
comprueba 999999 si 999999

# Y que quepa: el destino mas largo (20) y 6 cifras es el peor caso.
largo=$("$TMP/inicio" 999999 | tr -d '\n' | wc -c)
if [ "$largo" -lt 224 ]; then
    echo "ok     el cuerpo mas largo mide $largo < 224 (cabe en el buffer del envio)"
else
    echo "FALLO  el cuerpo mide $largo y el buffer es 224"; fallos=1
fi

[ "$fallos" = 0 ] || { echo; echo "FALLOS: $fallos"; exit 1; }
