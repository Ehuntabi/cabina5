/* brillo.c - Brillo en dos niveles, guardado en NVS.
 *
 * La persistencia NO se hace aqui a mano: se usa config_storage, que es donde
 * guarda sus cosas todo el resto del proyecto (salida, inclinacion, wifi,
 * viaje, parada). load_brightness()/save_brightness() ya existian desde el fork
 * anterior y no las llamaba nadie.
 */
#include "brillo.h"

#include "config_storage.h"
#include "display.h"
#include "esp_bsp.h"   /* bsp_display_brightness_set() */
#include "esp_log.h"

static const char *TAG = "brillo";

static uint8_t s_nivel = BRILLO_ALTO;

void brillo_init(void)
{
    uint8_t v = 0;
    /* Se valida contra los dos valores permitidos en vez de aceptar cualquier
     * 0-100: config_storage escribe un 5 por defecto la primera vez (herencia
     * del fork viejo, que arrancaba asi), y un 5 deja la pantalla practicamente
     * apagada. Si lo leido no vale, se corrige EN DISCO para que lo guardado no
     * siga diciendo una cosa mientras la pantalla hace otra. */
    if (load_brightness(&v) == ESP_OK && (v == BRILLO_BAJO || v == BRILLO_ALTO)) {
        s_nivel = v;
    } else {
        save_brightness(s_nivel);
    }

    ESP_LOGI(TAG, "Brillo inicial %u%%", s_nivel);
    bsp_display_brightness_set(s_nivel);
}

uint8_t brillo_nivel(void) { return s_nivel; }

uint8_t brillo_alternar(void)
{
    s_nivel = (s_nivel == BRILLO_ALTO) ? BRILLO_BAJO : BRILLO_ALTO;
    bsp_display_brightness_set(s_nivel);

    /* Se escribe solo al cambiarlo, no periodicamente: son dos toques de vez en
     * cuando, no hay riesgo de desgastar la flash. */
    if (save_brightness(s_nivel) != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar el brillo");
    }
    ESP_LOGI(TAG, "Brillo -> %u%%", s_nivel);
    return s_nivel;
}
