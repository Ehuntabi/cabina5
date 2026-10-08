/* Un solo sitio para el tamano de las fuentes de la UI.
 *
 * POR QUE EXISTE: la UI se porto del satelite de 3,5" (480x320 logicos) a la
 * placa de 5" (800x480 reales), que tiene 181 ppp en vez de 160, y ademas se
 * mira desde el asiento del conductor y no de cerca. En vez de tocar los ~105
 * sitios donde se elige fuente, se traducen aqui los nombres: asi el dia que
 * haya que reajustar (o cambiar de pantalla) se hace en este fichero y no en la
 * UI.
 *
 * COMO FUNCIONA: cada macro redefine el nombre viejo al tamano nuevo. Las vistas
 * siguen escribiendo lv_font_montserrat_20 y lo que sale es el 26; no hay que
 * tocar ni una linea de las vistas.
 *
 * ESCALA (un escalon cada uno). Los tamanos nuevos NO son inventados: LVGL trae
 * Montserrat de 2 en 2 hasta 48, asi que el escalon siguiente al 22 es el 26 y
 * al 24 el 30, aunque la UI no los usara nunca antes:
 *     viejo   ->  nuevo
 *      14     ->   20
 *      16     ->   22
 *      20     ->   26
 *      22     ->   30
 *      24     ->   34
 *      32     ->   40
 *      40     ->   48
 *      48     ->   56   (AQUI NO HAY: LVGL solo llega a 48; se queda en 48)
 *
 * LIMITE CONOCIDO: LVGL no trae Montserrat 56 (su fuente mas grande es la 48),
 * asi que el titular de 48 no puede subir. Si algun dia hace falta, hay que
 * generar la fuente a mano (el comando esta en fonts/montserrat_bold.h), no
 * basta con activarla en sdkconfig.
 *
 * NEGRITA: la negrita es generada a mano (ver fonts/montserrat_bold.h) y sube
 * el mismo escalon que el resto, con sus propios nombres de papel (FUENTE_*
 * _NEGRITA) para que se lea en la vista QUE es, no cuanto mide.
 *
 * Este fichero lo incluye lvgl.h... no: lo incluye ui/estilos.h, que a su vez
 * incluyen todas las vistas. Si anades una vista nueva, incluye estilos.h.
 */
#pragma once

#include "lvgl.h"
#include "fonts/montserrat_bold.h"

/* ── Escala nueva ─────────────────────────────────────────────────────────── */

/* La 48 es el techo de LVGL: el splash y los titulos grandes se quedan igual. */
#define FUENTE_TITULAR      lv_font_montserrat_48
#define FUENTE_GRANDE       lv_font_montserrat_40
#define FUENTE_MEDIA_GRANDE lv_font_montserrat_32
#define FUENTE_MEDIA        lv_font_montserrat_30
#define FUENTE_NORMAL       lv_font_montserrat_26
#define FUENTE_PEQUENA      lv_font_montserrat_22
#define FUENTE_MUY_PEQUENA  lv_font_montserrat_20

/* Negrita: mismos escalones, pero con nombre de papel (ver montserrat_bold.h).
 * La 20 y la 32 viejas siguen declaradas porque hay quien las pide por su
 * nombre; las nuevas (26/34/40) son las que usa la UI al subir la escala. */
#define FUENTE_NEGRITA      lv_font_montserrat_bold_26
#define FUENTE_NEGRITA_GRANDE lv_font_montserrat_bold_34
#define FUENTE_NEGRITA_TITULAR lv_font_montserrat_bold_40

/* ── Traduccion de los nombres que ya usaba la UI ─────────────────────────── */
/* Cuidado: estas macros SUSTITUYEN a los simbolos de LVGL dentro de la UI, asi
 * que no se pueden usar en un fichero que necesite el tamano de verdad. */
#define lv_font_montserrat_14   FUENTE_MUY_PEQUENA
#define lv_font_montserrat_16   FUENTE_PEQUENA
#define lv_font_montserrat_20   FUENTE_NORMAL
#define lv_font_montserrat_22   FUENTE_MEDIA
#define lv_font_montserrat_24   FUENTE_MEDIA_GRANDE
#define lv_font_montserrat_32   FUENTE_GRANDE
/* La 40 y la 48 ya son el techo: se quedan donde estan. */

/* ── Ajuste automatico del texto de un boton ─────────────────────────────────
 *
 * POR QUE: al subir la escala de fuentes, los rotulos largos de los botones
 * empezaron a salirse -- medido con las metricas reales de las fuentes,
 * "Terminar salida" en letra 40 mide 350 px y su boton tiene 260: se salia
 * 102 px. Ir arreglando boton a boton es pelearse con el sintoma; lo que hace
 * falta es que el rotulo se ajuste al hueco que tiene.
 *
 * COMO: se mide el texto con la fuente de verdad (lv_font_get_glyph_width, que
 * es sumar los anchos de los glifos) y se baja de escalon SOLO lo que haga
 * falta. Se aplica al pintar (LV_EVENT_DRAW_MAIN) y no solo al crear, porque
 * muchos de estos botones son cajas flexibles cuyo ancho definitivo no se sabe
 * hasta que LVGL hace el reparto.
 *
 * El tope inferior (FUENTE_MUY_PEQUENA) es a proposito: por debajo de esa letra
 * el boton ya no se lee de lejos y lo que hay que cambiar es el rotulo, no
 * seguir encogiendo.
 *
 * OJO, ESTO YA SE HIZO MAL UNA VEZ (8-oct-2026, y la placa se quedo colgada):
 * la primera version aplicaba la fuente desde el evento DRAW_MAIN, o sea
 * DENTRO del pintado. Cambiar la fuente invalida el objeto, la invalidacion
 * vuelve a pintarlo y el pintado vuelve a entrar aqui: recursion hasta que
 * salta el watchdog (medido: "task_wdt: Task watchdog got triggered", con la
 * traza repitiendo lv_obj_redraw -> rotulo_ajustar_cb -> lv_obj_set_style_text_font).
 * Por eso ahora se hace con LV_EVENT_SIZE_CHANGED / STYLE_CHANGED y con un
 * cerrojo de reentrada: fuera del pintado y una sola vez por cambio. */
static inline lv_coord_t texto_ancho(const lv_font_t *f, const char *txt)
{
    lv_coord_t w = 0;
    if (!f || !txt) return 0;
    while (*txt) {
        uint32_t letra = (uint32_t)(unsigned char)*txt;
        w += lv_font_get_glyph_width(f, letra, (uint32_t)(unsigned char)txt[1]);
        txt++;
    }
    return w;
}

/* Escalones que se prueban, de mayor a menor. El ultimo es el tope inferior:
 * por debajo, lo que hay que cambiar es el rotulo, no seguir encogiendo. */
/* El ultimo escalon es la RED DE SEGURIDAD: por debajo de el, un rotulo que no
 * quepa significa que el texto es demasiado largo para ese boton y lo que hay
 * que cambiar es el texto. Se llega hasta la 18 (que en esta pantalla se lee,
 * aunque sea pequena) porque hay botones justos con textos legitimos: medido,
 * "Apartarlo (era una prueba)" mide 284 px en letra 20 y su boton tiene ~350. */
#define ROTULO_ESCALONES { &FUENTE_GRANDE, &FUENTE_MEDIA_GRANDE, &FUENTE_MEDIA, \
                           &FUENTE_NORMAL, &FUENTE_PEQUENA, &FUENTE_MUY_PEQUENA, \
                           &lv_font_montserrat_18 }

/* ── El ajuste, y POR QUE NO PUEDE IR DENTRO DE UN EVENTO ────────────────────
 *
 * ESTO YA HA COLGADO LA PLACA DOS VECES (8-oct-2026), asi que el como importa
 * tanto como el que:
 *
 *   1. Primera version: el ajuste corria en LV_EVENT_DRAW_MAIN, o sea DENTRO del
 *      pintado. Cambiar la fuente invalida el objeto -> se repinta -> vuelve a
 *      entrar: recursion hasta agotar la pila (traza del watchdog con
 *      lv_obj_redraw / rotulo_ajustar_cb repitiendose).
 *   2. Segunda version: en LV_EVENT_SIZE_CHANGED / STYLE_CHANGED. Parecia lo
 *      correcto, pero cambiar la fuente PROVOCA un cambio de tamano del objeto,
 *      que manda otro SIZE_CHANGED, que vuelve a ajustar... bucle infinito. El
 *      sintoma es inconfundible: "refrescos LVGL: 0.0/s" con el panel barriendo
 *      a 39 Hz y el heap quieto (no se reserva ni se pierde memoria: solo gira).
 *      Se reprodujo abriendo el formulario de Peaje, que es el que tiene el
 *      campo con letra 48.
 *
 * AHORA: un TEMPORIZADOR de LVGL revisa los botones registrados cinco veces por
 * segundo, FUERA de cualquier evento y fuera del pintado. Ahi no hay
 * reentrada posible: se mira el ancho, se elige el escalon y se aplica, y si
 * algo cambia de tamano el siguiente tic lo vuelve a mirar. El coste es
 * despreciable (medir una cadena son unas decenas de sumas).
 *
 * Se decide SIEMPRE por el ancho del BOTON, que no depende de la fuente del
 * rotulo, asi que no puede oscilar entre dos escalones. */
#define ROTULO_MAX 64

static lv_obj_t *s_rotulos[ROTULO_MAX];
static int       s_rotulos_n = 0;

/* Pone en el rotulo la letra mas grande que quepa en el ancho de su boton. */
static void rotulo_ajustar(lv_obj_t *btn, lv_obj_t *lbl)
{
    if (!btn || !lbl) return;

    const lv_coord_t disponible = lv_obj_get_content_width(btn) - 8;
    if (disponible <= 0) return;

    /* Si el ancho no ha cambiado desde la ultima vez, no hay nada que hacer:
     * esto es lo que evita estar midiendo en cada tic. */
    if ((lv_coord_t)(lv_intptr_t)lv_obj_get_user_data(lbl) == disponible) return;
    lv_obj_set_user_data(lbl, (void *)(lv_intptr_t)disponible);

    const char *txt = lv_label_get_text(lbl);
    const lv_font_t *escalones[] = ROTULO_ESCALONES;
    const lv_font_t *elegida = &lv_font_montserrat_18;   /* red de seguridad */
    for (size_t i = 0; i < sizeof(escalones) / sizeof(escalones[0]); i++) {
        if (texto_ancho(escalones[i], txt) <= disponible) {
            elegida = escalones[i];
            break;
        }
    }
    lv_obj_set_style_text_font(lbl, elegida, 0);
}

static void rotulo_timer_cb(lv_timer_t *t)
{
    (void)t;
    for (int i = 0; i < s_rotulos_n; i++) {
        lv_obj_t *btn = s_rotulos[i];
        if (!btn) continue;
        rotulo_ajustar(btn, (lv_obj_t *)lv_obj_get_user_data(btn));
    }
}

/* Arranca el temporizador que ajusta los rotulos. Lo llama el BSP al montar
 * LVGL (ver esp_bsp.c): asi este fichero no necesita saber nada del arranque y
 * el ajuste existe antes de que se cree ningun boton. */
static inline void rotulos_arrancar(void)
{
    static bool ya = false;
    if (ya) return;
    ya = true;
    lv_timer_create(rotulo_timer_cb, 200, NULL);
}

/* Deja el rotulo de un boton apuntado para que el temporizador lo ajuste. */
static inline void rotulo_autoajustable(lv_obj_t *btn, lv_obj_t *lbl)
{
    if (!btn || !lbl) return;
    lv_obj_set_user_data(btn, lbl);
    if (s_rotulos_n < ROTULO_MAX) s_rotulos[s_rotulos_n++] = btn;
    rotulo_ajustar(btn, lbl);
}

/* ── Medidas de la pantalla (8-oct-2026) ──────────────────────────────────────
 *
 * POR QUE ESTE BLOQUE: la UI se porto del satelite de 3,5", que dibujaba en
 * 480x320 logicos (apaisado). Esta placa son 800x480 REALES (ver display.h y
 * LCD_H_RES/LCD_V_RES), asi que el alto es el mismo pero SOBRAN 320 px de
 * ancho: por eso la UI salia estrecha y arrimada a la izquierda, y por eso los
 * toques "no encontraban nada" que pulsar (los widgets no estaban donde la
 * pantalla grande los pinta, aunque el tactil si mandaba bien las
 * coordenadas).
 *
 * El eje que manda es el ALTO: 480 px son los mismos que antes, asi que el
 * tamano de letra (que ya se subio un escalon para los 181 ppp de esta
 * pantalla) NO se toca. Lo que se reparte de nuevo es el ancho. */
#define UI_ANCHO   800
#define UI_ALTO    480

/* Margen de la pantalla y hueco entre tarjetas (los mismos que ya usaba la
 * rejilla de la pantalla de datos: 4 y 4). */
#define UI_MARGEN    4
#define UI_HUECO     4

/* Ancho maximo del contenido de UNA columna (formularios y menus de Ajustes).
 *
 * Con 800 px de ancho, un campo de texto a pantalla completa mide 800: el
 * rotulo queda en una punta y el valor en la otra, y la vista se lee fatal. La
 * solucion es la de siempre en una pantalla ancha: el contenido se queda en una
 * columna centrada de ancho COMODO y el resto es margen.
 *
 * 560 px es el ancho al que un campo "Precio/litro" + su moneda siguen
 * leyendose de un vistazo. Si algun dia hay una pantalla mas ancha, se sube
 * este numero y ya esta: no hay que tocar ninguna vista. */
#define UI_ANCHO_COLUMNA  560

/* Margen lateral de la columna de contenido, y su variante ESTRECHA.
 *
 * Los formularios con dos campos por fila (o con un desplegable y un selector
 * al lado del importe, como la pernocta) necesitan mas ancho: con el margen
 * ancho, el contenido se queda en 320 px y las tres cosas de la fila de precio
 * salen apretadas y pegadas a la izquierda. Con UI_MARGEN_ANCHO el contenido
 * respira (680 px) sin llegar a los 800, que es lo que se lee mal. */
#define UI_MARGEN_ANCHO  60
