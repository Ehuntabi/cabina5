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

/* NIVELES DE BRILLO, en pasos (9-oct-2026).
 *
 * ANTES ERAN DOS (60 y 100) y el usuario lo describio como "el cambio de
 * iluminacion no funciona bien": al pulsar saltaba de golpe de uno a otro, sin
 * terminos medios, y de noche el 60 seguia siendo mucho. Ahora son cinco pasos,
 * de 20 a 100: se puede dejar al minimo para no deslumbrar y subir de uno en uno
 * hasta el maximo para el sol.
 *
 * El valor guardado es el PORCENTAJE, asi que la lista se puede cambiar sin
 * invalidar lo que ya hay en NVS (si el guardado no esta en la lista, se coge el
 * mas parecido). */
#define BRILLO_NIVELES  { 20, 40, 60, 80, 100 }
#define BRILLO_NIVELES_N 5
#define BRILLO_ALTO  100   /* el maximo (lo usa view_info para el contraste) */
#define BRILLO_BAJO   20   /* el minimo */

/* Enciende la retroiluminacion AL MAXIMO, siempre, mire lo que mire la NVS.
 * Necesita NVS ya arrancado y el display ya iniciado, porque el PWM de la
 * retroiluminacion se configura dentro de bsp_display_start_with_config().
 *
 * NO aplica el valor guardado a proposito: con el tema oscuro de esta interfaz,
 * arrancar en un paso bajo se ve como "la pantalla esta negra" y, como el valor
 * se recuerda, el siguiente encendido volvia a salir apagado. Arrancando al
 * maximo, un desliz bajando el brillo siempre se arregla desenchufando. */
void brillo_init(void);

/* BAJA al siguiente paso (100 -> 80 -> 60 -> 40 -> 20 -> 100), lo aplica con una
 * rampa suave, lo guarda y devuelve el nivel nuevo. Va hacia abajo porque ahora
 * se arranca arriba: lo que se quiere al tocar es quitar deslumbramiento. */
uint8_t brillo_alternar(void);

/* Nivel actual (porcentaje), para pintar acorde al arrancar. */
uint8_t brillo_nivel(void);

#ifdef __cplusplus
}
#endif
