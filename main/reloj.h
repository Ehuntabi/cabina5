/* reloj.h — la hora que da la P4, con correccion del tiempo transcurrido.
 * Ver la cabecera del .c: sin esto, un apunte hecho con la P4 apagada saldria
 * fechado cuando por fin se entrega, no cuando ocurrio. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lo llama rx_task con cada mensaje de la P4. Ignora el 0. */
void reloj_set_desde_p4(uint32_t epoch_local, int utc_off_quince);

/* CUANTOS SEGUNDOS HAY QUE SUMARLE A UN INICIO ANTIGUO para que la duracion
 * salga bien cuando ha cambiado el horario (14-oct-2026).
 *
 * El 'epoch_local' que manda la P4 es un epoch desplazado a hora local, asi que
 * en el cambio de horario DA UN SALTO ATRAS de 3600 s: una pernocta que cruza el
 * cambio salia una hora corta (y el cobro por 24 h, con ella). La P4 manda
 * tambien su desfase horario (utc_off_quince), y con eso se detecta el cambio.
 *
 * Devuelve 0 si no ha cambiado nada, o el salto (p. ej. -3600 al entrar en
 * horario de invierno) que hay que sumar al INICIO de un tramo abierto antes del
 * cambio. La hora de pared que se enseña NO se toca: se corrige el calculo, no
 * el dato. */

/* El delta que hay que sumarle al INICIO de un tramo, o 0 si el tramo empezo en
 * o despues del ultimo cambio de horario (ver reloj.c). */
int reloj_delta_horario_desde(uint32_t epoch_ini);

/* EL DESFASE CON EL QUE SE ABRIO CADA TRAMO (B19, auditoria del 27-oct-2026).
 *
 * reloj_delta_horario_desde() no puede desempatar en el cambio de OTONO (la hora
 * local salta atras y las dos escalas se solapan una hora), asi que la correccion
 * exacta necesita saber con que desfase se abrio el tramo. Se apunta al abrirlo
 * (ver salida.c) y se corrige con la diferencia contra el desfase de ahora. */
#define RELOJ_OFF_DESCONOCIDO  INT8_MIN   /* ningun huso real: "no se sabe" */

/* Desfase horario actual en cuartos de hora, o RELOJ_OFF_DESCONOCIDO si la P4
 * todavia no lo ha mandado. */
int8_t reloj_off_quince_actual(void);

/* Lo que hay que sumarle al INICIO de un tramo abierto con el desfase 'off_ini'.
 * Si el desfase del tramo no se conoce, se comporta como
 * reloj_delta_horario_desde() (el calculo de antes, con su limitacion). */
int reloj_delta_horario_off(uint32_t epoch_ini, int8_t off_ini);

/* Segundos desde 1970 en hora LOCAL de la P4. false si esta pantalla no ha
 * visto a la P4 desde que arranco: entonces no hay hora de ningun tipo. */
bool reloj_ahora(uint32_t *out);

#ifdef __cplusplus
}
#endif
