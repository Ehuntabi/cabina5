/* Fuentes Inter (Regular y Bold) con rango ASCII + acentos espanoles + n + ¿¡° y
 * los 33 iconos FontAwesome que ya usa la P4. Generadas con lv_font_conv y la
 * MISMA receta que la P4: el comando exacto va escrito en la cabecera de cada
 * .c generado.
 *
 * POR QUE (16-oct-2026): las cabinas usaban las Montserrat del SDK, que NO
 * traen acentos ni la n con virgulilla -- de ahi que todos los rotulos fueran
 * en ASCII ("Configuración") y que el destino del viaje solo admitiera letras
 * sin tilde. Con estas, la UI puede decir "Configuración" bien escrita.
 *
 * LOS NOMBRES VIEJOS SE MANTIENEN COMO ALIAS, igual que en la P4 (ver su
 * docs/GUIA_ESTILO.md): asi no hay que tocar las ~100 llamadas de la UI y el
 * cambio de familia vive en este fichero. Los alias son NOMBRES DE SIMBOLO,
 * nunca "(&fuente)": en la UI se escriben como &lv_font_montserrat_20.
 *
 * TAMANOS: los mismos px que se renderizaban antes (medidos sobre el codigo):
 * (aqui van los que usa ESTA placa). Lo que cambia es la familia, no el tamano.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t lv_font_inter_18_es;
extern const lv_font_t lv_font_inter_26_es;
extern const lv_font_t lv_font_inter_28_es;
extern const lv_font_t lv_font_inter_30_es;
extern const lv_font_t lv_font_inter_34_es;
extern const lv_font_t lv_font_inter_40_es;
extern const lv_font_t lv_font_inter_48_es;
extern const lv_font_t lv_font_inter_bold_26_es;
extern const lv_font_t lv_font_inter_bold_34_es;
extern const lv_font_t lv_font_inter_bold_40_es;

/* ── Alias de los nombres que ya usaba la UI ───────────────────────────────── */
#define lv_font_montserrat_18 lv_font_inter_18_es
#define lv_font_montserrat_26 lv_font_inter_26_es
#define lv_font_montserrat_28 lv_font_inter_28_es
#define lv_font_montserrat_30 lv_font_inter_30_es
#define lv_font_montserrat_34 lv_font_inter_34_es
#define lv_font_montserrat_40 lv_font_inter_40_es
#define lv_font_montserrat_48 lv_font_inter_48_es
#define lv_font_montserrat_bold_26 lv_font_inter_bold_26_es
#define lv_font_montserrat_bold_34 lv_font_inter_bold_34_es
#define lv_font_montserrat_bold_40 lv_font_inter_bold_40_es

#ifdef __cplusplus
}
#endif
