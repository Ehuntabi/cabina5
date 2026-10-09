/* brillo.c - Brillo de la retroiluminacion en DOS niveles, con rampa y guardado.
 *
 * La persistencia NO se hace aqui a mano: se usa config_storage, que es donde
 * guarda sus cosas todo el resto del proyecto (salida, inclinacion, wifi, viaje,
 * parada).
 *
 * ── HISTORIA, para no volver a dar las mismas vueltas (9-oct-2026) ──────────
 *
 * El usuario describio el brillo original (dos niveles, 60 y 100, y cambio de
 * golpe) como "el cambio de iluminacion no funciona bien". Se probaron CINCO
 * pasos (20/40/60/80/100) y el resultado fue una pantalla negra: el 6o paso
 * (60 %) NO se ve con el tema oscuro de esta interfaz. Leccion: cinco pasos solo
 * valen si los de abajo se ven, y aqui no se ven.
 *
 * El usuario corto por lo sano: "el cambio de iluminacion que sea o 100% o 30%".
 * Asi que son DOS (ver BRILLO_NIVELES en brillo.h) y lo unico que se queda de
 * todo aquello es LA RAMPA, que es lo que de verdad estaba roto: un salto de 100
 * a 30 de golpe se ve como un parpadeo, no como bajar la luz.
 *
 * La rampa va con esp_timer y no con una animacion de LVGL a proposito: asi
 * sigue funcionando aunque la tarea de LVGL este ocupada pintando (que es justo
 * cuando se pulsa el boton).
 */
#include "brillo.h"

#include "config_storage.h"
#include "display.h"
#include "esp_bsp.h"   /* bsp_display_brightness_set() / _get() */
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "brillo";

/* Los pasos, y en cual estamos. Lo que se guarda es el PORCENTAJE, asi que la
 * lista se puede cambiar sin invalidar lo que ya hay en NVS. */
static const uint8_t s_pasos[BRILLO_NIVELES_N] = BRILLO_NIVELES;
static int s_paso = BRILLO_NIVELES_N - 1;      /* el maximo */

/* ── La rampa ─────────────────────────────────────────────────────────────── */
#define RAMPA_PASOS   8
#define RAMPA_MS      20

static esp_timer_handle_t s_rampa = NULL;
static volatile int s_rampa_hasta = BRILLO_ALTO;   /* destino; lo lee el callback */

/* Un tic de la rampa: acerca la luz UN paso al destino.
 *
 * ── POR QUE NO SE ARRANCA NI SE PARA (9-oct-2026) ───────────────────────────
 *
 * El usuario dijo "el cambio de brillo no siempre funciona", y tenia razon.
 * Antes, cada toque hacia esp_timer_stop() y esp_timer_start_periodic() SIN
 * MIRAR lo que devolvian: si el temporizador estaba ejecutando este callback en
 * ese instante, el stop FALLA (ESP_ERR_INVALID_STATE, no espera a que acabe) y
 * el start de despues tambien, asi que el brillo nuevo no se aplicaba nunca y
 * el toque se perdia en silencio.
 *
 * Ahora el temporizador esta SIEMPRE en marcha (lo arranca brillo_init) y el
 * callback no arranca ni para nada: mira donde esta la luz, mira el destino, y
 * da un paso. Sin stop ni start no hay carrera posible, y un toque que llegue
 * en mal momento solo hace que la rampa cambie de destino a mitad de camino --
 * que es justo lo que se quiere.
 *
 * El coste es un tic cada 20 ms que casi siempre no hace nada (dos enteros y
 * una comparacion). */
static void rampa_cb(void *arg)
{
    (void)arg;
    const int actual = bsp_display_brightness_get();
    const int hasta  = s_rampa_hasta;
    if (actual == hasta) return;

    int delta = hasta - actual;
    int paso  = delta / RAMPA_PASOS;
    if (paso == 0) paso = (delta > 0) ? 1 : -1;   /* el ultimo tramo, de uno en uno */
    bsp_display_brightness_set(actual + paso);
}

/* Deja la luz en 'pct' con una rampa suave. Solo apunta el destino: del resto
 * se encarga el tic. */
static void aplicar_con_rampa(int pct)
{
    s_rampa_hasta = pct;
    if (!s_rampa) {
        /* Solo puede pasar si brillo_init() no llego a crear el temporizador:
         * mas vale un salto que un toque que no hace nada. */
        bsp_display_brightness_set(pct);
    }
}

/* ── API ──────────────────────────────────────────────────────────────────── */
/* ARRANCA SIEMPRE AL MAXIMO, y el valor guardado NO se aplica al encender.
 *
 * POR QUE (9-oct-2026, con la placa delante y el usuario diciendo "la pantalla
 * esta en negro"): en la prueba de los cinco pasos el arranque se puso "el paso
 * de en medio" (60 %) y con el tema oscuro de esta interfaz eso se ve como una
 * pantalla apagada -- es el mismo fallo que ya paso el 8-oct con el 30 %, que
 * esta escrito en el README ("La pantalla esta negra -> mirar el brillo antes
 * que nada"). Y como el valor se recuerda, un arranque a oscuras se quedaba
 * grabado y volvia a pasar en el siguiente encendido: la pantalla se apagaba
 * sola y no habia forma de salir sin saber donde tocar.
 *
 * LA REGLA: la luz NUNCA puede depender de lo que quedo guardado. Al encender,
 * 100 %. Si te pasas bajando al 30 % y no ves nada, se desenchufa y vuelve --
 * siempre hay una salida. Lo que se guarda sigue sirviendo para el contraste de
 * view_info.c y para dejar constancia en el log de por donde ibas. */
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

    s_rampa_hasta = s_pasos[s_paso];
    bsp_display_brightness_set(s_pasos[s_paso]);

    /* El tic de la rampa, EN MARCHA DESDE AQUI Y PARA SIEMPRE (ver rampa_cb:
     * arrancarlo y pararlo en cada toque es lo que hacia que el cambio de brillo
     * no siempre funcionara). Si no se puede crear, se sigue funcionando sin
     * rampa: aplicar_con_rampa() aplica el valor de golpe. */
    const esp_timer_create_args_t args = { .callback = rampa_cb, .name = "brillo_rampa" };
    if (esp_timer_create(&args, &s_rampa) != ESP_OK) {
        s_rampa = NULL;
        ESP_LOGW(TAG, "sin temporizador de rampa: el brillo cambiara de golpe");
    } else if (esp_timer_start_periodic(s_rampa, RAMPA_MS * 1000) != ESP_OK) {
        ESP_LOGW(TAG, "no arranca el tic de la rampa: el brillo cambiara de golpe");
        esp_timer_delete(s_rampa);
        s_rampa = NULL;
    }
}

uint8_t brillo_nivel(void) { return s_pasos[s_paso]; }

uint8_t brillo_alternar(void)
{
    /* Al OTRO nivel: 100 -> 30 -> 100. Con dos pasos, "+1 y modulo" y "hacia
     * abajo" son lo mismo, pero se deja escrito el porque del signo: se arranca
     * arriba, asi que el primer toque baja (quitar deslumbramiento) y el
     * siguiente vuelve a subir. */
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
