/* rotulos.c - El autoajuste de los rotulos de boton, con UNA tabla y UN
 * temporizador.
 *
 * POR QUE EXISTE ESTE FICHERO (auditoria del 16-oct-2026): esto vivia en
 * `main/ui/estilos.h`, que es una CABECERA, con la tabla y las funciones
 * `static`. Y una cabecera con `static` no es "un solo sitio": cada .c que la
 * incluye se queda con SU COPIA. La incluyen 7 unidades de compilacion, asi que
 * habia 7 tablas `s_rotulos`:
 *
 *   - el temporizador lo crea el BSP (esp_bsp.c -> rotulos_arrancar()), o sea en
 *     la copia de esp_bsp.c, con la lista VACIA de esp_bsp.c;
 *   - los botones se apuntan a la copia de quien los crea (view_registro.c,
 *     confirm_screen.c...).
 *
 * Resultado: el temporizador iteraba una lista vacia y el reajuste periodico NO
 * corria nunca. Solo se veia el ajuste que hace rotulo_autoajustable() al
 * registrar el boton -- y confirm_screen.c cuenta con el temporizador para
 * reajustar sus dos botones cuando cambia el TEXTO, porque su cache solo mira
 * el ancho. Con un .c de verdad hay una sola tabla y un solo temporizador.
 *
 * El porque del diseno (temporizador y no evento de LVGL, escalones, cache por
 * ancho) esta documentado en estilos.h, encima de ROTULO_ESCALONES. */
#include "ui/estilos.h"
#include "esp_log.h"

static const char *TAG = "rotulos";

/* Cuantos botones se pueden apuntar. Con 64 sobra: en toda la UI no hay ni la
 * mitad de botones con rotulo autoajustable a la vez. */
#define ROTULO_MAX 64

static lv_obj_t *s_rotulos[ROTULO_MAX];
static int       s_rotulos_n = 0;
static bool      s_arrancado = false;

/* Pone en el rotulo la letra mas grande que quepa en el ancho de su boton. */
static void rotulo_ajustar(lv_obj_t *btn, lv_obj_t *lbl)
{
    if (!btn || !lbl) return;

    const lv_coord_t disponible = lv_obj_get_content_width(btn) - 8;
    if (disponible <= 0) return;

    /* Si el ancho no ha cambiado desde la ultima vez, no hay nada que hacer:
     * esto es lo que evita estar midiendo en cada tic. */
    if ((lv_coord_t)(lv_intptr_t)lv_obj_get_user_data(lbl) == disponible) return;
    lv_obj_set_user_data(lbl, (void *)(lv_intptr_t)disponible);

    const char *txt = lv_label_get_text(lbl);
    const lv_font_t *escalones[] = ROTULO_ESCALONES;
    const lv_font_t *elegida = &lv_font_montserrat_18;   /* red de seguridad */
    for (size_t i = 0; i < sizeof(escalones) / sizeof(escalones[0]); i++) {
        if (texto_ancho(escalones[i], txt) <= disponible) {
            elegida = escalones[i];
            break;
        }
    }
    lv_obj_set_style_text_font(lbl, elegida, 0);
}

static void rotulo_timer_cb(lv_timer_t *t)
{
    (void)t;
    for (int i = 0; i < s_rotulos_n; i++) {
        lv_obj_t *btn = s_rotulos[i];
        if (!btn) continue;
        /* Un boton BORRADO deja el puntero colgando, y esto corre cada 200 ms
         * para siempre: se comprueba antes de tocarlo. Hoy no se borra ninguno
         * (las pantallas de esta UI viven toda la sesion), pero el dia que uno
         * se borre, el fallo seria un acceso a memoria liberada cada 200 ms. */
        if (!lv_obj_is_valid(btn)) { s_rotulos[i] = NULL; continue; }
        rotulo_ajustar(btn, (lv_obj_t *)lv_obj_get_user_data(btn));
    }
}

/* Arranca el temporizador que ajusta los rotulos. Lo llama el BSP al montar
 * LVGL (ver esp_bsp.c): asi este fichero no necesita saber nada del arranque y
 * el ajuste existe antes de que se cree ningun boton. */
void rotulos_arrancar(void)
{
    if (s_arrancado) return;
    s_arrancado = true;
    if (!lv_timer_create(rotulo_timer_cb, 200, NULL)) {
        ESP_LOGW(TAG, "sin temporizador de rotulos: los botones se quedaran con "
                      "la letra que se les puso al crearse");
    }
}

/* Deja el rotulo de un boton apuntado para que el temporizador lo ajuste. */
void rotulo_autoajustable(lv_obj_t *btn, lv_obj_t *lbl)
{
    if (!btn || !lbl) return;
    lv_obj_set_user_data(btn, lbl);
    if (s_rotulos_n < ROTULO_MAX) s_rotulos[s_rotulos_n++] = btn;
    rotulo_ajustar(btn, lbl);
}
