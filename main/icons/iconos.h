/* iconos.h - los iconos del cuaderno, en su propia fuente.
 *
 * POR QUE EXISTE: los simbolos que trae LVGL (LV_SYMBOL_*) son un puñado de
 * FontAwesome elegidos para interfaces de aparatos, y no hay ninguno que
 * signifique "bombona", "peaje" o "gasoil". Se estaban usando por parecido --
 * el rayo de CHARGE para el repostaje, las flechas de REFRESH para la bombona --
 * y un icono que significa otra cosa confunde mas que ayuda.
 *
 * De donde salen (los dos ficheros ya estaban en el equipo, no se descarga
 * nada al compilar):
 *   - FontAwesome 4.7  /usr/share/fonts/truetype/font-awesome/fontawesome-webfont.ttf
 *   - Noto Sans Symbols /usr/share/fonts/truetype/noto/NotoSansSymbols-Regular.ttf
 *     SOLO por el surtidor (U+26FD): FontAwesome 4 no tiene ninguno.
 *
 * Como se regenera (lv_font_conv, ya instalado en /usr/local/bin):
 *
 *   lv_font_conv \
 *     --font /usr/share/fonts/truetype/font-awesome/fontawesome-webfont.ttf \
 *     --range 0xf041,0xf043,0xf0ad,0xf145,0xf06d,0xf236,0xf07a,0xf030,\
 *             0xf0f4,0xf0f5,0xf186,0xf0ea,0xf018,0xf0ec,0xf067,0xf0c7 \
 *     --font /usr/share/fonts/truetype/noto/NotoSansSymbols-Regular.ttf \
 *     --range 0x26fd \
 *     --size 32 --bpp 4 --format lvgl --lv-include lvgl.h --no-compress \
 *     -o main/icons/iconos_32.c
 *
 * Al añadir un icono hay que meter su codigo en el --range Y aqui: si falta en
 * la fuente sale un hueco vacio, que es peor que no poner icono.
 */
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(iconos_32);

#define ICO_PARADA     "\xEF\x81\x81"   /* U+F041  chincheta de mapa */
#define ICO_AGUAS      "\xEF\x81\x83"   /* U+F043  gota */
#define ICO_REPOSTAJE  "\xE2\x9B\xBD"   /* U+26FD  surtidor de gasolina (Noto Symbols) */
#define ICO_PEAJE      "\xEF\x85\x85"   /* U+F145  ticket */
#define ICO_BOMBONA    "\xEF\x81\xAD"   /* U+F06D  llama */
#define ICO_AVERIA     "\xEF\x82\xAD"   /* U+F0AD  llave inglesa */
#define ICO_ITV        "\xEF\x83\xAA"   /* U+F0EA  portapapeles */
#define ICO_VIAJE      "\xEF\x80\x98"   /* U+F018  carretera */
#define ICO_PUNTUAL    "\xEF\x83\xAC"   /* U+F0EC  ida y vuelta */
#define ICO_PERNOCTA   "\xEF\x88\xB6"   /* U+F236  cama */
#define ICO_VISITA     "\xEF\x80\xB0"   /* U+F030  camara */
#define ICO_DESCANSO   "\xEF\x83\xB4"   /* U+F0F4  taza */
#define ICO_COMER      "\xEF\x83\xB5"   /* U+F0F5  cubiertos */
#define ICO_CENAR      "\xEF\x86\x86"   /* U+F186  luna */
#define ICO_COMPRAS    "\xEF\x81\xBA"   /* U+F07A  carro */
#define ICO_MAS        "\xEF\x81\xA7"   /* U+F067  mas (+) */
#define ICO_GUARDAR    "\xEF\x83\x87"   /* U+F0C7  disquete */
