/* nav.h - Carrusel de 3 pantallas con gesto horizontal, mas la pantalla
 * de Ajustes (fuera del carrusel, se abre/cierra con un icono, no gesto).
 *
 * Centro: info agrupada. Derecha (swipe izda): menu de registros.
 * Izquierda (swipe dcha): inclinacion.
 */
#pragma once

#include <stdbool.h>   /* bool en nav_subpantallas_activo() */

#ifdef __cplusplus
extern "C" {
#endif

/* Crea las 3 pantallas del carrusel + la de Ajustes, las llena y carga
 * la de info como pantalla activa inicial. */
void nav_init(void);

/* Abre la pantalla de Ajustes (Wi-Fi) por encima del carrusel. */
void nav_open_ajustes(void);

/* Lleva el carrusel a inclinacion o a info directamente (sin gesto). Ademas
 * del modo captura de pantallas (ver capture_carousel.h), nav_ir_a_info()
 * la usa view_registro.c al abrir/salir de una salida puntual: es la
 * pagina con datos (bateria, aguas, temperaturas...), y quedarse en
 * Registro no aporta nada mientras se conduce hacia el sitio. */
void nav_ir_a_inclinacion(void);
void nav_ir_a_info(void);

/* Lleva el carrusel a la pagina de registros, sin tocar en que menu esta.
 * La necesita el cierre de un apunte: la pregunta del arranque salta sobre la
 * pagina que estes mirando, pero el formulario vive en la de registros. */
void nav_ir_a_registros(void);

/* Lleva el carrusel a la pagina de registros y abre la lista de lo que queda
 * sin cerrar. Lo llama la pastilla de la pantalla de datos. */
void nav_ir_a_sin_cerrar(void);

/* Vuelve del carrusel a la pantalla de info. */
void nav_close_ajustes(void);

/* Abre el carrusel de registro en MODO PASEO: el indice con todas las
 * pantallas, sin necesidad de tener una salida abierta (ver view_registro.h).
 * Lo llama el boton "Ver todas las pantallas" de Ajustes.
 *
 * Pasa por aqui y no llama directo a view_registro_paseo_mostrar() para que el
 * carrusel sepa en que pantalla esta: si no, el estado de nav y el de la vista
 * se pelean y el primer deslizamiento deja la pantalla que no toca. */
void nav_ir_a_paseo(void);

#ifdef __cplusplus
}
#endif

/* ── Carrusel SOLO por las subpantallas (9-oct-2026) ────────────────────────
 *
 * Para revisar el cuaderno de una en una: se enciende con
 * CAPTURE_CAROUSEL_ENABLE=2 (ver capture_carousel.h) y entonces la pantalla de
 * registro arranca en REPOSTAJE, y deslizando a izquierda/derecha se pasa por
 * los nueve formularios en orden. Un rotulo dice cual es y en que numero va.
 *
 * POR QUE: revisar la disposicion de nueve formularios entrando y saliendo del
 * menu cada vez es una lata, y ademas asi se pueden fotografiar en orden. */
void nav_subpantallas_arrancar(void);
void nav_subpantallas_paso(int delta);
bool nav_subpantallas_activo(void);
int  nav_subpantallas_indice(void);
int  nav_subpantallas_total(void);

/* Nombre del formulario que se esta viendo, para el rotulo. NULL si el modo no
 * esta activo. */
const char *nav_subpantallas_rotulo(void);

/* Categoria (indice de view_registro) de la subpantalla que se ve, o -1. */
int  nav_subpantallas_categoria(void);
