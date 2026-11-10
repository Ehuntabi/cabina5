/* view_registro.h - Menu de registros con iconos (Fase 2, ampliado).
 *
 * Sustituye el tabview inicial de solo repostaje/bombona. Categorias:
 * inicio/fin de viaje, repostaje, peaje, cambio de bombona,
 * mantenimiento. Los "Guardar" envian a la P4: el inicio del viaje va
 * directo (net/p4_api.c) y el resto se encola (net/viaje_cola.c).
 */
#pragma once

#include "lvgl.h"
#include <stddef.h>   /* size_t en view_registro_set_sin_grabar() */

#ifdef __cplusplus
extern "C" {
#endif

void view_registro_create(lv_obj_t *parent);

/* Aviso "N SIN GRABAR EN LA P4 - TOCA AQUI" en las DOS pantallas de menu que se
 * pueden estar viendo mientras se graba: la principal (sin salida en marcha) y
 * el menu de la salida (con el viaje en marcha). Con n == 0 las pastillas se
 * esconden.
 *
 * POR QUE EXISTE (10-nov-2026): los apuntes que no han llegado a la P4 se
 * avisaban SOLO en la pantalla de datos, que es justo la que no se mira cuando
 * se esta anotando: el viaje se graba desde aqui. El fin de semana del
 * 7-8-nov-2026 la cola se lleno (16 huecos) y el usuario no se entero hasta
 * dias despues, cuando ya no habia nada que hacer. Ahora el aviso esta tambien
 * en las pantallas desde las que se graba -- incluida la del viaje en marcha,
 * que es la que se tiene delante mientras se anota -- y al tocarlo lleva a la
 * de datos, que es donde estan el detalle (clave mal, atascado, casi llena) y
 * su salida.
 *
 * La llama view_info_set_pendientes(): es el unico sitio que ya se entera de
 * que cambia el numero de pendientes (viaje_cola_init). Esa llamada viene de
 * la tarea del reparto y no de la de LVGL, asi que dentro se coge el cerrojo
 * del port y se aplaza el repintado con lv_async_call, igual que alli. Se
 * puede llamar antes de que la vista exista: no hace nada y no pierde el
 * numero (lo pinta la creacion de las pantallas). */
void view_registro_set_sin_grabar(size_t n);

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

/* DIAGNOSTICO: vuelca el arbol de objetos de un formulario al log. */
void view_registro_diag_arbol(int idx);
void view_registro_diag_pantalla(int p);

/* Rotulo del carrusel de subpantallas (ver nav.h). DIAGNOSTICO. */
void view_registro_rotulo_subpantallas(const char *txt);

/* DIAGNOSTICO temporal: vuelca al log el arbol de objetos de un formulario con
 * posiciones y tamanos reales (ver view_registro.c). */
bool view_registro_paseo_activo(void);
void view_registro_paseo_salir(void);

#ifdef __cplusplus
}
#endif
