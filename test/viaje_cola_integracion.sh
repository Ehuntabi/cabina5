#!/usr/bin/env bash
# ¿La cola y el diario son UNA sola cosa? (10-nov-2026)
#
# El 10-nov-2026 cada apunte paso a tener copia duradera en la flash
# (main/net/viaje_diario.c) y la cola de la NVS (main/net/viaje_cola.c) paso a
# ser un PREFIJO EXACTO de los pendientes del diario. De esa regla depende que el
# `fin` no adelante a los apuntes que solo estan en el diario: el fallo que se
# cazo ese dia fue que con la cola llena (14 apuntes) un apunte nuevo se quedaba
# solo en el diario, el `fin` entraba en los huecos reservados y se ponia DELANTE
# de el, y el viaje se cerraba en la P4 sin ese apunte y sin un solo aviso.
#
# Este test NO prueba una copia: compila los DOS ficheros DE PRODUCCION
#   - main/net/viaje_cola.c        (tal cual)
#   - main/net/viaje_diario.c      (con -DVIAJE_DIARIO_TEST: flash simulada en RAM)
# con dobles (test/dobles/) de lo de fuera: NVS (mapa en RAM que puede decir "no
# queda sitio"), FreeRTOS (xTaskCreate NO crea la tarea del repartidor: la llama
# el arnes), el POST a la P4 (guarda los cuerpos enviados y contesta el codigo
# que se le diga), el log y el reloj.
#
# Cubre: el alta (diario + cola, y solo diario con la cola llena), la INVARIANTE
# DE ORDEN (el fin no adelanta a nadie), la reconstruccion al arrancar (rescate
# de lo que solo estaba en la NVS y relleno desde el diario), el numero de
# `viaje_cola_sin_grabar()`, y la entrega (200 -> entregado) y el descarte a
# mano (-> DESCARTADO, sin resucitar).
set -euo pipefail

AQUI=$(cd "$(dirname "$0")" && pwd)
RAIZ=$(cd "$AQUI/.." && pwd)

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# -I test/dobles ANTES que main/net: las cabeceras que se doblan (nvs.h,
# esp_log.h, esp_timer.h, freertos/*) se buscan ahi. Las de produccion que si
# son de verdad (viaje_cola.h, viaje_diario.h) las resuelve el propio .c, y
# p4_api.h se incluye la REAL: de ella solo se implementa p4_api_post_resp() en
# test/dobles/dobles.c, asi que si cambia la firma, esto no compila.
gcc -std=gnu17 -Wall -Wextra -Werror -O1 \
    -DVIAJE_DIARIO_TEST \
    -I "$AQUI/dobles" -I "$RAIZ/main/net" \
    "$RAIZ/main/net/viaje_cola.c" \
    "$RAIZ/main/net/viaje_diario.c" \
    "$AQUI/dobles/dobles.c" \
    "$AQUI/viaje_cola_integracion_arnes.c" \
    -o "$TMP/t"

"$TMP/t"
