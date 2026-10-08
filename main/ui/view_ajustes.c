/* view_ajustes.c - CONFIGURACION del aparato.
 *
 * Dos pantallas en una: el MENU (que version lleva puesta y un boton para el
 * Wi-Fi) y el formulario del WI-FI. Antes "Configuracion" entraba directamente
 * al formulario, y ni siquiera habia donde mirar la version -- que es el primer
 * dato que hace falta cuando algo va raro.
 *
 * Wi-Fi: SSID/password de la P4 a la que se asocia, editables en el propio
 * aparato (guardado en NVS via net/udp_rx.c). Resuelve el mismo problema que ya
 * sufrio victron_mini: password fija en el firmware, sin forma de cambiar de P4
 * sin reflashear.
 */
#include "view_ajustes.h"
#include "../brillo.h"
#include "view_info.h"
#include "../net/udp_rx.h"
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
static lv_obj_t *s_brillo_lbl;   /* texto del boton de brillo/contraste */
static lv_obj_t *s_enlace;        /* estado del enlace con la P4 */
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
static lv_obj_t *make_field(lv_obj_t *parent, const char *label_text, uint32_t max_len)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 2, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *ta = lv_textarea_create(cont);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max_len);
    lv_obj_set_size(ta, lv_pct(100), 34);
    lv_obj_align(ta, LV_ALIGN_TOP_LEFT, 0, 18);
    /* En CLICKED y no en FOCUSED: al volver del editor el campo conserva el
     * foco, asi que con FOCUSED el segundo toque no volveria a abrirlo. El
     * rotulo viaja en el user_data y es el titulo que saca el editor. */
    lv_obj_set_user_data(ta, (void *)label_text);
    lv_obj_add_event_cb(ta, ta_click_cb, LV_EVENT_CLICKED, NULL);

    return ta;
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
/* Pinta el estado del enlace con la P4. Calla si la pantalla no se ve. */
static void enlace_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_enlace || lv_obj_has_flag(s_enlace, LV_OBJ_FLAG_HIDDEN)) return;
    if (!lv_obj_is_visible(s_enlace)) return;

    char ssid[33] = {0};
    bool asociado = false;
    int rssi = 0, sin_datos = -1;
    udp_rx_enlace(ssid, sizeof(ssid), &asociado, &rssi, &sin_datos);
    if (!asociado) {
        lv_label_set_text_fmt(s_enlace, "P4: sin red (busco %s)", ssid);
        lv_obj_set_style_text_color(s_enlace, lv_color_hex(0xFF9800), 0);
    } else if (sin_datos < 0) {
        /* Asociada pero SIN haber recibido ni un mensaje desde que arranco.
         * Antes esto se pintaba como "sin datos 0s", que se lee justo al reves
         * ("acaba de llegar un dato") cuando significa "nunca ha llegado nada":
         * el usuario lo vio el 2-oct-2026 y no habia quien lo entendiera. */
        lv_label_set_text_fmt(s_enlace, "P4: %d dBm, sin datos todavia", rssi);
        lv_obj_set_style_text_color(s_enlace, lv_color_hex(0xFF9800), 0);
    } else if (sin_datos > 5) {
        lv_label_set_text_fmt(s_enlace, "P4: %d dBm, sin datos %ds", rssi, sin_datos);
        lv_obj_set_style_text_color(s_enlace, lv_color_hex(0xFF9800), 0);
    } else {
        lv_label_set_text_fmt(s_enlace, "P4: %d dBm", rssi);
        lv_obj_set_style_text_color(s_enlace, lv_color_hex(0x00C851), 0);
    }
}

/* Cambia brillo y contraste, y actualiza el texto del boton. */
static void brillo_btn_cb(lv_event_t *e)
{
    (void)e;
    uint8_t nivel = brillo_alternar();
    view_info_set_contraste(nivel == BRILLO_ALTO);
    if (s_brillo_lbl) {
        lv_label_set_text_fmt(s_brillo_lbl, LV_SYMBOL_EYE_OPEN "   Brillo y contraste: %u%%",
                              (unsigned)nivel);
    }
}

static void wifi_back_cb(lv_event_t *e)
{
    (void)e;
    mostrar_menu(true);
}

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
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* ── El MENU: un boton de Wi-Fi y, en el resto, la version ───────────── */
    s_menu = lv_obj_create(parent);
    lv_obj_set_size(s_menu, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_menu, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_menu, 0, 0);
    lv_obj_set_style_pad_all(s_menu, 8, 0);
    lv_obj_set_style_pad_row(s_menu, 8, 0);
    lv_obj_clear_flag(s_menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_menu, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *mrow = lv_obj_create(s_menu);
    lv_obj_set_size(mrow, lv_pct(100), 34);
    lv_obj_set_style_bg_opa(mrow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mrow, 0, 0);
    lv_obj_set_style_pad_all(mrow, 0, 0);
    lv_obj_clear_flag(mrow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *mback = lv_btn_create(mrow);
    lv_obj_set_size(mback, 80, 32);
    lv_obj_set_style_bg_color(mback, lv_color_hex(0x333333), 0);
    lv_obj_align(mback, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(mback, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *mback_lbl = lv_label_create(mback);
    lv_label_set_text(mback_lbl, LV_SYMBOL_LEFT " Volver");
    lv_obj_center(mback_lbl);

    lv_obj_t *mtitle = lv_label_create(mrow);
    lv_label_set_text(mtitle, "CONFIGURACION");
    lv_obj_set_style_text_color(mtitle, lv_color_hex(0xFF9800), 0);
    lv_obj_set_style_text_font(mtitle, &lv_font_montserrat_22, 0);
    lv_obj_align(mtitle, LV_ALIGN_RIGHT_MID, 0, 0);

    /* El unico boton, y grande: es lo unico que se toca aqui. */
    lv_obj_t *wbtn = lv_btn_create(s_menu);
    lv_obj_set_size(wbtn, lv_pct(100), 64);
    lv_obj_set_style_bg_color(wbtn, lv_color_hex(0x1565C0), 0);
    lv_obj_add_event_cb(wbtn, wifi_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wlbl = lv_label_create(wbtn);
    lv_label_set_text(wlbl, LV_SYMBOL_WIFI "   Wi-Fi");
    lv_obj_set_style_text_font(wlbl, &lv_font_montserrat_24, 0);
    lv_obj_center(wlbl);

    /* Brillo y contraste. Existe porque el doble toque de la pantalla de datos
     * no sale a la primera: aqui hay un boton y se acabaron las peripecias.
     * Cambia las dos cosas a la vez, igual que el gesto. 30-sep-2026. */
    lv_obj_t *bbtn = lv_btn_create(s_menu);
    lv_obj_set_size(bbtn, lv_pct(100), 64);
    lv_obj_set_style_bg_color(bbtn, lv_color_hex(0x37474F), 0);
    lv_obj_add_event_cb(bbtn, brillo_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *blbl = lv_label_create(bbtn);
    lv_label_set_text_fmt(blbl, LV_SYMBOL_EYE_OPEN "   Brillo y contraste: %u%%",
                          (unsigned)brillo_nivel());
    lv_obj_set_style_text_font(blbl, &lv_font_montserrat_22, 0);
    lv_obj_center(blbl);
    s_brillo_lbl = blbl;

    /* Estado del enlace con la P4. Vive AQUI y no en la pantalla de datos
     * (mudado el 30-sep-2026): es lo que se mira cuando algo no va, y en
     * Configuracion no le quita sitio a ningun numero. Lo pinta un temporizador
     * de un segundo que solo trabaja si esta visible. */
    s_enlace = lv_label_create(s_menu);
    lv_obj_set_style_text_font(s_enlace, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_enlace, lv_color_hex(0x90A4AE), 0);
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
    lv_obj_set_style_bg_color(vcard, lv_color_hex(0x111111), 0);
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
    lv_label_set_text(vrot, "Version instalada");
    lv_obj_set_style_text_color(vrot, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(vrot, &lv_font_montserrat_20, 0);

    s_ver_lbl = lv_label_create(vcard);
    lv_label_set_text(s_ver_lbl, app ? app->version : "?");
    lv_obj_set_style_text_color(s_ver_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_ver_lbl, &lv_font_montserrat_40, 0);

    lv_obj_t *vfecha = lv_label_create(vcard);
    if (app) lv_label_set_text_fmt(vfecha, "%s  %s", app->date, app->time);
    else     lv_label_set_text(vfecha, "");
    lv_obj_set_style_text_color(vfecha, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(vfecha, &lv_font_montserrat_20, 0);

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
        lv_obj_set_style_text_color(vrein, lv_color_hex(0xE57373), 0);
        lv_obj_set_style_text_font(vrein, &lv_font_montserrat_16, 0);
    }

    /* ── El formulario del WI-FI ─────────────────────────────────────────── */
    s_wifi = lv_obj_create(parent);
    lv_obj_set_size(s_wifi, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_wifi, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_wifi, 0, 0);
    lv_obj_set_style_pad_all(s_wifi, 8, 0);
    lv_obj_set_style_pad_row(s_wifi, 4, 0);
    lv_obj_set_flex_flow(s_wifi, LV_FLEX_FLOW_COLUMN);
    /* Cuatro campos y dos explicaciones NO caben en 320 px de alto, asi que
     * esta pantalla se desliza. Solo en vertical: en horizontal no hay nada que
     * ver y ademas confundiria con el gesto del carrusel. */
    lv_obj_set_scroll_dir(s_wifi, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_wifi, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *row = lv_obj_create(s_wifi);
    lv_obj_set_size(row, lv_pct(100), 34);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_btn_create(row);
    lv_obj_set_size(back, 80, 32);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(back, wifi_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Volver");
    lv_obj_center(back_lbl);

    lv_obj_t *title = lv_label_create(row);
    lv_label_set_text(title, "AJUSTES WI-FI");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF9800), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_align(title, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *hint = lv_label_create(s_wifi);
    lv_label_set_text(hint, "Red de la P4 a la que se asocia la 35cabina\n(cambiar aqui al pasar a otra P4, sin reflashear)");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(hint, lv_pct(100));

    s_ssid_ta = make_field(s_wifi, "SSID", 32);          /* s_ssid[33] en udp_rx.c */
    s_pass_ta = make_field(s_wifi, "Password", 64);      /* s_pass[65] en udp_rx.c */

    lv_obj_t *hint2 = lv_label_create(s_wifi);
    lv_label_set_text(hint2, "Usuario y clave del PORTAL de la P4 (no del wifi),\npara mandarle los apuntes del viaje");
    lv_obj_set_style_text_color(hint2, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint2, &lv_font_montserrat_14, 0);
    lv_obj_set_width(hint2, lv_pct(100));

    s_http_user_ta = make_field(s_wifi, "Usuario del portal", 32);   /* hu[33] en view_ajustes_refresh */
    s_http_pass_ta = make_field(s_wifi, "Clave del portal", 64);     /* hp[65] en view_ajustes_refresh */

    lv_obj_t *btn = lv_btn_create(s_wifi);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2E7D32), 0);
    lv_obj_add_event_cb(btn, guardar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Guardar y reconectar");
    lv_obj_center(btn_lbl);

    s_status_lbl = lv_label_create(s_wifi);
    lv_label_set_text(s_status_lbl, "");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x4CD964), 0);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);

    /* Se entra siempre por el menu. */
    mostrar_menu(true);
}
