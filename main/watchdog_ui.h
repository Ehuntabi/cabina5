/* watchdog_ui.h — pausa del vigilante de la interfaz.
 *
 * El vigilante ("WDOG: UI sin avanzar") da la placa por colgada si LVGL no
 * avanza en 60 s y deja saltar el Task WDT: reinicia. La actualizacion por
 * Wi-Fi PARA LVGL a proposito mientras descarga, asi que sin esta pausa el
 * vigilante mataba la placa a mitad de la descarga y la OTA grande nunca
 * terminaba (23-nov-2026). Lo usa main/net/ota_rx.c.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* true mientras la UI este parada A PROPOSITO (descarga de la OTA). */
void app_watchdog_ui_pausar(bool pausar);

#ifdef __cplusplus
}
#endif
