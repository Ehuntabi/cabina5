#!/usr/bin/env bash
# Las decisiones de la actualizacion OTA, sin placa (17-oct-2026)
#
# La pantalla ya se actualiza sola desde Ajustes (main/net/ota_rx.c) y, desde el
# 17-oct-2026, tambien sola cada 60 s si la P4 anuncia una version nueva. Antes
# de marcar la version nueva para arrancar, ota_rx.c decide cuatro cosas, y las
# cuatro se pueden equivocar en silencio:
#
#   1. si la version anunciada es MAS NUEVA que la que corre -- comparando
#      NUMEROS, no texto: "v1.9" es mas vieja que "v1.10", al reves que como
#      texto;
#   2. si la version EMBEBIDA en el .bin es la anunciada (si la P4 sirve otro
#      fichero, se instala el que no es);
#   3. si el sha256 calculado al vuelo coincide con el anunciado (un byte
#      cambiado por el camino = un firmware que no arranca);
#   4. si el automatico puede preguntar ahora y si, con la version nueva ya en
#      la mano, puede instalar (sin red, con viaje abierto, con una descarga en
#      marcha o con el usuario escribiendo, NO).
#
# Esto saca el bloque "INICIO/FIN BLOQUE PRUEBA" del ota_rx.c REAL y lo compila
# en el PC, con el mismo metodo que los tests del viaje. Un error aqui no se ve
# hasta que la placa se queda con un firmware que no arranca, y entonces ya no
# hay pantalla donde mirar.
#
# Uso: bash test/ota_version.sh
set -euo pipefail

AQUI=$(cd "$(dirname "$0")" && pwd)

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

python3 - "$AQUI/ota_version_arnes.c" "$AQUI/../main/net/ota_rx.c" "$TMP/t.c" <<'PYEXT'
import pathlib, sys

arnes = pathlib.Path(sys.argv[1]).read_text()
fuente = pathlib.Path(sys.argv[2]).read_text()

# Los dos marcadores son comentarios del propio ota_rx.c. Si alguien los mueve o
# los borra, esto falla aqui y no en la placa.
try:
    ini = fuente.index('INICIO BLOQUE PRUEBA')
    fin = fuente.index('FIN BLOQUE PRUEBA')
except ValueError:
    raise SystemExit('no encuentro los marcadores "INICIO/FIN BLOQUE PRUEBA" en main/net/ota_rx.c')

# El marcador de arriba vive DENTRO de un comentario: el bloque empieza en la
# linea siguiente al cierre del comentario ("*/"). Y se corta por LINEAS
# COMPLETAS, hasta el principio de la linea del marcador de abajo.
cierre = fuente.index('*/', ini) + 2
bloque = fuente[fuente.index('\n', cierre) + 1:fuente.rindex('\n', 0, fin)]
if 'ota_rx_version_es_mas_nueva' not in bloque or 'ota_rx_sha_coincide' not in bloque:
    raise SystemExit('el bloque de pruebas de ota_rx.c no trae las funciones que se esperan')

pathlib.Path(sys.argv[3]).write_text(arnes.replace('__FN__', bloque))
PYEXT

gcc -std=gnu17 -Wall -Wextra -o "$TMP/t" "$TMP/t.c"
"$TMP/t"
