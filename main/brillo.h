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

/* DOS NIVELES: 100 % y 85 %.
 *
 * ── LO QUE SE MIDIO (9-oct-2026), con la prueba de capture_carousel.c ───────
 *
 * Se hizo un barrido de duty con la pantalla en negro y el valor en grande, y
 * esto es lo que contesto el usuario mirando la placa:
 *
 *     "4000 por debajo de 80 ya muy oscuro"
 *
 * O sea: de 100 a 80 la luz baja de verdad (se ve el escalon), y por debajo de
 * 80 la pantalla se apaga a efectos practicos. El 50 que se probo antes se veia
 * negro; el 30, negro del todo. El rango UTIL de la retroiluminacion de esta
 * placa es 80-100 y no hay mas.
 *
 * HISTORIA, para no volver a dar las mismas vueltas: el usuario describio el
 * brillo original (dos niveles, 60 y 100, cambio de golpe) como "el cambio de
 * iluminacion no funciona bien". Se probaron cinco pasos (20/40/60/80/100) y
 * salio una pantalla negra. Luego, a peticion suya, 100/30, 100/50 y 100/90 --
 * todos a ciegas. El barrido es lo que zanjo la pregunta: no era el codigo, es
 * que esta retroiluminacion casi no tiene terminos medios.
 *
 * EL USUARIO PIDIO DESPUES EL 85: "pon 85%". Es un paso mas fino que el 80
 * medido, dentro del rango util (80-100), asi que los dos niveles son 100 y 85.
 *
 * QUEDA UNA PREGUNTA ABIERTA: en la misma prueba se bajo la frecuencia del PWM
 * (4000 -> 1000 -> 200 Hz) por si el driver no arrancara a 4 kHz. Si a 200 Hz el
 * 60 % se viera, el rango se podria ensanchar bajando LCD_BL_LEDC_FREQ_HZ. Esta
 * sin confirmar. */
#define BRILLO_NIVELES  { 85, 100 }
#define BRILLO_NIVELES_N 2
#define BRILLO_ALTO  100   /* el maximo (lo usa view_info para el contraste) */
#define BRILLO_BAJO   85   /* el minimo: pedido por el usuario, ver brillo.h */

/* Enciende la retroiluminacion AL MAXIMO, siempre, mire lo que mire la NVS.
 * Necesita NVS ya arrancado y el display ya iniciado, porque el PWM de la
 * retroiluminacion se configura dentro de bsp_display_start_with_config().
 *
 * NO aplica el valor guardado a proposito: el 85 % de noche es correcto, pero
 * arrancar a oscuras porque el aparato se apago asi se ve como "la pantalla esta
 * negra" y no hay forma de salir sin saber donde tocar. Arrancando al maximo, un
 * desliz bajando el brillo siempre se arregla desenchufando. */
void brillo_init(void);

/* Pasa al OTRO nivel (100 -> 85 -> 100), lo aplica con una rampa suave de
 * 8 tramos, lo guarda y devuelve el nivel nuevo. */
uint8_t brillo_alternar(void);

/* Nivel actual (porcentaje), para pintar acorde al arrancar. */
uint8_t brillo_nivel(void);

#ifdef __cplusplus
}
#endif
