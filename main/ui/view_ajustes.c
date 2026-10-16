/* view_ajustes.c - CONFIGURACION del aparato.
 *
 * Dos pantallas en una: el MENU (que version lleva puesta y un boton para el
 * Wi-Fi) y el formulario del WI-FI. Antes "Configuración" entraba directamente
 * al formulario, y ni siquiera habia donde mirar la version -- que es el primer
 * dato que hace falta cuando algo va raro.
 *
 * Wi-Fi: SSID/password de la P4 a la que se asocia, editables en el propio
 * aparato (guardado en NVS via net/udp_rx.c). Resuelve el mismo problema que ya
 * sufrio victron_mini: password fija en el firmware, sin forma de cambiar de P4
 * sin reflashear.
 */
#include "view_ajustes.h"
#include "ui/ui_style.h"   /* la paleta, en un solo sitio */
#include "estilos.h"   /* la escala de fuentes y el ancho de columna */
#include "view_info.h"
#include "nav.h"       /* nav_close_ajustes(): salir de esta pantalla */
#include "../net/udp_rx.h"
#include "../net/mini_proto.h"   /* MINI_PROTO_VERSION: el diagnostico del enlace */
#include "confirm_screen.h"
#include "config_storage.h"
#include "diag_reset.h"
#include "entry_screen.h"
#include "nav.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "view_ajustes";

/* Las dos caras de la pantalla: se ensena una y se esconde la otra. */
static lv_obj_t *s_menu;
static lv_obj_t *s_wifi;

static lv_obj_t *s_ver_lbl;
static lv_obj_t *s_ssid_ta;
static lv_obj_t *s_enlace;        /* estado del enlace con la P4 */
static lv_obj_t *s_uptime_lbl;    /* cuanto lleva en marcha (ver diag_reset) */
static lv_timer_t *s_enlace_timer;
static lv_obj_t *s_pass_ta;
static lv_obj_t *s_status_lbl;
/* Usuario y clave del PORTAL de la P4 -- NO son los del Wi-Fi. Hacen falta
 * desde que el portal exige Basic Auth tambien en el nivel abierto (21-ago-2026)
 * y la 3.5" le escribe apuntes de viaje. Se ven en la P4, en Ajustes -> Wi-Fi. */
static lv_obj_t *s_http_user_ta;
static lv_obj_t *s_http_pass_ta;

/* Lo que habia al abrir la pantalla, para saber si de verdad se ha cambiado
 * algo. Mismos tamanos que en udp_rx_get_credentials(). */
static char s_ssid_orig[33];
static char s_pass_orig[65];

/* Tocar un campo abre el editor a PANTALLA COMPLETA (entry_screen.c), el mismo
 * que usan los formularios de registros.
 *
 * Antes el teclado se creaba aqui dentro, y estaba mal: esta pantalla es una
 * columna flex, asi que el teclado entraba en el flujo como un elemento mas y
 * lo colocaban DEBAJO de todo lo demas -- se salia por el borde inferior y no
 * se veian las teclas de abajo. Con 320 px de alto no hay sitio para cabecera,
 * dos campos, boton y un teclado usable a la vez; el editor grande resuelve las
 * dos cosas, y de paso enseña el valor en letra 40 mientras lo tecleas. */
static void ta_click_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    entry_screen_open(ta, (const char *)lv_obj_get_user_data(ta), false);
}

/* max_len: sin esto ningun campo tenia tope, y el guardado (save_wifi_config
 * via udp_rx_set_credentials / save_portal_creds) escribe en NVS sin
 * truncar -- pero el RECARGADO si usa buffers fijos (s_ssid/s_pass en
 * udp_rx.c: 32/64; hu/hp en view_ajustes_refresh: 32/64), asi que un valor
 * mas largo tecleado aqui se guardaba bien la primera vez y luego, en la
 * siguiente recarga de esta pantalla, fallaba a leerse -- el campo aparecia
 * vacio, como si "se hubiera olvidado" la credencial. entry_screen_open ya
 * respeta lv_textarea_get_max_length() del campo origen (entry_screen.c:113),
 * asi que el editor a pantalla completa hereda el tope sin tocar nada mas
 * alli. Detectado por el usuario el 09-sep-2026. */
/* 'oculto': el campo se enseña con puntitos y solo se ve al abrirlo en el
 * editor. Lo llevan las DOS claves (la del Wi-Fi y la del portal) desde la
 * auditoria de seguridad del 13-oct-2026: estaban a la vista, y de pie junto a
 * la pantalla se leian de un vistazo. En el P4 ya se hacia asi. */
/* EL CAMPO ES UNA FILA: ROTULO A LA IZQUIERDA, VALOR A LA DERECHA.
 *
 * Lo pidio el usuario el 16-oct-2026 con la foto delante: "los campos de SSID,
 * password etc ocupan todo el ancho; creo que se puede organizar mucho mejor el
 * contenido de la pantalla wifi". Antes cada campo era un bloque de lado a lado
 * con el rotulo encima en gris, y ademas salian mal: el valor se creaba SIN
 * fuente (le tocaba la del sistema, 20) mientras el rotulo -- 14, que por el
 * encadenado de estilos.h acaba en 26 -- y las explicaciones se veian MAS
 * GRANDES que el dato.
 *
 * Ahora: el dato manda (letra 34, literal) y el rotulo es una columna fija de
 * 190 px a su izquierda (letra 18, literal). Los dos tamaños van LITERALES a
 * proposito: 18 y 34 no son nombres viejos de estilos.h, asi que no se encadenan
 * y miden lo que dicen (ver el aviso de doble traduccion en estilos.h).
 *
 * 56 px de alto: 44 para la caja (con la letra 34, que pide 38) y el resto de
 * aire. El campo se sigue abriendo a pantalla completa al tocarlo, asi que un
 * SSID largo se lee entero alli. */
#define CAMPO_ALTO           56
#define CAMPO_ROTULO_W      190
#define CAMPO_VALOR_W       567   /* 768 - 190 del rotulo - 11 de aire */

/* REPARTO VERTICAL DE LA PANTALLA (16-oct-2026), en un solo sitio y con las
 * cuentas hechas: cada fila se coloca en su y, sin layout automatico. De arriba
 * abajo: fila del titulo, cabecera y campos de la red de la P4, cabecera y
 * campos del portal, y la fila del boton con el estado a su derecha. La ultima
 * acaba en 438 de los 448 utiles (16 de margen arriba y abajo), asi que NO hay
 * que deslizar nada. */
#define FILA_W          768
#define FILA_TITULO_H    60
#define FILA_TITULO_Y     0
#define FILA_G1_Y        66
#define FILA_F1_Y        98
#define FILA_F2_Y       160
#define FILA_G2_Y       222
#define FILA_F3_Y       254
#define FILA_F4_Y       316
#define FILA_BTN_Y      378
#define FILA_BTN_W      380
#define FILA_ESTADO_X   400
#define FILA_ESTADO_Y   (FILA_BTN_Y + (FILA_TITULO_H - GRUPO_ALTO) / 2)
#define FILA_ESTADO_W   (FILA_W - FILA_ESTADO_X)
/* El rotulo mide lo mismo que una cabecera de grupo (y asi los dos textos
 * pequeños de la pantalla casan en alto). */
#define GRUPO_ALTO           26
#define CAMPO_ROTULO_FUENTE (&lv_font_montserrat_18)
#define CAMPO_VALOR_FUENTE  (&lv_font_montserrat_34)

static lv_obj_t *make_field_en(lv_obj_t *parent, const char *label_text, uint32_t max_len,
                               bool oculto, int y)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, FILA_W, CAMPO_ALTO);
    lv_obj_set_pos(cont, 0, y);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* POSICIONES A MANO, sin flex (16-oct-2026). Con `flex_grow` en la caja del
     * valor, LVGL dejaba la ULTIMA caja de la pantalla con el tamaño de su
     * contenido (260x130, medio fuera del campo) y el boton de guardar en 0x0:
     * el formulario entero se desmontaba. Con dos numeros fijos no hay nada que
     * repartir y sale igual siempre: rotulo 190 px a la izquierda, caja 567 px a
     * la derecha (768 - 190 - 11 de aire). */
    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl, CAMPO_ROTULO_FUENTE, 0);
    lv_obj_set_size(lbl, CAMPO_ROTULO_W, GRUPO_ALTO);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(lbl, 0, (CAMPO_ALTO - GRUPO_ALTO) / 2);

    lv_obj_t *ta = lv_textarea_create(cont);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max_len);
    if (oculto) lv_textarea_set_password_mode(ta, true);
    lv_obj_set_size(ta, CAMPO_VALOR_W, 44);
    lv_obj_set_pos(ta, CAMPO_ROTULO_W + 11, (CAMPO_ALTO - 44) / 2);
    lv_obj_set_style_text_font(ta, CAMPO_VALOR_FUENTE, 0);
    /* Sin el relleno vertical del tema: con letra 34 en 44 px no cabe. */
    lv_obj_set_style_pad_top(ta, 0, 0);
    lv_obj_set_style_pad_bottom(ta, 0, 0);
    /* En CLICKED y no en FOCUSED: al volver del editor el campo conserva el
     * foco, asi que con FOCUSED el segundo toque no volveria a abrirlo. */
    lv_obj_set_user_data(ta, (void *)label_text);
    lv_obj_add_event_cb(ta, ta_click_cb, LV_EVENT_CLICKED, NULL);

    return ta;
}

/* Cabecera de grupo: dice QUE es cada bloque de campos. El texto que antes iba
 * en parrafos de ayuda sueltos (que se comian 58 px de alto) cabe aqui. */
static void make_grupo_en(lv_obj_t *parent, const char *texto, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, 0, y);
    lv_label_set_text(l, texto);
    lv_obj_set_style_text_color(l, UI_COLOR_ORANGE, 0);
    lv_obj_set_style_text_font(l, CAMPO_ROTULO_FUENTE, 0);
    lv_obj_set_size(l, FILA_W, GRUPO_ALTO);
}

static void mostrar_menu(bool menu)
{
    if (!s_menu || !s_wifi) return;
    if (menu) {
        lv_obj_clear_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_wifi, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_wifi, LV_OBJ_FLAG_HIDDEN);
    }
}

/* El Volver del formulario de Wi-Fi no sale de Configuracion: vuelve al menu,
 * que es de donde se entro. */
/* POR QUE NO LLEGAN DATOS (auditoria del 14-oct-2026): desde aqui, "la P4 no
 * esta" y "la P4 habla otro idioma" (firmware mezclado) se veian IGUAL -- un
 * guion y el punto gris -- y no se arreglan igual: una se arregla encendiendo
 * la P4, la otra grabando la placa que se quedo atras. Corto, que es una linea
 * de la pantalla de Ajustes. */
static void enlace_diag_sufijo(char *out, size_t n)
{
    udp_rx_drop_t motivo = UDP_RX_DROP_NO;
    int ver = -1, tam = -1;
    udp_rx_diagnostico(&motivo, &ver, &tam);
    out[0] = 0;
    switch (motivo) {
        case UDP_RX_DROP_TAM:
            snprintf(out, n, " (otro firmware: %dB v%d)", tam, ver);
            break;
        case UDP_RX_DROP_VERSION:
            snprintf(out, n, " (firmware v%d, no v%d)", ver, (int)MINI_PROTO_VERSION);
            break;
        case UDP_RX_DROP_CRC:
            snprintf(out, n, " (llega corrupto)");
            break;
        case UDP_RX_DROP_ORIGEN:
            snprintf(out, n, " (habla otro aparato)");
            break;
        default:
            break;
    }
}

/* Pinta el estado del enlace con la P4. Calla si la pantalla no se ve. */
static void enlace_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_enlace || lv_obj_has_flag(s_enlace, LV_OBJ_FLAG_HIDDEN)) return;
    if (!lv_obj_is_visible(s_enlace)) return;

    /* Y de paso, cuanto lleva en marcha: es lo que deja ver de un vistazo si la
     * pantalla ha vuelto a arrancar desde la ultima vez que se miro (16-oct-2026,
     * en el banco se reiniciaba por el cable USB). Solo se toca el texto cuando
     * cambia de verdad -- cada minuto, no cada segundo. */
    if (s_uptime_lbl) {
        char nuevo[24];
        snprintf(nuevo, sizeof(nuevo), "En marcha: %s", diag_reset_hace_texto());
        if (strcmp(lv_label_get_text(s_uptime_lbl), nuevo) != 0) {
            lv_label_set_text(s_uptime_lbl, nuevo);
        }
    }

    char ssid[33] = {0};
    bool asociado = false;
    int rssi = 0, sin_datos = -1;
    udp_rx_enlace(ssid, sizeof(ssid), &asociado, &rssi, &sin_datos);
    if (!asociado) {
        lv_label_set_text_fmt(s_enlace, "P4: sin red (busco %s)", ssid);
        lv_obj_set_style_text_color(s_enlace, UI_COLOR_ORANGE, 0);
    } else if (sin_datos < 0) {
        /* Asociada pero SIN haber recibido ni un mensaje desde que arranco.
         * Antes esto se pintaba como "sin datos 0s", que se lee justo al reves
         * ("acaba de llegar un dato") cuando significa "nunca ha llegado nada":
         * el usuario lo vio el 2-oct-2026 y no habia quien lo entendiera. */
        char diag[48];
        enlace_diag_sufijo(diag, sizeof(diag));
        lv_label_set_text_fmt(s_enlace, "P4: %d dBm, sin datos todavia%s", rssi, diag);
        lv_obj_set_style_text_color(s_enlace, UI_COLOR_ORANGE, 0);
    } else if (sin_datos > 5) {
        char diag[48];
        enlace_diag_sufijo(diag, sizeof(diag));
        lv_label_set_text_fmt(s_enlace, "P4: %d dBm, sin datos %ds%s", rssi, sin_datos, diag);
        lv_obj_set_style_text_color(s_enlace, UI_COLOR_ORANGE, 0);
    } else {
        lv_label_set_text_fmt(s_enlace, "P4: %d dBm", rssi);
        lv_obj_set_style_text_color(s_enlace, UI_COLOR_GREEN, 0);
    }
}

/* (aqui vivia brillo_btn_cb, el boton de brillo del menu: quitado el 16-oct-2026)
 *
 * El brillo se cambia con el DOBLE TOQUE en la pantalla de datos (y con el
 * toque largo, que es lo que sale a la primera): los dos llaman a
 * brillo_alternar() y alternan los dos niveles de brillo.h (85 % y 100 %). El
 * boton existia porque el gesto no se descubre solo, pero en el menu sobraba.
 * OJO: si algun dia se quita tambien el gesto, el brillo se queda sin forma de
 * cambiarse en marcha. */

static void wifi_back_cb(lv_event_t *e)
{
    (void)e;
    mostrar_menu(true);
}

/* (aqui vivia paseo_open_cb, el boton del modo paseo: quitado el 16-oct-2026) */

static void wifi_open_cb(lv_event_t *e)
{
    (void)e;
    mostrar_menu(false);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    nav_close_ajustes();
}

/* El cambio de verdad, ya confirmado. */
static void do_guardar(void *ud)
{
    (void)ud;
    const char *ssid = lv_textarea_get_text(s_ssid_ta);
    const char *pass = lv_textarea_get_text(s_pass_ta);
    bool ok_wifi = udp_rx_set_credentials(ssid, pass);

    /* La referencia pasa a ser lo recien guardado: un segundo toque seguido ya
     * no es un cambio y no volvera a preguntar. */
    snprintf(s_ssid_orig, sizeof(s_ssid_orig), "%s", ssid);
    snprintf(s_pass_orig, sizeof(s_pass_orig), "%s", pass);

    bool ok_portal = (save_portal_creds(lv_textarea_get_text(s_http_user_ta),
                                         lv_textarea_get_text(s_http_pass_ta))
                      == ESP_OK);

    if (ok_wifi && ok_portal) {
        lv_label_set_text(s_status_lbl, "Guardado, reconectando...");
        ESP_LOGI(TAG, "Nuevas credenciales guardadas desde Ajustes");
    } else {
        lv_label_set_text(s_status_lbl, "No he podido guardarlo");
        ESP_LOGW(TAG, "Guardado fallido: wifi=%d portal=%d", ok_wifi, ok_portal);
    }
}

/* Cambiar la red no es un ajuste cualquiera: si te equivocas de SSID o de
 * contrasena la pantalla se queda sin la P4 y sin datos, y para volver atras
 * hay que teclear a mano lo de antes con el vehiculo en marcha. Por eso pide
 * confirmacion -- pero solo si de verdad has tocado algo, para no meter un
 * dialogo en el camino de quien solo estaba mirando. */
static void guardar_cb(lv_event_t *e)
{
    (void)e;
    const char *ssid = lv_textarea_get_text(s_ssid_ta);
    const char *pass = lv_textarea_get_text(s_pass_ta);

    bool cambio_red = strcmp(ssid, s_ssid_orig) != 0 || strcmp(pass, s_pass_orig) != 0;

    /* Las del portal se guardan SIN preguntar: equivocarse ahi no deja la
     * pantalla incomunicada, solo hace que la P4 rechace los apuntes con un
     * mensaje claro. La confirmacion se reserva para lo que si puede dejarte
     * sin datos, que es cambiar de red. */
    if (!cambio_red) {
        if (save_portal_creds(lv_textarea_get_text(s_http_user_ta),
                              lv_textarea_get_text(s_http_pass_ta)) == ESP_OK) {
            lv_label_set_text(s_status_lbl, "Guardado.");
        } else {
            lv_label_set_text(s_status_lbl, "No he podido guardarlo");
        }
        return;
    }

    static char body[64];   /* estatico: el dialogo lo sigue leyendo despues */
    snprintf(body, sizeof(body), "Red:  %s", ssid);
    /* Naranja, el mismo color del titulo de esta pantalla. */
    confirm_screen_open("Cambiar de red?", body, 0xFF9800,
                        "Si, cambiar", "Cancelar", do_guardar, NULL);
}

/* ── A PANTALLA COMPLETA, que es de 5" ──────────────────────────────────────
 *
 * POR QUE CAMBIO (8-oct-2026): la primera version metia el contenido en una
 * columna centrada de 560 px, para que un campo de texto no acabara con el
 * rotulo en una punta y el valor en la otra. El resultado, visto en la placa,
 * era justo lo contrario de lo que se buscaba: dos franjas negras a los lados y
 * media pantalla vacia debajo, o sea una interfaz que parecia de la pantalla de
 * 3,5". El usuario lo dijo claro: "el tamano no ves que es el de la de 5".
 *
 * Ahora la pantalla se aprovecha ENTERA:
 *   - Los botones de accion ocupan los 800 px de ancho y se reparten el alto
 *     (110 px cada uno, con el rotulo en letra 30: se leen desde el asiento).
 *   - La tarjeta de la version se queda con TODO el alto que sobra, con el
 *     numero en letra 48.
 *   - Los campos del formulario de Wi-Fi se ensanchan a la pantalla, con
 *     margen a los lados pero sin columna.
 *
 * El unico sitio donde el ancho se limita a proposito es el ancho MAXIMO de un
 * campo de texto (ver UI_ANCHO_COLUMNA en estilos.h), porque ahi si molesta. */
void view_ajustes_refresh(void)
{
    udp_rx_get_credentials(s_ssid_orig, sizeof(s_ssid_orig),
                           s_pass_orig, sizeof(s_pass_orig));
    lv_textarea_set_text(s_ssid_ta, s_ssid_orig);
    lv_textarea_set_text(s_pass_ta, s_pass_orig);

    char hu[33] = {0}, hp[65] = {0};
    size_t hul = sizeof(hu), hpl = sizeof(hp);
    load_portal_creds(hu, &hul, hp, &hpl);   /* si no hay nada, quedan vacios */
    lv_textarea_set_text(s_http_user_ta, hu);
    lv_textarea_set_text(s_http_pass_ta, hp);
    mostrar_menu(true);

    lv_label_set_text(s_status_lbl, "");
}

void view_ajustes_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* ── El MENU: un boton de Wi-Fi y, en el resto, la version ───────────── */
    s_menu = lv_obj_create(parent);
    lv_obj_set_size(s_menu, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_menu, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_menu, 0, 0);
    lv_obj_set_style_pad_all(s_menu, 16, 0);
    lv_obj_set_style_pad_row(s_menu, 14, 0);
    lv_obj_clear_flag(s_menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_menu, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *mrow = lv_obj_create(s_menu);
    lv_obj_set_size(mrow, lv_pct(100), 60);
    lv_obj_set_style_bg_opa(mrow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mrow, 0, 0);
    lv_obj_set_style_pad_all(mrow, 0, 0);
    lv_obj_clear_flag(mrow, LV_OBJ_FLAG_SCROLLABLE);

    /* 192x56 y letra 26, como los "Volver" del cuaderno: es el boton que mas se
     * usa y tiene que ser el mismo en todas las pantallas (peticion del
     * usuario, 8-oct-2026). */
    lv_obj_t *mback = lv_btn_create(mrow);
    lv_obj_set_size(mback, 192, 56);
    lv_obj_set_style_radius(mback, 28, 0);
    lv_obj_set_style_bg_color(mback, UI_COLOR_BOTON, 0);
    lv_obj_align(mback, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(mback, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *mback_lbl = lv_label_create(mback);
    lv_label_set_text(mback_lbl, LV_SYMBOL_LEFT " Volver");
    lv_obj_set_style_text_font(mback_lbl, &lv_font_montserrat_26, 0);
    lv_obj_center(mback_lbl);

    lv_obj_t *mtitle = lv_label_create(mrow);
    lv_label_set_text(mtitle, "CONFIGURACIÓN");
    lv_obj_set_style_text_color(mtitle, UI_COLOR_ORANGE, 0);
    lv_obj_set_style_text_font(mtitle, &lv_font_montserrat_22, 0);
    lv_obj_align(mtitle, LV_ALIGN_RIGHT_MID, 0, 0);

    /* El unico boton, y grande: es lo unico que se toca aqui. */
    lv_obj_t *wbtn = lv_btn_create(s_menu);
    lv_obj_set_size(wbtn, lv_pct(100), 110);
    lv_obj_set_flex_grow(wbtn, 1);
    lv_obj_set_style_bg_color(wbtn, UI_COLOR_CYAN, 0);
    lv_obj_add_event_cb(wbtn, wifi_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wlbl = lv_label_create(wbtn);
    lv_label_set_text(wlbl, LV_SYMBOL_WIFI "   Wi-Fi");
    /* Letra 30 y el ajuste automatico de rotulos de estilos.h como red: en 800
     * px de ancho entra de sobra, y asi se lee de lejos. */
    lv_obj_set_style_text_font(wlbl, &lv_font_montserrat_30, 0);
    lv_obj_center(wlbl);

    /* AQUI ESTABA EL BOTON DE BRILLO ("Brillo: 85 %" / "100 %"). El usuario lo
     * mando quitar el 16-oct-2026: el brillo se cambia con el doble toque (y el
     * toque largo) en la pantalla de datos, que es donde se esta mirando. */

    /* AQUI ESTABA EL BOTON "Ver todas las pantallas" (modo paseo). El usuario lo
     * mando quitar el 16-oct-2026: es una herramienta de taller y en el menu de
     * Configuracion solo hace ruido. El modo paseo sigue en el codigo
     * (view_registro_paseo_* / nav_ir_a_paseo, ver view_registro.h) y se puede
     * volver a colgar de un boton si algun dia hace falta; para revisar las
     * pantallas sin tactil esta ademas el carrusel del modo captura
     * (capture_carousel.c). */

    /* Estado del enlace con la P4. Vive AQUI y no en la pantalla de datos
     * (mudado el 30-sep-2026): es lo que se mira cuando algo no va, y en
     * Configuracion no le quita sitio a ningun numero. Lo pinta un temporizador
     * de un segundo que solo trabaja si esta visible. */
    s_enlace = lv_label_create(s_menu);
    lv_obj_set_style_text_font(s_enlace, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_enlace, UI_COLOR_TEXT_DIM, 0);
    lv_label_set_text(s_enlace, "");
    if (!s_enlace_timer) {
        s_enlace_timer = lv_timer_create(enlace_timer_cb, 1000, NULL);
    }

    /* Y el resto de la pantalla, la VERSION. GRANDE y centrada -- letra 40 para
     * el numero y 20 para el rotulo y la fecha: sobra sitio y esto se mira de
     * lejos, con el aparato en su soporte y sin acercarse. Que es el
     * primer dato que hace falta cuando algo va raro: sale del tag de git al
     * compilar (esp_app_get_description), no hay ningun numero que mantener a
     * mano. Con la fecha debajo, que distingue dos builds de la misma. */
    lv_obj_t *vcard = lv_obj_create(s_menu);
    lv_obj_set_width(vcard, lv_pct(100));
    lv_obj_set_flex_grow(vcard, 1);
    lv_obj_set_style_bg_color(vcard, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(vcard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(vcard, 0, 0);
    lv_obj_set_style_radius(vcard, 10, 0);
    lv_obj_set_style_pad_all(vcard, 6, 0);
    lv_obj_set_style_pad_row(vcard, 2, 0);
    lv_obj_clear_flag(vcard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(vcard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(vcard, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    const esp_app_desc_t *app = esp_app_get_description();

    lv_obj_t *vrot = lv_label_create(vcard);
    lv_label_set_text(vrot, "Versión instalada");
    lv_obj_set_style_text_color(vrot, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(vrot, &lv_font_montserrat_26, 0);

    s_ver_lbl = lv_label_create(vcard);
    lv_label_set_text(s_ver_lbl, app ? app->version : "?");
    lv_obj_set_style_text_color(s_ver_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_ver_lbl, &lv_font_montserrat_48, 0);

    lv_obj_t *vfecha = lv_label_create(vcard);
    if (app) lv_label_set_text_fmt(vfecha, "%s  %s", app->date, app->time);
    else     lv_label_set_text(vfecha, "");
    lv_obj_set_style_text_color(vfecha, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(vfecha, &lv_font_montserrat_26, 0);

    /* Chivato de reinicio (ver diag_reset.c): aparece SOLO si el arranque
     * anterior no lo provoco el contacto (watchdog, panic, cuelgue). Con el
     * ajetreo normal -- esta pantalla arranca con cada contacto, varias veces
     * al dia -- una tarjeta fija seria puro ruido; y este es el unico momento
     * en que se puede saber, porque al siguiente corte de contacto el motivo se
     * pierde. Una sola etiqueta y en 16 para no crecer la tarjeta: vcard es
     * flex_grow y NO tiene scroll, asi que lo que no quepa se recorta. */
    if (diag_reset_fue_fallo()) {
        lv_obj_t *vrein = lv_label_create(vcard);
        lv_label_set_text(vrein, diag_reset_resumen());
        lv_obj_set_style_text_align(vrein, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(vrein, UI_COLOR_RED, 0);
        lv_obj_set_style_text_font(vrein, &lv_font_montserrat_16, 0);
    } else {
        /* SI EL ARRANQUE NO FUE UN FALLO, SE DICE IGUAL (16-oct-2026, el
         * usuario: "si, en configuracion"). Un BAJON DE TENSION o un RESET POR
         * EL CABLE USB no cuentan como fallo (es_fallo() los deja fuera a
         * proposito: en la autocaravana el arranque del motor hunde la tension),
         * asi que no salian en ninguna pantalla -- y son justo lo que hay que
         * ver cuando la pantalla parece reiniciarse sola. En gris y pequenito:
         * informa sin dar la alarma que da el chivato rojo de arriba. */
        const char *motivo = diag_reset_motivo_arranque();
        if (motivo && motivo[0]) {
            lv_obj_t *vmot = lv_label_create(vcard);
            lv_label_set_text_fmt(vmot, "Arranque: %s", motivo);
            lv_obj_set_style_text_align(vmot, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(vmot, UI_COLOR_TEXT_DIM, 0);
            lv_obj_set_style_text_font(vmot, &lv_font_montserrat_16, 0);
        }
    }

    /* CUANTO LLEVA EN MARCHA, siempre y en gris (16-oct-2026). Se actualiza sola
     * con el temporizador del enlace (1 s, y solo con la pantalla visible). Si
     * pone "En marcha: 2 min" cuando hace un rato ponia "1 h 10 min", es que ha
     * arrancado otra vez: sin PC, sin log y sin tocar el puerto serie. */
    s_uptime_lbl = lv_label_create(vcard);
    lv_label_set_text_fmt(s_uptime_lbl, "En marcha: %s", diag_reset_hace_texto());
    lv_obj_set_style_text_align(s_uptime_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_uptime_lbl, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_uptime_lbl, &lv_font_montserrat_16, 0);

    /* ── El formulario del WI-FI ─────────────────────────────────────────── */
    s_wifi = lv_obj_create(parent);
    lv_obj_set_size(s_wifi, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_wifi, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_wifi, 0, 0);
    lv_obj_set_style_pad_all(s_wifi, 16, 0);
    /* 6 y no 8: con los campos a 66 px, el titulo, el boton y el estado, la
     * cuenta da 435 de los 448 utiles. Los 8 px de antes eran de cuando los
     * campos median 56 y el dato se leia con lupa. */
    /* SIN LAYOUT AUTOMATICO (16-oct-2026): cada fila se coloca en su y, con las
     * cuentas hechas aqui abajo. El flex de LVGL se probo primero y dejaba la
     * ultima caja y el boton mal (ver make_field), asi que el reparto va escrito
     * y se comprueba en la placa con el volcado del arbol. */
    /* Cuatro campos y dos explicaciones caben justos en 480 px de alto (en la
     * pantalla de 320 px logicos no cabian, y de ahi que se deslice). Se deja
     * que se pueda deslizar igualmente: con la letra un escalon mas y un campo
     * de texto alto, es la red de seguridad para que nada quede inalcanzable.
     * Solo en vertical: en horizontal no hay nada que ver y ademas confundiria
     * con el gesto del carrusel. */
    lv_obj_set_scroll_dir(s_wifi, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_wifi, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *row = lv_obj_create(s_wifi);
    lv_obj_set_size(row, FILA_W, FILA_TITULO_H);
    lv_obj_set_pos(row, 0, FILA_TITULO_Y);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_btn_create(row);
    lv_obj_set_size(back, 192, 56);
    lv_obj_set_style_radius(back, 28, 0);
    lv_obj_set_style_bg_color(back, UI_COLOR_BOTON, 0);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(back, wifi_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Volver");
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_26, 0);
    lv_obj_center(back_lbl);

    /* EL TITULO, CENTRADO (16-oct-2026, el usuario: "el titulo esta
     * descentrado"). Estaba alineado al borde derecho, que con el boton Volver a
     * la izquierda dejaba el rotulo pegado a la esquina y sin relacion con nada.
     * Centrado en la fila cae en el medio de la pantalla; el boton acaba en 192
     * px, asi que no se tocan. */
    lv_obj_t *title = lv_label_create(row);
    lv_label_set_text(title, "AJUSTES WI-FI");
    lv_obj_set_style_text_color(title, UI_COLOR_ORANGE, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    /* LOS CAMPOS, EN DOS GRUPOS (16-oct-2026). Las dos explicaciones que habia
     * sueltas -- y que se comian 58 px de alto, dejando el boton de guardar
     * fuera de la pantalla -- pasan a ser las cabeceras de cada grupo: se leen
     * mejor (estan pegadas a lo que explican) y no ocupan alto de mas. */
    make_grupo_en(s_wifi, "RED DE LA P4 (la del wifi)", FILA_G1_Y);
    s_ssid_ta = make_field_en(s_wifi, "SSID", 32, false, FILA_F1_Y);          /* s_ssid[33] en udp_rx.c */
    s_pass_ta = make_field_en(s_wifi, "Password", 64, true, FILA_F2_Y);      /* s_pass[65] en udp_rx.c */

    make_grupo_en(s_wifi, "PORTAL DE LA P4 (usuario y clave, no los del wifi)", FILA_G2_Y);
    s_http_user_ta = make_field_en(s_wifi, "Usuario", 32, false, FILA_F3_Y);   /* hu[33] en view_ajustes_refresh */
    s_http_pass_ta = make_field_en(s_wifi, "Clave", 64, true, FILA_F4_Y);     /* hp[65] en view_ajustes_refresh */

    /* BOTON CON ALTO PROPIO, y esto era un fallo de verdad (16-oct-2026): el
     * boton se creaba SIN alto -- su rotulo va centrado, y un objeto con el
     * tamaño "lo que mida mi contenido" no mide nada si su unico hijo esta
     * alineado en vez de en el flujo -- asi que salia de 0x0 y el formulario
     * quedaba descuadrado (el usuario lo vio como que el boton no estaba). */
    lv_obj_t *btn = lv_btn_create(s_wifi);
    lv_obj_set_size(btn, FILA_BTN_W, FILA_TITULO_H);
    lv_obj_set_pos(btn, 0, FILA_BTN_Y);
    lv_obj_set_style_bg_color(btn, UI_COLOR_GREEN, 0);
    lv_obj_add_event_cb(btn, guardar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Guardar y reconectar");
    lv_obj_center(btn_lbl);

    /* El estado, a la derecha del boton: cabe en la misma fila y asi no se le
     * quita alto a nada. */
    s_status_lbl = lv_label_create(s_wifi);
    lv_label_set_text(s_status_lbl, "");
    lv_obj_set_size(s_status_lbl, FILA_ESTADO_W, GRUPO_ALTO);
    lv_obj_set_pos(s_status_lbl, FILA_ESTADO_X, FILA_ESTADO_Y);
    lv_obj_set_style_text_color(s_status_lbl, UI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);

    /* Se entra siempre por el menu. */
    mostrar_menu(true);
}
