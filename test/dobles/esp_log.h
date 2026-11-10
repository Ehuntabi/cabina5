/* esp_log.h — DOBLE del log para compilar main/net/viaje_cola.c en el PC
 * (ver test/viaje_cola_integracion.sh).
 *
 * POR QUE LOS MENSAJES VAN A stderr: el arnes escribe su informe (los "ok" y
 * los "FALLO") en stdout. Los logs de produccion son ruidosos a proposito
 * ("encolado #14", "la cola se reinicio"...), y si fueran a stdout taparian el
 * resultado. En stderr se siguen viendo al depurar y se pueden separar:
 *
 *     bash test/viaje_cola_integracion.sh 2>/dev/null   # solo el informe
 *
 * Los tres niveles se imprimen (no hay filtro): este test no llega a la placa y
 * lo que interesa es poder leer QUE dijo el codigo cuando algo falla.
 */
#pragma once

#include <stdio.h>

#define ESP_LOGE(tag, fmt, ...) \
    fprintf(stderr, "E %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) \
    fprintf(stderr, "W %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) \
    fprintf(stderr, "I %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) \
    fprintf(stderr, "D %s: " fmt "\n", tag, ##__VA_ARGS__)
