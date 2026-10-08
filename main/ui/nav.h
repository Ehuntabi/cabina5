/* nav.h - Carrusel de 3 pantallas con gesto horizontal, mas la pantalla
 * de Ajustes (fuera del carrusel, se abre/cierra con un icono, no gesto).
 *
 * Centro: info agrupada. Derecha (swipe izda): menu de registros.
 * Izquierda (swipe dcha): inclinacion.
 */
#pragma once

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

#ifdef __cplusplus
}
#endif
