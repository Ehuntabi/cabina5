/* Un solo sitio para el tamano de las fuentes de la UI.
 *
 * POR QUE EXISTE: la UI se porto del satelite de 3,5" (320x480 vertical) a la
 * placa de 5" (800x480 horizontal), que tiene 181 ppp en vez de 160. El 8-oct-2026
 * se subio TODA la escala un escalon de una vez, y en vez de tocar los 105 sitios
 * donde se elige fuente se traducen aqui los nombres: asi el dia que haya que
 * reajustar (o cambiar de pantalla) se hace en este fichero y no en la UI.
 *
 * COMO FUNCIONA: cada macro redefine el nombre viejo al tamano nuevo. Las vistas
 * siguen escribiendo lv_font_montserrat_20 y lo que sale es el 24; no hay que
 * tocar ni una linea de ui/*.c.
 *
 * ESCALA (un escalon cada uno):
 *     viejo   ->  nuevo
 *      14     ->   18
 *      16     ->   20
 *      20     ->   24
 *      22     ->   28
 *      24     ->   32
 *      32     ->   40
 *      40     ->   48
 *      48     ->   56   (AQUI NO HAY: LVGL solo trae hasta 48; se queda en 48)
 *
 * LIMITE CONOCIDO: las fuentes en NEGRITA son generadas a mano con lv_font_conv
 * y solo existen la 20 y la 32 (ver fonts/montserrat_bold.h). La negrita NO se
 * escala: se queda como esta. Si algun dia hace falta negrita mas grande, hay
 * que generarla (el comando esta en ese fichero) y anadirla aqui.
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
#define FUENTE_MEDIA        lv_font_montserrat_28
#define FUENTE_NORMAL       lv_font_montserrat_24
#define FUENTE_PEQUENA      lv_font_montserrat_20
#define FUENTE_MUY_PEQUENA  lv_font_montserrat_18

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
