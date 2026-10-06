/* Interfaz del BSP de cabina5 (placa Guition JC8048W550C).
 *
 * Se mantiene A PROPOSITO igual que la del satelite 3,5" (main/esp_bsp.h):
 * asi, cuando se copie la app (net/, data_model, reloj, salida, UI), encuentre
 * las mismas funciones que ya usa y no haya que tocar la logica. Lo que cambia
 * por dentro es todo: aqui el panel es RGB, no QSPI.
 */
#pragma once

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

/* Bus I2C compartido (tactil + lo que se cuelgue despues: acelerometro, etc.) */
#define BSP_I2C_CLK_SPEED_HZ    400000

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lvgl_port_cfg_t lvgl_port_cfg;  /*!< Configuracion del port de LVGL */
    uint32_t buffer_size;           /*!< Tamano del buffer de pantalla en pixeles */
    lv_disp_rot_t rotate;           /*!< Rotacion (el panel ya es horizontal) */
} bsp_display_cfg_t;

/* ── I2C ──────────────────────────────────────────────────────────────────── */
esp_err_t bsp_i2c_init(void);
i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void);
esp_err_t bsp_i2c_deinit(void);

/* ── Pantalla ─────────────────────────────────────────────────────────────── */
/* Arranca panel RGB + tactil + LVGL. La retroiluminacion se enciende al final
 * (y la ajusta luego brillo.c con el nivel guardado). Devuelve NULL si algo
 * falla. */
lv_disp_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg);

/* Indice del LVGL (tactil). Ya queda listo dentro del arranque. */
lv_indev_t *bsp_display_get_input_dev(void);

/* Mutex de LVGL (lo usa la app para tocar la UI desde otras tareas). */
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);

/* ── Brillo ───────────────────────────────────────────────────────────────── */
esp_err_t bsp_display_brightness_set(int brightness_percent);
int bsp_display_brightness_get(void);
esp_err_t bsp_display_backlight_off(void);
esp_err_t bsp_display_backlight_on(void);

#ifdef __cplusplus
}
#endif
