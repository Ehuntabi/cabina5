#!/usr/bin/env bash
# ¿Se puede perder un apunte del viaje? (10-nov-2026)
#
# El fin de semana del 7-8-nov el viaje no se pudo grabar. La cola de la NVS
# (16 huecos) era la UNICA copia. Ahora cada apunte se guarda antes en una
# particion propia de la flash (main/net/viaje_diario.c) y solo sale de ahi
# cuando la P4 confirma ese id.
#
# Este test compila el fichero DE PRODUCCION con -DVIAJE_DIARIO_TEST: usa 1 MB de
# flash simulada en RAM, pero el formato, el CRC y la compactacion son los
# mismos. Cubre los casos que de verdad han dolido: reinicio, corte a mitad de
# escritura, mitad llena y corte durante el commit de la compactacion.
set -euo pipefail

AQUI=$(cd "$(dirname "$0")" && pwd)
RAIZ=$(cd "$AQUI/.." && pwd)

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

gcc -std=gnu17 -Wall -Wextra -Werror -O1 -DVIAJE_DIARIO_TEST \
    -I "$RAIZ/main/net" \
    "$RAIZ/main/net/viaje_diario.c" "$AQUI/viaje_diario_arnes.c" \
    -o "$TMP/t"
"$TMP/t"
