/* montserrat_bold.h - Montserrat Bold, generada a partir de la fuente
 * variable oficial de Google Fonts (ofl/montserrat/Montserrat[wght].ttf),
 * instanciada a peso 700 con fonttools y convertida con lv_font_conv (ya
 * instalado en /usr/local/bin). Solo ASCII (0x20-0x7E): el resto de fuentes
 * de este proyecto tampoco llevan acentos (ver comentario en
 * ui/view_registro.c sobre el destino del viaje).
 *
 * Como se regenero:
 *
 *   curl -sL -o Montserrat-VF.ttf \
 *     https://raw.githubusercontent.com/google/fonts/main/ofl/montserrat/Montserrat%5Bwght%5D.ttf
 *   fonttools varLib.instancer -o Montserrat-Bold.ttf Montserrat-VF.ttf wght=700
  *   lv_font_conv --font Montserrat-Bold.ttf --range 0x20-0x7E --bpp 4 \
 *     --format lvgl --lv-include lvgl.h --no-compress \
 *     --size 32 --lv-font-name lv_font_montserrat_bold_32 \
 *     -o main/fonts/lv_font_montserrat_bold_32.c
 *
 * --no-compress a proposito, igual que iconos.h: con la compresion RLE por
 * defecto el texto salia invisible (sin error ni aviso, LVGL simplemente no
 * pintaba nada) -- descubierto el 5-sep-2026 al probarlo en la pantalla.
 *   (igual para el tamano 20, --lv-font-name lv_font_montserrat_bold_20)
 *
 * Anadir un tamano nuevo: repetir el ultimo comando con --size distinto y
 * declararlo aqui abajo.
 */
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(lv_font_montserrat_bold_20);
LV_FONT_DECLARE(lv_font_montserrat_bold_32);
