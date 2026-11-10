/* esp_err.h — DOBLE para compilar main/net/viaje_cola.c en el PC
 * (ver test/viaje_cola_integracion.sh).
 *
 * Solo lo que esos ficheros de produccion usan: esp_err_t, ESP_OK y los codigos
 * de la NVS que aparecen en su logica. Los valores son los de ESP-IDF 5.5
 * (components/nvs_flash/include/nvs.h: ESP_ERR_NVS_BASE = 0x1100 y sus
 * desplazamientos), para que un log del test diga lo mismo que diria la placa.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t esp_err_t;

#define ESP_OK    0
#define ESP_FAIL  (-1)

#define ESP_ERR_INVALID_ARG              0x102

#define ESP_ERR_NVS_BASE                 0x1100
#define ESP_ERR_NVS_NOT_FOUND            (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE     (ESP_ERR_NVS_BASE + 0x05)
#define ESP_ERR_NVS_INVALID_HANDLE       (ESP_ERR_NVS_BASE + 0x07)
#define ESP_ERR_NVS_READ_ONLY            (ESP_ERR_NVS_BASE + 0x08)
#define ESP_ERR_NVS_TYPE_MISMATCH        (ESP_ERR_NVS_BASE + 0x0a)
#define ESP_ERR_NVS_VALUE_TOO_LONG       (ESP_ERR_NVS_BASE + 0x0b)
#define ESP_ERR_NVS_INVALID_LENGTH       (ESP_ERR_NVS_BASE + 0x0c)
#define ESP_ERR_NVS_NO_FREE_PAGES        (ESP_ERR_NVS_BASE + 0x0d)

const char *esp_err_to_name(esp_err_t code);

#ifdef __cplusplus
}
#endif
