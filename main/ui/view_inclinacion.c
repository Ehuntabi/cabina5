/* view_inclinacion.c - Nivel de burbuja y brujula para aparcar (Fase 3).
 *
 * Burbuja clasica: un circulo pequeno que se desplaza dentro de un circulo
 * grande segun pitch/roll -- no hace falta ningun asset de imagen (no hay
 * pipeline de conversion PNG->LVGL en este entorno), son solo lv_obj
 * circulares con lv_obj_align + offset en pixeles. Color verde/ambar/rojo
 * segun cuanto se aleja de nivel, igual que el resto de la app.
 *
 * ESTA PANTALLA SE REHIZO EL 8-OCT-2026 para los 800x480 REALES de la placa
 * (antes dibujaba en los 480x320 logicos de la UI heredada, ver estilos.h):
 * las dos lecturas pasaron a dos filas con su etiqueta y su cifra grande.
 *
 * ── EL REPARTO DE AHORA (10-nov-2026): DOS ZONAS CLARAS ───────────────────
 *
 * El dueno la vio en la placa y dijo que "el diseno es engorroso y muy
 * mejorable": la brujula era un aro de 150 px, el titulo de la pantalla y el
 * aviso de calibracion le robaban el alto, y el nivel (lo que se mira al
 * colocar las rampas) se quedaba sin aire. Ahora la pantalla es:
 *
 *   - Una CABECERA de 52 px con el titulo en 30 px, violeta, centrado.
 *   - Dos ZONAS iguales de 380x416 (los 52 + 416 + 12 de margen cierran los
 *     480): IZQUIERDA la inclinacion, DERECHA la orientacion, separadas por
 *     16 px y con los mismos 12 px de margen.
 *   - IZQUIERDA: el nivel de 240 px y debajo las dos filas de cifras
 *     (CABECEO / BALANCEO) y "NIVELADA", todo centrado en la misma columna.
 *   - DERECHA: la brujula de 224 px (antes 150) con la rosa de los vientos, el
 *     rumbo y, debajo, el aviso de calibracion: integrado en el instrumento y
 *     no encima de la pantalla. La logica no cambia: sin calibrar, ambar ("gira
 *     el sensor (cal 0/3)"); a 3/3, "brujula calibrada" en gris tenue. El hueco
 *     esta reservado para que no salte nada al pasar de un texto al otro.
 *   - El boton de calibrar, 300x60, abajo en la zona derecha, y su mensaje
 *     ("Calibrando...", "Calibrado") justo encima: es la respuesta del boton y
 *     se busca donde se ha pulsado, no en la otra punta de la pantalla.
 *
 * TODO SE COLOCA CON lv_obj_align Y MEDIDAS DE LA REJILLA DE 4, no con flex:
 * cuando falta el sensor se esconden las filas y el aviso ocupa su hueco, y el
 * resto de la pantalla NO se mueve (con flex, "NIVELADA" y el boton saltarian
 * de sitio al desaparecer una fila).
 */
#include "net/ota_rx.h"
#include "view_inclinacion.h"
#include "ui/ui_style.h"   /* la paleta, en un solo sitio */
#include "estilos.h"
#include "esp_log.h"   /* DIAGNOSTICO: el volcado del arbol */
#include "../tilt.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>   /* abs() de la zona muerta */

/* El borde son 6 grados, que es lo que aguanta un frigo de absorcion de morro a
 * cola segun la especificacion de Dometic (3 de lado a lado). Empezo en 15, con
 * lo que todo lo util quedaba en el primer cuarto del circulo y la bola apenas
 * se movia. */
/* 120 de radio = 240 px de dial. Es lo que cierra la zona izquierda sin tocar
 * las cifras: 240 del dial + 8 + 56 + 4 + 56 de las dos filas + 4 + 48 de
 * "NIVELADA" = 416, que es justo el alto de la zona. Antes eran 280 (radio
 * 140): el reparto en dos zonas no deja esos 40 px, y aun asi el dial sigue
 * siendo el circulo mas grande de la pantalla. */
#define LEVEL_RADIUS    120
/* 18 y no 14: en un dial de 240 la bola de 14 se pierde (y el recorrido hasta
 * el borde es de 102 px, que tambien la haria pequena). */
#define BUBBLE_RADIUS   18   /* px, burbuja */
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

#define COL_NIVEL   UI_COLOR_GREEN_HEX   /* verde - nivelada, hasta 1 grado */
#define COL_CASI    UI_COLOR_YELLOW_HEX   /* ambar - hasta 3, el limite del frigo */
#define COL_MAL     UI_COLOR_RED_HEX   /* rojo  - mas de 3 */

/* Radio en pixeles de una inclinacion dada. */
#define RADIO_DE(grados) ((int)(((grados) / MAX_DEG_SHOWN) * LEVEL_RADIUS))

/* ── El reparto de la pantalla (ver la cabecera del fichero) ─────────────────
 *
 * 12 de margen + 380 + 16 de hueco + 380 + 12 = 800 de ancho; y 52 de cabecera
 * + 416 de zona + 12 de margen de abajo = 480 de alto. Los dos numeros que
 * mandan son ZONA_ANCHO y ZONA_ALTO: si cambian, las dos zonas cambian a la
 * vez y la pantalla sigue cuadrada. */
#define MARGEN_PAGINA   12
#define ZONA_ANCHO      380
#define ZONA_ALTO       416
#define ZONA_Y          52
#define ZONA_HUECO      16

/* El aro de la brujula: 224 px (112 de radio). La rosa (N/E/S/O) va a 88 del
 * centro: la letra (33 de alto) queda a 4 px del borde interior del aro, que es
 * como estaba en el aro de 150. La aguja escala con el aro (ver
 * brujula_draw_cb). */
#define BRUJULA_DIAM    224
#define BRUJULA_CARDINAL_OFF  88

static lv_obj_t *s_circle;
static lv_obj_t *s_bubble;
static lv_obj_t *s_label_status;
static lv_obj_t *s_label_nivel;   /* "NIVELADA", aparte del estado */
static lv_timer_t *s_timer;
/* Las dos lecturas, cada una con su cifra. Ver el reparto en
 * view_inclinacion_create(). */
static lv_obj_t *s_row_pitch;     /* "CABECEO"  + cifra */
static lv_obj_t *s_row_roll;      /* "BALANCEO" + cifra */
static lv_obj_t *s_val_pitch;
static lv_obj_t *s_val_roll;
static lv_obj_t *s_sin_sensor;    /* el aviso, en su sitio */
static lv_obj_t *s_calib_btn;

/* Rumbo actual en grados (lo pinta brujula_draw_cb). */
static int s_rumbo_deg;

/* Dibuja la aguja: dos triangulos (norte rojo, sur claro) girados al rumbo.
 * Se dibuja en el evento del aro, asi que queda por debajo de las letras. */
static void brujula_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (!layer) return;

    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int cx = (a.x1 + a.x2) / 2;
    int cy = (a.y1 + a.y2) / 2;

    /* La aguja escala con el aro: 88 de centro a punta (eran 60 con el aro de
     * 150) y 12 de media base, la misma proporcion. El dibujo es el mismo
     * triangulo; solo cambia el tamano para que no se quede pequena dentro del
     * aro nuevo. La punta norte se funde con la N roja, igual que antes. */
    const int LARGO = 88;   /* de centro a punta */
    const int ANCHO = 12;   /* media anchura en la base */
    float rad = (float)s_rumbo_deg * 3.14159265f / 180.0f;
    float sn = sinf(rad), cs = cosf(rad);

    /* gira un punto del plano de la aguja (x derecha, y abajo) */
    #define PT(px, py) ((lv_point_precise_t){ \
        (lv_value_precise_t)(cx + (px) * cs - (py) * sn), \
        (lv_value_precise_t)(cy + (px) * sn + (py) * cs) })

    lv_draw_triangle_dsc_t t;
    lv_draw_triangle_dsc_init(&t);

    /* NORTE: rojo, punta arriba */
    t.bg_color = UI_COLOR_RED;
    t.bg_opa = LV_OPA_COVER;
    t.p[0] = PT(0, -LARGO);
    t.p[1] = PT(-ANCHO, 0);
    t.p[2] = PT(ANCHO, 0);
    lv_draw_triangle(layer, &t);

    /* SUR: claro, punta abajo */
    t.bg_color = UI_COLOR_TEXT_SOFT;
    t.p[0] = PT(0, LARGO);
    t.p[1] = PT(-ANCHO, 0);
    t.p[2] = PT(ANCHO, 0);
    lv_draw_triangle(layer, &t);

    #undef PT
}

/* Puntos cardinales de la brujula (N, NE, E...). */
static const char *cardinal_de(float deg)
{
    static const char *n[8] = {"N", "NE", "E", "SE", "S", "SO", "O", "NO"};
    int i = (int)((deg + 22.5f) / 45.0f) % 8;
    if (i < 0) i += 8;
    return n[i];
}

/* Brujula (solo si el sensor es un BNO055: el ADXL345 no tiene magnetometro) */
static lv_obj_t *s_brujula, *s_rumbo, *s_rumbo_cal, *s_rosa;
static int       s_rumbo_pintado = -1;

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
    lv_obj_set_style_text_font(l, &FUENTE_NORMAL, 0);
    /* El rotulo va SOBRE EL ANILLO, un poco hacia dentro: con el dial de 240 el
     * borde queda a 120 de la cruz, y ahi cae el numero de los 6 grados. */
    lv_obj_align(l, LV_ALIGN_CENTER, radio - 16, 12);
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
/* 7 px y no 5: con 5 el usuario, viendolo en la placa, pidio apretar mas
 * (24-ago-2026). Con el dial de radio 120 son 0,41 grados, por debajo del
 * circulo verde de 1 grado que marca "nivelada": no se come informacion. */
#define BOLA_ZONA_MUERTA_PX  7
#define TEXTO_HISTERESIS_DEG 0.08f

static int   s_bola_x, s_bola_y;
static bool  s_bola_pintada;
static float s_pitch_escrito, s_roll_escrito;
static bool  s_texto_escrito;

static void refresh_cb(lv_timer_t *t)
{
    /* Ni el nivel mientras se graba la flash (ver ota_rx_grabando). */
    if (ota_rx_grabando()) return;

    (void)t;
    if (!tilt_is_present()) {
        lv_obj_add_flag(s_bubble, LV_OBJ_FLAG_HIDDEN);
        s_bola_pintada  = false;   /* al volver, que se coloque sin zona muerta */
        s_texto_escrito = false;
        lv_label_set_text(s_label_nivel, "");
        /* Sin sensor no hay cifras que enseñar: se esconden las dos filas y en
         * su hueco va el aviso. Antes se ponia "--" en las cifras y el aviso
         * debajo, con lo que la pantalla parecia decir que estas a nivel. */
        lv_obj_add_flag(s_row_pitch, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_row_roll, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_sin_sensor, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_label_status, "");
        if (s_brujula) lv_obj_add_flag(s_brujula, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_row_pitch, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_row_roll, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_sin_sensor, LV_OBJ_FLAG_HIDDEN);

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
     * queda basta para que la bola no pare: el dial reparte 6 grados en 102 px,
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

    /* ── Brujula ────────────────────────────────────────────────────────
     * Solo si el sensor la tiene (BNO055). El rumbo es un entero de grados:
     * con la aguja no hace falta decimales y asi no hay temblor de texto. */
    {
        float rumbo = 0.0f;
        uint8_t cal = 0;
        if (tilt_get_heading(&rumbo, &cal)) {
            lv_obj_clear_flag(s_brujula, LV_OBJ_FLAG_HIDDEN);
            int r = (int)(rumbo + 0.5f) % 360;
            if (r != s_rumbo_pintado) {
                s_rumbo_pintado = r;
                /* LVGL gira en decimas de grado: 3600 = vuelta completa. */
                /* la aguja se DIBUJA: basta cambiar el angulo y repintar */
                s_rumbo_deg = r;
                if (s_rosa) lv_obj_invalidate(s_rosa);
                char txt[16];
                snprintf(txt, sizeof txt, "%d\xC2\xB0 %s", r, cardinal_de(rumbo));
                lv_label_set_text(s_rumbo, txt);
            }
            /* La calidad del magnetometro: 3 = calibrado. Menos, en ambar. */
            bool ok = (cal >= 3);
            lv_obj_set_style_text_color(s_rumbo, ok ? UI_COLOR_TEXT : UI_COLOR_YELLOW, 0);
            char c[32];
            snprintf(c, sizeof c, ok ? "brujula calibrada" : "gira el sensor (cal %u/3)",
                     (unsigned)cal);
            lv_label_set_text(s_rumbo_cal, c);
            lv_obj_set_style_text_color(s_rumbo_cal, ok ? UI_COLOR_TEXT_DIM : UI_COLOR_YELLOW, 0);
        } else {
            lv_obj_add_flag(s_brujula, LV_OBJ_FLAG_HIDDEN);
        }
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
        char buf[16];
        /* El grado va pegado a la cifra y en la misma etiqueta: separarlo en su
         * propia etiqueta (como la "V" y la "A" de la bateria) obligaria a
         * fijar tres huecos para un texto que cambia de ancho con el signo. */
        snprintf(buf, sizeof(buf), "%+.1f\xC2\xB0", pitch);
        lv_label_set_text(s_val_pitch, buf);
        snprintf(buf, sizeof(buf), "%+.1f\xC2\xB0", roll);
        lv_label_set_text(s_val_roll, buf);
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

/* Una fila de lectura: "CABECEO" a la izquierda y la cifra grande alineada a
 * la DERECHA dentro de su hueco. El hueco fijo es lo que impide que la cifra
 * baile: "+2,3" y "-12,4" ocupan anchos distintos, y con la etiqueta pegada
 * detras el numero se moveria en cada refresco (500 ms).
 *
 * Mismo patron que las temperaturas de la pantalla de datos (view_info.c,
 * make_fila_dato): es la pantalla que se mira desde fuera del vehiculo, y las
 * dos cifras tienen que leerse de un vistazo.
 *
 * Los anchos estan MEDIDOS con las fuentes del proyecto, no elegidos a ojo:
 * "BALANCEO" mide 143 px en la fuente de rotulo (26) y el valor mas ancho que
 * puede salir, "+90.0", 131 en la de la cifra (40). Con 176 + 144 = 320 la
 * etiqueta y la cifra nunca se pisan (quedan 33 px de aire entre ellas) y la
 * fila, centrada en la zona de 380, es mas estrecha que el dial: se lee como un
 * bloque debajo del circulo, no como dos columnas sueltas. */
#define FILA_ALTO     56
#define FILA_ETIQ_W  176
#define FILA_VAL_W   144

static lv_obj_t *make_readout(lv_obj_t *padre, const char *etiqueta, lv_coord_t y,
                              lv_obj_t **valor_out)
{
    lv_obj_t *fila = lv_obj_create(padre);
    lv_obj_remove_style_all(fila);
    lv_obj_set_size(fila, FILA_ETIQ_W + FILA_VAL_W, FILA_ALTO);
    lv_obj_clear_flag(fila, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(fila, LV_ALIGN_TOP_MID, 0, y);

    lv_obj_t *l = lv_label_create(fila);
    lv_label_set_text(l, etiqueta);
    lv_obj_set_style_text_color(l, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(l, &FUENTE_NORMAL, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *v = lv_label_create(fila);
    lv_label_set_text(v, "--");
    /* Blanco de dato, por el token de rol: las dos cifras son el dato que se
     * mira y no llevan color de estado (el estado lo dice la burbuja). */
    lv_obj_set_style_text_color(v, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(v, &FUENTE_GRANDE, 0);
    lv_obj_set_width(v, FILA_VAL_W);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
    if (valor_out) *valor_out = v;
    return fila;
}

/* DIAGNOSTICO: vuelca el arbol de esta pantalla. */
void view_inclinacion_diag_arbol(void)
{
    lv_obj_t *scr = lv_screen_active();
    if (!scr) return;
    ESP_LOGW("diag", "MARCA-INCL: %dx%d", (int)lv_obj_get_width(scr), (int)lv_obj_get_height(scr));
    for (int i = 0; i < lv_obj_get_child_count(scr); i++) {
        lv_obj_t *c = lv_obj_get_child(scr, i);
        ESP_LOGW("diag", "MARCA-INCL [%d] %dx%d en (%d,%d)", i,
                 (int)lv_obj_get_width(c), (int)lv_obj_get_height(c),
                 (int)lv_obj_get_x(c), (int)lv_obj_get_y(c));
        for (int j = 0; j < lv_obj_get_child_count(c); j++) {
            lv_obj_t *g = lv_obj_get_child(c, j);
            ESP_LOGW("diag", "MARCA-INCL   [%d.%d] %dx%d en (%d,%d)", i, j,
                     (int)lv_obj_get_width(g), (int)lv_obj_get_height(g),
                     (int)lv_obj_get_x(g), (int)lv_obj_get_y(g));
        }
    }
}

/* Brujula: circulo con N/E/S/O y una aguja que gira al rumbo. Debajo, el
 * rumbo en grados y la calidad del magnetometro (0-3): sin calibrar, el
 * rumbo no vale y se avisa en ambar. */
static void make_brujula(lv_obj_t *zona)
{
    /* El contenedor mide el aro + el rumbo + el aviso: 224 + 8 + 48 + 4 + 22 =
     * 306. Se redondea a 308 (rejilla de 4) y el aviso va pegado arriba, asi
     * que sobran 2 px por abajo. Es el bloque que se esconde entero cuando el
     * sensor no tiene brujula (ADXL345): el boton de calibrar NO va aqui
     * dentro, porque el nivel si existe en esa placa. */
    s_brujula = lv_obj_create(zona);
    lv_obj_set_size(s_brujula, ZONA_ANCHO, 308);
    lv_obj_set_style_bg_opa(s_brujula, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_brujula, 0, 0);
    lv_obj_set_style_pad_all(s_brujula, 0, 0);
    lv_obj_clear_flag(s_brujula, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_brujula, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *cj = lv_obj_create(s_brujula);
    lv_obj_set_size(cj, BRUJULA_DIAM, BRUJULA_DIAM);
    lv_obj_set_style_radius(cj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cj, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(cj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cj, 4, 0);
    lv_obj_set_style_border_color(cj, UI_COLOR_CARD_BORDER, 0);
    lv_obj_set_style_pad_all(cj, 0, 0);
    lv_obj_clear_flag(cj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(cj, LV_ALIGN_TOP_MID, 0, 0);

    /* Puntos cardinales: N en rojo arriba, el resto en gris. A 88 del centro,
     * la letra (33 de alto) queda a 4 px del borde interior del aro: se lee
     * entera y no se pega a la circunferencia. */
    static const struct { const char *t; int x, y; } pts[4] = {
        {"N",  0, -BRUJULA_CARDINAL_OFF}, {"S",  0,  BRUJULA_CARDINAL_OFF},
        {"E",  BRUJULA_CARDINAL_OFF, 0},  {"O", -BRUJULA_CARDINAL_OFF, 0},
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *l = lv_label_create(cj);
        lv_label_set_text(l, pts[i].t);
        lv_obj_set_style_text_font(l, &FUENTE_NORMAL, 0);
        lv_obj_set_style_text_color(l, (i == 0) ? UI_COLOR_RED : UI_COLOR_TEXT_DIM, 0);
        lv_obj_align(l, LV_ALIGN_CENTER, pts[i].x, pts[i].y);
    }

    /* AGUJA DIBUJADA CON TRIANGULOS (09-nov-2026). La referencia del dueño es
     * una aguja de triangulos rellenos y afilados, con un buje blanco en el
     * centro; con rectangulos no se consigue. LVGL 9 trae lv_draw_triangle, asi
     * que la aguja se PINTA en el evento de dibujo del aro (por debajo de las
     * letras N/E/S/O y del buje) y el angulo se recalcula al cambiar el rumbo:
     * no hay objetos girando, no hay bordes dentados y el triangulo se ve
     * afilado en cualquier direccion. */
    s_rosa = cj;
    lv_obj_add_event_cb(cj, brujula_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    /* buje: circulo blanco con aro gris en el centro. 36 px (era 24 en el aro
     * de 150): escala con el aro para que la aguja no salga de la nada. */
    lv_obj_t *buje = lv_obj_create(cj);
    lv_obj_set_size(buje, 36, 36);
    lv_obj_set_style_radius(buje, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(buje, UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(buje, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(buje, 4, 0);
    lv_obj_set_style_border_color(buje, UI_COLOR_TEXT_DIM, 0);
    lv_obj_clear_flag(buje, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(buje);

    /* El rumbo, debajo y en grande: es el dato que se mira. A 232 (224 del aro
     * + 8 de aire) para que la aguja no se pegue al texto. */
    s_rumbo = lv_label_create(s_brujula);
    lv_label_set_text(s_rumbo, "--");
    lv_obj_set_style_text_font(s_rumbo, &FUENTE_GRANDE, 0);
    lv_obj_set_style_text_color(s_rumbo, UI_COLOR_TEXT, 0);
    lv_obj_align(s_rumbo, LV_ALIGN_TOP_MID, 0, 232);

    /* El aviso de calibracion va pegado al rumbo y no en la cabecera: es una
     * nota del instrumento (y en letra de pie, 18, para que no compita con el
     * dato). El texto lo pone refresh_cb: ambar sin calibrar y "brujula
     * calibrada" en gris a 3/3; aqui solo se le reserva el sitio. */
    s_rumbo_cal = lv_label_create(s_brujula);
    lv_label_set_text(s_rumbo_cal, "");
    lv_obj_set_style_text_font(s_rumbo_cal, &FUENTE_PIE, 0);
    lv_obj_set_style_text_color(s_rumbo_cal, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(s_rumbo_cal, LV_ALIGN_TOP_MID, 0, 284);
}

void view_inclinacion_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    /* Sin flex: las dos zonas y la cabecera van a coordenadas fijas (ver la
     * cabecera del fichero). Asi una fila que se esconde no arrastra a las
     * demas, que es lo que pasaba con la columna flex de antes. */
    lv_obj_set_style_pad_all(parent, 0, 0);

    /* ── CABECERA ──────────────────────────────────────────────────────
     * Titulo arriba y en 30 px (antes estaba en la columna de la brujula, en
     * 28, y le robaba el alto al aro). Centrado: la auditoria de estilo no
     * admite un rotulo en mayusculas alineado a la derecha, y ademas es el
     * nombre de la pantalla. */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "INCLINACION");
    lv_obj_set_style_text_color(title, UI_COLOR_VIOLET, 0);
    lv_obj_set_style_text_font(title, &FUENTE_MEDIA, 0);
    lv_obj_set_style_text_letter_space(title, UI_LS_TITULO, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    /* ── ZONA IZQUIERDA: la inclinacion ────────────────────────────────
     * El nivel manda (240 px), debajo las dos cifras y "NIVELADA". Todo
     * centrado en la misma columna de 380 para que se lea como un bloque. */
    lv_obj_t *left = lv_obj_create(parent);
    lv_obj_set_size(left, ZONA_ANCHO, ZONA_ALTO);
    lv_obj_set_pos(left, MARGEN_PAGINA, ZONA_Y);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    s_circle = lv_obj_create(left);
    lv_obj_set_size(s_circle, LEVEL_RADIUS * 2, LEVEL_RADIUS * 2);
    lv_obj_set_style_radius(s_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_circle, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(s_circle, LV_OPA_COVER, 0);
    /* 4 px, el mismo borde que el aro de la brujula: son los dos
     * instrumentos de la pantalla y se dibujan igual. */
    lv_obj_set_style_border_width(s_circle, 4, 0);
    /* Rojo FIJO: es el ultimo escalon del semaforo (mas de MAX_DEG_SHOWN, o
     * sea mas de 6 grados, el tope del dial). Antes
     * cambiaba de color con la inclinacion, pero ahora eso lo dice la bola, y
     * dos cosas cambiando a la vez confunden mas que informan. */
    lv_obj_set_style_border_color(s_circle, lv_color_hex(COL_MAL), 0);
    lv_obj_clear_flag(s_circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_circle, LV_ALIGN_TOP_MID, 0, 0);

    /* Anillos de referencia, de fuera adentro. El del centro es la zona de
     * NIVELADO y va en verde: cuando la bola entra ahi, ya puedes parar. Es
     * pequeno (NIVELADO_DEG es 1 grado, que a esta escala son 20 px) y la
     * propia bola lo tapa -- a proposito: taparlo ES la senal. */
    make_ovalo(s_circle);
    make_anillo(s_circle, RADIO_DE(NIVELADO_DEG), COL_NIVEL, 2);

    /* Cruz central de referencia (nivel = 0,0), de lado a lado */
    /* 2 px y gris claro: a 1 px y en gris oscuro sobre el fondo casi negro del
     * dial no se veia, que es como no tenerla. */
    lv_obj_t *cross_h = lv_obj_create(s_circle);
    lv_obj_set_size(cross_h, LEVEL_RADIUS * 2, 2);
    lv_obj_set_style_bg_color(cross_h, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_bg_opa(cross_h, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cross_h, 0, 0);
    lv_obj_clear_flag(cross_h, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(cross_h);

    lv_obj_t *cross_v = lv_obj_create(s_circle);
    lv_obj_set_size(cross_v, 2, LEVEL_RADIUS * 2);
    lv_obj_set_style_bg_color(cross_v, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_bg_opa(cross_v, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cross_v, 0, 0);
    lv_obj_clear_flag(cross_v, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(cross_v);

    /* Cuanto vale cada anillo, escrito. Sin esto los anillos decoran pero no
     * miden. */
    make_rotulo_anillo(s_circle, RADIO_DE(NIVELADO_DEG), "1", COL_NIVEL);
    make_rotulo_anillo(s_circle, RADIO_DE(AMBAR_ROLL_DEG), "3", COL_CASI);
    /* El "6" de fuera va 8 px hacia dentro: a radio completo el rotulo cae
     * justo sobre el borde del dial y lo pisa (medido en el volcado). */
    make_rotulo_anillo(s_circle, LEVEL_RADIUS - 8, "6\xC2\xB0", COL_MAL);

    s_bubble = lv_obj_create(s_circle);
    lv_obj_set_size(s_bubble, BUBBLE_RADIUS * 2, BUBBLE_RADIUS * 2);
    lv_obj_set_style_radius(s_bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_bubble, UI_COLOR_GREEN, 0);
    lv_obj_set_style_bg_opa(s_bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_bubble, 0, 0);
    lv_obj_clear_flag(s_bubble, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(s_bubble);

    /* Las dos cifras: 240 + 8 + 56 + 4 + 56 + 4 + 48 = 416, el alto de la zona
     * justo. Si se toca una, hay que tocar el reparto entero (las constantes
     * de arriba). */
    s_row_pitch = make_readout(left, "CABECEO", 248, &s_val_pitch);
    s_row_roll  = make_readout(left, "BALANCEO", 308, &s_val_roll);

    /* El aviso de que no hay sensor ocupa el hueco de las dos filas: 272 lo
     * deja centrado en ese bloque (248..364) y sobran ~24 px de aire arriba y
     * abajo, asi que no se pega ni al dial ni a "NIVELADA". */
    s_sin_sensor = lv_label_create(left);
    lv_label_set_text(s_sin_sensor, "Sensor de nivel\nno detectado");
    lv_obj_set_style_text_color(s_sin_sensor, UI_COLOR_YELLOW, 0);
    lv_obj_set_style_text_font(s_sin_sensor, &FUENTE_NORMAL, 0);
    lv_obj_set_style_text_align(s_sin_sensor, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_sin_sensor, FILA_ETIQ_W + FILA_VAL_W);
    lv_obj_align(s_sin_sensor, LV_ALIGN_TOP_MID, 0, 272);
    lv_obj_add_flag(s_sin_sensor, LV_OBJ_FLAG_HIDDEN);

    /* "NIVELADA" en verde y grande: es la respuesta que se busca al colocar las
     * rampas, y se lee desde fuera de la autocaravana. Va debajo de las cifras,
     * no en la otra zona: es del nivel. */
    s_label_nivel = lv_label_create(left);
    lv_label_set_text(s_label_nivel, "");
    lv_obj_set_style_text_color(s_label_nivel, UI_COLOR_GREEN, 0);
    lv_obj_set_style_text_font(s_label_nivel, &FUENTE_GRANDE, 0);
    lv_obj_align(s_label_nivel, LV_ALIGN_TOP_MID, 0, 368);

    /* ── ZONA DERECHA: la orientacion ──────────────────────────────────
     * La brujula es la protagonista (224 px) y debajo su rumbo y el aviso. El
     * boton de calibrar cierra la zona abajo, con su mensaje encima. */
    lv_obj_t *right = lv_obj_create(parent);
    lv_obj_set_size(right, ZONA_ANCHO, ZONA_ALTO);
    lv_obj_set_pos(right, MARGEN_PAGINA + ZONA_ANCHO + ZONA_HUECO, ZONA_Y);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 0, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    make_brujula(right);

    s_label_status = lv_label_create(right);
    lv_label_set_text(s_label_status, "");
    lv_obj_set_style_text_color(s_label_status, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_label_status, &FUENTE_NORMAL, 0);
    lv_obj_set_style_text_align(s_label_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_label_status, FILA_ETIQ_W + FILA_VAL_W);
    lv_obj_align(s_label_status, LV_ALIGN_TOP_MID, 0, 316);

    s_calib_btn = lv_btn_create(right);
    /* 300x60: es un boton tactil de verdad (un dedo son ~40 px) y cabe en la
     * zona con 40 px de margen a cada lado. */
    lv_obj_set_size(s_calib_btn, 300, 60);
    lv_obj_set_style_bg_color(s_calib_btn, UI_COLOR_BOTON, 0);
    lv_obj_add_event_cb(s_calib_btn, calib_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(s_calib_btn, LV_ALIGN_TOP_MID, 0, 356);
    lv_obj_t *calib_lbl = lv_label_create(s_calib_btn);
    lv_label_set_text(calib_lbl, "Calibrar nivel");
    lv_obj_set_style_text_font(calib_lbl, &FUENTE_NORMAL, 0);
    lv_obj_center(calib_lbl);

    lv_obj_add_flag(s_brujula, LV_OBJ_FLAG_HIDDEN);   /* hasta saber si hay */

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
