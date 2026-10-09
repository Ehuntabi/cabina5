/* brillo.c - Brillo de la retroiluminacion EN PASOS, con rampa y guardado en NVS.
 *
 * La persistencia NO se hace aqui a mano: se usa config_storage, que es donde
 * guarda sus cosas todo el resto del proyecto (salida, inclinacion, wifi, viaje,
 * parada).
 *
 * QUE CAMBIO EL 9-oct-2026, y por que: antes eran DOS niveles (60 % y 100 %) y
 * al pulsar el boton el cambio era de golpe. El usuario lo describio asi: "el
 * cambio de iluminacion no funciona bien". Dos problemas:
 *
 *   1. Solo dos valores: de noche el 60 seguia siendo mucho y al sol no habia
 *      nada por encima.
 *   2. El salto era seco. Un cambio de 60 a 100 de golpe se ve como un
 *      parpadeo, no como subir la luz.
 *
 * Ahora son CINCO pasos (20/40/60/80/100) y el cambio se aplica con una RAMPA de
 * 8 tramos de 15 ms. La rampa va con esp_timer y no con una animacion de LVGL a
 * proposito: asi sigue funcionando aunque la tarea de LVGL este ocupada pintando
 * (que es justo cuando se pulsa el boton).
 */
#include "brillo.h"

#include "config_storage.h"
#include "display.h"
#include "esp_bsp.h"   /* bsp_display_brightness_set() / _get() */
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "brillo";

/* Los pasos, y en cual estamos. Lo que se guarda es el PORCENTAJE, asi que la
 * lista se puede cambiar sin invalidar lo que ya hay en NVS: si el valor
 * guardado no coincide con ningun paso, se coge el mas parecido. */
static const uint8_t s_pasos[BRILLO_NIVELES_N] = BRILLO_NIVELES;
static int s_paso = BRILLO_NIVELES_N / 2;

/* ── La rampa ─────────────────────────────────────────────────────────────── */
#define RAMPA_PASOS   8
#define RAMPA_MS      15

static esp_timer_handle_t s_rampa = NULL;
static int s_rampa_desde, s_rampa_hasta, s_rampa_i;

static void rampa_cb(void *arg)
{
    (void)arg;
    s_rampa_i++;
    if (s_rampa_i >= RAMPA_PASOS) {
        bsp_display_brightness_set(s_rampa_hasta);
        esp_timer_stop(s_rampa);
        return;
    }
    bsp_display_brightness_set(s_rampa_desde +
                               (s_rampa_hasta - s_rampa_desde) * s_rampa_i / RAMPA_PASOS);
}

static void aplicar_con_rampa(int pct)
{
    s_rampa_desde = bsp_display_brightness_get();
    s_rampa_hasta = pct;
    s_rampa_i = 0;

    if (!s_rampa) {
        const esp_timer_create_args_t args = {
            .callback = rampa_cb,
            .name = "brillo_rampa",
        };
        if (esp_timer_create(&args, &s_rampa) != ESP_OK) {
            bsp_display_brightness_set(pct);   /* sin rampa, al menos que cambie */
            return;
        }
    }
    esp_timer_stop(s_rampa);
    esp_timer_start_periodic(s_rampa, RAMPA_MS * 1000);
}

/* ── API ──────────────────────────────────────────────────────────────────── */
void brillo_init(void)
{
    uint8_t v = 0;

    if (load_brightness(&v) == ESP_OK && v >= s_pasos[0] &&
        v <= s_pasos[BRILLO_NIVELES_N - 1]) {
        /* El mas parecido a lo guardado (por si la lista de pasos cambio) */
        int mejor = 0, mejor_dif = 1000;
        for (int i = 0; i < BRILLO_NIVELES_N; i++) {
            const int dif = (s_pasos[i] > v) ? (s_pasos[i] - v) : (v - s_pasos[i]);
            if (dif < mejor_dif) { mejor_dif = dif; mejor = i; }
        }
        s_paso = mejor;
    } else {
        /* Primera vez (o valor imposible): el de en medio, y se corrige EN
         * DISCO para que lo guardado no diga una cosa mientras la pantalla hace
         * otra. */
        s_paso = BRILLO_NIVELES_N / 2;
        save_brightness(s_pasos[s_paso]);
    }

    ESP_LOGI(TAG, "Brillo inicial %u%% (paso %d de %d)",
             (unsigned)s_pasos[s_paso], s_paso + 1, BRILLO_NIVELES_N);
    bsp_display_brightness_set(s_pasos[s_paso]);
}

uint8_t brillo_nivel(void) { return s_pasos[s_paso]; }

uint8_t brillo_alternar(void)
{
    s_paso = (s_paso + 1) % BRILLO_NIVELES_N;      /* del maximo vuelve al minimo */
    aplicar_con_rampa(s_pasos[s_paso]);

    /* Se escribe solo al cambiarlo, no periodicamente: son dos toques de vez en
     * cuando, no hay riesgo de desgastar la flash. */
    if (save_brightness(s_pasos[s_paso]) != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar el brillo");
    }
    ESP_LOGI(TAG, "Brillo -> %u%% (paso %d de %d)",
             (unsigned)s_pasos[s_paso], s_paso + 1, BRILLO_NIVELES_N);
    return s_pasos[s_paso];
}
