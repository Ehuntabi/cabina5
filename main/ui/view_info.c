/* view_info.c - Pantalla de info agrupada (Fase 1).
 *
 * Patron de colores/umbrales y LEDs de agua portado del satelite viejo C6
 * (victron_mini/main/ui/view_quad.c, retirado el 17-sep-2026), reescrito como
 * grid (todas las cards visibles a la vez, sin rotacion) porque aqui hay
 * mucho mas sitio que en la pantalla de 320x172 del mini.
 *
 * REPARTO (rehecho el 22-ago-2026: la version anterior eran CINCO tarjetas del
 * mismo peso y el usuario la describio como "horrible" -- todo pesaba igual,
 * los numeros eran pequenos y habia demasiados bordes de colores):
 *
 *   +--------------------------------------------------+
 *   |  BATERIA          62%   13,43 V  -3,0 A   MOTOR  |  <- toda la franja
 *   |  ##################............          12,8 V  |     de arriba
 *   +---------------------------+----------------------+
 *   |  AGUAS                    |  TEMPERATURAS        |
 *   +---------------------------+----------------------+
 *
 * La bateria manda: es lo que se mira el 90% de las veces, asi que se lleva la
 * franja entera, el numero mas grande de la pantalla y una BARRA de carga que
 * antes no existia. La del motor va dentro, que es bateria tambien y solo tiene
 * un dato. Las dos temperaturas comparten tarjeta (peticion del usuario) y
 * aguas encoge: antes ocupaba un cuarto de la pantalla para mostrar cinco
 * lucecitas.
 */
#include "estilos.h"   /* ESC(): la escala de la pantalla */
#include "view_info.h"
#include "brillo.h"
#include "net/viaje_cola.h"   /* VIAJE_COLA_CAPACIDAD, para el aviso de casi llena */
#include "net/p4_api.h"       /* mandar la orden de silencio a la P4 */
#include "net/mini_proto.h"   /* MINI_ALARM_*, el byte de alarmas de la telemetria */
#include "nav.h"
#include "view_registro.h"    /* view_registro_puntual_declarar_llegada() */
#include "../data_model.h"
#include "../net/udp_rx.h"
#include "lv_port_compat.h"       /* lvgl_port_lock/unlock, ver view_info_set_pendientes() */
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>

#define COL_CARD_BG_TOP  lv_color_hex(0x0A0A0A)
#define COL_CARD_BG_BOT  lv_color_hex(0x161616)
#define COL_TEXT         lv_color_hex(0xFFFFFF)
/* ── Paleta con dos modos ──────────────────────────────────────────────────
 * El 30% de brillo se queda con los colores de siempre. Con el 100% (que es lo
 * que se usa cuando hay sol) se sube el contraste: los azules y los grises se
 * cambian por blanco, porque en un TFT al sol el azul es lo primero que
 * desaparece, y los bordes de las tarjetas pasan a blanco. Se cambia con el
 * mismo doble toque que el brillo: un gesto, las dos cosas. 30-sep-2026. */
static bool s_contraste = false;

static inline lv_color_t col2(uint32_t normal, uint32_t contraste)
{
    return lv_color_hex(s_contraste ? contraste : normal);
}

/* Objetos pintados una sola vez (bordes y rotulos): se apuntan aqui para
 * repintarlos cuando cambia el modo. Las cifras no hacen falta: se repintan
 * solas cada 500 ms. */
#define PALETA_MAX 48
static lv_obj_t   *s_pal_obj[PALETA_MAX];
static uint32_t    s_pal_normal[PALETA_MAX];
static uint32_t    s_pal_contraste[PALETA_MAX];
static bool        s_pal_borde[PALETA_MAX];
static int         s_pal_n;

/* Registra (y pinta) un rotulo: color normal y color en modo contraste. */
static void paleta_texto(lv_obj_t *o, uint32_t normal, uint32_t contraste)
{
    if (!o) return;
    lv_obj_set_style_text_color(o, lv_color_hex(s_contraste ? contraste : normal), 0);
    if (s_pal_n < PALETA_MAX && !s_pal_borde[s_pal_n]) {
        s_pal_obj[s_pal_n] = o;
        s_pal_normal[s_pal_n] = normal;
        s_pal_contraste[s_pal_n] = contraste;
        s_pal_borde[s_pal_n] = false;
        s_pal_n++;
    }
}

/* Lo mismo para el borde de una tarjeta. */
static void paleta_borde(lv_obj_t *o, uint32_t normal, uint32_t contraste)
{
    if (!o) return;
    lv_obj_set_style_border_color(o, lv_color_hex(s_contraste ? contraste : normal), 0);
    if (s_pal_n < PALETA_MAX) {
        s_pal_obj[s_pal_n] = o;
        s_pal_normal[s_pal_n] = normal;
        s_pal_contraste[s_pal_n] = contraste;
        s_pal_borde[s_pal_n] = true;
        s_pal_n++;
    }
}

/* Valor RGB888 (0xRRGGBB) de un color, que es el espacio en el que estan
 * escritos los literales de abajo.
 *
 * OJO (migracion a LVGL 9): aqui antes se usaba lv_color_to32(), que en la v9
 * devuelve una ESTRUCTURA lv_color32_t, no un numero. Se construye el valor a
 * mano con los canales, que en LVGL 9 son campos con nombre. */
static uint32_t color_rgb888(lv_color_t c)
{
    return ((uint32_t)c.red << 16) | ((uint32_t)c.green << 8) | (uint32_t)c.blue;
}

/* Registra un borde del que solo tenemos el color normal: en modo contraste,
 * los azules y los grises pasan a blanco; el naranja y el amarillo se quedan,
 * que ya se leen bien. */
static void paleta_borde_auto(lv_obj_t *o, lv_color_t color)
{
    uint32_t normal = color_rgb888(color);
    uint32_t contraste = normal;
    switch (normal) {
        case 0x4FC3F7: case 0x66CCFF: case 0x29B6F6: case 0x888888:
            contraste = 0xFFFFFF; break;
        case 0xFFD54F:
            contraste = 0xFFEB3B; break;
        default:
            break;
    }
    paleta_borde(o, normal, contraste);
}

/* Repinta todo lo registrado con el modo actual. */
static void paleta_aplicar(void)
{
    for (int i = 0; i < s_pal_n; ++i) {
        lv_color_t c = lv_color_hex(s_contraste ? s_pal_contraste[i] : s_pal_normal[i]);
        if (s_pal_borde[i]) lv_obj_set_style_border_color(s_pal_obj[i], c, 0);
        else                lv_obj_set_style_text_color(s_pal_obj[i], c, 0);
    }
}

/* Modo contraste: la maquinaria sigue aqui, pero YA NO LO ENCIENDE NADIE.
 *
 * ── POR QUE (9-oct-2026) ───────────────────────────────────────────────────
 *
 * Estaba atado al brillo: al 100 % se ponia el contraste alto y al nivel bajo se
 * quedaba el color. Eso tiene sentido para el sol, pero en la placa de 5" ha
 * dado dos problemas seguidos, los dos contados por el usuario:
 *
 *   1. "es blanco y negro en la pantalla de datos (bateria etc)". Y era verdad:
 *      al pasar el arranque al 100 % (ver brillo.c), la pantalla de datos se
 *      quedaba SIEMPRE en contraste alto, o sea gris/blanca, en vez de con los
 *      colores de la referencia de la 3,5" (naranja de bateria, azul de aguas,
 *      azul de temperaturas). La captura de como tiene que verse es
 *      .scratch/cabina/screenshot/info.png.
 *   2. "es o encendido o apagado, no 100% 50%": al tocar el brillo cambiaba
 *      TAMBIEN toda la paleta, asi que los dos estados no se veian como el mismo
 *      cuadro mas claro o mas oscuro, sino como dos pantallas distintas.
 *
 * AHORA: el color es siempre el mismo y el boton cambia SOLO la luz. El modo
 * contraste no se borra (la paleta, el registro y esta funcion siguen enteros)
 * por si algun dia se quiere un "modo sol" con su propio boton; lo que se quita
 * es que vaya pegado al brillo. */
void view_info_set_contraste(bool activo)
{
    if (s_contraste == activo) return;
    /* El GPS se pinta en refresh_cb(), que ademas decide su color por estado
     * (gris/naranja/verde): no puede ir en la paleta generica, o el modo
     * contraste le borraria el estado. Se repinta el solo en el siguiente
     * tick (500 ms), asi que aqui no hay nada que hacer. */
    s_contraste = activo;
    if (lvgl_port_lock(200)) {
        paleta_aplicar();
        lvgl_port_unlock();
    }
    ESP_LOGI("view_info", "Contraste %s (a mano; ya no sigue al brillo)",
             activo ? "ALTO" : "normal");
}

#define COL_TEXT_DIM     col2(0x888888, 0xE0E0E0)
/* Mas claro que COL_TEXT_DIM: para rotulos que hay que poder leer de un
 * vistazo (la escala de aguas, los nombres de las temperaturas) y no solo
 * intuir. El 0x888888 vale para lo accesorio, no para esto. */
#define COL_TEXT_ESCALA  col2(0xCCCCCC, 0xFFFFFF)
#define COL_VAL_GOOD     col2(0x4CD964, 0xFFFFFF)
#define COL_VAL_WARN     col2(0xFFD54F, 0xFFEB3B)
#define COL_VAL_BAD      col2(0xFF4444, 0xFF6E6E)
#define COL_CONN_NONE    lv_color_hex(0x555555)
#define COL_CONN_OK      lv_color_hex(0x00C851)
#define COL_CONN_LOST    lv_color_hex(0xFF4444)
#define CONN_TIMEOUT_MS  5000

#define COL_BORDER_BAT   col2(0xFF9800, 0xFFFFFF)  /* SmartShunt naranja */
#define COL_BORDER_AUX   col2(0x4FC3F7, 0xFFFFFF)  /* Bateria motor cyan */
#define COL_BORDER_COLD  col2(0x66CCFF, 0xFFFFFF)  /* Frigo azul claro */
#define COL_BORDER_WATER col2(0x29B6F6, 0xFFFFFF)  /* Aguas azul saturado */
#define COL_BORDER_HEAT  col2(0xFFD54F, 0xFFEB3B)  /* Exterior amarillo calido */

/* Bateria: la tarjeta grande de arriba, con sus piezas sueltas. */
static lv_obj_t   *s_bat_card;
static lv_obj_t   *s_bat_dot;
/* El marco de GPS de arriba: es el que lleva el punto de conexion y el icono
 * del GPS desde el 9-oct-2026 (pedido: "los iconos gps y el led verde de
 * comunicacion llevalos arriba"). Antes iban los dos dentro de la tarjeta de
 * bateria, que ahora es una columna estrecha y no tiene sitio para ellos. */
static lv_obj_t   *s_gps_card;
static lv_obj_t   *s_bat_soc;      /* el numero, DENTRO del dibujo */
static lv_obj_t   *s_bat_volt;     /* solo el numero, alineado a la derecha */
static lv_obj_t   *s_bat_amp;
static lv_obj_t   *s_bat_amp_u;    /* la "A", que se tine con el signo */
static lv_obj_t   *s_aux_val;      /* bateria motor, en la misma tarjeta */

/* Abajo: aguas a la izquierda, las dos temperaturas a la derecha. */
static lv_obj_t   *s_water_card;
static lv_obj_t   *s_led_clean[4];
static lv_obj_t   *s_led_gray;
static lv_obj_t   *s_lbl_gray;     /* el rotulo, que tambien avisa */
static lv_obj_t   *s_frigo_val;
static lv_obj_t   *s_frigo_trend;  /* flecha de tendencia */
static lv_obj_t   *s_ext_trend;
static lv_obj_t   *s_pendientes;

/* Definidas abajo, con el resto de la pastilla. */
static void pendientes_aplicar(void *arg);
static void pendientes_click_cb(lv_event_t *e);
static lv_obj_t   *s_gps;             /* indicador de GPS de la P4 */
static lv_obj_t   *s_frigo_fan_track;
static lv_obj_t   *s_frigo_fan_fill;
static lv_obj_t   *s_ext_val;
static lv_timer_t *s_refresh_timer;

static lv_color_t color_for_soc(int16_t soc_deci) {
    if (soc_deci >= 600) return COL_VAL_GOOD;
    if (soc_deci >= 300) return COL_VAL_WARN;
    return COL_VAL_BAD;
}

/* === Silenciar las alarmas desde aqui (14-sep-2026) =======================
 *
 * El pitido lo hace la P4: lleva el codec ES8311 y su amplificador. Esta
 * pantalla no tiene altavoz, asi que callarlo desde el asiento del conductor es
 * mandarle una orden por el portal (net/p4_api.c). El diseño esta cerrado en
 * /home/jc/DS_joint/especificacion_silenciar_desde_cabina.md:
 *
 *   - el icono del altavoz va en la ESQUINA de la tarjeta que esta en alarma, no
 *     un cartel grande: no puede tapar los datos que se miran en marcha;
 *   - la zona tactil es MAYOR que el dibujo (lv_obj_set_ext_click_area, el mismo
 *     truco que las flechas del historico de la P4);
 *   - alterna: el primer toque silencia (altavoz tachado, en gris), el segundo
 *     vuelve a habilitar el sonido. Sin menus y sin confirmacion;
 *   - tocar la propia tarjeta tambien silencia, y ademas deja abierto lo que
 *     hace esa tarjeta (ver alarm_card_cb);
 *   - se calla SOLO el sonido: la tarjeta sigue parpadeando y su aviso sigue
 *     diciendo que pasa, con "(silenciada)" detras.
 *
 * CUAL esta en alarma lo dice la P4 en la telemetria (mini_msg.alarmas), no lo
 * deduce esta pantalla de los niveles: los umbrales de la bateria y del
 * congelador son suyos y aqui no se saben. Ese byte dice que la condicion se
 * cumple, suene alli o este silenciada, y por eso el dibujo del icono (tachado
 * o no) lo decide el estado local de ESTA pantalla, que es quien ha mandado la
 * orden. */
typedef enum {
    AL_INFO_AGUA = 0,
    AL_INFO_GRISES,
    AL_INFO_BATERIA,
    AL_INFO_CONGELADOR,
    AL_INFO_CUANTAS
} al_info_t;

/* El mismo orden que el enum, con los bits de mini_proto.h. */
static const uint8_t AL_INFO_BIT[AL_INFO_CUANTAS] = {
    MINI_ALARM_AGUA, MINI_ALARM_GRISES, MINI_ALARM_BATERIA, MINI_ALARM_CONGELADOR
};

/* Nombres CORTOS, para la cabina y para el aviso de la orden ("agua", no
 * "agua limpia en reserva"): en 480x320 y de reojo, cuanto menos texto mejor. */
static const char *const AL_INFO_NOMBRE[AL_INFO_CUANTAS] = {
    "agua", "grises", "bateria", "congelador"
};

/* Estado local de cada alarma. */
static struct {
    lv_obj_t *icono;        /* el altavoz, DENTRO de su tarjeta */
    bool      silenciada;   /* lo que hemos mandado callar */
    bool      orden_pend;   /* hay una orden en vuelo para esta alarma */
    uint32_t  orden_ms;     /* cuando se mando (para el plazo de respuesta) */
    uint32_t  orden_seq;    /* numero de la orden en vuelo: con el viaja el
                             * user_data y sirve para ignorar respuestas viejas */
} s_al[AL_INFO_CUANTAS];

static lv_obj_t   *s_orden_msg;      /* aviso de "enviado" / "sin respuesta" */
static uint32_t    s_orden_msg_ms;   /* cuando se puso, 0 = nada que enseñar */

#define ORDEN_TIMEOUT_MS   10000   /* sin respuesta en 10 s: se deshace y se dice */
#define ORDEN_MSG_MS        4000   /* cuanto se queda el aviso en pantalla */
#define AL_ICONO_EXTRA        14   /* zona tactil extra, como en la P4 */

/* Las tarjetas se montan mas arriba en este fichero que el resto del bloque de
 * alarmas (make_water_cell esta antes), asi que hace falta anunciarlas. */
static lv_obj_t *al_icono_crear(lv_obj_t *card, al_info_t i,
                                lv_align_t alineacion, lv_coord_t x, lv_coord_t y);
static void al_card_cb_alarma(lv_event_t *e);
static void al_card_cb_agua(lv_event_t *e);
static void al_icono_cb_alarma(lv_event_t *e);
static bool al_info_activa(al_info_t i);

static lv_color_t color_for_frigo(int16_t centi) {
    if (centi <= -1500) return COL_VAL_GOOD;  /* <= -15C ok */
    if (centi <= -500)  return COL_VAL_WARN;  /* -15..-5 aviso */
    return COL_VAL_BAD;                        /* > -5 mal */
}

/* === Dibujo de bateria de coche ==========================================
 *
 * El porcentaje NO es un numero al lado de una barra: es el RELLENO de una
 * bateria dibujada, que sube y baja y cambia de color. Se entiende sin leer.
 *
 * Todo con lv_obj rectangulares, sin imagenes: en este entorno no hay pipeline
 * de PNG a LVGL, y ademas asi escala y cambia de color solo. Mismo criterio que
 * la burbuja del nivel.
 *
 *      ##   ##      <- bornes
 *    +--------+
 *    |        |
 *    |%%%%%%%%|     <- relleno, pegado abajo, alto proporcional al SoC
 *    +--------+
 */
/* Medidas del dibujo de la bateria.
 *
 * SUBIDAS EL 8-OCT-2026 al adaptar la pantalla a sus 800x480 reales (antes la
 * UI dibujaba en 480x320 logicos, ver estilos.h): la tarjeta de bateria pasa de
 * 138 a 230 px de alto y de 472 a 792 de ancho, asi que el dibujo que la
 * preside se queda pequeno si no crece con ella. Se escala 1,6 (86 -> 138 de
 * alto), que es lo que crece la tarjeta de alto, para que el dibujo siga
 * mandando en la franja en vez de nadar en ella.
 *
 * Las letras NO se tocan: el tamano de letra ya estaba calculado para los
 * 181 ppp de esta pantalla, y ese numero no ha cambiado. Lo que cambia es el
 * sitio que hay alrededor. */
#define BAT_W        150
#define BAT_H        156
#define BAT_BORNE_W   26
#define BAT_BORNE_H   11

/* Hueco fijo del numero de tension/corriente, con la unidad justo detras. Ver
 * el porque en view_info_create. */
#define BAT_NUM_W     160
#define BAT_NUM_X      28

/* Hueco fijo de las temperaturas, con la flecha de tendencia detras. */
#define TEMP_NUM_W    190

/* Alto de las dos filas de la rejilla (ver view_info_create) y, a partir de
 * ellos, donde cae cada cosa dentro de la tarjeta de bateria.
 *
 * 210 (bateria) + 4 de hueco + 246 (aguas y temperaturas) + 8 de margen = 480
 * clavados: la rejilla no depende de que la pantalla mida lo que mide hoy. */
/* ── REPARTO NUEVO (9-oct-2026): TRES FILAS ─────────────────────────────────
 *
 * A peticion del usuario: "trasladar la card bateria abajo y dejar el hueco que
 * ocupa ahora solo con marco hasta que se llene de contenido gps". O sea:
 *
 *     fila 0:  [ MARCO GPS, vacio, todo el ancho ]   <- se llenara con
 *     fila 1:  [ BATERIA, todo el ancho ]               velocidad, satelites,
 *     fila 2:  [ AGUAS | TEMPERATURAS ]                 km y tiempo de viaje
 *
 * Los 472 utiles (480 menos los 4+4 de margen) se reparten: 116 + 4 + 184 + 4 +
 * 164. La bateria baja de 210 a 184 -- su dibujo mide 138 y el titulo 33, o sea
 * 171 + 8 de relleno = 179, asi que 184 es lo justo. */
#define GPS_CARD_H      116
#define BAT_CARD_H      184
/* 246 era el alto fijo de la fila de abajo. Ya no se usa: esa fila es elastica
 * (LV_GRID_FR(1)) y se lleva todo el alto que sobre, que es lo que quita los
 * 12 px muertos del fondo. Se deja el numero documentado por si hay que volver. */
#define TARJETAS_CARD_H 246   /* (ya no se usa: ver row_dsc) */

/* El titulo de la tarjeta ocupa 8 (relleno) + 24 (letra) + 10 de aire = 42 */
#define BAT_TITULO_H     42

/* El bloque dibujo+bornes (BAT_H + BAT_BORNE_H) centrado en el hueco que queda
 * por debajo del titulo. */
#define BAT_DIB_Y  (BAT_TITULO_H + (BAT_CARD_H - 2 * 8 - BAT_TITULO_H \
                                    - (BAT_H + BAT_BORNE_H)) / 2)

/* Los dos numeros de la bateria (tension y corriente) van en columna a la
 * izquierda del dibujo, y la columna se coloca en el MISMO sitio que el dibujo:
 * si se dejan centrados en la tarjeta entera, con los 210 px de ahora salen
 * mas bajos que la bateria y no se leen como una fila. */
#define BAT_NUM_Y  ((BAT_DIB_Y + BAT_BORNE_H + BAT_H / 2) - 44)


static lv_obj_t *s_bat_relleno;

static lv_obj_t *make_bateria_dibujo(lv_obj_t *padre)
{
    /* Contenedor del conjunto: cuerpo + los dos bornes que sobresalen arriba. */
    lv_obj_t *cont = lv_obj_create(padre);
    lv_obj_set_size(cont, BAT_W, BAT_H + BAT_BORNE_H);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < 2; i++) {
        lv_obj_t *borne = lv_obj_create(cont);
        lv_obj_set_size(borne, BAT_BORNE_W, BAT_BORNE_H + 4);   /* +4: se meten
                                                                 * bajo el cuerpo
                                                                 * para que no se
                                                                 * vea la union */
        lv_obj_set_style_bg_color(borne, lv_color_hex(0x888888), 0);
        lv_obj_set_style_bg_opa(borne, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(borne, 0, 0);
        lv_obj_set_style_radius(borne, 2, 0);
        lv_obj_set_style_pad_all(borne, 0, 0);
        lv_obj_clear_flag(borne, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(borne, LV_ALIGN_TOP_LEFT, i == 0 ? 22 : BAT_W - 22 - BAT_BORNE_W, 0);
    }

    lv_obj_t *cuerpo = lv_obj_create(cont);
    lv_obj_set_size(cuerpo, BAT_W, BAT_H);
    lv_obj_set_style_bg_color(cuerpo, lv_color_hex(0x0E0E0E), 0);
    lv_obj_set_style_bg_opa(cuerpo, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cuerpo, lv_color_hex(0x888888), 0);
    lv_obj_set_style_border_width(cuerpo, 3, 0);
    lv_obj_set_style_radius(cuerpo, 6, 0);
    lv_obj_set_style_pad_all(cuerpo, 0, 0);
    lv_obj_clear_flag(cuerpo, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(cuerpo, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* El relleno crece desde ABAJO: se ancla al fondo y solo cambia de alto. */
    s_bat_relleno = lv_obj_create(cuerpo);
    lv_obj_set_width(s_bat_relleno, lv_pct(100));
    lv_obj_set_height(s_bat_relleno, 0);
    lv_obj_set_style_bg_opa(s_bat_relleno, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_bat_relleno, 0, 0);
    lv_obj_set_style_radius(s_bat_relleno, 3, 0);
    lv_obj_set_style_pad_all(s_bat_relleno, 0, 0);
    lv_obj_clear_flag(s_bat_relleno, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_bat_relleno, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* El numero va DENTRO y encima del relleno (se crea despues, asi queda por
     * delante en el orden de dibujo). */
    s_bat_soc = lv_label_create(cuerpo);
    lv_label_set_text(s_bat_soc, "--");
    lv_obj_set_style_text_font(s_bat_soc, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_bat_soc, COL_TEXT, 0);
    lv_obj_clear_flag(s_bat_soc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(s_bat_soc);

    return cont;
}

/* === LEDs de aguas (mismo patron que view_quad.c del mini) ============= */
static void led_blink_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, v, 0);
    lv_obj_set_style_border_opa((lv_obj_t *)var, v, 0);
}

/* mode: 0 = apagado - 1 = lleno (azul) - 2 = alarma (rojo parpadeando)
 *
 * Idempotente: si ya estaba en ese modo, no hace nada. Sin esto, cada tick
 * del refresco (cada 500 ms, aunque el estado no haya cambiado) borraba la
 * animacion de parpadeo y la volvia a arrancar desde cero -- el parpadeo
 * nunca llegaba a completar ni un ciclo (400 ms subida + 400 ms bajada,
 * reiniciado cada 500 ms). El modo se guarda en el user_data del propio LED
 * (libre, nada mas lo usa aqui). Detectado auditando el 07-sep-2026. */
static void led_set(lv_obj_t *led, uint8_t mode)
{
    if ((uint8_t)(uintptr_t)lv_obj_get_user_data(led) == mode) return;
    lv_obj_set_user_data(led, (void *)(uintptr_t)mode);

    lv_anim_del(led, led_blink_cb);
    if (mode == 2) {
        lv_obj_set_style_bg_color(led, COL_VAL_BAD, 0);
        lv_obj_set_style_border_color(led, COL_VAL_BAD, 0);
        lv_obj_set_style_bg_opa(led, LV_OPA_COVER, 0);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, led);
        lv_anim_set_exec_cb(&a, led_blink_cb);
        lv_anim_set_values(&a, 255, 60);
        lv_anim_set_time(&a, 400);
        lv_anim_set_playback_time(&a, 400);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    } else {
        lv_obj_set_style_bg_opa(led, LV_OPA_COVER, 0);
        lv_obj_set_style_border_opa(led, LV_OPA_COVER, 0);
        if (mode == 1) {
            lv_obj_set_style_bg_color(led, lv_color_hex(0x4FC3F7), 0);
            lv_obj_set_style_border_color(led, lv_color_hex(0x4FC3F7), 0);
        } else {
            lv_obj_set_style_bg_color(led, lv_color_hex(0x141414), 0);
            lv_obj_set_style_border_color(led, lv_color_hex(0x333333), 0);
        }
    }
}

/* Medidas del indicador de aguas. La tarjeta mide ahora ~166 px de alto y ~278
 * de ancho (antes 123 x 180): con las medidas viejas el indicador se quedaba
 * pequeno y nadando en la tarjeta. Los segmentos crecen de 11x56 a 16x84 y su
 * separacion de 1 a 3 px.
 *
 * Con 16 de segmento salian 4*16 + 3*3 = 73 px de columna, y con el titulo (25)
 * suman 98 de los ~150 utiles: sigue sobrando alto a proposito, porque las
 * fracciones ("1/4") en letra 18 ocupan 22 px por fila y son las que mandan.
 *
 * Los de limpia son SEGMENTOS anchos y bajos, no cuadraditos: apilados leen
 * como un deposito. El de grises es un bloque del alto de dos segmentos y algo
 * mas, para que pese lo mismo que la columna sin ser un puntito. */
#define LED_SEG_W    84
/* ── LAS FRACCIONES MANDA EL ALTO DE LA FILA, Y ESO FUE UN FALLO (9-oct-2026) ─
 *
 * El usuario lo describio asi: "en aguas el 1/4 se ve, 2/4, 3/4 y 4/4 se
 * recortan". La causa: el comentario de aqui decia que la fraccion se pinta "en
 * letra 18, que ocupa 22 px de alto", y con eso la fila se fijo en 22. Pero la
 * letra que se pide de verdad es `lv_font_montserrat_14`, y estilos.h traduce
 * ese nombre ENCADENANDO las macros (14 -> 20 -> 26): la letra que se pinta es
 * la 26, cuya linea mide 33 px. Con la fila en 22 el rotulo salia cortado.
 *
 * Ahora el alto de la fila NO se escribe a mano: se le pregunta a la fuente
 * (lv_font_get_line_height) en el sitio donde se crea. Asi, si algun dia cambia
 * la escala, la fila crece sola en vez de recortar el texto otra vez.
 *
 * El segmento sube de 22 a 27 para seguir leyendose como un deposito: con la
 * fila en 33 y el segmento en 22 quedaba demasiado aire entre barra y barra. */
#define LED_SEG_H    18
#define LED_SEG_GAP   3
/* Hueco de la fraccion: se calcula del texto de verdad (ver el bucle), no a
 * mano; 38 se queda como el minimo por si la fuente no midiera nada. */
#define LED_FRAC_W   38
/* Grises: MISMA forma y ancho que los segmentos de limpia, pero de una pieza en
 * vez de cuatro. Antes era un circulo, y un redondel grande al lado de una
 * columna de rectangulos quedaba raro. Se distingue de sobra por ser un bloque
 * unico y por su rotulo; no hace falta cambiarle la forma.
 * Tan alto como la columna de limpia ENTERA, rotulo incluido, para que el
 * conjunto quede cuadrado. */
#define LED_GRIS_H   (LED_SEG_H * 4 + LED_SEG_GAP * 3 - 10)

static lv_obj_t *make_led(lv_obj_t *parent, bool round)
{
    lv_obj_t *led = lv_obj_create(parent);
    lv_obj_set_size(led, LED_SEG_W, round ? LED_GRIS_H : LED_SEG_H);
    lv_obj_set_style_radius(led, 3, 0);
    lv_obj_set_style_border_width(led, 2, 0);
    lv_obj_set_style_pad_all(led, 0, 0);
    lv_obj_clear_flag(led, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    /* Sentinela que no es ningun modo real (0/1/2): fuerza a que la primera
     * llamada a led_set() de verdad aplique el estilo en vez de creerse que
     * "ya esta en modo 0" solo porque el user_data por defecto tambien es 0. */
    lv_obj_set_user_data(led, (void *)(uintptr_t)0xFF);
    led_set(led, 0);
    return led;
}

static void make_water_cell(lv_obj_t *grid, uint8_t col, uint8_t span, uint8_t row)
{
    lv_obj_t *card = lv_obj_create(grid);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, col, span, LV_GRID_ALIGN_STRETCH, row, 1);
    lv_obj_set_style_bg_color(card, COL_CARD_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(card, COL_CARD_BG_BOT, 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    paleta_borde(card, 0x29B6F6, 0xFFFFFF);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    /* Mismo burbujeo que make_card(): asi el toque en la tarjeta sube hasta la
     * pantalla, donde escucha el doble toque del brillo. */
    lv_obj_add_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE);
    s_water_card = card;

    /* Esta tarjeta ya NO lleva punto de conexion. Habia dos, aqui y en la de
     * bateria, y los DOS se pintaban del mismo color calculado una sola vez:
     * eran el mismo indicador repetido. El usuario pregunto si significaban
     * cosas distintas -- que es justo lo que sugiere ver dos -- y no. Se queda
     * el de la tarjeta de bateria, que es la principal. */
    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, "AGUAS");
    paleta_texto(t, 0x29B6F6, 0xFFFFFF);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);

    /* Los cuatro de agua limpia van en COLUMNA y se encienden de ABAJO ARRIBA,
     * como se llena un deposito de verdad. En fila no significaban nada: cuatro
     * lucecitas en horizontal no dicen "nivel".
     *
     * COLUMN_REVERSE es lo que pone el indice 0 abajo, que es el primero que se
     * enciende (ver refresh_aguas). Con COLUMN normal el deposito se llenaria
     * por el techo. */
    lv_obj_t *fila = lv_obj_create(card);
    lv_obj_set_size(fila, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(fila, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fila, 0, 0);
    lv_obj_set_style_pad_all(fila, 0, 0);
    lv_obj_set_style_pad_column(fila, 18, 0);
    lv_obj_set_flex_flow(fila, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fila, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(fila, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    /* Desplazada 10 px a la izquierda del centro: centrada del todo, la columna
     * de agua limpia con sus fracciones quedaba metida hacia dentro de la
     * tarjeta. Peticion del usuario, 23-ago-2026. */
    lv_obj_align(fila, LV_ALIGN_CENTER, -10, 8);

    /* Columna de segmentos de agua limpia, cada uno con su fraccion al lado */
    lv_obj_t *col_limpia = lv_obj_create(fila);
    lv_obj_set_size(col_limpia, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col_limpia, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col_limpia, 0, 0);
    lv_obj_set_style_pad_all(col_limpia, 0, 0);
    lv_obj_set_style_pad_row(col_limpia, 3, 0);
    lv_obj_set_flex_flow(col_limpia, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col_limpia, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(col_limpia, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *leds = lv_obj_create(col_limpia);
    lv_obj_set_size(leds, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(leds, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(leds, 0, 0);
    lv_obj_set_style_pad_all(leds, 0, 0);
    lv_obj_set_style_pad_row(leds, LED_SEG_GAP, 0);
    lv_obj_set_flex_flow(leds, LV_FLEX_FLOW_COLUMN_REVERSE);
    lv_obj_set_flex_align(leds, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(leds, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    /* Cada segmento va en su propia fila junto a su fraccion, para que numero y
     * barra queden a la misma altura. El indice 0 es 1/4 y queda ABAJO
     * (COLUMN_REVERSE): el deposito se llena de abajo arriba. */
    static const char *const FRACCION[4] = { "1/4", "2/4", "3/4", "4/4" };
    /* La fuente de las fracciones y SUS medidas de verdad, preguntadas a la
     * fuente (ver el comentario de LED_SEG_H: darlas por supuestas es lo que
     * recortaba el texto). */
    /* lv_font_montserrat_18 y NO la 14: la 14 es macro en estilos.h y el
     * preprocesador la encadena hasta la 26 (linea de 33 px), que con la fila
     * de aguas ya recortada por el reparto de tres filas no cabe. La 18 no es
     * macro: se queda en 18, con linea de 22. */
    const lv_font_t *fuente_frac = &lv_font_montserrat_18;
    const lv_coord_t frac_alto  = lv_font_get_line_height(fuente_frac);
    const lv_coord_t frac_ancho = texto_ancho(fuente_frac, FRACCION[3]) + 4;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *fila_seg = lv_obj_create(leds);
        lv_obj_set_size(fila_seg, frac_ancho + 12 + LED_SEG_W, frac_alto);
        lv_obj_set_style_bg_opa(fila_seg, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(fila_seg, 0, 0);
        lv_obj_set_style_pad_all(fila_seg, 0, 0);
        lv_obj_set_style_pad_column(fila_seg, 12, 0);
        lv_obj_set_flex_flow(fila_seg, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(fila_seg, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(fila_seg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *frac = lv_label_create(fila_seg);
        lv_label_set_text(frac, FRACCION[i]);
        paleta_texto(frac, 0xCCCCCC, 0xFFFFFF);
        lv_obj_set_style_text_font(frac, fuente_frac, 0);
        lv_obj_set_width(frac, frac_ancho);
        lv_obj_set_style_text_align(frac, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_clear_flag(frac, LV_OBJ_FLAG_CLICKABLE);

        s_led_clean[i] = make_led(fila_seg, false);
    }


    /* Grises: uno solo, es alarma de lleno, no nivel */
    lv_obj_t *col_gris = lv_obj_create(fila);
    lv_obj_set_size(col_gris, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col_gris, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col_gris, 0, 0);
    lv_obj_set_style_pad_all(col_gris, 0, 0);
    lv_obj_set_style_pad_row(col_gris, 3, 0);
    lv_obj_set_flex_flow(col_gris, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col_gris, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(col_gris, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Mas grande que los de nivel: esto no es un nivel, es una ALARMA. Con el
     * deposito de grises lleno hay que vaciar antes de seguir usando el
     * fregadero, y en la P4 se ve como un tanque entero pintado de rojo --
     * aqui era un puntito de 16 px que pasaba desapercibido. */
    s_led_gray = make_led(col_gris, true);

    /* El rotulo va DENTRO del bloque para no robarle alto (ver LED_GRIS_H). */
    s_lbl_gray = lv_label_create(s_led_gray);
    lv_label_set_text(s_lbl_gray, "grises");
    lv_obj_set_style_text_color(s_lbl_gray, COL_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_lbl_gray, &lv_font_montserrat_14, 0);
    lv_obj_clear_flag(s_lbl_gray, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(s_lbl_gray);

    /* Iconos del altavoz de las dos alarmas de aguas (limpia en reserva y
     * grises llenas): arriba a la DERECHA, corridos un poco a la izquierda
     * porque la tarjeta tiene 2 px de borde + 4 de relleno. Esta tarjeta no
     * lleva punto de enlace (el unico esta en la de bateria), asi que no hay
     * nada con lo que chocar. Separados 32 px: juntos (y=2 e y=26) sus zonas
     * tactiles extendidas (AL_ICONO_EXTRA) se solapaban, y con las dos alarmas
     * a la vez un toque entre medias callaba la que no era. */
    al_icono_crear(card, AL_INFO_AGUA,   LV_ALIGN_TOP_RIGHT, -6, 2);
    al_icono_crear(card, AL_INFO_GRISES, LV_ALIGN_TOP_RIGHT, -6, 34);

    /* Toda la tarjeta silencia: la alarma de limpia si esta activa, y si no la
     * de grises (pueden estar las dos a la vez, pero el toque calla una; el
     * icono de cada una es el que va a lo suyo -- ver al_card_cb_agua). Las
     * tarjetas de aqui no hacen nada mas al tocarlas, asi que no se le quita el
     * sitio a ningun gesto: en la P4 si lo hacen -- alli tienen su pantalla de
     * detalle -- y por eso alli la tarjeta no silencia y solo lo hace el icono. */
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, al_card_cb_agua, LV_EVENT_CLICKED, NULL);
    /* El icono del altavoz tiene su propio toque (alterna el sonido), y las
     * tarjetas del mini no burbujean eventos, asi que no hay que parar nada. */
}

/* === Refresco =========================================================== */

static void refresh_bat(const mini_data_t *d)
{
    char buf[32];
    /* CADA CAMPO POR SEPARADO ("NA por campo"): el SoC puede venir sin dato
     * mientras el voltaje y la corriente si estan (es lo normal mientras el
     * SmartShunt sincroniza). Antes esto era un unico "if (has_data)" y ponia
     * "--" en los tres: se tiraban V e I aunque la P4 los mandara buenos.
     * Auditoria del 23-sep-2026. */
    if (d->soc_valido) {
        int soc = d->shunt_soc_deci / 10;
        if (soc < 0) soc = 0;
        if (soc > 100) soc = 100;
        /* El relleno usa los MISMOS umbrales de color que ya se usaban para el
         * numero: verde >=60, ambar >=30, rojo por debajo. */
        lv_color_t col = color_for_soc(d->shunt_soc_deci);

        snprintf(buf, sizeof(buf), "%d%%", soc);
        lv_label_set_text(s_bat_soc, buf);

        lv_obj_set_height(s_bat_relleno, (BAT_H - 6) * soc / 100);
        lv_obj_set_style_bg_color(s_bat_relleno, col, 0);
        /* Con la bateria llena el numero queda SOBRE el relleno, y el verde y
         * el ambar son claros: en blanco no se leeria. Se pasa a negro. */
        lv_obj_set_style_text_color(s_bat_soc,
                                    soc >= 45 ? lv_color_hex(0x000000) : COL_TEXT, 0);
    } else {
        lv_label_set_text(s_bat_soc, "--");
        lv_obj_set_style_text_color(s_bat_soc, COL_TEXT, 0);
        lv_obj_set_height(s_bat_relleno, 0);
    }

    if (d->v_valido) {
        snprintf(buf, sizeof(buf), "%d.%02d",
                 d->shunt_voltage_centi / 100, d->shunt_voltage_centi % 100);
        lv_label_set_text(s_bat_volt, buf);
    } else {
        lv_label_set_text(s_bat_volt, "--");
    }

    if (d->i_valido) {
        int32_t ma = d->shunt_current_milli;
        char sgn = ma < 0 ? '-' : '+';
        int32_t am = ma < 0 ? -ma : ma;
        snprintf(buf, sizeof(buf), "%c%ld.%ld", sgn,
                 (long)(am / 1000), (long)((am % 1000) / 100));
        lv_label_set_text(s_bat_amp, buf);
        /* Verde cargando, NARANJA consumiendo: se ve si la bateria sube o baja
         * sin pararse a leer el signo. Naranja y no rojo a proposito -- gastar
         * es lo normal, no una averia; el rojo se reserva para el nivel bajo. */
        lv_color_t col_amp = ma >= 0 ? COL_VAL_GOOD : lv_color_hex(0xFF9800);
        lv_obj_set_style_text_color(s_bat_amp, col_amp, 0);
        lv_obj_set_style_text_color(s_bat_amp_u, col_amp, 0);   /* la A, a juego */
    } else {
        /* "--" tambien aqui, igual que el SoC y que MOTOR. Antes ponia "sin
         * datos" en el hueco del voltaje y dejaba los amperios EN BLANCO: el
         * texto se salia de su caja de ancho fijo y el hueco vacio parecia un
         * fallo de pintado en vez de una falta de dato. */
        lv_label_set_text(s_bat_amp, "--");
        /* Devolver el color neutro: si no, el "--" se queda del verde o el
         * naranja de la ultima lectura buena, como si siguiera cargando. */
        lv_obj_set_style_text_color(s_bat_amp, COL_TEXT, 0);
        lv_obj_set_style_text_color(s_bat_amp_u, COL_TEXT, 0);
    }
}

/* Bateria del motor: va DENTRO de la tarjeta de bateria, que tambien es
 * bateria y solo tiene un dato que ensenar.
 * Mismo criterio de formato que ui_format_aux_value() en la P4: aux_value_raw
 * crudo, la unidad depende de aux_input (0/1=V*100, 2=Kelvin*100). */
/* Mismo sentinel que usa la P4 en ui_format_aux_value() para "sin dato": si
 * el canal auxiliar esta configurado (aux_input valido) pero la LECTURA en
 * si es 0xFFFF, no es un voltaje/temperatura real, es "N/A" propagado desde
 * el shunt. Antes solo se miraba aux_input, y esto se pintaba como
 * "655.35 V". Detectado auditando el 07-sep-2026. */
#define AUX_NA 0xFFFFu

static void refresh_aux(const mini_data_t *d)
{
    char buf[32];
    if (d->aux_has_data && d->aux_value_raw != AUX_NA) {
        /* El canal auxiliar del shunt esta configurado como bateria de arranque
         * (aux_input 0 = voltage2), que es lo que dice el titulo MOTOR. Los
         * otros dos modos que admite -- punto medio de un banco (1) y sonda de
         * temperatura (2) -- no se usan en esta instalacion, asi que no se
         * rotulan; si algun dia se configuraran, aqui saldria un voltaje o unos
         * grados sin avisar de cual es. */
        if (d->aux_input == 2) {
            /* Signo aparte del entero: con -0,5 grados, temp_centi/100 da 0 (la
             * division trunca hacia cero) y el "-" se perdia -- salia "0.5" en
             * vez de "-0.5". Bug de signo real, detectado auditando el
             * 07-sep-2026: este calculo reimplementaba a mano lo que ya hacia
             * bien ui_format_aux_value() en la P4 (victron/main/ui/widgets/
             * ui_format.c), sin llamarlo -- ver tambien componente borrado
             * ui_format.c de este proyecto, que tampoco se llamaba nunca. */
            int temp_centi = (int)d->aux_value_raw - 27315;
            bool negativo = temp_centi < 0;
            int abs_centi = negativo ? -temp_centi : temp_centi;
            int ti = abs_centi / 100;
            int td = abs_centi % 100 / 10;
            snprintf(buf, sizeof(buf), "%s%d.%d\xC2\xB0" "C",
                     negativo ? "-" : "", ti, td);
        } else {
            snprintf(buf, sizeof(buf), "%d.%02d V",
                     d->aux_value_raw / 100, d->aux_value_raw % 100);
        }
        lv_label_set_text(s_aux_val, buf);
    } else {
        lv_label_set_text(s_aux_val, "--");
    }
}

/* Tendencia del frigo: flecha ARRIBA naranja si la temperatura esta subiendo,
 * ABAJO azul si baja o se mantiene.
 *
 * No se compara con la lectura anterior: el dato llega cada segundo y oscila
 * unas decimas, asi que la flecha estaria todo el rato cambiando y no diria
 * nada. Se compara con una media lenta (el 2% de cada muestra, o sea unos 50 s
 * de memoria) y ademas hay HISTERESIS: para pasar a "sube" hace falta estar
 * 0,3 grados por encima de la media, y para volver a "baja" hay que caer por
 * debajo de 0,05. Entre medias se queda como estaba, que es lo que evita el
 * parpadeo justo en el umbral.
 *
 * "Se mantiene" cuenta como bajar a proposito (peticion del usuario): en un
 * congelador lo que preocupa es que suba.
 *
 * La exterior lleva la misma flecha y los mismos colores, aunque ahi no sea una
 * alarma sino informacion: naranja = calentando, azul = enfriando. Se lee igual
 * de bien y no hay que aprenderse dos codigos. */
#define TREND_SUBE_CENTI   30   /* +0,30 C sobre la media para decir que sube */
#define TREND_BAJA_CENTI    5   /* +0,05 C para volver a decir que baja */

/* El estado va por sensor: son dos flechas independientes y cada una tiene su
 * propia media y su propio recuerdo de hacia donde iba. */
typedef struct {
    bool  lista;
    float media;
    bool  subiendo;
} tendencia_t;

static void refresh_tendencia(tendencia_t *st, lv_obj_t *flecha,
                               bool has, int16_t centi)
{
    if (!has) {
        st->lista = false;
        st->subiendo = false;
        lv_label_set_text(flecha, "");
        return;
    }

    if (!st->lista) {              /* primera lectura: la media ES el dato */
        st->media = (float)centi;
        st->lista = true;
    } else {
        st->media += ((float)centi - st->media) * 0.02f;
    }

    float diff = (float)centi - st->media;
    if (diff > TREND_SUBE_CENTI)       st->subiendo = true;
    else if (diff < TREND_BAJA_CENTI)  st->subiendo = false;

    lv_label_set_text(flecha, st->subiendo ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(flecha,
                                st->subiendo ? lv_color_hex(0xFF9800)  /* naranja */
                                             : lv_color_hex(0x4FC3F7), /* azul */
                                0);
}

/* Una de las dos temperaturas que comparten tarjeta. */
static void refresh_temp(lv_obj_t *val, bool has, int16_t centi, bool is_frigo)
{
    char buf[24];
    if (has) {
        int sign = centi < 0 ? -1 : 1;
        int abs_c = centi * sign;
        snprintf(buf, sizeof(buf), "%s%d.%d\xC2\xB0" "C",
                 sign < 0 ? "-" : "", abs_c / 100, (abs_c % 100) / 10);
        lv_label_set_text(val, buf);
        lv_obj_set_style_text_color(val, is_frigo ? color_for_frigo(centi) : COL_TEXT, 0);
    } else {
        lv_label_set_text(val, "--");
        paleta_texto(val, 0x888888, 0xE0E0E0);
    }
}

/* Cada tanque se dibuja segun SU PROPIO flag, no un flag combinado: una
 * trama nativa NE185 (sin NE187) trae limpia valida pero grises siempre a
 * "sin dato" (ver ne185.c), y antes eso apagaba tambien el bloque de limpia
 * aunque su lectura fuera buena. Detectado auditando el 07-sep-2026. */
static void refresh_aguas(const mini_data_t *d)
{
    if (d->water_clean_has_data) {
        uint8_t cl = d->water_clean > 4 ? 4 : d->water_clean;
        for (int i = 0; i < 4; i++) {
            uint8_t mode = (cl == 0) ? 2 : (i < cl ? 1 : 0);
            led_set(s_led_clean[i], mode);
        }
    } else {
        for (int i = 0; i < 4; i++) led_set(s_led_clean[i], 0);
    }

    if (d->water_gray_has_data) {
        /* Grises: 0 = vacio (OK), cualquier cosa por encima = lleno. Mismo
         * criterio que la P4 (ver r1 en ne185.h). */
        bool lleno = d->water_gray > 0;
        led_set(s_led_gray, lleno ? 2 : 0);
        lv_label_set_text(s_lbl_gray, lleno ? "LLENO" : "grises");
        /* Sobre el bloque rojo el texto va en negro; apagado, en gris. */
        lv_obj_set_style_text_color(s_lbl_gray,
                                    lleno ? lv_color_hex(0x000000) : COL_TEXT_DIM, 0);
    } else {
        led_set(s_led_gray, 0);
        lv_label_set_text(s_lbl_gray, "grises");
        lv_obj_set_style_text_color(s_lbl_gray, COL_TEXT_DIM, 0);
    }
}

/* --- Silenciar: aviso de la orden, plazo de respuesta y toques ------------- */

/* La alarma esta activa segun la P4, con el enlace fresco. Caduca con el
 * enlace: si la P4 deja de hablar, su ultimo byte se queda congelado y no se
 * puede seguir enseñando un altavoz de algo que ya no se sabe. */
static bool al_info_activa(al_info_t i)
{
    mini_data_t d;
    data_model_get(&d);
    uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
    bool fresco = d.last_update_ms != 0 && (ms - d.last_update_ms < CONN_TIMEOUT_MS);
    /* Sin enlace NO se enseña altavoz aunque el ultimo byte diga que hay
     * alarma: con la P4 muda no se sabe si sigue pasando, y un icono que no
     * responde (porque no hay con quien hablar) es peor que no tenerlo. Mismo
     * criterio que el resto de la pantalla, que apaga los datos al caducar el
     * enlace. */
    if (!fresco) return false;
    return (d.alarmas & AL_INFO_BIT[i]) != 0;
}

/* La alarma sigue puesta a ojos de esta pantalla, aunque el enlace haya
 * caducado. Se usa SOLO para no borrar el estado de silencio mientras la P4
 * esta muda unos segundos: si se borrara, al volver el enlace el icono saldria
 * diciendo "sonando" cuando en la P4 esta callada, y el siguiente toque
 * mandaria "silencio" en vez de "vuelve a sonar". */
static bool al_info_sigue(const mini_data_t *d, al_info_t i)
{
    return (d->alarmas & AL_INFO_BIT[i]) != 0;
}

/* Aviso pequeno abajo, al lado de la pastilla de pendientes. Existe porque la
 * orden va por Wi-Fi a la P4 y puede no llegar: sin esto, tocar el altavoz y
 * que no pase nada (ni se calle ni se sepa por que) es exactamente el fallo que
 * mas desespera. Se autoborra a los pocos segundos: no es un estado, es un
 * acuse de recibo. */
static void orden_msg(const char *txt, uint32_t ahora_ms)
{
    if (!s_orden_msg) return;
    lv_label_set_text(s_orden_msg, txt);
    s_orden_msg_ms = ahora_ms ? ahora_ms : 1;
    lv_obj_clear_flag(s_orden_msg, LV_OBJ_FLAG_HIDDEN);
    /* Por delante: la pastilla de pendientes y este aviso comparten hueco
     * (abajo al centro) y el que acaba de pasar es el que importa. */
    lv_obj_move_foreground(s_orden_msg);
}

/* Vuelve del envio, ya en el hilo de LVGL (ver p4_api.h). */
static void orden_done_cb(bool ok, int estado, void *user_data)
{
    /* El user_data dice de que alarma era y con que numero de orden salio:
     * si mientras tanto se lanzo otra orden para esa misma alarma (o el plazo
     * de 10 s ya la dio por perdida), esta respuesta es VIEJA y se ignora --
     * resolverla invertiria el estado equivocado. */
    uintptr_t v = (uintptr_t)user_data;
    al_info_t i = (al_info_t)((v >> 16) & 0xFF);
    uint32_t seq = (uint32_t)(v & 0xFFFF);
    if (!s_al[i].orden_pend || seq != (s_al[i].orden_seq & 0xFFFF)) {
        ESP_LOGW("alarma", "respuesta vieja de la orden de %s (seq %lu): se ignora",
                 AL_INFO_NOMBRE[i], (unsigned long)seq);
        return;
    }
    s_al[i].orden_pend = false;
    if (!ok) {
        /* Se deshace lo que se acaba de mandar: la pantalla no puede decir
         * "silenciada" si la P4 no se ha enterado, porque seguiria pitando. */
        bool era_silencio = s_al[i].silenciada;
        s_al[i].silenciada = !s_al[i].silenciada;
        ESP_LOGW("alarma", "la P4 no acepto la orden de %s (HTTP %d): %s",
                 AL_INFO_NOMBRE[i], estado,
                 era_silencio ? "se queda sonando" : "se queda callada");
        char b[48];
        snprintf(b, sizeof(b), "Sin respuesta de la P4 (%s)", AL_INFO_NOMBRE[i]);
        orden_msg(b, (uint32_t)(esp_timer_get_time() / 1000));
    } else {
        ESP_LOGI("alarma", "orden de %s aceptada por la P4 (HTTP %d)",
                 AL_INFO_NOMBRE[i], estado);
    }
}

/* Plazo de respuesta de la orden en vuelo. Sin esto, una P4 que se apaga justo
 * despues de recibir la peticion dejaria la pantalla diciendo "silenciada" para
 * siempre. */
static void orden_timeout_check(uint32_t ahora_ms)
{
    for (int i = 0; i < AL_INFO_CUANTAS; i++) {
        if (!s_al[i].orden_pend) continue;
        if ((ahora_ms - s_al[i].orden_ms) < ORDEN_TIMEOUT_MS) continue;
        s_al[i].orden_pend = false;
        s_al[i].silenciada = !s_al[i].silenciada;
        ESP_LOGW("alarma", "la P4 no contesto a la orden de %s en %d ms: se deshace",
                 AL_INFO_NOMBRE[i], ORDEN_TIMEOUT_MS);
        char b[48];
        snprintf(b, sizeof(b), "Sin respuesta de la P4 (%s)", AL_INFO_NOMBRE[i]);
        orden_msg(b, ahora_ms);
    }
}

/* El toque en la tarjeta: silencia (o vuelve a habilitar) ESA alarma y manda la
 * orden a la P4. Devuelve true si la alarma estaba activa, o sea si habia algo
 * que hacer: quien llama puede seguir con lo suyo (abrir una pantalla) cuando no
 * lo habia. */
static bool al_info_alternar(al_info_t i, uint32_t ahora_ms)
{
    if (i < 0 || i >= AL_INFO_CUANTAS) return false;
    if (!al_info_activa(i)) return false;

    s_al[i].silenciada = !s_al[i].silenciada;
    /* Una orden cada vez: si ya hay una en vuelo, esta la sustituye (el estado
     * local es el que manda, y la P4 se queda con la ultima que reciba). */
    s_al[i].orden_pend = true;
    s_al[i].orden_ms   = ahora_ms ? ahora_ms : 1;
    /* Numero de la orden: la respuesta vuelve con el (ver orden_done_cb) y, si
     * para entonces ya se lanzo otra, la vieja se ignora en vez de resolver el
     * estado actual. */
    s_al[i].orden_seq++;

    bool ok = p4_api_silenciar_alarma(AL_INFO_BIT[i], orden_done_cb,
                                      (void *)(intptr_t)(((uintptr_t)i << 16) |
                                                         (s_al[i].orden_seq & 0xFFFF)));
    if (!ok) {
        /* Ni siquiera se pudo lanzar el envio (sin memoria): no dejamos la
         * pantalla mintiendo. */
        s_al[i].orden_pend = false;
        s_al[i].silenciada = !s_al[i].silenciada;
        orden_msg("Sin memoria para mandar la orden", ahora_ms);
        return true;
    }

    ESP_LOGI("alarma", "%s: %s (mandado a la P4)", AL_INFO_NOMBRE[i],
             s_al[i].silenciada ? "silenciar" : "volver a habilitar el sonido");
    char b[48];
    snprintf(b, sizeof(b), "Silenciar %s: %s", AL_INFO_NOMBRE[i],
             s_al[i].silenciada ? "enviado" : "sonido otra vez");
    orden_msg(b, ahora_ms);
    return true;
}

static void al_card_cb_alarma(lv_event_t *e)
{
    al_info_t i = (al_info_t)(intptr_t)lv_event_get_user_data(e);
    al_info_alternar(i, (uint32_t)(esp_timer_get_time() / 1000));
}

/* El toque en la tarjeta de AGUAS, que tiene dos alarmas: calla la de limpia
 * si esta activa, y si no la de grises. Cada icono sigue yendo a lo suyo
 * (al_icono_cb_alarma), esto es solo el gesto de "tocar la tarjeta". */
static void al_card_cb_agua(lv_event_t *e)
{
    (void)e;
    uint32_t ahora_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (al_info_activa(AL_INFO_AGUA)) {
        al_info_alternar(AL_INFO_AGUA, ahora_ms);
        return;
    }
    if (al_info_activa(AL_INFO_GRISES)) al_info_alternar(AL_INFO_GRISES, ahora_ms);
}

/* El mismo gesto, pero desde el ICONO. Se corta la propagacion a proposito: el
 * icono cuelga de la tarjeta y las tarjetas de esta pantalla burbujean el evento
 * hasta la pantalla, que es donde vive el doble toque del brillo. Sin cortarlo,
 * dos toques seguidos en el altavoz (silenciar y volver a poner el sonido)
 * contarian ademas como doble toque y cambiarian el brillo sin querer.
 * En las TARJETAS no se corta, porque ahi el doble toque del brillo es lo que se
 * quiere (y esta documentado). Auditado el 14-sep-2026. */
static void al_icono_cb_alarma(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    al_card_cb_alarma(e);
}

/* El icono del altavoz de cada tarjeta. Se crea OCULTO (solo se ve con la
 * alarma activa) y se alinea a la esquina que se le diga: en la tarjeta de
 * bateria hay que dejarlo a la izquierda del punto de enlace, que ya esta en esa
 * esquina. */
static lv_obj_t *al_icono_crear(lv_obj_t *card, al_info_t i,
                                lv_align_t alineacion, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *ic = lv_label_create(card);
    lv_obj_add_flag(ic, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(ic, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ic, "");
    /* Fuente montserrat a secas, SIN la variante _es: esta comprobado que
     * LV_SYMBOL_MUTE (U+F026) y LV_SYMBOL_VOLUME_MAX (U+F028) estan en su tabla
     * de caracteres. Con una fuente que no los tuviera saldria un rectangulo
     * (paso con el punto medio en la P4). */
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_20, 0);
    lv_obj_align(ic, alineacion, x, y);
    /* Zona tactil mayor que el dibujo: el icono son ~20x22 px y eso no se
     * acierta con el dedo, menos en marcha. Es el mismo truco (y el mismo
     * margen) que las flechas del historico de la P4. */
    lv_obj_add_flag(ic, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(ic, AL_ICONO_EXTRA);
    lv_obj_add_event_cb(ic, al_icono_cb_alarma, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    s_al[i].icono = ic;
    return ic;
}

/* Pinta los iconos: solo el de la alarma activa, y con el altavoz tachado y en
 * gris si esta silenciada. */
static void al_refresh_iconos(void)
{
    mini_data_t d;
    data_model_get(&d);
    for (int i = 0; i < AL_INFO_CUANTAS; i++) {
        lv_obj_t *ic = s_al[i].icono;
        if (!ic) continue;
        if (!al_info_sigue(&d, (al_info_t)i)) {
            /* La alarma se recupero: la P4 rearma su silencio sola y aqui se
             * rearma igual, para que la proxima vez vuelva a sonar. Se mira el
             * byte CRUDO (sin exigir enlace) para no rearmar por un simple
             * corte de Wi-Fi con la alarma todavia puesta. */
            s_al[i].silenciada = false;
            s_al[i].orden_pend = false;
        }
        if (!al_info_activa((al_info_t)i)) {
            /* Sin alarma, o sin enlace para saberlo: no hay nada que enseñar
             * (y el altavoz no se puede tocar con provecho). */
            lv_obj_add_flag(ic, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        bool callada = s_al[i].silenciada;
        lv_label_set_text(ic, callada ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
        /* Con el sonido puesto, el altavoz CON ondas en blanco invita a
         * tocarlo; callado, tachado y en gris se lee de un vistazo. */
        lv_obj_set_style_text_color(ic, callada ? lv_color_hex(0x777777) : COL_TEXT, 0);
        lv_obj_clear_flag(ic, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_conn_dots(const mini_data_t *d)
{
    /* Un unico frame UDP 1Hz trae todo el paquete -> mismo estado de
     * enlace para todas las cards (igual que en el mini).
     *
     * Lo que se mira es el ENLACE, o sea last_update_ms, que se sella con cada
     * mensaje valido. Antes se exigia ademas d->has_data, que NO significa "hay
     * enlace" sino "el SmartShunt esta mandando SoC": en un banco de pruebas sin
     * shunt, el punto salia GRIS con la P4 hablando a 1 Hz, diciendo "no hay
     * conexion" cuando la habia (24-ago-2026). Mismo fallo que tenia el icono
     * del GPS y por el mismo motivo. */
    lv_color_t col;
    if (d->last_update_ms == 0) {
        col = COL_CONN_NONE;          /* todavia no ha llegado NADA */
    } else {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        col = (now - d->last_update_ms < CONN_TIMEOUT_MS) ? COL_CONN_OK : COL_CONN_LOST;
    }
    lv_obj_set_style_bg_color(s_bat_dot, col, 0);
}

static void refresh_cb(lv_timer_t *t)
{
    (void)t;
    mini_data_t d;
    data_model_get(&d);

    /* El estado del enlace se enseña en Ajustes -> Configuracion (mudado el
     * 30-sep-2026: es un dato de diagnostico, no de conduccion, y ahi deja
     * libre el hueco de abajo). Aqui solo se deja rastro en el log cada 30 s,
     * que es lo que se mira cuando algo va mal. */
    {
        static unsigned cada_30s = 0;
        if (++cada_30s % 60 == 0) {
            char ssid[33] = {0};
            bool asociado = false;
            int rssi = 0, sin_datos = -1;
            udp_rx_enlace(ssid, sizeof(ssid), &asociado, &rssi, &sin_datos);
            if (!asociado)                       ESP_LOGI("enlace", "P4: sin red (busco %s)", ssid);
            else if (sin_datos < 0 || sin_datos > 5) ESP_LOGI("enlace", "P4: %d dBm, sin datos %ds", rssi, sin_datos < 0 ? 0 : sin_datos);
            else                                 ESP_LOGI("enlace", "P4: %d dBm", rssi);
        }
    }

    refresh_bat(&d);
    refresh_aux(&d);
    static tendencia_t t_frigo, t_ext;
    refresh_temp(s_frigo_val, d.frigo_has_data, d.frigo_temp_centi, true);
    refresh_tendencia(&t_frigo, s_frigo_trend, d.frigo_has_data, d.frigo_temp_centi);
    refresh_temp(s_ext_val, d.exterior_has_data, d.exterior_temp_centi, false);
    refresh_tendencia(&t_ext, s_ext_trend, d.exterior_has_data, d.exterior_temp_centi);

    if (s_gps) {
        /* CADUCA con el enlace, igual que los numeros. El estado del GPS lo
         * manda la P4; si la P4 deja de hablar, el ultimo valor recibido se
         * queda congelado en el modelo y el icono seguiria VERDE diciendo que
         * hay posicion sin que haya ni comunicacion. Mismo criterio que el
         * punto de conexion: sin dato fresco, gris.
         *
         * Es el mismo fallo que gps.c ya evita en la P4 con su caducidad de
         * 5 s; faltaba aplicarlo en el lado que recibe. */
        uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
        /* Lo que caduca es el ENLACE con la P4, y eso lo dice last_update_ms
         * (se sella con CADA mensaje valido). NO vale d.has_data: eso significa
         * "el shunt esta mandando SoC", que es otra cosa -- en un banco de
         * pruebas sin SmartShunt es false, y el icono se quedaba GRIS aunque la
         * P4 estuviera diciendo que el GPS busca satelites (visto el
         * 24-ago-2026). El != 0 es para el primer arranque: sin ningun mensaje
         * todavia, last_update_ms vale 0 y la resta daria "fresco". */
        bool fresco = d.last_update_ms != 0 &&
                      (ms - d.last_update_ms < CONN_TIMEOUT_MS);
        /* LOS MISMOS COLORES QUE LA P4, que es donde tambien se mira este
         * estado: gris apagado, ambar buscando, verde fijado. Se probo el cian
         * un rato para "buscando" creyendo que el naranja se confundia con el
         * marco de la tarjeta de bateria (que es ese mismo 0xFF9800), pero el
         * problema no era el color: el icono estaba gris de verdad, colgado del
         * shunt. Y el rotulo BATERIA, naranja y pegado al mismo marco, se lee
         * perfectamente. Dos pantallas diciendo lo mismo de dos colores
         * distintos confunde mas que un naranja junto a otro. */
        uint32_t c = !fresco             ? 0x666666    /* sin enlace */
                   : (d.gps_estado == 2) ? 0x4CD964    /* fijado   */
                   : (d.gps_estado == 1) ? 0xFF9800    /* buscando */
                                         : 0x666666;   /* la P4 no ve el GPS */
        lv_obj_set_style_text_color(s_gps, lv_color_hex(c), 0);
    }
    uint8_t fan_pct = d.frigo_has_data ? d.frigo_fan_pct : 0;
    lv_coord_t track_w = lv_obj_get_width(s_frigo_fan_track);
    lv_obj_set_width(s_frigo_fan_fill, track_w * fan_pct / 100);
    refresh_aguas(&d);
    update_conn_dots(&d);

    /* Alarmas: iconos del altavoz, plazo de la orden que este en vuelo y
     * caducidad del aviso. El mismo tick que refresca el resto (500 ms) -- el
     * pitido de la P4 se calla o no en cuanto llegue la orden, y 500 ms de
     * retraso en pintar el icono no los ve nadie. */
    uint32_t ahora_ms = (uint32_t)(esp_timer_get_time() / 1000);
    orden_timeout_check(ahora_ms);
    al_refresh_iconos();
    if (s_orden_msg) {
        if (s_orden_msg_ms != 0 && (ahora_ms - s_orden_msg_ms) >= ORDEN_MSG_MS) {
            s_orden_msg_ms = 0;
            lv_obj_add_flag(s_orden_msg, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* DOBLE TOQUE = cambiar el brillo (peticion del 25-ago-2026).
 *
 * Va aqui y no en la barra de Ajustes a proposito: lo que surge conduciendo se
 * resuelve en esta pantalla, sin menus y sin apuntar. Toda la superficie vale
 * menos la pastilla de pendientes, que tiene su propio toque y manda ella.
 *
 * Un deslizamiento NO cuenta como toque: nav.c llama a lv_indev_wait_release()
 * en cuanto detecta el gesto, y entonces LVGL manda PRESS_LOST en vez de
 * CLICKED. O sea que cambiar de pantalla no toca el brillo por sorpresa. */
#define DOBLE_TOQUE_MS  650

static uint32_t s_toque_previo;   /* 0 = no hay ningun toque a medias */

/* Toque largo: mantener el dedo un segundo. Se anyadio el 30-sep-2026 porque
 * el doble toque de 400 ms no salia a la primera ni de lejos: con el panel y
 * las prisas, pedir dos clics seguidos en menos de medio segundo es pedir
 * pericia. El toque largo no falla y hace lo mismo. */
static void toque_largo_cb(lv_event_t *e)
{
    (void)e;
    s_toque_previo = 0;   /* que no encadene con un doble toque a medias */
    /* SOLO la luz. El contraste ya no va pegado al brillo: ver el comentario de
     * view_info_set_contraste() -- cambiar los dos a la vez hacia que el toque
     * pareciese encender y apagar, no subir o bajar la luz. */
    brillo_alternar();
}

static void doble_toque_cb(lv_event_t *e)
{
    (void)e;
    if (s_toque_previo && lv_tick_elaps(s_toque_previo) <= DOBLE_TOQUE_MS) {
        s_toque_previo = 0;   /* un tercer toque empieza cuenta nueva, no encadena */
        /* Solo la luz, igual que el toque largo. */
        brillo_alternar();
    } else {
        uint32_t t = lv_tick_get();
        s_toque_previo = t ? t : 1;   /* el 0 esta reservado para "ninguno" */
    }
}

/* Una tarjeta vacia con su borde de color, su punto de enlace y su titulo
 * arriba a la izquierda. El contenido lo pone cada cual. */
static lv_obj_t *make_card(lv_obj_t *grid, lv_color_t border, const char *titulo,
                            uint8_t col, uint8_t span, uint8_t row, lv_obj_t **dot_out,
                            bool titulo_centrado, const lv_font_t *fuente_titulo)
{
    lv_obj_t *card = lv_obj_create(grid);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, col, span, LV_GRID_ALIGN_STRETCH, row, 1);
    lv_obj_set_style_bg_color(card, COL_CARD_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(card, COL_CARD_BG_BOT, 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    paleta_borde_auto(card, border);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    /* Las tarjetas no hacen nada al tocarlas, pero SI se tragan el toque (un
     * lv_obj_create nace clicable). Sin esto el doble toque del brillo no
     * funcionaria en casi ningun sitio, porque las tarjetas cubren la pantalla
     * entera. Con EVENT_BUBBLE el toque sube hasta la pantalla, que es donde
     * escucha view_info_create(). */
    lv_obj_add_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE);

    if (dot_out) {
        lv_obj_t *dot = lv_obj_create(card);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_set_style_bg_color(dot, COL_CONN_NONE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_radius(dot, 6, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, 0, 2);
        *dot_out = dot;
    }

    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, titulo);
    lv_obj_set_style_text_color(t, border, 0);
    lv_obj_set_style_text_font(t, fuente_titulo, 0);
    /* No clickable: si no, el titulo se tragaria el toque y ni la tarjeta ni
     * el doble toque del brillo lo verian (ver el bloque de abajo). */
    lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE);
    /* Los titulos van centrados: pegados a la esquina se perdian, y con el de
     * bateria coronando su dibujo los de abajo desalineados cantaban. */
    lv_obj_align(t, titulo_centrado ? LV_ALIGN_TOP_MID : LV_ALIGN_TOP_LEFT, 0, 0);
    return card;
}

/* Etiqueta pequena + valor grande, uno al lado del otro. La usan la tarjeta de
 * temperaturas y la bateria del motor. */
static lv_obj_t *make_fila_dato(lv_obj_t *padre, const char *etiqueta,
                                 const lv_font_t *fuente_val, lv_coord_t y)
{
    lv_obj_t *l = lv_label_create(padre);
    lv_label_set_text(l, etiqueta);
    lv_obj_set_style_text_color(l, COL_TEXT_ESCALA, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    /* Centrado vertical respecto al valor: la etiqueta es mas baja (font 20)
     * que el valor, cuyo tamano varia segun la tarjeta. */
    lv_coord_t label_y = y + (lv_font_get_line_height(fuente_val) -
                               lv_font_get_line_height(&lv_font_montserrat_20)) / 2;
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, label_y);

    lv_obj_t *v = lv_label_create(padre);
    lv_label_set_text(v, "--");
    lv_obj_set_style_text_color(v, COL_TEXT, 0);
    lv_obj_set_style_text_font(v, fuente_val, 0);
    lv_obj_clear_flag(v, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(v, LV_ALIGN_TOP_RIGHT, 0, y);
    return v;
}

/* DIAGNOSTICO: vuelca el arbol de esta pantalla con posiciones y tamanos. */
void view_info_diag_arbol(void)
{
    lv_obj_t *scr = lv_screen_active();
    if (!scr) return;
    ESP_LOGW("diag", "MARCA-INFO: %dx%d", (int)lv_obj_get_width(scr), (int)lv_obj_get_height(scr));
    for (int i = 0; i < lv_obj_get_child_count(scr); i++) {
        lv_obj_t *c = lv_obj_get_child(scr, i);
        ESP_LOGW("diag", "MARCA-INFO [%d] %dx%d en (%d,%d)", i,
                 (int)lv_obj_get_width(c), (int)lv_obj_get_height(c),
                 (int)lv_obj_get_x(c), (int)lv_obj_get_y(c));
        for (int j = 0; j < lv_obj_get_child_count(c); j++) {
            lv_obj_t *g = lv_obj_get_child(c, j);
            ESP_LOGW("diag", "MARCA-INFO   [%d.%d] %dx%d en (%d,%d)", i, j,
                     (int)lv_obj_get_width(g), (int)lv_obj_get_height(g),
                     (int)lv_obj_get_x(g), (int)lv_obj_get_y(g));
        }
    }
}

void view_info_create(lv_obj_t *parent)
{
    /* SIEMPRE EN COLOR (9-oct-2026): ver el comentario de view_info_set_contraste.
     * Antes esto era `brillo_nivel() == BRILLO_ALTO`, que con el arranque al
     * 100 % dejaba la pantalla de datos en blanco y negro. La referencia de como
     * tiene que verse es .scratch/cabina/screenshot/info.png (la de la 3,5"). */
    s_contraste = false;
    /* La pantalla REAL de esta placa: 800x480 (ver display.h). El alto es el
     * mismo que tenia la UI del satelite de 3,5" (480), asi que lo que se
     * reparte de nuevo es el ancho: 320 px mas que antes.
     *
     * DOS columnas y DOS filas, con la bateria ocupando la fila de arriba
     * entera. La fila de arriba se lleva 210 px FIJOS (no una fraccion): ahi va
     * el dibujo de la bateria con su relleno, que es lo que se mira, y su alto
     * no depende del ancho. Las de abajo se quedan con lo que sobra (246),
     * tambien fijo, para que el reparto no baile si algun dia cambia el ancho.
     * 210 + 4 + 246 + 8 de margen = 480 exactos. */
    /* COLUMNAS IGUALES Y LA FILA DE ABAJO ELASTICA (9-oct-2026).
     *
     * Antes eran FR(2) y FR(3) -- aguas 317 px y temperaturas 475 -- y las dos
     * filas de alto FIJO: 210 + 4 + 246 = 460 de los 472 utiles, o sea 12 px
     * MUERTOS al fondo. El usuario lo vio: "el contenido desaprovecha espacio
     * por debajo".
     *
     * Reparto nuevo: la franja de bateria se queda con su alto (es el dibujo,
     * que no depende del ancho) y la fila de abajo se lleva TODO lo que sobre,
     * con las dos tarjetas a partes iguales. Aguas es la que sale ganando: con
     * 317 px la columna de fracciones mas la barra mas el bloque de grises
     * quedaba apretadisima. */
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {GPS_CARD_H, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_set_size(grid, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(grid, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 4, 0);
    lv_obj_set_style_pad_gap(grid, 4, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_EVENT_BUBBLE);   /* ver make_card() */
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);

    /* El doble toque escucha en la PANTALLA, no en la rejilla: asi tambien
     * valen los huecos que quedan fuera de ella. */
    lv_obj_add_event_cb(parent, doble_toque_cb, LV_EVENT_CLICKED, NULL);
    /* Y el toque largo, que es lo que de verdad se consigue a la primera. */
    lv_obj_add_event_cb(parent, toque_largo_cb, LV_EVENT_LONG_PRESSED, NULL);

    /* --- Bateria: toda la franja de arriba -------------------------------- */
    s_bat_card = /* La de bateria manda, y su titulo tambien: 24 contra los 20 de las de
     * abajo. */
    make_card(grid, COL_BORDER_BAT, "BATERIA", 1, 1, 1, NULL, true,
                           &lv_font_montserrat_24);

    /* --- GPS: de momento SOLO EL MARCO ------------------------------------
     *
     * Va vacio a proposito: aqui iran la velocidad real, los satelites, los
     * kilometros del viaje y el tiempo de conduccion, y esos datos HOY NO
     * LLEGAN (el protocolo con la P4 solo manda el estado del GPS; ver
     * mini_proto.h). Se reserva el sitio ya para que el reparto de la pantalla
     * no haya que rehacerlo cuando lleguen.
     *
     * El titulo y el color son provisionales: cuando tenga contenido, este
     * marco se cambia por una tarjeta de verdad. El gris es a proposito, para
     * que se vea que esta sin estrenar y no parezca una tarjeta rota. */
    s_gps_card = make_card(grid, lv_color_hex(0x888888), "GPS", 0, 3, 0,
                           &s_bat_dot, true, &lv_font_montserrat_20);

    /* El dibujo va CENTRADO en la tarjeta y es el protagonista: los voltios y
     * amperios a su izquierda, la bateria del motor a su derecha.
     *
     * El +8 baja el conjunto lo que ocupa el titulo, para que quede centrado a
     * la vista y no solo en la cuenta. Con la tarjeta de 210 px ese ajuste ya
     * no vale: el centro geometrico (105) le deja el titulo pegado al dibujo.
     * Ahora se calcula a partir del alto de la tarjeta (BAT_CARD_H, definida
     * junto a la rejilla en view_info_create): el bloque dibujo+bornes se
     * centra en el hueco que queda POR DEBAJO del titulo. */
    lv_obj_t *dib = make_bateria_dibujo(s_bat_card);
    lv_obj_align(dib, LV_ALIGN_TOP_MID, 0, 118);

    /* Numero y UNIDAD van en etiquetas separadas, y no en un solo texto, para que
     * la V y la A no se muevan: el numero se alinea a la DERECHA dentro de un
     * hueco fijo, asi que crece hacia la izquierda y la unidad se queda clavada.
     * Juntos, "9.99 V" y "13.43 V" dejaban la V en sitios distintos y bailaba
     * cada vez que la tension cruzaba una decena. */
    s_bat_volt = lv_label_create(s_bat_card);
    lv_label_set_text(s_bat_volt, "--");
    lv_obj_set_style_text_color(s_bat_volt, COL_TEXT, 0);
    lv_obj_set_style_text_font(s_bat_volt, &lv_font_montserrat_32, 0);
    lv_obj_set_width(s_bat_volt, BAT_NUM_W);
    lv_obj_set_style_text_align(s_bat_volt, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_clear_flag(s_bat_volt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_bat_volt, LV_ALIGN_TOP_MID, 0, 34);

    lv_obj_t *u_v = lv_label_create(s_bat_card);
    lv_label_set_text(u_v, "V");
    paleta_texto(u_v, 0xCCCCCC, 0xFFFFFF);
    lv_obj_set_style_text_font(u_v, &lv_font_montserrat_24, 0);
    lv_obj_clear_flag(u_v, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(u_v, LV_ALIGN_LEFT_MID, BAT_NUM_X + BAT_NUM_W + 10, -BAT_NUM_Y + 4);

    s_bat_amp = lv_label_create(s_bat_card);
    lv_label_set_text(s_bat_amp, "");    lv_obj_set_style_text_color(s_bat_amp, COL_TEXT, 0);
    /* Mismo tamano que los voltios: los dos son el dato principal de su lado. */
    lv_obj_set_style_text_font(s_bat_amp, &lv_font_montserrat_32, 0);
    lv_obj_set_width(s_bat_amp, BAT_NUM_W);
    lv_obj_set_style_text_align(s_bat_amp, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_clear_flag(s_bat_amp, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_bat_amp, LV_ALIGN_TOP_MID, 0, 76);

    s_bat_amp_u = lv_label_create(s_bat_card);
    lv_label_set_text(s_bat_amp_u, "A");
    paleta_texto(s_bat_amp_u, 0xCCCCCC, 0xFFFFFF);
    lv_obj_set_style_text_font(s_bat_amp_u, &lv_font_montserrat_24, 0);
    lv_obj_clear_flag(s_bat_amp_u, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_bat_amp_u, LV_ALIGN_LEFT_MID, BAT_NUM_X + BAT_NUM_W + 10, BAT_NUM_Y + 4);

    /* La del motor, a la derecha del todo y mas discreta: es bateria tambien,
     * pero solo se mira cuando el vehiculo no arranca. */
    /* Titulo y valor en una columna centrada, no colocados a mano: asi "MOTOR"
     * queda centrado sobre el numero SEA CUAL SEA su ancho -- y cambia, que no
     * es lo mismo "9.99 V" que "12.66 V". A mano habria que reajustarlo cada
     * vez que el valor cruza una decena. */
    lv_obj_t *col_motor = lv_obj_create(s_bat_card);
    lv_obj_set_size(col_motor, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col_motor, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col_motor, 0, 0);
    lv_obj_set_style_pad_all(col_motor, 0, 0);
    lv_obj_set_style_pad_row(col_motor, 10, 0);   /* el aire entre los dos */
    lv_obj_set_flex_flow(col_motor, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col_motor, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(col_motor, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(col_motor, LV_ALIGN_BOTTOM_MID, 0, -6);

    lv_obj_t *aux_tit = lv_label_create(col_motor);
    lv_label_set_text(aux_tit, "MOTOR");
    paleta_texto(aux_tit, 0x4FC3F7, 0xFFFFFF);
    lv_obj_set_style_text_font(aux_tit, &lv_font_montserrat_20, 0);
    lv_obj_clear_flag(aux_tit, LV_OBJ_FLAG_CLICKABLE);

    s_aux_val = lv_label_create(col_motor);
    lv_label_set_text(s_aux_val, "--");
    lv_obj_set_style_text_color(s_aux_val, COL_TEXT, 0);
    /* Mismo tamano que los voltios de la principal: es el otro dato de tension
     * de la tarjeta y no tiene por que leerse peor. */
    lv_obj_set_style_text_font(s_aux_val, &lv_font_montserrat_32, 0);
    lv_obj_clear_flag(s_aux_val, LV_OBJ_FLAG_CLICKABLE);

    /* Icono del altavoz de la bateria: arriba a la DERECHA, pero corrido a la
     * izquierda porque ahi ya esta el punto de enlace de la tarjeta (12 px de
     * punto + su margen). A la izquierda no cabe: ese hueco lo ocupa el icono
     * del GPS de la P4, que va en la pantalla, no en la tarjeta. */
    al_icono_crear(s_bat_card, AL_INFO_BATERIA, LV_ALIGN_TOP_RIGHT, -26, 2);
    /* La tarjeta entera silencia. Va con EVENT_BUBBLE para que el toque suba
     * desde el dibujo y los numeros, que son objetos hijos (mismo patron que
     * usan los botones de la card camper). */
    lv_obj_add_flag(s_bat_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_bat_card, al_card_cb_alarma, LV_EVENT_CLICKED,
                        (void *)(intptr_t)AL_INFO_BATERIA);

    /* --- Abajo izquierda: aguas ------------------------------------------- */
    make_water_cell(grid, 0, 1, 1);

    /* --- Abajo derecha: las dos temperaturas juntas ----------------------- */
    lv_obj_t *temp_card = make_card(grid, COL_BORDER_COLD, "TEMPERATURAS", 2, 1, 1, NULL, true,
                                       &lv_font_montserrat_20);
    /* El valor del frigo va en un hueco fijo y la flecha al borde: asi la
     * flecha no se mueve cuando el numero cambia de ancho ("-5.0" contra
     * "-18.0"), igual que con la V y la A de la bateria.
     *
     * Las dos filas de esta tarjeta (Frigo y Exterior) se reparten el alto
     * nuevo: la tarjeta pasa de 146 a 246 px, y con las posiciones de antes
     * (26 y 62) los dos datos quedaban amontonados arriba con 150 px de negro
     * debajo. Ahora van a 64 y 132, o sea centrados en sus dos mitades, y la
     * flecha de tendencia de cada uno sube con ellos (ver mas abajo). */
    s_frigo_val = make_fila_dato(temp_card, "Frigo", &lv_font_montserrat_32, 64);
    lv_obj_set_width(s_frigo_val, TEMP_NUM_W);
    lv_obj_set_style_text_align(s_frigo_val, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_frigo_val, LV_ALIGN_TOP_RIGHT, -26, 64);

    s_frigo_trend = lv_label_create(temp_card);
    lv_label_set_text(s_frigo_trend, "");
    lv_obj_set_style_text_font(s_frigo_trend, &lv_font_montserrat_20, 0);
    lv_obj_clear_flag(s_frigo_trend, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_frigo_trend, LV_ALIGN_TOP_RIGHT, 0, 86);

    s_ext_val   = make_fila_dato(temp_card, "Exterior", &lv_font_montserrat_32, 132);
    lv_obj_set_width(s_ext_val, TEMP_NUM_W);
    lv_obj_set_style_text_align(s_ext_val, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_ext_val, LV_ALIGN_TOP_RIGHT, -26, 132);

    s_ext_trend = lv_label_create(temp_card);
    lv_label_set_text(s_ext_trend, "");
    lv_obj_set_style_text_font(s_ext_trend, &lv_font_montserrat_20, 0);
    lv_obj_clear_flag(s_ext_trend, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_ext_trend, LV_ALIGN_TOP_RIGHT, 0, 154);

    /* Icono del altavoz de la alarma del congelador: arriba a la DERECHA, que
     * es el unico hueco libre de la tarjeta (el titulo va centrado, "Frigo" a
     * la izquierda y la flecha de tendencia ocupando el borde derecho mas
     * abajo). Esta tarjeta no lleva punto de enlace. */
    al_icono_crear(temp_card, AL_INFO_CONGELADOR, LV_ALIGN_TOP_RIGHT, -6, 2);
    /* Tocar la tarjeta silencia el congelador (es la unica alarma que tiene). */
    lv_obj_add_flag(temp_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(temp_card, al_card_cb_alarma, LV_EVENT_CLICKED,
                        (void *)(intptr_t)AL_INFO_CONGELADOR);

    /* Ventilador del frigo: etiqueta + barra de nivel, a la izquierda de la
     * tarjeta (antes era texto "vent. NN%" a la derecha, tapado por el
     * numero de Exterior al subir este a fuente 32). */
    lv_obj_t *fan_lbl = lv_label_create(temp_card);
    lv_label_set_text(fan_lbl, "Vent.");
    lv_obj_set_style_text_color(fan_lbl, COL_TEXT, 0);
    lv_obj_set_style_text_font(fan_lbl, &lv_font_montserrat_14, 0);
    lv_obj_clear_flag(fan_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(fan_lbl, LV_ALIGN_BOTTOM_LEFT, 0, -3);

    /* La barra va justo a la derecha de la etiqueta, centrada verticalmente
     * CON ELLA (align_to a su borde derecho) en vez de anclada al borde de la
     * tarjeta por su cuenta: asi quedan a la misma altura pase lo que pase
     * con el alto exacto de cada una, sin ajustar offsets a ojo. */
    s_frigo_fan_track = lv_obj_create(temp_card);
    lv_obj_set_size(s_frigo_fan_track, ESC(120), ESC(14));
    lv_obj_set_style_bg_color(s_frigo_fan_track, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(s_frigo_fan_track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_frigo_fan_track, 0, 0);
    lv_obj_set_style_radius(s_frigo_fan_track, 3, 0);
    lv_obj_set_style_pad_all(s_frigo_fan_track, 0, 0);
    lv_obj_clear_flag(s_frigo_fan_track, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    /* update_layout fuerza el calculo del tamano de fan_lbl YA: align_to
     * necesita su borde derecho resuelto, que si no sale 0 (ver commit
     * anterior, mismo problema con el ancho leido demasiado pronto). */
    lv_obj_update_layout(fan_lbl);
    lv_obj_align_to(s_frigo_fan_track, fan_lbl, LV_ALIGN_OUT_RIGHT_MID, 16, 0);

    /* El relleno crece desde la izquierda, igual que el de la bateria crece
     * desde abajo: se ancla al lado fijo y solo cambia de ancho. */
    s_frigo_fan_fill = lv_obj_create(s_frigo_fan_track);
    lv_obj_set_height(s_frigo_fan_fill, lv_pct(100));
    lv_obj_set_width(s_frigo_fan_fill, 0);
    lv_obj_set_style_bg_color(s_frigo_fan_fill, COL_TEXT, 0);
    lv_obj_set_style_bg_opa(s_frigo_fan_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_frigo_fan_fill, 0, 0);
    lv_obj_set_style_radius(s_frigo_fan_fill, 3, 0);
    lv_obj_set_style_pad_all(s_frigo_fan_fill, 0, 0);
    lv_obj_clear_flag(s_frigo_fan_fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_frigo_fan_fill, LV_ALIGN_LEFT_MID, 0, 0);

    /* Pastilla de pendientes: encima de todo y FUERA de la rejilla, para no
     * robarle sitio a ninguna tarjeta -- casi siempre no esta. Abajo al centro,
     * que es donde no tapa ningun numero. */

    s_pendientes = lv_label_create(parent);
    lv_obj_add_flag(s_pendientes, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pendientes, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_label_set_text(s_pendientes, "");
    lv_obj_set_style_bg_color(s_pendientes, lv_color_hex(0xFF9800), 0);
    lv_obj_set_style_bg_opa(s_pendientes, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_pendientes, lv_color_hex(0x000000), 0);
    /* Letra 24 y no 14: con la 14 quedaba tan pequena que se confundia con un
     * rotulo mas de la pantalla, y esto es lo unico que puede impedirte irte a
     * casa con un apunte sin entregar. Tiene que verse de un vistazo desde el
     * asiento, no leerse de cerca. */
    lv_obj_set_style_text_font(s_pendientes, &lv_font_montserrat_24, 0);
    lv_obj_set_style_pad_hor(s_pendientes, 20, 0);
    lv_obj_set_style_pad_ver(s_pendientes, 8, 0);
    lv_obj_set_style_radius(s_pendientes, LV_RADIUS_CIRCLE, 0);
    /* Sombra negra alrededor: la pastilla cae ENCIMA de las tarjetas de aguas y
     * temperaturas, y sin separacion el naranja se pegaba a sus bordes. */
    lv_obj_set_style_border_color(s_pendientes, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(s_pendientes, 3, 0);
    lv_obj_align(s_pendientes, LV_ALIGN_BOTTOM_MID, 0, -6);
    /* Se TOCA cuando hay algo que cerrar: lleva derecho a la lista. Un aviso
     * que solo avisa obliga a acordarse de a donde hay que ir. */
    lv_obj_add_flag(s_pendientes, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_pendientes, 10);
    lv_obj_add_event_cb(s_pendientes, pendientes_click_cb, LV_EVENT_CLICKED, NULL);
    /* La pantalla de registros puede haberse creado antes y haber contado ya lo
     * que quedo abierto; sin esto, ese aviso no saldria hasta el primer cambio. */
    pendientes_aplicar(NULL);

    /* Indicador del GPS de la P4. Arriba a la izquierda, justo detras del punto
     * de conexion: la cabecera de la tarjeta de bateria lleva el titulo
     * CENTRADO, asi que ese hueco esta libre y no le quita sitio a ningun dato.
     *
     * Tres colores, los mismos que en la P4: gris no llega nada, naranja
     * buscando, verde posicion fijada. Un GPS recien encendido tarda un par de
     * minutos, y ver "buscando" en vez de "no hay" evita ir a mirar el cable
     * cuando lo unico que hay que hacer es esperar. */
    /* Ahora es HIJO DE LA TARJETA y no de la pantalla (8-oct-2026). En la
     * pantalla de 480 de ancho el icono caia en el margen de 4 px de la
     * rejilla; con la tarjeta ocupando los 800 px, ese mismo (14, 11) lo
     * dejaba clavado SOBRE EL BORDE de la tarjeta. Colgado de la tarjeta va
     * donde tiene que ir sin cuentas: su esquina de dentro. */
    s_gps = lv_label_create(s_gps_card);
    lv_label_set_text(s_gps, LV_SYMBOL_GPS);
    /* Dentro de la tarjeta, en su esquina de arriba a la izquierda: ahi no hay
     * nada -- el punto de conexion va arriba a la DERECHA (make_card lo alinea
     * TOP_RIGHT), el titulo va centrado y el icono del altavoz tambien esta a
     * la derecha (al_icono_crear con -26). */
    lv_obj_set_style_text_font(s_gps, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_gps, lv_color_hex(0x666666), 0);
    lv_obj_align(s_gps, LV_ALIGN_TOP_LEFT, 6, 2);

    /* Aviso de la orden de silencio ("enviado" / "sin respuesta"). Abajo al
     * centro y oculto casi siempre: la pastilla de pendientes vive en el mismo
     * sitio, y cuando aparece esto es porque el usuario acaba de tocar algo, o
     * sea que lo suyo es lo urgente (orden_msg hace move_foreground). */
    s_orden_msg = lv_label_create(parent);
    lv_obj_add_flag(s_orden_msg, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(s_orden_msg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_orden_msg, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(s_orden_msg, "");
    lv_obj_set_style_text_font(s_orden_msg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_orden_msg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_color(s_orden_msg, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_bg_opa(s_orden_msg, LV_OPA_90, 0);
    lv_obj_set_style_pad_hor(s_orden_msg, 10, 0);
    lv_obj_set_style_pad_ver(s_orden_msg, 4, 0);
    lv_obj_set_style_radius(s_orden_msg, 8, 0);
    lv_obj_align(s_orden_msg, LV_ALIGN_BOTTOM_MID, 0, -6);

    s_refresh_timer = lv_timer_create(refresh_cb, 500, NULL);
}

/* --- Pastilla de pendientes ------------------------------------------------
 *
 * Llega desde la tarea del repartidor, no desde LVGL, asi que el trabajo real
 * se aplaza con lv_async_call: tocar un widget desde otra tarea corrompe la
 * lista de objetos y el fallo aparece mucho despues y en otro sitio. */
static size_t s_pend_valor;
static size_t s_sin_cerrar;
/* Salida puntual abierta pero sin declarar todavia (ver salida_vista_t.
 * declarado en salida.h). En la practica nunca coincide con s_sin_cerrar
 * (declarar es justo lo que la hace pasar a "sin cerrar"), pero SI puede
 * coincidir con s_pend_valor si queda algo atascado en la cola de un viaje
 * anterior -- de ahi que se combine con el mismo criterio que los otros
 * dos en vez de necesitar una pastilla propia. */
static bool s_puntual_pendiente;
static char s_puntual_nombre[32];

/* UNA sola pastilla para las tres cosas, y no varias: son avisos distintos
 * pero comparten el unico hueco de la pantalla donde no tapan un numero.
 * Varias pastillas se pisarian, y moverlas de sitio segun cual haya se ve
 * peor que leerlas juntas. */
static void pendientes_aplicar(void *arg)
{
    (void)arg;
    if (!s_pendientes) return;
    if (!s_puntual_pendiente && s_pend_valor == 0 && s_sin_cerrar == 0) {
        lv_obj_add_flag(s_pendientes, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    /* Primero lo que pide algo de ti (llegar a un sitio, o cerrar un
     * apunte); despues lo que se arregla solo en cuanto la P4 aparezca.
     * La flecha solo cuando el toque lleva a algun sitio: "sin enviar" no
     * se arregla tocando nada, se arregla encendiendo la P4. */
    if (s_puntual_pendiente && s_pend_valor) {
        lv_label_set_text_fmt(s_pendientes, "Toca al llegar a %s - %u sin enviar  >",
                              s_puntual_nombre, (unsigned)s_pend_valor);
    } else if (s_puntual_pendiente) {
        lv_label_set_text_fmt(s_pendientes, "Toca al llegar a %s  >", s_puntual_nombre);
    } else if (s_sin_cerrar && s_pend_valor) {
        lv_label_set_text_fmt(s_pendientes, "%u sin cerrar - %u sin enviar  >",
                              (unsigned)s_sin_cerrar, (unsigned)s_pend_valor);
    } else if (s_sin_cerrar) {
        lv_label_set_text_fmt(s_pendientes, "%u sin cerrar  >", (unsigned)s_sin_cerrar);
    } else {
        lv_label_set_text_fmt(s_pendientes, "%u sin enviar", (unsigned)s_pend_valor);
    }

    /* Cerca del tope, la pastilla se pone ROJA y lo dice.
     *
     * Que se acumulen apuntes no es lo normal: la P4 esta siempre encendida y la
     * cola se vacia sola en segundos. Si esto llega a verse es que llevan un
     * buen rato sin llegar -- la P4 sin corriente, o esta pantalla fuera de su
     * Wi-Fi -- y conviene enterarse ANTES de que no quepa el siguiente, que ahi
     * ya no se puede anotar. Tres de margen: da tiempo a reaccionar sin dar la
     * lata por uno o dos. */
    bool casi = s_pend_valor + 3 >= VIAJE_COLA_CAPACIDAD;
    /* Un 409 sostenido no se arregla solo aunque la P4 este encendida y
     * respondiendo (es un choque de numeracion, no que este apagada) -- por
     * eso lleva su propio aviso, distinto de "casi llena". Ver el comentario
     * de INTENTOS_409_ATASCO en viaje_cola.c. */
    bool atascada = viaje_cola_bloqueada();
    /* Credenciales mal puestas NO se arreglan solas ni encendiendo la P4 (a
     * diferencia de "sin llegar" a secas, que puede ser solo que este
     * apagada): aviso propio para no hacer perder el tiempo mirando el cable
     * o la alimentacion cuando el problema esta en Ajustes. Ver el comentario
     * de INTENTOS_401_ATASCO en viaje_cola.c. */
    bool cred_mal = viaje_cola_credenciales_mal();
    if (cred_mal) {
        lv_label_ins_text(s_pendientes, LV_LABEL_POS_LAST, "  -  CLAVE MAL, AJUSTES");
    } else if (atascada) {
        lv_label_ins_text(s_pendientes, LV_LABEL_POS_LAST, "  -  ATASCADO, MIRA LA P4");
    } else if (casi) {
        lv_label_ins_text(s_pendientes, LV_LABEL_POS_LAST, "  -  CASI LLENA");
    }
    lv_obj_set_style_bg_color(s_pendientes,
                              lv_color_hex((casi || atascada || cred_mal) ? 0xFF4444 : 0xFF9800), 0);
    lv_obj_set_style_text_color(s_pendientes,
                                lv_color_hex((casi || atascada || cred_mal) ? 0xFFFFFF : 0x000000), 0);

    lv_obj_clear_flag(s_pendientes, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_pendientes, LV_ALIGN_BOTTOM_MID, 0, -6);
}

static void pendientes_click_cb(lv_event_t *e)
{
    (void)e;
    /* Primero lo que pide algo de ti, en el mismo orden que pendientes_
     * aplicar(): llegar a un sitio antes que cerrar un apunte. */
    if (s_puntual_pendiente) { view_registro_puntual_declarar_llegada(); return; }
    if (s_sin_cerrar == 0) return;   /* "sin enviar" no lleva a ningun sitio */
    nav_ir_a_sin_cerrar();
}

/* lv_async_call() por si solo NO es seguro llamado desde otra tarea en esta
 * version de LVGL (8.4): toca la lista global de timers sin ningun lock
 * propio, y lv_timer_handler() la recorre desde la tarea LVGL al mismo
 * tiempo. view_info_set_pendientes() SI llega de otra tarea (viaje_cola.c);
 * se protege aqui con el mismo lock que ya usa el bucle principal de LVGL.
 * Detectado auditando el 07-sep-2026. */
void view_info_set_pendientes(size_t pendientes)
{
    /* La asignacion va DENTRO del lock, no antes: s_pend_valor lo lee
     * pendientes_aplicar() desde la tarea LVGL sin ninguna proteccion
     * propia, asi que dejarla fuera es una carrera de datos de verdad
     * (aunque en ESP32, con un size_t alineado, en la practica no llegue a
     * partirse). Detectado el 09-sep-2026.
     * lock(0) = espera SIN LIMITE (ver lvgl_port_lock()), no un timeout de
     * 1s: con timeout, un flush largo de LVGL perdia el dato en silencio
     * (ni se actualizaba s_pend_valor ni se programaba el async_call) hasta
     * el siguiente cambio de cola. Mismo patron ya usado en p4_api.c: mutex
     * reentrante, la tarea LVGL nunca espera por esta llamada, sin riesgo
     * de deadlock. Detectado por el usuario el 09-sep-2026. */
    if (lvgl_port_lock(0)) {
        s_pend_valor = pendientes;
        lv_async_call(pendientes_aplicar, NULL);
        lvgl_port_unlock();
    }
}

void view_info_set_sin_cerrar(size_t sin_cerrar)
{
    /* Esta llega DESDE LVGL (la pantalla de registros), pero se aplaza y se
     * protege igual: asi las dos entradas hacen lo mismo y no hay que
     * acordarse de cual es cual el dia que se toque (o de que una de ellas
     * empiece a llamarse tambien desde otro sitio). El lock es reentrante,
     * asi que tomarlo aqui aunque ya se este en la tarea LVGL no bloquea. */
    if (lvgl_port_lock(0)) {
        s_sin_cerrar = sin_cerrar;
        lv_async_call(pendientes_aplicar, NULL);
        lvgl_port_unlock();
    }
}

void view_info_set_puntual_pendiente(const char *nombre)
{
    /* Misma pastilla que "sin cerrar"/"sin enviar" -- ver el comentario de
     * pendientes_aplicar sobre por que se combinan en vez de llevar cada
     * una la suya. Asignacion DENTRO del lock, mismo motivo que en
     * view_info_set_pendientes(). */
    if (lvgl_port_lock(0)) {
        s_puntual_pendiente = (nombre && nombre[0]);
        if (s_puntual_pendiente) snprintf(s_puntual_nombre, sizeof(s_puntual_nombre), "%s", nombre);
        lv_async_call(pendientes_aplicar, NULL);
        lvgl_port_unlock();
    }
}

void view_info_captura_alarma_silenciada(void)
{
    /* El estado local, que es lo unico que pinta el icono tachado. NO se manda
     * nada a la P4: en una captura no hay P4 al otro lado, y la orden que
     * manda el toque de verdad se prueba en la placa.
     *
     * NO se marca orden_pend (3.B2): sin una orden en vuelo no hay plazo de
     * 10 s (orden_timeout_check) ni respuesta tardia que puedan deshacer el
     * silencio, y en las vueltas siguientes del carrusel el icono sigue
     * saliendo tachado. */
    s_al[AL_INFO_BATERIA].silenciada = true;
}
