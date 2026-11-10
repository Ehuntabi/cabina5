/* esp_timer.h — DOBLE del reloj para compilar main/net/viaje_cola.c en el PC
 * (ver test/viaje_cola_integracion.sh).
 *
 * Firma real (ESP-IDF 5.5, components/esp_timer/include/esp_timer.h):
 *     int64_t esp_timer_get_time(void);
 *
 * En la placa son microsegundos desde el arranque. Aqui es un reloj de mentira
 * que avanza un segundo por llamada: solo lo usa la caducidad del dato de hueco
 * de la SD (30 min), que no es lo que prueba este arnes, pero que el fichero de
 * produccion llama y hay que enlazar.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t esp_timer_get_time(void);

#ifdef __cplusplus
}
#endif
