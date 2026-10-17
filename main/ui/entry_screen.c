/* entry_screen.c - Editor de un campo a pantalla completa. Ver entry_screen.h. */
#include "entry_screen.h"
#include "ui/ui_style.h"   /* la paleta, en un solo sitio */
#include "estilos.h"   /* la escala de fuentes, en un solo sitio */

/* Reparto vertical de los 480 px de alto de esta pantalla (display.h).
 * El teclado se lleva 250 -- mas de la mitad -- porque es lo que se toca con el
 * dedo en movimiento; el valor solo hay que leerlo. Debajo del teclado va una
 * fila de 48 px con los dos botones PROPIOS (verde Guardar, rojo Cancelar): el
 * teclado del tema trae un ✓ y un ✕ grises que no cumplen la norma del dueno.
 *
 * Lo que mide de verdad el codigo de hoy, con las fuentes Inter del proyecto:
 * el rotulo es la Inter 30 (alto de linea 37) y empieza en y=8; el valor es la
 * Inter 40 (48 px de linea) dentro de una fila de 62 desde y=34. El comentario
 * viejo decia "48 y 26" y sumaba 150 de teclado: eran numeros de una version
 * anterior que ya no cuadraban con el codigo. */
#define ENTRY_LABEL_Y    8
#define ENTRY_VALUE_Y    34
#define ENTRY_VALUE_H    62
#define ENTRY_KB_H       250
/* Fila de acciones, pegada abajo. El teclado se mueve hacia arriba lo justo
 * para no pisarla, pero conserva sus 250 px: en esta pantalla sobra hueco entre
 * el valor y el teclado. */
#define ENTRY_BTN_H      48
#define ENTRY_BTN_BOTTOM 8
#define ENTRY_BTN_GAP    4    /* aire entre el teclado y la fila de botones */

static lv_obj_t *s_root;
static lv_obj_t *s_label;
static lv_obj_t *s_value;
static lv_obj_t *s_kb;
static lv_obj_t *s_target;

/* Profundidad de silencio mientras se escribe por codigo: ver
 * entry_screen_mudo() en el .h. Lleva cuenta porque una zona muda puede
 * contener a otra (p.ej. el flujo del viaje rellenando su campo). */
static int s_mudo;

void entry_screen_mudo(bool mudo)
{
    if (mudo) s_mudo++;
    else if (s_mudo > 0) s_mudo--;
}

bool entry_screen_mudo_ahora(void)
{
    return s_mudo > 0;
}

static void close_overlay(void)
{
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_target = NULL;
    /* El editor es UNO y se reutiliza para todos los campos. Si el anterior era
     * una contrasena (Wi-Fi, portal) y el siguiente no, el modo password se
     * quedaba pegado y el SSID o el destino salian como puntitos -- o, al
     * reves, una clave nueva se veia en claro (D3). Se limpia SIEMPRE al
     * cerrar; entry_screen_open() lo vuelve a poner si el destino lo lleva. */
    if (s_value) lv_textarea_set_password_mode(s_value, false);
}

void entry_screen_close(void)
{
    if (s_root) close_overlay();
}

/* Se consulta desde la tarea de LVGL (temporizador de Ajustes): este fichero y
 * sus objetos solo se tocan ahi, asi que no hace falta cerrojo. */
bool entry_screen_is_open(void)
{
    return s_root && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

/* Aceptar: vuelca el texto al campo original y avisa UNA VEZ a quien escuche
 * VALUE_CHANGED (el precio/litro del repostaje cuelga de ahi, y el flujo del
 * viaje encadena el paso siguiente).
 *
 * EL AVISO ES UNO, NO ONCE (auditoria del 16-oct-2026): lv_textarea_set_text()
 * manda VALUE_CHANGED por CADA caracter que va añadiendo -- los campos del
 * viaje tienen accepted_chars y max_length, asi que pasan por add_char -- y otra
 * vez al final (LVGL 9.2: lv_textarea.c:266 y :419). Con un destino de 9 letras
 * el que escucha recibia 11 avisos y disparaba 11 veces su flujo (11
 * temporizadores de encadenado, 11 carteles). Se silencian los intermedios con
 * entry_screen_mudo() y se manda UNO al final, ya con el texto puesto. */
static void entry_guardar(void)
{
    if (s_target) {
        entry_screen_mudo(true);
        lv_textarea_set_text(s_target, lv_textarea_get_text(s_value));
        entry_screen_mudo(false);
        lv_obj_send_event(s_target, LV_EVENT_VALUE_CHANGED, NULL);
    }
    close_overlay();
}

static void entry_guardar_cb(lv_event_t *e)
{
    (void)e;
    entry_guardar();
}

static void entry_cancelar_cb(lv_event_t *e)
{
    (void)e;
    close_overlay();
}

/* Boton de la fila de abajo. Los dos miden lo mismo y llevan el color de la
 * accion (verde/rojo) con texto oscuro, igual que los de confirm_screen: la
 * regla no admite excepciones y aqui el teclado del tema no la cumplia. */
static lv_obj_t *make_entry_btn(lv_obj_t *parent, const char *txt, uint32_t color,
                                lv_align_t align, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, lv_pct(45), ENTRY_BTN_H);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(btn, lv_color_darken(lv_color_hex(color), LV_OPA_30),
                              LV_STATE_PRESSED);
    lv_obj_align(btn, align, align == LV_ALIGN_BOTTOM_LEFT ? 8 : -8,
                 -ENTRY_BTN_BOTTOM);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, UI_COLOR_SOBRE_CLARO, 0);
    lv_obj_center(lbl);
    return btn;
}

void entry_screen_init(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_root, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    /* FUERA del layout del padre. Este panel se muda a la pantalla activa al
     * abrirse, y la de Ajustes es una COLUMNA FLEX: sin esto se colocaria como
     * un elemento mas de la columna -- detras de todo y fuera de la pantalla,
     * que es exactamente el fallo que tenia el teclado incrustado que este
     * panel vino a sustituir. Con IGNORE_LAYOUT manda su propia posicion. */
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(s_root, LV_ALIGN_TOP_LEFT, 0, 0);

    s_label = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_label, UI_COLOR_TEXT_SOFT, 0);
    lv_obj_set_style_text_font(s_label, &lv_font_montserrat_16, 0);
    lv_obj_align(s_label, LV_ALIGN_TOP_LEFT, 12, ENTRY_LABEL_Y);

    s_value = lv_textarea_create(s_root);
    lv_textarea_set_one_line(s_value, true);
    lv_obj_set_size(s_value, lv_pct(94), ENTRY_VALUE_H);
    lv_obj_align(s_value, LV_ALIGN_TOP_MID, 0, ENTRY_VALUE_Y);
    lv_obj_set_style_text_font(s_value, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(s_value, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_align(s_value, LV_TEXT_ALIGN_CENTER, 0);
    /* Sin marco ni relleno: con letra del 40 (48 px de linea) en una fila de
     * 62 px, el relleno del tema solo estorbaria. */
    lv_obj_set_style_bg_opa(s_value, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_value, 0, 0);
    lv_obj_set_style_pad_all(s_value, 0, 0);

    s_kb = lv_keyboard_create(s_root);
    lv_obj_set_size(s_kb, lv_pct(100), ENTRY_KB_H);
    /* El teclado sube lo que mide la fila de botones para que las dos cosas
     * quepan sin solaparse. Su tamano no cambia: en la 5" sobra alto. */
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0,
                 -(ENTRY_BTN_H + ENTRY_BTN_BOTTOM + ENTRY_BTN_GAP));
    lv_keyboard_set_textarea(s_kb, s_value);
    /* Las teclas ✓ y ✕ del tema siguen existiendo, pero ya no son la unica
     * salida: los botones de abajo son los que llevan el color de la accion.
     * Se conectan a lo MISMO (guardar/cancelar) para que no haya dos
     * comportamientos distintos. */
    lv_obj_add_event_cb(s_kb, entry_guardar_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_kb, entry_cancelar_cb, LV_EVENT_CANCEL, NULL);

    /* La fila de acciones propia, debajo del teclado. Verde Guardar y rojo
     * Cancelar: la norma del dueno, que el teclado del tema no cumple. */
    make_entry_btn(s_root, "Guardar", COL_ACCION_OK, LV_ALIGN_BOTTOM_LEFT,
                   entry_guardar_cb);
    make_entry_btn(s_root, "Cancelar", COL_ACCION_STOP, LV_ALIGN_BOTTOM_RIGHT,
                   entry_cancelar_cb);
}

/* ENCADENAR DOS CAMPOS: abrir el siguiente DESDE el callback del anterior.
 *
 * POR QUE NO VALE entry_screen_open() A SECAS, y esto costo un fallo en la
 * placa el 16-oct-2026 ("la 5 no pide km al iniciar"): el "Aceptar" del teclado
 * avisa PRIMERO a quien escucha (entry_guardar manda VALUE_CHANGED, y ahi es donde
 * el flujo del viaje abre el campo siguiente) y SOLO DESPUES cierra este panel
 * (close_overlay). Si el que escucha abre el paso nuevo con entry_screen_open(),
 * ese close_overlay -- que viene detras, en la misma llamada -- lo vuelve a
 * esconder: el usuario ve cerrarse el primer campo y no aparece ninguno mas.
 *
 * Aplazando la apertura al ciclo siguiente de LVGL, el cierre ya ha pasado y el
 * paso nuevo se queda abierto. Va con un temporizador de un solo uso y no con
 * lv_async_call() porque el valor de "ha ido bien" se llama distinto en LVGL 8
 * (LV_RES_OK) y en LVGL 9 (LV_RESULT_OK), y este fichero es el mismo en las dos
 * pantallas salvo los includes. */
static lv_obj_t *s_sig_target;
static char      s_sig_label[48];
static bool      s_sig_numeric;

static void abrir_siguiente_timer_cb(lv_timer_t *t)
{
    (void)t;
    entry_screen_open(s_sig_target, s_sig_label, s_sig_numeric);
}

void entry_screen_open_siguiente(lv_obj_t *target, const char *label, bool numeric)
{
    s_sig_target  = target;
    s_sig_numeric = numeric;
    snprintf(s_sig_label, sizeof(s_sig_label), "%s", label ? label : "");

    lv_timer_t *t = lv_timer_create(abrir_siguiente_timer_cb, 20, NULL);
    if (!t) {
        /* Sin memoria para el temporizador: se abre ya. Con el cierre de por
         * medio el panel se escondera, pero es mejor eso que quedarse sin el
         * paso y sin poder empezar el viaje. */
        entry_screen_open(s_sig_target, s_sig_label, s_sig_numeric);
        return;
    }
    lv_timer_set_repeat_count(t, 1);   /* de un solo uso */
}

void entry_screen_open(lv_obj_t *target, const char *label, bool numeric)
{
    if (!s_root || !target) return;

    s_target = target;
    lv_label_set_text(s_label, label ? label : "");

    /* Los caracteres aceptados se fijan ANTES de meter el texto: si no, un valor
     * previo con caracteres ahora prohibidos entraria igual.
     *
     * Se COPIAN del campo de destino, no un fijo "0123456789." para todo lo
     * numerico: un cuentakilometros (ver make_number_field) no lleva punto a
     * proposito, y con el fijo se podia teclear "123.5" aqui y que al volcarlo
     * a un destino sin punto LVGL lo dejara mudo en "1235", sin ningun aviso
     * (el filtro de accepted_chars se aplica tambien al hacer set_text, no
     * solo al teclear). Detectado auditando el 07-sep-2026.
     *
     * El limite de caracteres tambien se copia del campo de destino: sin esto
     * se podia escribir sin tope aqui (el textarea del editor es uno propio,
     * no el 'target'), y el limite puesto en el destino -- 40 en el motivo de
     * "Otros", por ejemplo -- no protegia nada mientras se tecleaba.
     *
     * Y EL MODO CONTRASENA, TAMBIEN (D3): el campo de destino lo lleva (los dos
     * campos de clave de Ajustes lo activan) y este editor no lo copiaba, asi
     * que la clave se leia EN CLARO a letra 40. close_overlay() lo limpia al
     * cerrar para que el siguiente campo -- SSID, destino... -- no salga con
     * puntitos, y una clave nueva no se quede a la vista. */
    lv_textarea_set_accepted_chars(s_value, lv_textarea_get_accepted_chars(target));
    lv_textarea_set_max_length(s_value, lv_textarea_get_max_length(target));
    lv_textarea_set_password_mode(s_value, lv_textarea_get_password_mode(target));
    lv_textarea_set_text(s_value, lv_textarea_get_text(target));
    lv_keyboard_set_mode(s_kb, numeric ? LV_KEYBOARD_MODE_NUMBER
                                       : LV_KEYBOARD_MODE_TEXT_LOWER);

    /* Se muda a lv_layer_top() (la capa de arriba de todo), NO a la pantalla
     * activa: mismo apano y mismos dos motivos que confirm_screen_open() -- alli
     * esta el comentario largo. En resumen: lv_scr_load_anim() no cambia la
     * pantalla activa hasta que acaba la animacion, y la pantalla de FILTROS
     * (que tambien vive en esa capa) taparia el editor. */
    lv_obj_t *arriba = lv_layer_top();
    if (arriba && lv_obj_get_parent(s_root) != arriba) lv_obj_set_parent(s_root, arriba);

    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_root);
}
