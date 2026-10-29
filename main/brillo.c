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

/* Los dos niveles: el alto, fijo en BRILLO_ALTO, y el minimo, que se elige en
 * Configuracion (5..80). Se guarda el porcentaje, no un indice, asi que cambiar
 * la escala no invalida lo que ya hay en NVS. */
static int s_nivel = BRILLO_ALTO;
static int s_min   = BRILLO_MIN_DEFECTO;

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
 * siempre hay una salida. El nivel en el que se quedo la pantalla ya NO se lee
 * ni se escribe desde aqui (auditoria del 27-oct-2026, X15): no lo consumia
 * nadie -- view_info lo desconecto el 9-oct y este arranque nunca lo aplico --,
 * y a cambio cada doble toque hacia un nvs_commit. Solo se guarda el MINIMO,
 * que es el unico valor de brillo que se aplica de verdad. */
void brillo_init(void)
{
    /* El minimo si se aplica como valor de referencia (es lo que el usuario
     * eligio), pero el nivel de arranque es SIEMPRE el alto. */
    uint8_t min = BRILLO_MIN_DEFECTO;
    if (load_brillo_min(&min) != ESP_OK) min = BRILLO_MIN_DEFECTO;
    s_min = brillo_min_acota((int)min);
    s_nivel = BRILLO_ALTO;

    ESP_LOGI(TAG, "Brillo inicial %d%% (minimo %d%%)", s_nivel, s_min);

    s_rampa_hasta = s_nivel;
    bsp_display_brightness_set(s_nivel);

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

uint8_t brillo_nivel(void) { return (uint8_t)s_nivel; }

/* Las dos decisiones puras (ver brillo.h): se prueban en el host. */
int brillo_min_acota(int pct)
{
    if (pct < BRILLO_MIN_SUELO) pct = BRILLO_MIN_SUELO;
    if (pct > BRILLO_MIN_TOPE)  pct = BRILLO_MIN_TOPE;
    return pct;
}

int brillo_alterna_desde(int actual, int min)
{
    /* Se arranca arriba: el primer toque baja (quitar deslumbramiento) y el
     * siguiente vuelve a subir. Cualquier nivel que no sea el alto cuenta como
     * "estoy abajo", aunque venga de una version con otro minimo. */
    return (actual >= BRILLO_ALTO) ? brillo_min_acota(min) : BRILLO_ALTO;
}

uint8_t brillo_min_get(void) { return (uint8_t)s_min; }

uint8_t brillo_min_set(int pct)
{
    s_min = brillo_min_acota(pct);
    if (save_brillo_min((uint8_t)s_min) != ESP_OK)
        ESP_LOGW(TAG, "No se pudo guardar el minimo de brillo");
    /* Si la pantalla esta AHORA en el nivel bajo, se aplica ya: si no, el
     * usuario elige un minimo nuevo y no ve nada hasta el siguiente toque. */
    if (s_nivel != BRILLO_ALTO) {
        s_nivel = s_min;
        aplicar_con_rampa(s_nivel);
    }
    ESP_LOGI(TAG, "Brillo minimo -> %d%%", s_min);
    return (uint8_t)s_min;
}

uint8_t brillo_alternar(void)
{
    s_nivel = brillo_alterna_desde(s_nivel, s_min);
    aplicar_con_rampa(s_nivel);

    /* AQUI YA NO SE ESCRIBE EN NVS (auditoria del 27-oct-2026, X15): el nivel no
     * lo leia nadie -- load_brightness() solo alimentaba una linea de log y este
     * arranque aplica siempre el 100 % --, asi que cada doble toque era un
     * nvs_set_u8 + nvs_commit para nada (y al cambiar el minimo con la pantalla
     * baja, DOS commits seguidos). El MINIMO si se guarda, en brillo_min_set():
     * ese es el que se aplica. */
    ESP_LOGI(TAG, "Brillo -> %d%% (minimo %d%%)", s_nivel, s_min);
    return (uint8_t)s_nivel;
}
