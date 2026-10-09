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

/* Aplica el nivel guardado (el de en medio la primera vez). Necesita NVS ya
 * arrancado y el display ya iniciado, porque el PWM de la retroiluminacion se
 * configura dentro de bsp_display_start_with_config(). */
void brillo_init(void);

/* Sube al siguiente paso (y del maximo vuelve al minimo), lo aplica con una
 * rampa suave, lo guarda y devuelve el nivel nuevo. */
uint8_t brillo_alternar(void);

/* Nivel actual (porcentaje), para pintar acorde al arrancar. */
uint8_t brillo_nivel(void);

#ifdef __cplusplus
}
#endif
