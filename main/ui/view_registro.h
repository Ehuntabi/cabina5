/* view_registro.h - Menu de registros con iconos (Fase 2, ampliado).
 *
 * Sustituye el tabview inicial de solo repostaje/bombona. Categorias:
 * inicio/fin de viaje, repostaje, peaje, cambio de bombona,
 * mantenimiento. Los "Guardar" envian a la P4: el inicio del viaje va
 * directo (net/p4_api.c) y el resto se encola (net/viaje_cola.c).
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void view_registro_create(lv_obj_t *parent);

/* Devuelve la vista al menu de iconos, cierre el formulario que estuviera
 * abierto y el editor de campo si lo estaba.
 *
 * Lo llama nav.c al salir de esta pagina del carrusel: si no, al volver te
 * encontrabas el formulario tal y como lo dejaste, y no el menu. Ojo: lo
 * tecleado a medias se pierde, que es lo que se quiere -- te habias ido. */
void view_registro_reset(void);

/* Abre directamente la lista de lo que queda sin cerrar. La usa la pastilla de
 * la pantalla de datos: el aviso se ve mientras conduces, y desde el se llega
 * de un toque a lo que hay que hacer. No hace nada si no hay nada abierto. */
void view_registro_abrir_sin_cerrar(void);

/* Declara la llegada de la salida puntual en curso: guarda la hora y la
 * marca como ya declarada. La usa el aviso "Toca al llegar a <categoria>"
 * de la pantalla principal (ver view_info_set_puntual_pendiente en
 * view_info.h) al tocarlo. No hace nada si no hay una salida puntual
 * esperando declararse. */
void view_registro_puntual_declarar_llegada(void);

/* Solo para el modo captura de pantallas (ver capture_carousel.h): recorrer
 * todos los menus y formularios de este carrusel sin pasar por la
 * navegacion normal (que exige una salida de verdad abierta). No las use
 * nada mas. */
int view_registro_num_pantallas(void);
int view_registro_num_formularios(void);
void view_registro_mostrar_pantalla(int p);
void view_registro_mostrar_formulario(int idx);
const char *view_registro_nombre_pantalla(int p);
const char *view_registro_nombre_formulario(int idx);

/* ── Modo PASEO (8-oct-2026) ────────────────────────────────────────────────
 *
 * Para poder RECORRER todas las pantallas del cuaderno sin tener una salida
 * abierta de verdad. Los menus de registro (y sus formularios) solo se abren
 * desde dentro de un viaje o una salida puntual, asi que sin esto no hay forma
 * de revisarlos: ni para mirarlos, ni para comprobar que se ven bien.
 *
 * Con el paseo en marcha:
 *   - no se vacian los formularios al cambiar de pantalla, asi que se puede ir
 *     y volver sin perder lo tecleado;
 *   - nav.c NO devuelve la vista al menu de iconos al salir del carrusel (el
 *     estado se queda donde lo dejaste, que es lo que quiere quien esta
 *     paseando). Se sale con el boton "Salir del paseo", en la propia pantalla.
 *
 * Nada de esto toca los datos: es solo que pantalla se ve. */
void view_registro_paseo_mostrar(void);

/* DIAGNOSTICO temporal: vuelca al log el arbol de objetos de un formulario con
 * posiciones y tamanos reales (ver view_registro.c). */
bool view_registro_paseo_activo(void);
void view_registro_paseo_salir(void);

#ifdef __cplusplus
}
#endif
