/* splash_logo_5.h - la imagen del splash de la pantalla de 5" (la autocaravana).
 *
 * El .c que la define es un volcado de pixeles (RGB565) generado con un script,
 * igual que el splash_logo_3_5.c del satelite de 3,5": no se toca a mano. La
 * receta para rehacerlo (recuperar la imagen del proyecto viejo, recortar el
 * marco negro, reescalar y volcar) esta en docs/RELEVO_port_5pulgadas.md.
 *
 * POR QUE HAY UN .h PARA UNA SOLA DECLARACION: porque el .c NO se incluye
 * nunca desde otro .c (son 3 MB de tabla), asi que el resto del proyecto
 * necesita la declaracion por separado.
 */
#pragma once

#include "lvgl.h"

LV_IMAGE_DECLARE(splash_logo_5);
