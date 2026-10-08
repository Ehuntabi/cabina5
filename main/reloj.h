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
void reloj_set_desde_p4(uint32_t epoch_local);

/* Segundos desde 1970 en hora LOCAL de la P4. false si esta pantalla no ha
 * visto a la P4 desde que arranco: entonces no hay hora de ningun tipo. */
bool reloj_ahora(uint32_t *out);

#ifdef __cplusplus
}
#endif
