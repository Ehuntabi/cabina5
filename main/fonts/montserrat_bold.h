/* montserrat_bold.h - Montserrat Bold, generada a partir de la fuente
 * variable oficial de Google Fonts (ofl/montserrat/Montserrat[wght].ttf),
 * instanciada a peso 700 con fonttools y convertida con lv_font_conv (ya
 * instalado en /usr/local/bin). Solo ASCII (0x20-0x7E): el resto de fuentes
 * de este proyecto tampoco llevan acentos (ver comentario en
 * ui/view_registro.c sobre el destino del viaje).
 *
 * COMO SE REGENERA (los cuatro comandos, de arriba abajo):
 *
 *   mkdir -p .fuentes && cd .fuentes
 *   curl -sL -o Montserrat-VF.ttf \
 *     https://raw.githubusercontent.com/google/fonts/main/ofl/montserrat/Montserrat%5Bwght%5D.ttf
 *   fonttools varLib.instancer -o Montserrat-Bold.ttf Montserrat-VF.ttf wght=700
 *   for SZ in 26 34 40; do \
 *     lv_font_conv --font Montserrat-Bold.ttf --range 0x20-0x7E --bpp 4 \
 *       --format lvgl --lv-include lvgl.h --no-compress \
 *       --size $SZ --lv-font-name lv_font_montserrat_bold_$SZ \
 *       -o ../main/fonts/lv_font_montserrat_bold_$SZ.c; done
 *
 * (La carpeta .fuentes/ con los .ttf es de trabajo y no se versiona.)
 *
 * --no-compress a proposito, igual que iconos.h: con la compresion RLE por
 * defecto el texto salia invisible (sin error ni aviso, LVGL simplemente no
 * pintaba nada) -- descubierto el 5-sep-2026 al probarlo en la pantalla.
 *
 * QUE TAMANOS HAY Y POR QUE: la negrita solo se usa en contadas etiquetas
 * (los sitios de pernocta y su cartel de "DE PAGO"), y hasta el 8-oct-2026
 * solo existian la 20 y la 32, que eran los dos tamanos que pedia la UI
 * heredada del satelite de 3,5". Al subir toda la escala un escalon en esta
 * pantalla (ver ui/estilos.h) hicieron falta los tres siguientes, y se
 * generaron: 26, 34 y 40. No se generan mas porque no se usan: anadir uno son
 * 90-180 KB de flash y una linea aqui.
 */
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(lv_font_montserrat_bold_20);
LV_FONT_DECLARE(lv_font_montserrat_bold_26);
LV_FONT_DECLARE(lv_font_montserrat_bold_32);
LV_FONT_DECLARE(lv_font_montserrat_bold_34);
LV_FONT_DECLARE(lv_font_montserrat_bold_40);
