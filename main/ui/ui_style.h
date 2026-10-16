/* ui_style.h — LA PALETA, en un solo sitio (16-oct-2026).
 *
 * Copiada de la P4 (ver su docs/GUIA_ESTILO.md y main/ui/widgets/ui_style.h)
 * para que las tres placas se vean como el mismo producto. Antes de esto, entre
 * las dos cabinas habia 29 colores escritos a mano: cuatro grises distintos para
 * lo mismo, dos verdes, dos azules, y tonos de fondo que nadie sabia de donde
 * salian. Ahora hay 14, cada uno con su papel.
 *
 * REGLAS (las mismas que en la P4):
 *   1. En la UI NO se escribe un color a mano: se usa un token de aqui.
 *   2. TEXT_DIM es SOLO para "no hay dato" (el "--" de un hueco, un guion). Los
 *      rotulos y subtitulos van con TEXT_SOFT o con los colores de estado.
 *   3. Cada color de estado significa UNA cosa: verde = correcto, amarillo =
 *      atencion, naranja = aviso, rojo = error. No se usan para decorar.
 *
 * DOS TOKENS QUE LA P4 NO TIENE, y por que:
 *   - UI_COLOR_BOTON: las pastillas de boton de las cabinas son un gris propio
 *     (0x333333) que sobre el fondo casi negro se lee bien y no es ni tarjeta ni
 *     borde. Se queda, pero con nombre.
 *   - los *_HEX: para los sitios donde el valor se guarda como numero (las
 *     tablas de color por fila, por ejemplo) y el lv_color_hex() se pone luego.
 */
#pragma once

#include "lvgl.h"

/* ── Superficies ─────────────────────────────────────────────────────────── */
#define UI_COLOR_BG_HEX          0x0A0D13   /* fondo de pagina */
#define UI_COLOR_CARD_HEX        0x1B2230   /* superficie de tarjeta */
#define UI_COLOR_CARD_BORDER_HEX 0x39424F   /* borde de tarjeta */
#define UI_COLOR_BOTON_HEX       0x333333   /* pastilla de boton (ver arriba) */

/* ── Texto ───────────────────────────────────────────────────────────────── */
#define UI_COLOR_TEXT_HEX        0xFFFFFF   /* dato principal */
#define UI_COLOR_TEXT_SOFT_HEX   0xE4E9F0   /* texto descriptivo */
#define UI_COLOR_TEXT_DIM_HEX    0x8A93A6   /* SOLO "no hay dato" o rotulo tenue */

/* ── Estados y acentos ───────────────────────────────────────────────────── */
#define UI_COLOR_CYAN_HEX        0x4FC3F7   /* informacion / solar */
#define UI_COLOR_GREEN_HEX       0x00C851   /* correcto */
#define UI_COLOR_YELLOW_HEX      0xFFD54F   /* atencion */
#define UI_COLOR_ORANGE_HEX      0xFF9800   /* aviso */
#define UI_COLOR_RED_HEX         0xFF4444   /* error */
#define UI_COLOR_RED_DARK_HEX    0xCC3333   /* error atenuado / fondo de aviso */
#define UI_COLOR_VIOLET_HEX      0xB388FF   /* zona habitable (camper) */
#define UI_COLOR_ICE_HEX         0x64B5F6   /* frio (congelador) */


/* ── Texto sobre fondo CLARO ───────────────────────────────────────────────
 *
 * La paleta de arriba es para una UI oscura (texto claro sobre fondo oscuro),
 * pero hay sitios con el fondo claro: el campo blanco de los formularios y los
 * botones de color (el OK verde, el aviso amarillo). Ahi el texto tiene que ir
 * OSCURO. Es un papel distinto, no "el color de fondo": por eso tiene su token
 * (16-oct-2026: al pasar la paleta a tokens, seis sitios de estos se habian
 * quedado apuntando al fondo, que se ve casi igual pero no dice lo mismo). */
#define UI_COLOR_SOBRE_CLARO_HEX 0x000000
#define UI_COLOR_SOBRE_CLARO     lv_color_hex(UI_COLOR_SOBRE_CLARO_HEX)

/* ── Y ya listos para usar ───────────────────────────────────────────────── */
#define UI_COLOR_BG           lv_color_hex(UI_COLOR_BG_HEX)
#define UI_COLOR_CARD         lv_color_hex(UI_COLOR_CARD_HEX)
#define UI_COLOR_CARD_BORDER  lv_color_hex(UI_COLOR_CARD_BORDER_HEX)
#define UI_COLOR_BOTON        lv_color_hex(UI_COLOR_BOTON_HEX)
#define UI_COLOR_TEXT         lv_color_hex(UI_COLOR_TEXT_HEX)
#define UI_COLOR_TEXT_SOFT    lv_color_hex(UI_COLOR_TEXT_SOFT_HEX)
#define UI_COLOR_TEXT_DIM     lv_color_hex(UI_COLOR_TEXT_DIM_HEX)
#define UI_COLOR_CYAN         lv_color_hex(UI_COLOR_CYAN_HEX)
#define UI_COLOR_GREEN        lv_color_hex(UI_COLOR_GREEN_HEX)
#define UI_COLOR_YELLOW       lv_color_hex(UI_COLOR_YELLOW_HEX)
#define UI_COLOR_ORANGE       lv_color_hex(UI_COLOR_ORANGE_HEX)
#define UI_COLOR_RED          lv_color_hex(UI_COLOR_RED_HEX)
#define UI_COLOR_RED_DARK     lv_color_hex(UI_COLOR_RED_DARK_HEX)
#define UI_COLOR_VIOLET       lv_color_hex(UI_COLOR_VIOLET_HEX)
#define UI_COLOR_ICE          lv_color_hex(UI_COLOR_ICE_HEX)
