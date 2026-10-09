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
/* ARRANCA SIEMPRE AL MAXIMO, y el valor guardado NO se aplica al encender.
 *
 * POR QUE (9-oct-2026, con la placa delante y el usuario diciendo "la pantalla
 * esta en negro"): al pasar de dos niveles a cinco, el arranque se puso "el paso
 * de en medio" (60 %) y con el tema oscuro de esta interfaz eso se ve como una
 * pantalla apagada -- es el mismo fallo que ya paso el 8-oct con el 30 %, que
 * esta escrito en el README ("La pantalla esta negra -> mirar el brillo antes
 * que nada"). Y como el valor se recuerda, un arranque a oscuras se quedaba
 * grabado y volvia a pasar en el siguiente encendido: la pantalla se apagaba
 * sola y no habia forma de salir sin saber donde tocar.
 *
 * LA REGLA AHORA: la luz NUNCA puede depender de lo que quedo guardado. Al
 * encender, 100 %. Si te pasas bajando, se desenchufa y vuelve -- siempre hay
 * una salida. Lo que se guarda sigue sirviendo para recordar por donde ibas
 * dentro de la sesion (y para el contraste de view_info.c). */
void brillo_init(void)
{
    /* Se sigue leyendo para dejar constancia en el log de lo que habia
     * guardado: si el usuario dice "arranco apagada", el log lo dice. */
    uint8_t guardado = 0;
    if (load_brightness(&guardado) != ESP_OK) guardado = 0;

    s_paso = BRILLO_NIVELES_N - 1;                  /* el maximo */

    ESP_LOGI(TAG, "Brillo inicial %u%% (paso %d de %d; en NVS habia %u%%, no se aplica)",
             (unsigned)s_pasos[s_paso], s_paso + 1, BRILLO_NIVELES_N,
             (unsigned)guardado);
    bsp_display_brightness_set(s_pasos[s_paso]);
}

uint8_t brillo_nivel(void) { return s_pasos[s_paso]; }

uint8_t brillo_alternar(void)
{
    /* HACIA ABAJO y dando la vuelta: 100 -> 80 -> 60 -> 40 -> 20 -> 100. Al
     * reves que antes (que subia), porque ahora se arranca arriba: lo primero
     * que se quiere al tocar es bajar el deslumbramiento, no subirlo. */
    s_paso = (s_paso + BRILLO_NIVELES_N - 1) % BRILLO_NIVELES_N;
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
