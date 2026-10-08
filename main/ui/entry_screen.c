/* entry_screen.c - Editor de un campo a pantalla completa. Ver entry_screen.h. */
#include "entry_screen.h"
#include "estilos.h"   /* la escala de fuentes, en un solo sitio */

/* Reparto vertical de los 480 px de alto de esta pantalla (display.h).
 * El teclado se lleva 250 -- mas de la mitad -- porque es lo que se toca con el
 * dedo en movimiento; el valor solo hay que leerlo.
 *
 * Lo que cambio el 8-oct-2026: el valor se escribe con la fuente de 48 (antes
 * 40, ver estilos.h) y su fila mide 62 px, que es lo que pide esa letra; el
 * rotulo sube a 26 y se lleva 26 px. 8 + 26 + 62 + 150 de teclado y aire = 480.
 * El teclado en si NO se toca: su tamano lo decide LVGL por el alto de la
 * pantalla, y a 480 px ya sale comodo. */
#define ENTRY_LABEL_Y   8
#define ENTRY_VALUE_Y   34
#define ENTRY_VALUE_H   62
#define ENTRY_KB_H      250

static lv_obj_t *s_root;
static lv_obj_t *s_label;
static lv_obj_t *s_value;
static lv_obj_t *s_kb;
static lv_obj_t *s_target;

static void close_overlay(void)
{
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_target = NULL;
}

void entry_screen_close(void)
{
    if (s_root) close_overlay();
}

/* Aceptar: vuelca el texto al campo original y avisa a quien escuche
 * VALUE_CHANGED (el precio/litro del repostaje cuelga de ahi). */
static void kb_ready_cb(lv_event_t *e)
{
    (void)e;
    if (s_target) {
        lv_textarea_set_text(s_target, lv_textarea_get_text(s_value));
        lv_obj_send_event(s_target, LV_EVENT_VALUE_CHANGED, NULL);
    }
    close_overlay();
}

static void kb_cancel_cb(lv_event_t *e)
{
    (void)e;
    close_overlay();
}

void entry_screen_init(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x000000), 0);
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
    lv_obj_set_style_text_color(s_label, lv_color_hex(0xDDDDDD), 0);
    lv_obj_set_style_text_font(s_label, &lv_font_montserrat_16, 0);
    lv_obj_align(s_label, LV_ALIGN_TOP_LEFT, 12, ENTRY_LABEL_Y);

    s_value = lv_textarea_create(s_root);
    lv_textarea_set_one_line(s_value, true);
    lv_obj_set_size(s_value, lv_pct(94), ENTRY_VALUE_H);
    lv_obj_align(s_value, LV_ALIGN_TOP_MID, 0, ENTRY_VALUE_Y);
    lv_obj_set_style_text_font(s_value, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(s_value, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_value, LV_TEXT_ALIGN_CENTER, 0);
    /* Sin marco ni relleno: con letra del 40 en 52 px no sobra un pixel. */
    lv_obj_set_style_bg_opa(s_value, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_value, 0, 0);
    lv_obj_set_style_pad_all(s_value, 0, 0);

    s_kb = lv_keyboard_create(s_root);
    lv_obj_set_size(s_kb, lv_pct(100), ENTRY_KB_H);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_kb, s_value);
    lv_obj_add_event_cb(s_kb, kb_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_kb, kb_cancel_cb, LV_EVENT_CANCEL, NULL);
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
     * "Otros", por ejemplo -- no protegia nada mientras se tecleaba. */
    lv_textarea_set_accepted_chars(s_value, lv_textarea_get_accepted_chars(target));
    lv_textarea_set_max_length(s_value, lv_textarea_get_max_length(target));
    lv_textarea_set_text(s_value, lv_textarea_get_text(target));
    lv_keyboard_set_mode(s_kb, numeric ? LV_KEYBOARD_MODE_NUMBER
                                       : LV_KEYBOARD_MODE_TEXT_LOWER);

    /* Se muda a la pantalla que este activa. Nace colgado de la de registros
     * (entry_screen_init), pero Ajustes vive en OTRA pantalla del carrusel y
     * alli no se veria. Mismo apano que confirm_screen. */
    lv_obj_t *scr = lv_scr_act();
    if (scr && lv_obj_get_parent(s_root) != scr) lv_obj_set_parent(s_root, scr);

    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_root);
}
