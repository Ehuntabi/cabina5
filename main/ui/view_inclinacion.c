/* view_inclinacion.c - Nivel de burbuja para aparcar (Fase 3).
 *
 * Burbuja clasica: un circulo pequeno que se desplaza dentro de un circulo
 * grande segun pitch/roll -- no hace falta ningun asset de imagen (no hay
 * pipeline de conversion PNG->LVGL en este entorno), son solo lv_obj
 * circulares con lv_obj_align + offset en pixeles. Color verde/ambar/rojo
 * segun cuanto se aleja de nivel, igual que el resto de la app.
 *
 * Layout en fila (circulo a la izda, info a la dcha): la resolucion
 * logica de la pantalla es LANDSCAPE 480x320 (ver ui_theme.h), apilar
 * todo verticalmente como en un primer intento no cabia comodo en solo
 * 320px de alto -- corregido 18-ago-2026.
 */
#include "view_inclinacion.h"
#include "../tilt.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>   /* abs() de la zona muerta */

/* El borde son 6 grados, que es lo que aguanta un frigo de absorcion de morro a
 * cola segun la especificacion de Dometic (3 de lado a lado). Empezo en 15, con
 * lo que todo lo util quedaba en el primer cuarto del circulo y la bola apenas
 * se movia. */
/* 120 de radio = 240 px de dial, el maximo que entra a lo ancho: 240 del dial
 * + 200 de la columna de lecturas + los 10 de margen a cada lado suman 460 de
 * los 480. De alto sobra (240 de 300). */
#define LEVEL_RADIUS    120  /* px, circulo exterior */
#define BUBBLE_RADIUS   14   /* px, burbuja */
#define MAX_DEG_SHOWN   6.0f  /* a partir de esto la burbuja se pega al borde */

/* Por debajo de esto se da por nivelada.
 *
 * Empezo en 0,5 y era irreal: medio centimetro por metro, mas fino que la
 * precision del propio montaje del sensor, y no lo pide ni el frigo ni la
 * espalda. Los numeros de verdad (Dometic, referencia del sector): un frigo de
 * absorcion aguanta 3 grados de lado a lado y 6 de morro a cola, y avisan de no
 * dejarlo desnivelado mas de 1-2 horas funcionando, porque el amoniaco
 * cristaliza y taponaria el circuito. Para dormir, la pendiente se empieza a
 * notar sobre 1-2 grados.
 *
 * Asi que 1 grado para el verde: comodo para dormir y de sobra para el frigo.
 * Decision del usuario (21-ago-2026). */
#define NIVELADO_DEG    1.0f

/* Anillos de referencia rotulados. Sin ellos la bola te dice hacia donde, pero
 * no CUANTO: habia que bajar la vista al texto para enterarse.
 *
 * Van en SEMAFORO, de dentro afuera, y la bola toma el color del anillo en el
 * que esta: asi el color solo ya dice si vas bien, sin comparar posiciones ni
 * leer los numeros. */
/* Lo que aguanta un frigo de absorcion NO es igual en los dos ejes (Dometic):
 * 3 grados de lado a lado y 6 de morro a cola, porque va montado de costado.
 * Asi que la zona aceptable es un OVALO, no un circulo: estrecho en balanceo y
 * alto en cabeceo. Dibujarla redonda seria mentir por los dos lados a la vez --
 * te asustaria de mas cabeceando y de menos balanceando. */
#define AMBAR_ROLL_DEG    3.0f   /* lado a lado */
#define AMBAR_PITCH_DEG   6.0f   /* morro a cola */

#define COL_NIVEL   0x4CD964   /* verde - nivelada, hasta 1 grado */
#define COL_CASI    0xFFD54F   /* ambar - hasta 3, el limite del frigo */
#define COL_MAL     0xFF4444   /* rojo  - mas de 3 */

/* Radio en pixeles de una inclinacion dada. */
#define RADIO_DE(grados) ((int)(((grados) / MAX_DEG_SHOWN) * LEVEL_RADIUS))

static lv_obj_t *s_circle;
static lv_obj_t *s_bubble;
static lv_obj_t *s_label_deg;
static lv_obj_t *s_label_status;
static lv_obj_t *s_label_nivel;   /* "NIVELADA", aparte del estado */
static lv_timer_t *s_timer;

/* El color mira cada eje con SU vara, igual que el ovalo que se dibuja. */
static lv_color_t color_for_level(float pitch, float roll)
{
    float ap = fabsf(pitch), ar = fabsf(roll);
    if (ap <= NIVELADO_DEG && ar <= NIVELADO_DEG) return lv_color_hex(COL_NIVEL);
    if (ap <= AMBAR_PITCH_DEG && ar <= AMBAR_ROLL_DEG) return lv_color_hex(COL_CASI);
    return lv_color_hex(COL_MAL);
}

/* Zona aceptable, en forma de ovalo. LVGL no dibuja elipses, pero un rectangulo
 * con el radio al maximo da una "pastilla" -- semicirculos arriba y abajo con
 * los lados rectos -- que a estas proporciones se lee igual de bien. Se mete 4
 * px para dentro porque a 6 grados el alto coincide justo con el borde del dial
 * y se saldria por encima de su propio marco. */
static void make_ovalo(lv_obj_t *padre)
{
    lv_obj_t *o = lv_obj_create(padre);
    lv_obj_set_size(o, RADIO_DE(AMBAR_ROLL_DEG) * 2,
                       RADIO_DE(AMBAR_PITCH_DEG) * 2 - 4);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(COL_CASI), 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(o);
}

/* Un anillo de referencia: circulo hueco con el borde fino. Se crean ANTES que
 * la burbuja para que esta quede por encima. */
static void make_anillo(lv_obj_t *padre, int radio, uint32_t color, int grosor)
{
    lv_obj_t *a = lv_obj_create(padre);
    lv_obj_set_size(a, radio * 2, radio * 2);
    lv_obj_set_style_radius(a, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(a, grosor, 0);
    lv_obj_set_style_border_color(a, lv_color_hex(color), 0);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(a);
}

/* Rotulo del anillo, sobre el eje horizontal y justo debajo de la linea: ahi no
 * pisa ni la cruz ni el recorrido vertical de la bola. */
static void make_rotulo_anillo(lv_obj_t *padre, int radio, const char *txt,
                                uint32_t color)
{
    lv_obj_t *l = lv_label_create(padre);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, radio - 12, 10);
}

static void calib_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_label_set_text(s_label_status, "Calibrando...");
    /* Este callback corre en la propia tarea de LVGL: sin forzar el
     * repintado aqui, "Calibrando..." se quedaba en el arbol de objetos
     * pero nunca llegaba a pintarse en pantalla -- tilt_calibrate() bloquea
     * esa MISMA tarea ~0.5s (20 muestras x 20ms, ver tilt.c) antes de que
     * el bucle de LVGL pueda volver a hacer flush. lv_refr_now() dibuja YA
     * lo pendiente (aqui, esta etiqueta) sin esperar al siguiente ciclo.
     * Detectado por el usuario el 09-sep-2026. */
    lv_refr_now(NULL);
    bool ok = tilt_calibrate();   /* bloquea ~0.5s (ver tilt.h) */
    lv_label_set_text(s_label_status, ok ? "Calibrado" : "No he podido guardarlo");
}

/* Lo ultimo PINTADO, para no repintar por un resto de ruido. Ver la zona muerta
 * en refresh_cb(). */
/* 5 px y no 3: con 3 quedaba bien pero el usuario, viendolo en la placa, pidio
 * apretar mas (24-ago-2026). Son 0,28 grados, todavia menos de un tercio de
 * grado y muy por debajo del circulo verde de 1. */
#define BOLA_ZONA_MUERTA_PX  5
#define TEXTO_HISTERESIS_DEG 0.08f

static int   s_bola_x, s_bola_y;
static bool  s_bola_pintada;
static float s_pitch_escrito, s_roll_escrito;
static bool  s_texto_escrito;

static void refresh_cb(lv_timer_t *t)
{
    (void)t;
    if (!tilt_is_present()) {
        lv_obj_add_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
        s_bola_pintada  = false;   /* al volver, que se coloque sin zona muerta */
        s_texto_escrito = false;
        lv_label_set_text(s_label_deg, "--");
        lv_label_set_text(s_label_nivel, "");
        lv_label_set_text(s_label_status, "Sensor ADXL345 no detectado");
        return;
    }

    float pitch, roll;
    if (!tilt_get(&pitch, &roll)) {
        lv_label_set_text(s_label_nivel, "");
        lv_label_set_text(s_label_status, "Error de lectura I2C");
        return;
    }
    lv_obj_clear_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);

    float cr = roll  >  MAX_DEG_SHOWN ?  MAX_DEG_SHOWN : (roll  < -MAX_DEG_SHOWN ? -MAX_DEG_SHOWN : roll);
    float cp = pitch >  MAX_DEG_SHOWN ?  MAX_DEG_SHOWN : (pitch < -MAX_DEG_SHOWN ? -MAX_DEG_SHOWN : pitch);
    /* La bola rueda al lado BAJO, como una canica: donde este la bola, ahi va la
     * rampa. Asi no hay que traducir nada mentalmente al nivelar.
     *
     * El balanceo ya salia asi, pero el cabeceo iba al reves (levantabas el
     * morro y la bola se iba hacia delante, o sea al lado ALTO): las dos
     * formulas de tilt.c no llevan el mismo signo, una tiene el menos y la otra
     * no. Se corrige AQUI, en el dibujo, y no en tilt.c a proposito: alli
     * cambiaria tambien el signo del angulo que se escribe debajo ("Cabeceo
     * +2.3") y el de la calibracion ya guardada en NVS, que no tienen nada de
     * malo. */
    int off_x = (int)((cr / MAX_DEG_SHOWN) * (LEVEL_RADIUS - BUBBLE_RADIUS));
    int off_y = -(int)((cp / MAX_DEG_SHOWN) * (LEVEL_RADIUS - BUBBLE_RADIUS));
    /* ZONA MUERTA. El filtro de tilt.c quita casi todo el ruido, pero lo que
     * queda basta para que la bola no pare: el dial reparte 6 grados en 106 px,
     * asi que UNA centesima de grado ya es un cuarto de pixel y cualquier resto
     * mueve el dibujo. Aqui se corta por lo sano: la bola no se pinta en otro
     * sitio hasta que el sitio nuevo esta a BOLA_ZONA_MUERTA_PX o mas.
     *
     * Y cuando se mueve va al sitio EXACTO, no a saltos de ese tamano: la zona
     * muerta decide CUANDO se redibuja, no donde. */
    if (!s_bola_pintada ||
        abs(off_x - s_bola_x) >= BOLA_ZONA_MUERTA_PX ||
        abs(off_y - s_bola_y) >= BOLA_ZONA_MUERTA_PX) {
        s_bola_x = off_x;
        s_bola_y = off_y;
        s_bola_pintada = true;
        lv_obj_align(s_bubble, LV_ALIGN_CENTER, off_x, off_y);
    }

    lv_color_t col = color_for_level(pitch, roll);
    lv_obj_set_style_bg_color(s_bubble, col, 0);

    /* Que este nivelada se dice ADEMAS con palabras: el color solo no vale si
     * lo miras de reojo desde fuera del vehiculo, colocando las rampas. */
    /* En su propia linea y no en la de estado: esa la usa el boton de calibrar
     * ("Calibrando...", "Calibrado") y el refresco de 200 ms se la comeria. */
    lv_label_set_text(s_label_nivel,
                      (fabsf(pitch) <= NIVELADO_DEG && fabsf(roll) <= NIVELADO_DEG)
                      ? "NIVELADA" : "");

    /* Los numeros, con la misma idea: sin esto la ultima cifra bailaba sola
     * cuando el valor caia justo entre dos decimas. */
    if (!s_texto_escrito ||
        fabsf(pitch - s_pitch_escrito) > TEXTO_HISTERESIS_DEG ||
        fabsf(roll  - s_roll_escrito)  > TEXTO_HISTERESIS_DEG) {
        s_pitch_escrito = pitch;
        s_roll_escrito  = roll;
        s_texto_escrito = true;
        char buf[48];
        snprintf(buf, sizeof(buf), "Cabeceo %+.1f\xC2\xB0\nBalanceo %+.1f\xC2\xB0",
                 pitch, roll);
        lv_label_set_text(s_label_deg, buf);
    }
    lv_label_set_text(s_label_status, "");
}

/* Pausa/reanuda la lectura periodica del ADXL345 segun si esta pantalla es
 * la que se ve ahora mismo. Ver el comentario de la creacion del timer. */
static void screen_evento_cb(lv_event_t *e)
{
    if (!s_timer) return;
    if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOADED) {
        lv_timer_resume(s_timer);
    } else {
        lv_timer_pause(s_timer);
    }
}

void view_inclinacion_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 10, 0);

    /* Columna izquierda: solo el circulo (le sobra alto de sobra en 320px) */
    lv_obj_t *left = lv_obj_create(parent);
    lv_obj_set_size(left, LEVEL_RADIUS * 2 + 8, LEVEL_RADIUS * 2 + 8);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    s_circle = lv_obj_create(left);
    lv_obj_set_size(s_circle, LEVEL_RADIUS * 2, LEVEL_RADIUS * 2);
    lv_obj_set_style_radius(s_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_circle, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(s_circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_circle, 3, 0);
    /* Rojo FIJO: es el ultimo escalon del semaforo (mas de MAX_DEG_SHOWN, o
     * sea mas de 6 grados, el tope del dial). Antes
     * cambiaba de color con la inclinacion, pero ahora eso lo dice la bola, y
     * dos cosas cambiando a la vez confunden mas que informan. */
    lv_obj_set_style_border_color(s_circle, lv_color_hex(COL_MAL), 0);
    lv_obj_clear_flag(s_circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(s_circle);

    /* Anillos de referencia, de fuera adentro. El del centro es la zona de
     * NIVELADO y va en verde: cuando la bola entra ahi, ya puedes parar. Es
     * pequeno (NIVELADO_DEG es 1 grado, que a esta escala son 20 px) y la
     * propia bola lo tapa -- a proposito: taparlo ES la senal. */
    make_ovalo(s_circle);
    make_anillo(s_circle, RADIO_DE(NIVELADO_DEG), COL_NIVEL, 2);

    /* Cruz central de referencia (nivel = 0,0), de lado a lado */
    /* 2 px y gris claro: a 1 px y en 0x444444 sobre el fondo casi negro del
     * dial no se veia, que es como no tenerla. */
    lv_obj_t *cross_h = lv_obj_create(s_circle);
    lv_obj_set_size(cross_h, LEVEL_RADIUS * 2, 2);
    lv_obj_set_style_bg_color(cross_h, lv_color_hex(0x888888), 0);
    lv_obj_set_style_bg_opa(cross_h, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cross_h, 0, 0);
    lv_obj_clear_flag(cross_h, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(cross_h);

    lv_obj_t *cross_v = lv_obj_create(s_circle);
    lv_obj_set_size(cross_v, 2, LEVEL_RADIUS * 2);
    lv_obj_set_style_bg_color(cross_v, lv_color_hex(0x888888), 0);
    lv_obj_set_style_bg_opa(cross_v, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cross_v, 0, 0);
    lv_obj_clear_flag(cross_v, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(cross_v);

    /* Cuanto vale cada anillo, escrito. Sin esto los anillos decoran pero no
     * miden. */
    make_rotulo_anillo(s_circle, RADIO_DE(NIVELADO_DEG), "1", COL_NIVEL);
    make_rotulo_anillo(s_circle, RADIO_DE(AMBAR_ROLL_DEG), "3", COL_CASI);
    make_rotulo_anillo(s_circle, LEVEL_RADIUS, "6\xC2\xB0", COL_MAL);

    s_bubble = lv_obj_create(s_circle);
    lv_obj_set_size(s_bubble, BUBBLE_RADIUS * 2, BUBBLE_RADIUS * 2);
    lv_obj_set_style_radius(s_bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_bubble, lv_color_hex(0x4CD964), 0);
    lv_obj_set_style_bg_opa(s_bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_bubble, 0, 0);
    lv_obj_clear_flag(s_bubble, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(s_bubble);

    /* Columna derecha: titulo + lecturas + boton de calibrar, apilados */
    lv_obj_t *right = lv_obj_create(parent);
    lv_obj_set_size(right, 200, lv_pct(100));   /* 20 px cedidos al dial */
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 0, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(right, 12, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(right);
    lv_label_set_text(title, "INCLINACION");
    lv_obj_set_style_text_color(title, lv_color_hex(0xAB47BC), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

    s_label_deg = lv_label_create(right);
    lv_label_set_text(s_label_deg, "--");
    lv_obj_set_style_text_color(s_label_deg, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_label_deg, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(s_label_deg, LV_TEXT_ALIGN_CENTER, 0);

    s_label_nivel = lv_label_create(right);
    lv_label_set_text(s_label_nivel, "");
    lv_obj_set_style_text_color(s_label_nivel, lv_color_hex(0x4CD964), 0);
    lv_obj_set_style_text_font(s_label_nivel, &lv_font_montserrat_22, 0);

    s_label_status = lv_label_create(right);
    lv_label_set_text(s_label_status, "");
    lv_obj_set_style_text_color(s_label_status, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(s_label_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_label_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_label_status, lv_pct(100));

    lv_obj_t *calib_btn = lv_btn_create(right);
    lv_obj_set_size(calib_btn, 170, 42);
    lv_obj_set_style_bg_color(calib_btn, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(calib_btn, calib_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *calib_lbl = lv_label_create(calib_btn);
    lv_label_set_text(calib_lbl, "Calibrar nivel");
    lv_obj_center(calib_lbl);

    s_timer = lv_timer_create(refresh_cb, 200, NULL);
    /* Arranca en pausa: esta pantalla nace oculta (NAV_INFO es la que se ve
     * al arrancar, ver nav.c) y sin esto el ADXL345 se leeria por I2C real
     * (hasta 100 ms de timeout, ver tilt.c) 5 veces por segundo aunque nadie
     * este mirando esta vista. Se reanuda/pausa con los eventos de carga de
     * pantalla de LVGL, que "parent" (la screen entera) ya recibe al navegar
     * con el carrusel. Detectado auditando el 07-sep-2026. */
    lv_timer_pause(s_timer);
    lv_obj_add_event_cb(parent, screen_evento_cb, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(parent, screen_evento_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
}
