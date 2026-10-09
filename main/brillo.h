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

/* DOS NIVELES: 100 % y 50 %.
 *
 * PEDIDO LITERAL DEL USUARIO (9-oct-2026), en dos pasos y con la placa delante:
 * primero "el cambio de iluminacion que sea o 100% o 30%", y al probarlo, "30%
 * es casi apagado, prueba 50%". El 50 % es el nivel de noche.
 *
 * Historia corta, para no repetirla: el brillo original tenia dos niveles (60 y
 * 100) y el salto era SECO, que es lo que el usuario describio como "el cambio de
 * iluminacion no funciona bien". Se probaron cinco pasos (20/40/60/80/100) y el
 * resultado fue una pantalla negra: el 60 % tampoco se ve con el tema oscuro de
 * esta interfaz (es el "la pantalla esta negra" del 8-oct al 30 %, que esta en el
 * README). Leccion medida en esta placa: el rango util de la retroiluminacion es
 * ESTRECHO y esta pegado al maximo -- el 30 % es casi apagado y el 60 % tampoco
 * vale. Con dos niveles no hay terminos medios que elegir mal.
 *
 * Lo que se queda de todo aquello es LA RAMPA (ver brillo.c): eso si estaba roto
 * de verdad, porque un salto de 100 a 50 de golpe se ve como un parpadeo. */
#define BRILLO_NIVELES  { 50, 100 }
#define BRILLO_NIVELES_N 2
#define BRILLO_ALTO  100   /* el maximo (lo usa view_info para el contraste) */
#define BRILLO_BAJO   50   /* el minimo, el "modo noche" */

/* Enciende la retroiluminacion AL MAXIMO, siempre, mire lo que mire la NVS.
 * Necesita NVS ya arrancado y el display ya iniciado, porque el PWM de la
 * retroiluminacion se configura dentro de bsp_display_start_with_config().
 *
 * NO aplica el valor guardado a proposito: el 50 % de noche es correcto, pero
 * arrancar a oscuras porque el aparato se apago asi se ve como "la pantalla esta
 * negra" y no hay forma de salir sin saber donde tocar. Arrancando al maximo, un
 * desliz bajando el brillo siempre se arregla desenchufando. */
void brillo_init(void);

/* Pasa al OTRO nivel (100 -> 50 -> 100), lo aplica con una rampa suave de
 * 8 tramos, lo guarda y devuelve el nivel nuevo. */
uint8_t brillo_alternar(void);

/* Nivel actual (porcentaje), para pintar acorde al arrancar. */
uint8_t brillo_nivel(void);

#ifdef __cplusplus
}
#endif
