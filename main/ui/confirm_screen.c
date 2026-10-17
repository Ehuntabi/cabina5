/* confirm_screen.c - Confirmacion a pantalla completa. Ver confirm_screen.h. */
#include "confirm_screen.h"
#include "ui/ui_style.h"   /* la paleta, en un solo sitio */
#include "estilos.h"   /* la escala de fuentes, en un solo sitio */
#include <string.h>

/* Reparto de los 480 px de alto de esta pantalla. Los botones van abajo del
 * todo, que es donde cae el pulgar.
 *
 * EL CUERPO NO SE CENTRA A CIEGAS (D2, 17-oct-2026). Antes iba con
 * LV_ALIGN_CENTER y un desplazamiento fijo, sin mirar cuanto median de verdad
 * el titulo y el cuerpo: con el titulo en DOS lineas y un resumen de OCHO, el
 * cuerpo empezaba en y=54 y tapaba 52 px del titulo. Ahora se coloca DEBAJO
 * del titulo, usando el alto REAL que ha calculado LVGL
 * (lv_obj_get_height(s_title)), y su alto se limita al hueco que queda hasta
 * los botones. Si el texto no cabe, LV_LABEL_LONG_DOT recorta con puntos en
 * vez de solaparse con nada.
 *
 * El cuerpo va en letra 40 o 26 SEGUN LO LARGO QUE SEA (ver
 * confirm_screen_open). OJO CON EL HUECO: el titular sube a la 40, pero el
 * texto largo se queda en la 40/26 de siempre; con la 48 solo caben cinco
 * lineas y un resumen largo -- una parada con todos sus servicios -- se saldria.
 *
 * Aun asi conviene no pasarse: build_resumen() usa nombres cortos para los
 * tipos de parada por eso mismo, y dos rotulos van abreviados ("Precio/L:" en
 * vez de "Precio/litro:", "Precio:" en vez de "Precio total:"). */
#define CONF_TITLE_Y    10
#define CONF_BODY_GAP   6    /* aire entre titulo/cuerpo y cuerpo/botones */
#define CONF_BTN_BOTTOM 8    /* margen de los botones al borde inferior */
#define CONF_BTN_H      84
/* 340 y no 240: 240 era el ancho de la pantalla vieja de 3,5" sin escalar a
 * esta (800 de ancho, igual que el resto de la UI -- ver el "SOBRAN 320 px de
 * ancho" de estilos.h). Con 240, un texto normal como "Continuar igual" solo
 * cabia en la letra mas pequena de la escala (18, de seguridad) y se veia
 * diminuto -- visto en la placa. Con 340 entra holgado en una letra bastante
 * mayor. */
#define CONF_BTN_W     340

#define COL_FG         UI_COLOR_BG_HEX   /* texto sobre los botones claros */
/* Aceptar/cancelar salen de la paleta (COL_ACCION_OK/STOP, ver ui_style.h):
 * antes el verde de este dialogo era 0x66BB6A y el de los botones de guardar
 * 0x00C851 (D16). Un solo verde para "aceptar" en toda la UI. */

static lv_obj_t *s_root;
static lv_obj_t *s_title;
static lv_obj_t *s_body;
static lv_obj_t *s_no_btn;
static lv_obj_t *s_no_lbl;
static lv_obj_t *s_ok_btn;
static lv_obj_t *s_ok_lbl;

static confirm_cb_t s_cb;
static void        *s_user_data;
/* Accion opcional del boton de la IZQUIERDA (el que cancela). Ver
 * confirm_screen_no_accion(). Va aparte del de aceptar porque en casi todos los
 * dialogos el "no" solo cierra. */
static confirm_cb_t s_no_cb;
static void        *s_no_user_data;

static void close_overlay(void)
{
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_cb = NULL;
    s_user_data = NULL;
    s_no_cb = NULL;
    s_no_user_data = NULL;
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
    /* Igual que ok_cb(): la accion del "no" tambien puede abrir otro dialogo, y
     * para entonces estos punteros ya tienen que estar a salvo. */
    confirm_cb_t cb = s_no_cb;
    void *ud = s_no_user_data;
    close_overlay();
    if (cb) cb(ud);
}

/* El color de un boton, con su tono de pulsado. Aparte porque los dos botones
 * cambian de color segun el dialogo (ver confirm_screen_ok_destructivo). */
/* El ajuste del rotulo al ancho del boton vive en estilos.h (rotulo_autoajustable). */


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
    lv_obj_align(btn, align, align == LV_ALIGN_BOTTOM_LEFT ? 8 : -8,
                 -CONF_BTN_BOTTOM);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_FG), 0);
    lv_obj_center(lbl);
    /* El rotulo se ajusta al ancho del boton (ver estilos.h): los textos los
     * pone quien llama y son distintos en cada sitio. */
    rotulo_autoajustable(btn, lbl);
    if (lbl_out) *lbl_out = lbl;
    return btn;
}

/* Coloca el cuerpo con el alto REAL de cada texto (D2).
 *
 * POR QUE ASI Y NO CENTRADO: el alto del titulo cambia segun cuantas lineas
 * ocupe (1, 2 o 3), y el del cuerpo tambien. Centrar con un desplazamiento
 * fijo suponia que los dos median siempre lo mismo; cuando no, se tapaban.
 * Aqui se mide primero (lv_obj_update_layout + lv_obj_get_height, que es el
 * mismo alto que pinta LVGL), se pone el cuerpo justo debajo y se le da como
 * tope el hueco que queda hasta la fila de botones. Si el texto natural no
 * cabe, LONG_DOT recorta con puntos: se ve que hay mas, pero nada se solapa.
 * El cuerpo se deja con su altura natural cuando SI cabe, para no estirar un
 * aviso corto ni desplazarlo. */
static void reparto_cuerpo(void)
{
    if (!s_root || !s_title || !s_body) return;

    /* Resuelve ya el alto del titulo con el texto de ESTA apertura. */
    lv_obj_update_layout(s_root);

    const lv_coord_t alto_titulo  = lv_obj_get_height(s_title);
    const lv_coord_t alto_pantalla = lv_obj_get_height(s_root);
    const lv_coord_t y_cuerpo     = CONF_TITLE_Y + alto_titulo + CONF_BODY_GAP;
    const lv_coord_t y_botones    = alto_pantalla - CONF_BTN_BOTTOM - CONF_BTN_H;
    lv_coord_t alto_max = y_botones - CONF_BODY_GAP - y_cuerpo;
    if (alto_max < 0) alto_max = 0;

    lv_obj_align(s_body, LV_ALIGN_TOP_MID, 0, y_cuerpo);
    /* Primero sin tope y en WRAP: asi se mide lo que ocupa el texto de
     * verdad. Luego se decide entre dejarlo entero o recortarlo con puntos.
     * El orden importa: salir de DOT ANTES de tocar el alto evita que el
     * evento SIZE_CHANGED restaure puntos sobre el texto nuevo. */
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(s_body, LV_SIZE_CONTENT);
    lv_obj_update_layout(s_body);

    const lv_coord_t alto_natural = lv_obj_get_height(s_body);
    if (alto_natural > alto_max) {
        lv_obj_set_height(s_body, alto_max);
        lv_label_set_long_mode(s_body, LV_LABEL_LONG_DOT);
        /* El recorte con puntos se calcula al aplicar el alto nuevo; se fuerza
         * aqui para que el primer pintado ya salga recortado y no un frame con
         * el texto invadiendo los botones. */
        lv_obj_update_layout(s_body);
    }
}

void confirm_screen_init(lv_obj_t *parent)
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

    s_title = lv_label_create(s_root);
    lv_obj_set_width(s_title, lv_pct(94));
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, CONF_TITLE_Y);

    s_body = lv_label_create(s_root);
    lv_obj_set_width(s_body, lv_pct(96));
    lv_obj_set_style_text_font(s_body, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_body, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_align(s_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(s_body, 8, 0);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    /* La posicion definitiva la pone reparto_cuerpo() al abrir, con el
     * alto real del titulo. Aqui solo se deja pegada arriba, no centrada. */
    lv_obj_align(s_body, LV_ALIGN_TOP_MID, 0, CONF_TITLE_Y);

    /* "No" a la izquierda y "Si" a la derecha: el destructivo lejos del pulgar
     * que viene de confirmar, y el orden habitual de lectura. */
    s_no_btn = make_btn(s_root, COL_ACCION_STOP, no_cb, LV_ALIGN_BOTTOM_LEFT, &s_no_lbl);

    s_ok_btn = make_btn(s_root, COL_ACCION_OK, ok_cb, LV_ALIGN_BOTTOM_RIGHT, &s_ok_lbl);
    lv_label_set_text(s_ok_lbl, "Si");
}

void confirm_screen_open(const char *title, const char *body,
                         uint32_t color, const char *ok_text, const char *no_text,
                         confirm_cb_t cb, void *user_data)
{
    if (!s_root) return;

    s_cb = cb;
    s_user_data = user_data;
    /* El dialogo es UNO solo y se reutiliza: cada apertura tiene que empezar sin
     * accion en el "no". Sin esto, el callback de un cartel anterior (el
     * "Deshacer" de "Anotado") seguiria guardado y dispararia al pulsar el "no"
     * del siguiente. */
    s_no_cb = NULL;
    s_no_user_data = NULL;

    lv_label_set_text(s_title, title ? title : "");
    lv_obj_set_style_text_color(s_title, lv_color_hex(color), 0);
    /* Colores normales: la accion en verde y el "no" en rojo. Quien necesite lo
     * contrario llama a confirm_screen_ok_destructivo() justo despues. Se
     * rehace en CADA apertura porque el dialogo es uno solo y se reutiliza. */
    btn_color(s_ok_btn, COL_ACCION_OK);
    btn_color(s_no_btn, COL_ACCION_STOP);
    /* Puede venir oculto de un aviso anterior. */
    lv_obj_clear_flag(s_no_btn, LV_OBJ_FLAG_HIDDEN);
    /* La letra del cuerpo se ajusta a lo que hay que contar: hasta 110
     * caracteres va en la 40 y a partir de ahi en la 26, que mete mas letras
     * por linea y mas lineas en el mismo alto. (El comentario viejo hablaba de
     * una 34 intermedia, pero FUENTE_MEDIA_GRANDE es la misma 40 desde que la
     * escala es plana: no habia tal escalon.) El alto disponible ya no se
     * supone: lo calcula reparto_cuerpo() con el titulo real de cada apertura
     * y, si el texto natural no cabe, lo recorta con puntos. */
    const char *texto = (body && body[0]) ? body : "";
    size_t n = strlen(texto);
    const lv_font_t *fuente = (n <= 110) ? &FUENTE_MEDIA_GRANDE   /* 40 */
                                         : &FUENTE_NORMAL;        /* 26 */
    lv_obj_set_style_text_font(s_body, fuente, 0);
    /* El cuerpo pudo quedar en LONG_DOT de la apertura anterior. Hay que salir
     * de ese modo ANTES de cambiar el texto: al salir, LVGL reescribe los
     * caracteres que tapaban los puntos en la posicion del texto VIEJO; con el
     * texto nuevo ya puesto, corromperia el cartel. reparto_cuerpo() vuelve a
     * decidir despues si toca DOT con el texto de esta apertura. */
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_body, texto);
    lv_label_set_text(s_ok_lbl, ok_text ? ok_text : "Si");
    lv_label_set_text(s_no_lbl, no_text ? no_text : "No, corregir");
    /* rotulo_ajustar() se salta el reparto si el ANCHO del boton no cambio
     * desde la ultima vez (cache barata, ver su comentario) -- pero este
     * dialogo reutiliza los MISMOS botones en cada apertura y solo cambia el
     * TEXTO, que la cache no mira. Resultado: un texto largo como "Continuar
     * igual" se quedaba con la letra elegida para un texto mas corto de la
     * apertura anterior y se salia del boton (visto en la placa).
     *
     * Se invalida la cache a mano, PERO el reajuste se deja al temporizador
     * de 200 ms (rotulo_timer_cb): llamar a rotulo_ajustar() aqui, a pelo,
     * colgo la placa (refrescos LVGL cayendo a ~2/s, el mismo sintoma que el
     * bucle infinito ya documentado mas arriba) -- la funcion esta pensada
     * para correr SOLO desde ese temporizador, fuera de cualquier evento. */
    lv_obj_set_user_data(s_ok_lbl, (void *)(lv_intptr_t)-1);
    lv_obj_set_user_data(s_no_lbl, (void *)(lv_intptr_t)-1);

    /* DONDE VIVE EL DIALOGO MIENTRAS ESTA ABIERTO: en lv_layer_top(), la capa
     * que va por ENCIMA de todas las pantallas. Nace colgado de la de registros
     * (confirm_screen_init) y antes se mudaba a lv_scr_act() en cada apertura;
     * con eso fallaban dos cosas, las dos vistas:
     *
     *  1. lv_scr_load_anim() NO cambia la pantalla activa hasta que acaba la
     *     animacion (220 ms, NAV_ANIM_MS): justo despues de nav_ir_a_info() --
     *     que es como se abre el aviso "Salida abierta" -- lv_scr_act()
     *     devolvia la pantalla VIEJA, asi que el cartel se reparentaba a ella y
     *     se iba con la pantalla al terminar la animacion: no lo veia nadie. Y
     *     peor: confirm_screen_is_open() seguia diciendo que si, con lo que el
     *     temporizador de preguntas del arranque (parada_boot_timer_cb) dejaba
     *     de preguntar por los eventos sin cerrar en ese encendido.
     *  2. La pantalla de FILTROS vive en lv_layer_top() y tapa TODO: un dialogo
     *     reparentado a la pantalla activa se quedaba DEBAJO, invisible
     *     mientras los filtros estuvieran abiertos.
     *
     * En la capa de arriba las dos desaparecen: el dialogo se ve con cualquier
     * pantalla cargada (o cargandose) y por encima de los filtros. */
    lv_obj_t *arriba = lv_layer_top();
    if (arriba && lv_obj_get_parent(s_root) != arriba) lv_obj_set_parent(s_root, arriba);

    /* Con el dialogo ya colgado de la capa de arriba (que es la que fija el
     * ancho real), se colocan titulo, cuerpo y botones con sus altos medidos. */
    reparto_cuerpo();

    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    /* Por delante de lo que ya hubiera en esa capa (la pantalla de filtros). */
    lv_obj_move_foreground(s_root);
}

void confirm_screen_no_accion(confirm_cb_t cb, void *user_data)
{
    /* Se llama DESPUES de confirm_screen_open(), que es quien limpia esto. */
    s_no_cb = cb;
    s_no_user_data = user_data;
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
    /* YA NO HACE NADA, Y ES A PROPOSITO (16-oct-2026, norma del dueño):
     *
     *     "El boton que cancela, ROJO; el de aceptar, VERDE. En todos los casos."
     *
     * Esto repintaba el boton de aceptar de rojo cuando la accion era
     * destructiva (cerrar el viaje, borrar). El usuario lo vio en la pantalla de
     * finalizar el viaje: le salian los dos rojos (o el que acepta rojo y el de
     * cancelar verde, segun el dialogo) y le parecio antiintuitivo. La norma es
     * mas simple y vale para los nueve dialogos: aceptar SIEMPRE verde, cancelar
     * SIEMPRE rojo, y el aviso de lo que va a pasar va en el TEXTO del cartel.
     *
     * La funcion se queda (vacia) porque la llaman varios sitios y borrarla
     * obligaria a tocarlos todos para nada. */
    return;
#if 0
    if (!s_ok_btn || !s_no_btn) return;
    btn_color(s_ok_btn, COL_ACCION_STOP);
#endif
}

void confirm_screen_close(void)
{
    if (s_root) close_overlay();
}

bool confirm_screen_is_open(void)
{
    return s_root && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}
