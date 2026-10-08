/* Interfaz del BSP de cabina5 (placa Guition JC8048W550C, 5" 800x480).
 *
 * Adaptacion a esta pantalla: panel RGB paralelo, tactil GT911 por I2C y
 * brillo por LEDC. Esta interfaz es la del contrato habitual de
 * BSP de Espressif (arrancar pantalla, tactil, cerrojo de LVGL y brillo), que
 * es lo que va a necesitar la aplicacion que se monte encima.
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

/* DIAGNOSTICO: resume en el log lo que hay pintado en el framebuffer (porcentaje
 * de muestras negras y brillo medio). Sirve para distinguir "la UI no se pinta"
 * de "no se ve" (brillo, panel). Ver esp_bsp.c. */
void bsp_mirar_framebuffer(const char *etiqueta);

typedef struct {
    lvgl_port_cfg_t lvgl_port_cfg;  /*!< Configuracion del port de LVGL (tarea y tic) */
} bsp_display_cfg_t;

/* ── I2C ──────────────────────────────────────────────────────────────────── */
esp_err_t bsp_i2c_init(void);
i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void);
esp_err_t bsp_i2c_deinit(void);

/* ── Pantalla ─────────────────────────────────────────────────────────────── */
/* Arranca panel RGB + tactil + LVGL. La retroiluminacion se enciende al final
 * (y la ajusta luego brillo.c con el nivel guardado). Devuelve NULL si algo
 * falla. */
lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg);

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
