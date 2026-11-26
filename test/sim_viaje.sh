#!/usr/bin/env bash
# sim_viaje.sh - la vida de una salida, de punta a punta y sin placa (26-nov-2026).
#
# POR QUE EXISTE. Las piezas del cuaderno de viaje ya se prueban por separado,
# pero no habia nada que juntase el ESTADO de la salida con la COLA y el DIARIO
# y recorriese lo que le pasa a una salida de verdad: abrirla, declarar cuatro
# cosas a la vez, cortar la corriente, volver, cerrarlas en cualquier orden y
# terminar la salida. Los fallos que se buscan son los de esa union: ids que se
# pierden al reiniciar, un quinto evento que se cuela, un apunte contado dos
# veces al reintentar, el fin adelantando a los pendientes, o una duracion
# negativa al cambiar la hora.
#
# Este test NO prueba copias: compila TAL CUAL
#     main/salida.c
#     main/reloj.c
#     main/net/viaje_cola.c
#     main/net/viaje_diario.c   (con -DVIAJE_DIARIO_TEST: flash en RAM)
# y dobla solo lo de fuera (test/dobles/): NVS, FreeRTOS, esp_log, esp_timer y
# el POST a la P4. config_storage.c NO se compila (necesita esp_mac.h y media
# NVS real): las funciones de almacenamiento que usa salida.c se doblan en el
# arnes, con la misma firma que
# components/config_storage/config_storage.h.
#
# main/reloj.c SI se compila de verdad y no se dobla: el escenario 8 (cambio de
# hora) depende de su correccion (B19). El reloj se maneja desde el arnes con
# reloj_set_desde_p4(), igual que hace rx_task en la placa. Para compilarlo solo
# hacen falta los tipos de seccion critica (portMUX) del doble de FreeRTOS.
#
# LA CAPA DE UI (main/ui/view_registro.c) TAMPOCO SE COMPILA: el arnes reproduce
# su secuencia al cerrar un evento (id reservado, contado, push y borrar) y monta
# los cuerpos a mano como apunte.c. Lo que se prueba de verdad es salida.c,
# viaje_cola.c y viaje_diario.c; la UI solo se imita.
#
# gcc a un temporal y a correr. Codigo de salida 0 solo si pasan todos los
# casos: si uno falla, el resumen dice cuantos y el script termina en 1.
#
# Uso: bash test/sim_viaje.sh            (los logs de produccion, por stderr;
#                                        viaje_diario en modo test usa stdout)
#      bash test/sim_viaje.sh 2>/dev/null  (oculta los logs de esp_log, no los
#                                           de viaje_diario en modo test)
set -euo pipefail

AQUI=$(cd "$(dirname "$0")" && pwd)
RAIZ=$(cd "$AQUI/.." && pwd)

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# -I test/dobles ANTES que main y main/net: las cabeceras que se doblan (nvs.h,
# esp_log.h, esp_timer.h, freertos/*) se buscan ahi. Las de produccion que si
# son de verdad (salida.h, reloj.h, viaje_cola.h, viaje_diario.h, p4_api.h) se
# resuelven con -I main y -I main/net. config_storage.h se incluye la REAL: de
# ella solo se implementan las funciones que enlaza salida.c, asi que si cambia
# una firma, esto no compila.
gcc -std=gnu17 -Wall -Wextra -Werror -O1 \
    -DVIAJE_DIARIO_TEST \
    -I "$AQUI/dobles" -I "$RAIZ/main" -I "$RAIZ/main/net" \
    -I "$RAIZ/components/config_storage/include" \
    "$RAIZ/main/salida.c" \
    "$RAIZ/main/reloj.c" \
    "$RAIZ/main/net/viaje_cola.c" \
    "$RAIZ/main/net/viaje_diario.c" \
    "$AQUI/dobles/dobles.c" \
    "$AQUI/sim_viaje_arnes.c" \
    -o "$TMP/t"

"$TMP/t"
