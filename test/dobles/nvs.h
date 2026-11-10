/* nvs.h — DOBLE de la NVS para compilar main/net/viaje_cola.c en el PC
 * (ver test/viaje_cola_integracion.sh).
 *
 * Las firmas son las de ESP-IDF 5.5 (components/nvs_flash/include/nvs.h) para
 * que el test llame a lo mismo que llamara la placa: nvs_open con
 * nvs_open_mode_t y nvs_handle_t de verdad, nvs_get_str con el truco de
 * out_value = NULL (que solo pregunta el tamaño), etc.
 *
 * Lo que hay detras (test/dobles/dobles.c) es un MAPA EN RAM. No simula la
 * paginacion ni los sectores de la NVS: lo que se prueba aqui es la integracion
 * de la cola con el diario, no la NVS, que ya tiene su propio comportamiento en
 * la placa. Lo que si puede hacer el mapa es negarse a escribir (devolver
 * ESP_ERR_NVS_NOT_ENOUGH_SPACE a voluntad), que es el unico fallo de la NVS del
 * que depende la logica bajo prueba.
 */
#pragma once

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t nvs_handle_t;

typedef enum {
    NVS_READONLY,
    NVS_READWRITE,
} nvs_open_mode_t;

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
                   nvs_handle_t *out_handle);
void      nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);

esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value);
esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value);

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value,
                      size_t *length);

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);

#ifdef __cplusplus
}
#endif
