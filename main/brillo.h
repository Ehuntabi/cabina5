/* brillo.h - Brillo de la pantalla en dos niveles, con memoria.
 *
 * Se alterna con un doble toque en la pantalla de datos (ver view_info.c) y se
 * recuerda al reiniciar (config_storage, namespace "display"). Hasta la v1.10
 * el brillo estaba clavado al 5% en main.c, heredado del ejemplo del
 * fabricante, y no habia forma de cambiarlo.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Los dos unicos valores validos. Si se cambian, el nivel guardado de antes
 * deja de ser valido y brillo_init() cae al ALTO: es a proposito, mas vale
 * pasarse de luz que quedarse con una pantalla que no se ve.
 *
 * 60 Y NO 30 (8-oct-2026): con el 30 la placa arranco con la pantalla
 * aparentemente NEGRA -- el usuario lo reporto como "la pantalla esta negra" y
 * costo un rato descubrir que el framebuffer tenia la UI perfectamente pintada
 * (medido: 0% de muestras negras, brillo medio 121) y lo que pasaba es que, con
 * el tema oscuro de esta interfaz, un 30% de retroiluminacion no se ve. El
 * nivel bajo es para no deslumbrar de noche, no para apagar la pantalla. */
#define BRILLO_BAJO   60
#define BRILLO_ALTO  100

/* Aplica el nivel guardado (ALTO la primera vez). Necesita NVS ya arrancado y
 * el display ya iniciado, porque el PWM de la retroiluminacion se configura
 * dentro de bsp_display_start_with_config(). */
void brillo_init(void);

/* Cambia al otro nivel, lo aplica, lo guarda y devuelve el nivel nuevo.
 * Devuelve el valor para que quien lo llama pueda ajustar el contraste de la
 * pantalla al mismo tiempo (ver view_info.c): el 30% se queda como estaba y el
 * 100% sube el contraste, que es lo que hace falta para verlo al sol. */
uint8_t brillo_alternar(void);

/* Nivel actual (BRILLO_BAJO o BRILLO_ALTO), para pintar acorde al arrancar. */
uint8_t brillo_nivel(void);

#ifdef __cplusplus
}
#endif
