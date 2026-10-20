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
# CON EL BAJO. Los dos numeros siguen pareciendo numeros -- un viaje de 1200,0 km
# (12.000 decimas = 0x2EE0) se lee como 57.390 decimas = 5.739,0 km -- y no se
# descubre hasta que uno mira el cuaderno en casa,
# semanas despues. Aqui se comprueba el camino entero SIN PLACA: se saca de
# udp_tx.c de la P4 el trozo que parte los contadores, de data_model.c de la
# cabina el que los recompone, y se les pasan valores.
#
# Uso: bash test/viaje_contadores.sh [ruta del repo victron]
set -euo pipefail

AQUI=$(cd "$(dirname "$0")" && pwd)

# DONDE ESTA EL REPO DE LA P4. No vale una ruta fija: en el PC los tres repos son
# hermanos (~/joint/victron), y en el CI depende de si el propio repo se bajo en
# la raiz del workspace (cabina5: <raiz>/35cabina/test) o en una subcarpeta
# (35cabina: <raiz>/35cabina/35cabina/test). El 16-oct-2026 esto hizo fallar el
# CI de la 3,5" con "no encuentro el repo victron" teniendolo al lado.
VICT=""
for c in "${1:-}" "$AQUI/../../victron" "$AQUI/../victron" "$PWD/victron" "$PWD/../victron"; do
    [ -n "$c" ] && [ -f "$c/main/net/udp_tx.c" ] && { VICT=$(cd "$c" && pwd); break; }
done

[ -n "$VICT" ] || {
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

# ── Lo que MANDA la P4: el BLOQUE ENTERO de build_msg() que pasa de km y
#    segundos a los cuatro bytes, CON SU if/else, y se cambia "out->" por "src."
#    (el struct del arnes).
#
#    EMPIEZA EN EL if (t.active), NO EN "long km_dec", y eso importa (16-oct-2026):
#    empezando en el calculo solo se ejecutaba la rama de "hay viaje abierto", asi
#    que la rama del "sin dato" -- la que manda el centinela 0xFFFF para que la
#    pantalla enseñe "--" -- NO la tocaba ninguna prueba. Cambiar ahi el centinela
#    por ceros (y que la pantalla diga "0,0 km / 0:00" de un viaje que no existe)
#    pasaba el test en verde. Lo comprobo un arbitraje el 16-oct-2026 saboteando
#    justo esa rama.
m = re.search(r'^[ \t]*if \(t\.active\) \{[ \t]*$', udp_tx, re.M)
if not m:
    raise SystemExit('no encuentro el if (t.active) de los contadores en udp_tx.c')
i = m.start()
# Se cuenta desde el PROPIO if y, al cuadrar las llaves, se mira si detras viene
# un "else": el bloque es if/else y hay que llevarse las DOS ramas. Parando en el
# primer cierre (lo que hacia antes) solo entraba la rama del "hay viaje".
nivel = 0
k = None
pos = i
while pos < len(udp_tx):
    c = udp_tx[pos]
    if c == '{':
        nivel += 1
    elif c == '}':
        nivel -= 1
        if nivel == 0:
            if udp_tx[pos + 1:pos + 40].lstrip().startswith('else'):
                pos += 1
                continue
            k = pos + 1
            break
    pos += 1
if k is None:
    raise SystemExit('no encuentro el cierre del bloque de contadores en udp_tx.c')
encode = udp_tx[i:k].replace('out->', 'src.')
if 'viaje_km_hi' not in encode or 'viaje_min_lo' not in encode:
    raise SystemExit('el trozo sacado de udp_tx.c no tiene los cuatro bytes')
# Y que de verdad venga el else del centinela: si alguien vuelve a estrechar la
# extraccion, esto lo dice en vez de dejar la rama sin cubrir en silencio.
if 'MINI_NO_DATA_U16' not in encode or 'else' not in encode:
    raise SystemExit('el trozo sacado de udp_tx.c NO trae la rama del centinela')

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
sys.path.insert(0, str(pathlib.Path(sys.argv[1]).resolve() / 'test'))
from extrae_cuerpo import desde
repo = pathlib.Path(sys.argv[1]).resolve()
fuente = (repo / 'main/net/p4_api.c').read_text()
m = re.search(r'void p4_api_cuerpo_inicio\(char \*out, size_t n, uint32_t id,\s*\n\s*const char \*destino, uint32_t fecha_dias,\s*\n\s*uint32_t km_cuadro\)\s*\n\{', fuente)
if not m:
    raise SystemExit('no encuentro p4_api_cuerpo_inicio() en main/net/p4_api.c')
fn = desde(fuente, m)
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

# ── 3. El cuentakilometros del cuadro en el cuerpo del CIERRE ──────────────
#
# Al cerrar el viaje se pide tambien la lectura del cuadro, y va en el fin. Aqui
# se comprueba lo que de verdad puede romperse: que 'destino' y 'km_cuadro' son
# opcionales POR SEPARADO (cuatro combinaciones) y que el JSON sale bien en las
# cuatro. Con la cadena de formato variable, un %lu leyendo un char* no falla al
# compilar: falla en la placa.
echo
echo "== el km del cuadro en el cuerpo del cierre =="
python3 - "$AQUI/.." "$TMP/fin.c" <<'PYEXT3'
import re, sys, pathlib
sys.path.insert(0, str(pathlib.Path(sys.argv[1]).resolve() / 'test'))
from extrae_cuerpo import desde
repo = pathlib.Path(sys.argv[1]).resolve()
fuente = (repo / 'main/net/p4_api.c').read_text()
m = re.search(r'void p4_api_cuerpo_fin\(char \*out, size_t n, uint32_t id, uint32_t eventos,\s*\n\s*const char \*destino, uint32_t km_cuadro\)\s*\n\{', fuente)
if not m:
    raise SystemExit('no encuentro p4_api_cuerpo_fin() en main/net/p4_api.c')
fn = desde(fuente, m)
plantilla = '''#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

const char *p4_api_dev_id(void) { return "AABBCCDDEEFF"; }

__FN__

int main(int argc, char **argv)
{
    char cuerpo[224];
    const char *dest = (argc > 1 && argv[1][0]) ? argv[1] : NULL;
    uint32_t km = (argc > 2) ? (uint32_t)strtoul(argv[2], NULL, 10) : 0;
    p4_api_cuerpo_fin(cuerpo, sizeof(cuerpo), 77, 12, dest, km);
    printf("%s\\n", cuerpo);
    return 0;
}
'''
pathlib.Path(sys.argv[2]).write_text(plantilla.replace('__FN__', fn))
PYEXT3
gcc -std=gnu17 -Wall -Wextra -I "$AQUI/../main/net" -o "$TMP/fin" "$TMP/fin.c"

fallos2=0
comprueba_fin() {   # $1 destino ("" = ninguno), $2 km, $3 "si"/"no" destino, $4 "si"/"no" km
    json=$("$TMP/fin" "$1" "$2")
    echo "$json" | python3 -c "
import json, sys
d = json.load(sys.stdin)
assert d['op'] == 'fin', d
assert d['id'] == 77, d
assert d['eventos'] == 12, d
assert d['dev'] == 'AABBCCDDEEFF', d
assert ('destino' in d) == ('$3' == 'si'), d
assert ('km_cuadro' in d) == ('$4' == 'si'), d
if '$3' == 'si': assert d['destino'] == '$1', d
if '$4' == 'si': assert d['km_cuadro'] == int('$2'), d
" 2>/dev/null && echo "ok     destino='$1' km=$2 -> $([ "$3" = si ] && echo con || echo sin) destino, $([ "$4" = si ] && echo con || echo sin) km" \
      || { echo "FALLO  destino='$1' km=$2"; echo "       $json"; fallos2=1; }
}
comprueba_fin ""        0      no no
comprueba_fin "Tarifa"  0      si no
comprueba_fin ""        123789 no si
comprueba_fin "Tarifa"  123789 si si

# Y que quepa: destino de 20 + 6 cifras es el peor caso del cierre.
largo=$("$TMP/fin" "Un destino largo ya" 999999 | tr -d '\n' | wc -c)
if [ "$largo" -lt 160 ]; then
    echo "ok     el fin mas largo mide $largo < 160 (cabe en el buffer del cierre)"
else
    echo "FALLO  el fin mide $largo y el buffer es 160"; fallos2=1
fi

[ "$fallos2" = 0 ] || { echo; echo "FALLOS: $fallos2"; exit 1; }
