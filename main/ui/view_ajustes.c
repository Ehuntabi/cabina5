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
#include "../net/ota_rx.h"   /* actualizacion por Wi-Fi: la lanza y se pinta aqui */
#include "../net/mini_proto.h"   /* MINI_PROTO_VERSION: el diagnostico del enlace */
#include "confirm_screen.h"
#include "config_storage.h"
#include "brillo.h"   /* el minimo de brillo, ajustable desde aqui */
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
/* La pantalla de Ajustes entera (el padre de las dos caras). Se guarda para
 * saber si esta DELANTE: el temporizador de la actualizacion no puede abrir el
 * cartel de confirmacion encima de la pantalla que el usuario este mirando. */
static lv_obj_t *s_raiz;

static lv_obj_t *s_ver_lbl;
static lv_obj_t *s_ssid_ta;
static lv_obj_t *s_enlace;        /* estado del enlace con la P4 */
/* El numero del minimo de brillo ("80 %"), que se repinta al cambiarlo. */
static lv_obj_t *s_brillo_min_lbl;

/* EL MINIMO DE BRILLO, AJUSTABLE (21-oct-2026, peticion del usuario).
 *
 * Hasta hoy los dos niveles eran fijos (100 y 85) y para cambiar el bajo habia
 * que recompilar y grabar. El doble toque (y el toque largo) en Datos alterna
 * 100 con ESTE valor, asi que aqui es donde se elige cual es, de 5 en 5.
 *
 * Se llama a brillo_min_set(), que lo acota, lo guarda y -- si la pantalla esta
 * ahora en el nivel bajo -- lo aplica en el acto. Asi se ve lo que se esta
 * eligiendo sin tocar la pantalla de datos. */
static void brillo_min_pinta(void)
{
    if (!s_brillo_min_lbl) return;
    lv_label_set_text_fmt(s_brillo_min_lbl, "%u %%", (unsigned)brillo_min_get());
}

static void brillo_min_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    brillo_min_set(brillo_min_get() + delta * BRILLO_MIN_PASO);
    brillo_min_pinta();
}
static lv_obj_t *s_ota_lbl;       /* estado de la actualizacion (comparte sitio) */
static lv_timer_t *s_ota_timer;
/* Cierto mientras el cartel "hay version nueva" ya se abrio para esta oferta.
 * Sin esto, el temporizador lo abriria otra vez cada 500 ms al cancelarlo. */
static bool s_ota_preguntando;
/* El cuerpo del cartel, estatico: confirm_screen_open() se queda con el puntero
 * y lo lee despues, asi que no puede ser una variable local de la funcion. */
static char s_ota_body[96];
static lv_obj_t *s_uptime_lbl;    /* cuanto lleva en marcha (ver diag_reset) */
static lv_timer_t *s_enlace_timer;
static int s_ticks_cartel;   /* segundos que lleva puesto el cartel de la OTA */
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
 * respeta lv_textarea_get_max_length() del campo origen (entry_screen_open),
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
/* 66 de alto y fuente de PIE (18) para el estado del guardado (D4): la caja
 * era de 26 px y la fuente de entonces (Inter 26, 33 px de linea) no cabia --
 * se recortaban 7 px. Con 66 caben dos lineas de la 18 (22 px) y el mensaje
 * mas largo medido, "Falta el SSID: no lo he guardado" (393 px en la 26, 274
 * en la 18), entra en los 368 de ancho. El alto de 66 se centra en la fila del
 * boton (60 px); con el pad de 16 del contenedor ocupa de y=391 a 457, dentro
 * de los 480 de la pantalla. */
#define FILA_ESTADO_H   66
#define FILA_ESTADO_Y   (FILA_BTN_Y + (FILA_TITULO_H - FILA_ESTADO_H) / 2)
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

/* Estado del guardado del Wi-Fi, con el COLOR del resultado (D4).
 *
 * POR QUE: s_status_lbl nacia en verde y se quedaba en verde tambien cuando el
 * guardado fallaba o faltaba el SSID; el usuario leia "Falta el SSID" en verde
 * de exito. Aqui se cambia el texto y el color a la vez, para que no haya una
 * salida que se deje uno de los dos. Verde = se guardo; rojo = no. */
static void wifi_estado(const char *txt, bool ok)
{
    if (!s_status_lbl) return;
    lv_label_set_text(s_status_lbl, txt);
    lv_obj_set_style_text_color(s_status_lbl,
                                ok ? UI_COLOR_GREEN : UI_COLOR_RED, 0);
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

    /* CUANTO LLEVA EN MARCHA, ANTES DE NADA Y SIN MIRAR EL ENLACE (17-oct-2026).
     * Antes esto iba despues del "si el enlace no se ve, sal", y el estado de la
     * OTA esconde la linea del enlace (comparten sitio) y no siempre se limpia:
     * resultado, el "En marcha" se quedaba clavado en "1 s" hasta reiniciar.
     * Es un dato propio: solo necesita que su etiqueta exista. */
    if (s_uptime_lbl) {
        char nuevo[24];
        snprintf(nuevo, sizeof(nuevo), "En marcha: %s", diag_reset_hace_texto());
        if (strcmp(lv_label_get_text(s_uptime_lbl), nuevo) != 0) {
            lv_label_set_text(s_uptime_lbl, nuevo);
        }
    }

    if (!s_enlace || lv_obj_has_flag(s_enlace, LV_OBJ_FLAG_HIDDEN)) return;
    if (!lv_obj_is_visible(s_enlace)) return;

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

/* ── ACTUALIZACION POR WI-FI (OTA), 17-oct-2026 ─────────────────────────────
 *
 * Lo pidio el dueño: "¿es posible implementar una actualizacion por OTA en la 5
 * y 3,5?" -> "dale". El trabajo de verdad (preguntar a la P4, descargar 2 MB,
 * comprobar el sha256 y escribir la flash) lo hace net/ota_rx.c en sus propias
 * tareas. AQUI solo se pinta su estado y se pide confirmacion: nada de esta
 * pantalla se queda esperando a la red, porque eso congelaria la UI y el
 * watchdog de tareas reiniciaria la placa a mitad de la descarga.
 */

/* Los codigos de "no se pudo empezar", en una linea corta (ver ota_rx.h). */
static const char *ota_inicio_texto(ota_rx_inicio_t r)
{
    switch (r) {
        case OTA_RX_INICIO_OK:        return "";
        case OTA_RX_INICIO_VIAJE:     return "Termina el viaje antes";
        case OTA_RX_INICIO_OCUPADO:   return "Ya hay una actualizacion en marcha";
        case OTA_RX_INICIO_SIN_DATOS: return "Primero comprueba la version";
        case OTA_RX_INICIO_SIN_MEM:   return "Sin memoria para actualizar";
        default:                      return "No se pudo empezar";
    }
}

/* Aceptar el cartel: empieza la descarga. Verde a la derecha, como todas. */
static void ota_aceptar_cb(void *ud)
{
    (void)ud;
    ota_rx_inicio_t r = ota_rx_actualizar();
    if (r != OTA_RX_INICIO_OK && s_ota_lbl) {
        lv_label_set_text(s_ota_lbl, ota_inicio_texto(r));
        lv_obj_set_style_text_color(s_ota_lbl, UI_COLOR_RED, 0);
    }
    /* s_ota_preguntando se queda puesto a proposito: si no arranco (un viaje que
     * empezo entre la pregunta y el "si"), no se vuelve a preguntar lo mismo
     * cada medio segundo. Se limpia al pulsar el boton o al cancelar. */
}

/* Cancelar (el rojo de la izquierda): se olvida la oferta para que el proximo
 * toque del boton vuelva a comprobar de cero. */
static void ota_cancelar_cb(void *ud)
{
    (void)ud;
    ota_rx_olvidar();
    s_ota_preguntando = false;
    if (s_ota_lbl) {
        lv_label_set_text(s_ota_lbl, "");
        lv_obj_set_style_text_color(s_ota_lbl, UI_COLOR_TEXT_DIM, 0);
    }
}

/* El boton "Actualizar pantalla": pregunta a la P4 si hay version nueva. */
static void ota_btn_cb(lv_event_t *e)
{
    (void)e;
    ota_rx_vista_t v;
    ota_rx_estado(&v);
    if (v.estado == OTA_RX_COMPROBANDO || v.estado == OTA_RX_DESCARGANDO ||
        v.estado == OTA_RX_LISTO) {
        return;   /* ya hay algo en marcha: no se lanza otra */
    }
    /* Este toque es "vuelve a empezar": si la comprobacion anterior dejo el
     * cartel ya preguntado, se limpia para que pregunte otra vez si hay nueva. */
    s_ota_preguntando = false;

    ota_rx_inicio_t r = ota_rx_comprobar();
    if (!s_ota_lbl) return;
    if (r != OTA_RX_INICIO_OK) {
        lv_label_set_text(s_ota_lbl, ota_inicio_texto(r));
        lv_obj_set_style_text_color(s_ota_lbl, UI_COLOR_RED, 0);
    } else {
        lv_label_set_text(s_ota_lbl, "Buscando version nueva...");
        lv_obj_set_style_text_color(s_ota_lbl, UI_COLOR_TEXT_SOFT, 0);
    }
}

/* Pinta el estado y pide confirmacion cuando hay version nueva.
 *
 * Corre en la tarea de LVGL (es un lv_timer) y SOLO mira el estado: ota_rx_estado
 * copia y vuelve. El estado de la OTA y el del enlace comparten sitio en el menu:
 * mientras hay algo que contar de la actualizacion se enseña eso y se esconde el
 * enlace, y al reves al terminar. Asi la fila de botones nueva no le quita alto a
 * la tarjeta de la version. */
static void ota_timer_cb(lv_timer_t *t)
{
    (void)t;

    /* ANTES DE NADA: avisar a la OTA automatica de si hay algo delante que no
     * se puede interrumpir. Esto corre en la tarea de LVGL, que es la dueña de
     * los objetos; ota_rx.c lo lee desde su temporizador de 60 s y no puede
     * tocar LVGL. Se hace aunque Ajustes no este delante: el editor y los
     * dialogos viven en lv_layer_top() y pueden estar abiertos en otra
     * pantalla. */
    ota_rx_ui_ocupada(entry_screen_is_open() || confirm_screen_is_open());

    /* Solo con Ajustes DELANTE y en el MENU (no en el formulario de Wi-Fi): si
     * no, el cartel de confirmacion saltaria encima de lo que el usuario este
     * mirando. */
    if (!s_ota_lbl || !s_enlace || !s_raiz || !s_menu) return;
    if (lv_screen_active() != s_raiz) return;
    if (lv_obj_has_flag(s_menu, LV_OBJ_FLAG_HIDDEN)) return;

    ota_rx_vista_t v;
    ota_rx_estado(&v);

    /* EL CARTEL SE RETIRA SOLO (17-oct-2026). La comprobacion automatica deja el
     * estado en AL_DIA (o en ERROR) y NADIE lo volvia a QUIETO: como el cartel
     * comparte linea con el enlace y lo esconde, la pantalla se quedaba sin el
     * "P4: -78 dBm" (y sin el "En marcha", que colgaba del enlace) hasta el
     * siguiente reinicio. Los carteles informativos duran unos segundos y el
     * estado vuelve a QUIETO; lo que esta trabajando (o espera respuesta) no se
     * toca. */
    if (v.estado == OTA_RX_AL_DIA || v.estado == OTA_RX_ERROR) {
        if (++s_ticks_cartel > (v.estado == OTA_RX_ERROR ? 20 : 6)) {
            s_ticks_cartel = 0;
            ota_rx_olvidar();
            lv_obj_add_flag(s_ota_lbl, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_enlace, LV_OBJ_FLAG_HIDDEN);
            return;
        }
    } else {
        s_ticks_cartel = 0;
    }

    if (v.estado == OTA_RX_QUIETO) {
        lv_obj_add_flag(s_ota_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_enlace, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_ota_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_enlace, LV_OBJ_FLAG_HIDDEN);

    switch (v.estado) {
        case OTA_RX_COMPROBANDO:
            lv_label_set_text(s_ota_lbl, "Buscando version nueva...");
            lv_obj_set_style_text_color(s_ota_lbl, UI_COLOR_TEXT_SOFT, 0);
            break;

        case OTA_RX_HAY_NUEVA: {
            lv_label_set_text_fmt(s_ota_lbl, "Hay version nueva: %s", v.ofrecida);
            lv_obj_set_style_text_color(s_ota_lbl, UI_COLOR_CYAN, 0);
            if (!s_ota_preguntando && !confirm_screen_is_open()) {
                s_ota_preguntando = true;
                const esp_app_desc_t *app = esp_app_get_description();
                snprintf(s_ota_body, sizeof(s_ota_body),
                         "Instalada: %s     Nueva: %s",
                         (app && app->version[0]) ? app->version : "?", v.ofrecida);
                /* Verde = aceptar (derecha), rojo = cancelar (izquierda): la
                 * norma del dueño, y confirm_screen ya la cumple. */
                confirm_screen_open("Actualizar pantalla?", s_ota_body,
                                    UI_COLOR_ORANGE_HEX, "Si, actualizar", "Cancelar",
                                    ota_aceptar_cb, NULL);
                /* El rojo tiene que OLVIDAR la oferta. Sin esto, al cancelar el
                 * estado seguiria en HAY_NUEVA y el cartel volveria a salir en
                 * el siguiente tic: un bucle imposible de cerrar. */
                confirm_screen_no_accion(ota_cancelar_cb, NULL);
            }
            break;
        }

        case OTA_RX_AL_DIA: {
            const esp_app_desc_t *app = esp_app_get_description();
            lv_label_set_text_fmt(s_ota_lbl, "Estas al dia (%s)",
                                  (app && app->version[0]) ? app->version : "?");
            lv_obj_set_style_text_color(s_ota_lbl, UI_COLOR_GREEN, 0);
            break;
        }

        case OTA_RX_DESCARGANDO:
            /* La automatica avisa de donde viene: el dueño subio el .bin desde
             * el movil y no ha tocado la pantalla, asi que el cartel se lo
             * cuenta sin pedirle que confirme nada. */
            if (v.automatica) {
                lv_label_set_text_fmt(s_ota_lbl,
                    "Actualizacion recibida de la P4: descargando... %d %%", v.pct);
            } else {
                lv_label_set_text_fmt(s_ota_lbl, "Descargando... %d %%", v.pct);
            }
            lv_obj_set_style_text_color(s_ota_lbl, UI_COLOR_YELLOW, 0);
            break;

        case OTA_RX_LISTO:
            lv_label_set_text(s_ota_lbl, "Listo, reiniciando");
            lv_obj_set_style_text_color(s_ota_lbl, UI_COLOR_GREEN, 0);
            break;

        case OTA_RX_ERROR:
            lv_label_set_text(s_ota_lbl,
                              v.motivo[0] ? v.motivo : "No se pudo actualizar");
            lv_obj_set_style_text_color(s_ota_lbl, UI_COLOR_RED, 0);
            break;

        default:
            break;
    }
}

/* (aqui vivia brillo_btn_cb, el boton de brillo del menu: quitado el 16-oct-2026)
 *
 * El nivel (100 <-> minimo) se cambia con el DOBLE TOQUE en la pantalla de datos
 * (y con el toque largo, que es lo que sale a la primera): los dos llaman a
 * brillo_alternar(). Lo que SI vive en el menu desde el 21-oct-2026 es el VALOR
 * del minimo, que se elige con los botones - / + (ver brillo_min_cb).
 * OJO: si algun dia se quita tambien el gesto, el nivel no se podria cambiar en
 * marcha (el minimo si, desde aqui). */

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

    /* Sin SSID no hay red que guardar (ver udp_rx_set_credentials): se dice POR
     * QUE en vez del generico "No he podido guardarlo", que no explica nada al
     * que acaba de borrar el campo sin querer. No se toca nada mas: ni la NVS,
     * ni la referencia de "lo que habia", ni las credenciales del portal. */
    if (ssid[0] == '\0') {
        wifi_estado("Falta el SSID: no lo he guardado", false);
        ESP_LOGW(TAG, "Guardado cancelado: el SSID esta vacio");
        return;
    }

    /* Y SIN CLAVE, TAMPOCO (auditoria del 27-oct-2026, hallazgo cab5-core-02):
     * aqui se asocia SIEMPRE con WPA2-PSK (threshold.authmode en udp_rx.c), que
     * exige 8..63 bytes (63 es el tope del campo, ver make_field_en). Antes se
     * guardaba la clave vacia, la UI decia "Guardado, reconectando..." y en el
     * arranque siguiente la pantalla se quedaba SIN P4, porque el fallback de
     * fabrica solo miraba el SSID. Se avisa igual que con el SSID vacio. */
    if (strlen(pass) < 8) {
        wifi_estado("Clave Wi-Fi no valida (minimo 8)", false);
        ESP_LOGW(TAG, "Guardado cancelado: la clave Wi-Fi tiene menos de 8 bytes");
        return;
    }

    bool ok_wifi = udp_rx_set_credentials(ssid, pass);

    /* La referencia pasa a ser lo recien guardado: un segundo toque seguido ya
     * no es un cambio y no volvera a preguntar. */
    snprintf(s_ssid_orig, sizeof(s_ssid_orig), "%s", ssid);
    snprintf(s_pass_orig, sizeof(s_pass_orig), "%s", pass);

    bool ok_portal = (save_portal_creds(lv_textarea_get_text(s_http_user_ta),
                                         lv_textarea_get_text(s_http_pass_ta))
                      == ESP_OK);

    if (ok_wifi && ok_portal) {
        wifi_estado("Guardado, reconectando...", true);
        ESP_LOGI(TAG, "Nuevas credenciales guardadas desde Ajustes");
    } else {
        wifi_estado("No he podido guardarlo", false);
        ESP_LOGW(TAG, "Guardado fallido: wifi=%d portal=%d", ok_wifi, ok_portal);
    }
}

/* Guarda SOLO las credenciales del PORTAL. Es la accion del boton rojo
 * ("Cancelar") del cartel "Cambiar de red?" (D7).
 *
 * POR QUE HACE FALTA: si has cambiado la red, el cartel pregunta antes de
 * tocar la Wi-Fi. Si decides cancelar, hasta ahora se salia sin guardar NADA,
 * asi que lo que habias tecleado en Usuario/Clave del portal se perdia sin
 * avisar (la red no se toca, pero el portal si se puede guardar sin riesgo).
 * Este callback guarda esa parte y lo dice en la linea de estado. */
static void guardar_solo_portal(void *ud)
{
    (void)ud;
    if (save_portal_creds(lv_textarea_get_text(s_http_user_ta),
                          lv_textarea_get_text(s_http_pass_ta)) == ESP_OK) {
        wifi_estado("Guardado.", true);
    } else {
        wifi_estado("No he podido guardarlo", false);
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
        guardar_solo_portal(NULL);
        return;
    }

    static char body[64];   /* estatico: el dialogo lo sigue leyendo despues */
    snprintf(body, sizeof(body), "Red:  %s", ssid);
    /* Naranja, el mismo color del titulo de esta pantalla. */
    confirm_screen_open("Cambiar de red?", body, 0xFF9800,
                        "Si, cambiar", "Cancelar", do_guardar, NULL);
    /* El rojo ("Cancelar") no puede tirar lo tecleado del portal: guarda SOLO
     * esa parte y vuelve al formulario. Se engancha DESPUES de open porque
     * cada apertura limpia la accion del boton izquierdo. */
    confirm_screen_no_accion(guardar_solo_portal, NULL);
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

    wifi_estado("", true);
}

void view_ajustes_create(lv_obj_t *parent)
{
    /* Se guarda la pantalla para que el temporizador de la OTA sepa si esta
     * delante (ver ota_timer_cb). */
    s_raiz = parent;

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
    lv_obj_align(mtitle, LV_ALIGN_CENTER, 0, 0);   /* TODOS los titulos van centrados (usuario, 16-oct) */

    /* DOS ACCIONES EN LA MISMA FILA: Wi-Fi y "Actualizar pantalla".
     *
     * La actualizacion por Wi-Fi la pidio el dueño el 17-oct-2026 y vive aqui
     * porque es donde se mira la version instalada. Comparten fila y alto (110
     * px) para no quitarle sitio a la tarjeta de la version: dos botones de 377
     * px con 14 de aire suman los 768 utiles. */
    lv_obj_t *acciones = lv_obj_create(s_menu);
    lv_obj_set_size(acciones, lv_pct(100), 110);
    lv_obj_set_style_bg_opa(acciones, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(acciones, 0, 0);
    lv_obj_set_style_pad_all(acciones, 0, 0);
    lv_obj_clear_flag(acciones, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *wbtn = lv_btn_create(acciones);
    lv_obj_set_size(wbtn, 377, 110);
    /* SIN flex_grow (16-oct-2026): pedia altura extra y se la quitaba a la
     * tarjeta de la version, que es la que tiene texto que enseñar. El boton se
     * queda en sus 110 px, que ya es un boton grande. */
    lv_obj_set_flex_grow(wbtn, 0);
    lv_obj_set_style_bg_color(wbtn, UI_COLOR_CYAN, 0);
    lv_obj_align(wbtn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(wbtn, wifi_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wlbl = lv_label_create(wbtn);
    lv_label_set_text(wlbl, LV_SYMBOL_WIFI "   Wi-Fi");
    /* Letra 30 y el ajuste automatico de rotulos de estilos.h como red: en 800
     * px de ancho entra de sobra, y asi se lee de lejos. */
    lv_obj_set_style_text_font(wlbl, &lv_font_montserrat_30, 0);
    lv_obj_center(wlbl);

    lv_obj_t *ubtn = lv_btn_create(acciones);
    lv_obj_set_size(ubtn, 377, 110);
    lv_obj_set_flex_grow(ubtn, 0);
    lv_obj_set_style_bg_color(ubtn, UI_COLOR_CYAN, 0);
    lv_obj_align(ubtn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(ubtn, ota_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ulbl = lv_label_create(ubtn);
    lv_label_set_text(ulbl, "Actualizar pantalla");
    /* Letra 26 y no 30: "Actualizar pantalla" es mucho mas largo que "Wi-Fi".
     * Con la 26 entra en los 377 px y se sigue leyendo de lejos. */
    lv_obj_set_style_text_font(ulbl, &lv_font_montserrat_26, 0);
    lv_obj_center(ulbl);

    /* EL MINIMO DE BRILLO (21-oct-2026). El nivel alto es siempre 100 y se
     * alterna con el doble toque en la pantalla de datos; lo que se elige AQUI
     * es el nivel bajo, de 5 en 5 entre 5 y 80 (por encima de 80 este panel no
     * se nota y por debajo de 5 se queda sin luz, ver brillo.h). */
    lv_obj_t *brow = lv_obj_create(s_menu);
    lv_obj_set_size(brow, lv_pct(100), 88);
    lv_obj_set_style_bg_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brow, 0, 0);
    lv_obj_set_style_pad_all(brow, 0, 0);
    lv_obj_clear_flag(brow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *blbl = lv_label_create(brow);
    lv_label_set_text(blbl, "Brillo mínimo");
    lv_obj_set_style_text_font(blbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(blbl, UI_COLOR_TEXT, 0);

    lv_obj_t *bmenos = lv_btn_create(brow);
    lv_obj_set_size(bmenos, 76, 76);
    lv_obj_set_style_radius(bmenos, 38, 0);
    lv_obj_set_style_bg_color(bmenos, UI_COLOR_BOTON, 0);
    lv_obj_add_event_cb(bmenos, brillo_min_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_t *ml = lv_label_create(bmenos);
    lv_label_set_text(ml, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_font(ml, &lv_font_montserrat_28, 0);
    lv_obj_center(ml);

    s_brillo_min_lbl = lv_label_create(brow);
    lv_obj_set_width(s_brillo_min_lbl, 120);
    lv_obj_set_style_text_align(s_brillo_min_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_brillo_min_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_brillo_min_lbl, UI_COLOR_TEXT, 0);
    brillo_min_pinta();

    lv_obj_t *bmas = lv_btn_create(brow);
    lv_obj_set_size(bmas, 76, 76);
    lv_obj_set_style_radius(bmas, 38, 0);
    lv_obj_set_style_bg_color(bmas, UI_COLOR_BOTON, 0);
    lv_obj_add_event_cb(bmas, brillo_min_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_t *pl = lv_label_create(bmas);
    lv_label_set_text(pl, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(pl, &lv_font_montserrat_28, 0);
    lv_obj_center(pl);

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

    /* EL ESTADO DE LA ACTUALIZACION. Comparte sitio con el del enlace (ver
     * ota_timer_cb): nace escondido y se enseña solo cuando hay algo que contar,
     * asi que la fila de dos botones no le quita alto a la tarjeta de abajo. */
    s_ota_lbl = lv_label_create(s_menu);
    lv_obj_set_style_text_font(s_ota_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ota_lbl, UI_COLOR_TEXT_DIM, 0);
    lv_label_set_text(s_ota_lbl, "");
    lv_obj_set_width(s_ota_lbl, lv_pct(100));
    lv_label_set_long_mode(s_ota_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_ota_lbl, LV_OBJ_FLAG_HIDDEN);
    /* 500 ms: el progreso se mueve como mucho cada ~40 ms, asi que medio segundo
     * va sobrado. El temporizador no hace nada si Ajustes no esta delante. */
    if (!s_ota_timer) {
        s_ota_timer = lv_timer_create(ota_timer_cb, 500, NULL);
        if (!s_ota_timer) ESP_LOGW(TAG, "sin temporizador de OTA: no se vera el progreso");
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
    lv_obj_set_style_pad_all(vcard, 8, 0);
    lv_obj_set_style_pad_row(vcard, 4, 0);
    /* CON SCROLL, aunque quepa (16-oct-2026). El usuario vio "informacion que se
     * pierde por encima y por debajo del numero de version": esta tarjeta crece
     * con flex_grow, no tenia scroll y lo que no cabia se RECORTABA en silencio
     * (el aviso estaba escrito aqui mismo, y aun asi le meti dos lineas mas sin
     * darle aire). Con scroll, como mucho hay que deslizar: no se pierde nada. */
    lv_obj_set_scroll_dir(vcard, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(vcard, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(vcard, LV_FLEX_FLOW_COLUMN);
    /* START Y NO CENTER EN EL EJE PRINCIPAL (auditoria del 16-oct-2026): con
     * CENTER, lo que sobresale por ARRIBA de un contenedor con scroll queda
     * FUERA del rango alcanzable -- el scroll solo llega de [-scroll_bottom, 0],
     * asi que lo de arriba no se puede ni deslizar ni tocar, y el cartel rojo de
     * dos lineas de diag_reset_resumen() se recortaba unos 9 px. Alineado
     * ARRIBA, la tarjeta empieza en 0 y todo lo que sobra se alcanza bajando. */
    lv_obj_set_flex_align(vcard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
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
    lv_obj_set_style_text_font(vfecha, &FUENTE_PIE, 0);

    /* Chivato de reinicio (ver diag_reset.c): aparece SOLO si el arranque
     * anterior no lo provoco el contacto (watchdog, panic, cuelgue). Con el
     * ajetreo normal -- esta pantalla arranca con cada contacto, varias veces
     * al dia -- una tarjeta fija seria puro ruido; y este es el unico momento
     * en que se puede saber, porque al siguiente corte de contacto el motivo se
     * pierde. Con la letra de PIE y la tarjeta con scroll (ver arriba). */
    if (diag_reset_fue_fallo()) {
        lv_obj_t *vrein = lv_label_create(vcard);
        lv_label_set_text(vrein, diag_reset_resumen());
        lv_obj_set_style_text_align(vrein, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(vrein, UI_COLOR_RED, 0);
        lv_obj_set_style_text_font(vrein, &FUENTE_PIE, 0);
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
            lv_obj_set_style_text_font(vmot, &FUENTE_PIE, 0);
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
    lv_obj_set_style_text_font(s_uptime_lbl, &FUENTE_PIE, 0);

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
    /* 31 Y 63, no 32 y 64 (16-oct-2026): el campo del driver (wifi_config_t,
     * sta.ssid[32] / sta.password[64]) reserva un byte para el cero final, asi
     * que el SSID mas largo que admite son 31 caracteres y la clave 63. Con el
     * tope en 32/64 se podia guardar un valor que wifi_configure_sta() recortaba
     * en silencio (ssid[31] = 0) y el resultado era un NO_AP_FOUND que parecia
     * "la P4 no emite" -- con un simple ESP_LOGW en el log. El editor a pantalla
     * completa hereda este tope solo (entry_screen_open copia el max_length del
     * campo de destino: ver el comentario de make_field). */
    s_ssid_ta = make_field_en(s_wifi, "SSID", 31, false, FILA_F1_Y);          /* s_ssid[33] en udp_rx.c */
    s_pass_ta = make_field_en(s_wifi, "Password", 63, true, FILA_F2_Y);      /* s_pass[65] en udp_rx.c */

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
    lv_obj_set_size(s_status_lbl, FILA_ESTADO_W, FILA_ESTADO_H);
    lv_obj_set_pos(s_status_lbl, FILA_ESTADO_X, FILA_ESTADO_Y);
    /* FUENTE_PIE (Inter 18) y no el alias de la 14, que en esta pantalla acaba
     * en la Inter 26: 33 px de linea no cabian en la caja de 26 (D4). */
    lv_obj_set_style_text_font(s_status_lbl, &FUENTE_PIE, 0);
    lv_label_set_long_mode(s_status_lbl, LV_LABEL_LONG_WRAP);
    wifi_estado("", true);

    /* Se entra siempre por el menu. */
    mostrar_menu(true);
}
