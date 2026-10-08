/* confirm_screen.c - Confirmacion a pantalla completa. Ver confirm_screen.h. */
#include "confirm_screen.h"
#include <string.h>

/* Reparto de los 320 px de alto. Los botones van abajo del todo, que es donde
 * cae el pulgar, y el resumen se CENTRA en el hueco que queda entre el titulo y
 * ellos (de ahi el desplazamiento negativo): antes estaba pegado arriba y
 * dejaba mas de 120 px en negro.
 *
 * El cuerpo va en letra 32, 24 o 20 SEGUN LO LARGO QUE SEA (ver
 * confirm_screen_open): con la 32 solo caben cuatro lineas de ~25 caracteres, y
 * un resumen largo -- una parada con todos sus servicios -- se salia por abajo
 * empujando los botones fuera de la pantalla. Tamanos COMPILADOS en el
 * proyecto: 14/16/20/22/24/32/40 (la 28 no existe y no compila).
 *
 * Aun asi conviene no pasarse: build_resumen() usa nombres cortos para los
 * tipos de parada por eso mismo, y dos rotulos van abreviados ("Precio/L:" en
 * vez de "Precio/litro:", "Precio:" en vez de "Precio total:"). */
#define CONF_TITLE_Y   8
#define CONF_BODY_DY   -18
#define CONF_BTN_H     72
#define CONF_BTN_W     220

#define COL_FG         0x000000   /* texto sobre los botones claros */
#define COL_OK         0x66BB6A   /* verde  8,8:1 con el negro */
#define COL_NO         0xE57373   /* rojo   7,0:1 */

static lv_obj_t *s_root;
static lv_obj_t *s_title;
static lv_obj_t *s_body;
static lv_obj_t *s_no_btn;
static lv_obj_t *s_no_lbl;
static lv_obj_t *s_ok_btn;
static lv_obj_t *s_ok_lbl;

static confirm_cb_t s_cb;
static void        *s_user_data;

static void close_overlay(void)
{
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_cb = NULL;
    s_user_data = NULL;
}

static void ok_cb(lv_event_t *e)
{
    (void)e;
    /* Se copian ANTES de cerrar: close_overlay() los borra, y la accion podria
     * abrir otro dialogo. */
    confirm_cb_t cb = s_cb;
    void *ud = s_user_data;
    close_overlay();
    if (cb) cb(ud);
}

static void no_cb(lv_event_t *e)
{
    (void)e;
    close_overlay();
}

/* El color de un boton, con su tono de pulsado. Aparte porque los dos botones
 * cambian de color segun el dialogo (ver confirm_screen_ok_destructivo). */
static void btn_color(lv_obj_t *btn, uint32_t color)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(btn, lv_color_darken(lv_color_hex(color), LV_OPA_30),
                              LV_STATE_PRESSED);
}

static lv_obj_t *make_btn(lv_obj_t *parent, uint32_t color, lv_event_cb_t cb,
                           lv_align_t align, lv_obj_t **lbl_out)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, CONF_BTN_W, CONF_BTN_H);
    lv_obj_set_style_radius(btn, 12, 0);
    btn_color(btn, color);
    lv_obj_align(btn, align, align == LV_ALIGN_BOTTOM_LEFT ? 8 : -8, -8);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_FG), 0);
    lv_obj_center(lbl);
    if (lbl_out) *lbl_out = lbl;
    return btn;
}

void confirm_screen_init(lv_obj_t *parent)
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

    s_title = lv_label_create(s_root);
    lv_obj_set_width(s_title, lv_pct(94));
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, CONF_TITLE_Y);

    s_body = lv_label_create(s_root);
    lv_obj_set_width(s_body, lv_pct(96));
    lv_obj_set_style_text_font(s_body, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_body, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(s_body, 8, 0);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_body, LV_ALIGN_CENTER, 0, CONF_BODY_DY);

    /* "No" a la izquierda y "Si" a la derecha: el destructivo lejos del pulgar
     * que viene de confirmar, y el orden habitual de lectura. */
    s_no_btn = make_btn(s_root, COL_NO, no_cb, LV_ALIGN_BOTTOM_LEFT, &s_no_lbl);

    s_ok_btn = make_btn(s_root, COL_OK, ok_cb, LV_ALIGN_BOTTOM_RIGHT, &s_ok_lbl);
    lv_label_set_text(s_ok_lbl, "Si");
}

void confirm_screen_open(const char *title, const char *body,
                         uint32_t color, const char *ok_text, const char *no_text,
                         confirm_cb_t cb, void *user_data)
{
    if (!s_root) return;

    s_cb = cb;
    s_user_data = user_data;

    lv_label_set_text(s_title, title ? title : "");
    lv_obj_set_style_text_color(s_title, lv_color_hex(color), 0);
    /* Colores normales: la accion en verde y el "no" en rojo. Quien necesite lo
     * contrario llama a confirm_screen_ok_destructivo() justo despues. Se
     * rehace en CADA apertura porque el dialogo es uno solo y se reutiliza. */
    btn_color(s_ok_btn, COL_OK);
    btn_color(s_no_btn, COL_NO);
    /* Puede venir oculto de un aviso anterior. */
    lv_obj_clear_flag(s_no_btn, LV_OBJ_FLAG_HIDDEN);
    /* La letra del cuerpo se ajusta a lo que hay que contar. Entre el titulo y
     * los botones quedan ~184 px, y con letra 32 (46 px por linea) eso son
     * cuatro lineas justas: un resumen largo -- una parada con todos sus
     * servicios descritos -- se salia por abajo y empujaba los botones fuera de
     * la pantalla.
     *
     * Bajando a 24 o a 20 caben mas lineas Y mas caracteres por linea (~45 con
     * la 20, frente a ~25 con la 32), asi que el texto largo entra entero en vez
     * de tener que resumirse. Se elige por longitud total, que es la medida
     * barata: no hace falta afinar, solo no pasarse de alto. */
    const char *texto = (body && body[0]) ? body : "";
    size_t n = strlen(texto);
    const lv_font_t *fuente = (n <= 60)  ? &lv_font_montserrat_32
                            : (n <= 110) ? &lv_font_montserrat_24
                                         : &lv_font_montserrat_20;
    lv_obj_set_style_text_font(s_body, fuente, 0);
    lv_label_set_text(s_body, texto);
    lv_label_set_text(s_ok_lbl, ok_text ? ok_text : "Si");
    lv_label_set_text(s_no_lbl, no_text ? no_text : "No, corregir");

    /* El dialogo se muda a la pantalla que este activa. Nace colgado de la de
     * registros (confirm_screen_init), pero Ajustes vive en OTRA pantalla del
     * carrusel y alli no se veria nada. Reparentar en cada apertura sale mas
     * barato que tener un dialogo por pantalla, y deja el widget siempre donde
     * el usuario esta mirando. */
    lv_obj_t *scr = lv_scr_act();
    if (scr && lv_obj_get_parent(s_root) != scr) lv_obj_set_parent(s_root, scr);

    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_root);
}

void confirm_screen_aviso(const char *title, const char *body,
                          uint32_t color, const char *ok_text)
{
    /* Se apoya en el dialogo normal y luego esconde el "No": no hay dos
     * caminos, solo enterarse. Con callback nulo, aceptar solo cierra. */
    confirm_screen_open(title, body, color, ok_text ? ok_text : "Entendido",
                        NULL, NULL, NULL);
    if (s_no_btn) lv_obj_add_flag(s_no_btn, LV_OBJ_FLAG_HIDDEN);
}

void confirm_screen_ok_destructivo(void)
{
    if (!s_ok_btn || !s_no_btn) return;
    btn_color(s_ok_btn, COL_NO);
    btn_color(s_no_btn, COL_OK);
}

void confirm_screen_close(void)
{
    if (s_root) close_overlay();
}

bool confirm_screen_is_open(void)
{
    return s_root && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}
