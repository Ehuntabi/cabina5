/* view_ajustes.h - Pantalla de ajustes Wi-Fi (SSID/password de la P4).
 *
 * Permite cambiar de P4 (ej. la de repuesto para pruebas) sin reflashear:
 * escribe las credenciales nuevas, se guardan en NVS y reconecta.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void view_ajustes_create(lv_obj_t *parent);

/* Vuelve a leer las credenciales actuales (udp_rx_get_credentials) y
 * refresca los campos -- llamar justo antes de mostrar la pantalla,
 * porque se crea en nav_init() ANTES de que udp_rx_start() cargue las
 * credenciales reales de NVS. */
void view_ajustes_refresh(void);

#ifdef __cplusplus
}
#endif
