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
