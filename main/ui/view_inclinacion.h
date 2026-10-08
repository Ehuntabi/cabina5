/* view_inclinacion.h - Pantalla de inclinacion/nivelacion al aparcar.
 *
 * Placeholder en la Fase 2: la Fase 3 conecta aqui la lectura real del
 * ADXL345 (bus I2C ya compartido, ver esp_bsp.c) y el indicador visual
 * tipo burbuja/aguja.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void view_inclinacion_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
