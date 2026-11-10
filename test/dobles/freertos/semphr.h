/* freertos/semphr.h — DOBLE de los semaforos para compilar main/net/viaje_cola.c
 * en el PC (ver test/viaje_cola_integracion.sh).
 *
 * Firmas de ESP-IDF 5.5 (freertos/FreeRTOS-Kernel/include/freertos/semphr.h):
 *
 *   typedef QueueHandle_t SemaphoreHandle_t;   // aqui, void *
 *   SemaphoreHandle_t xSemaphoreCreateMutex(void);
 *   SemaphoreHandle_t xSemaphoreCreateBinary(void);   // en IDF es una macro
 *   BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
 *   BaseType_t xSemaphoreGive(SemaphoreHandle_t);
 *
 * En el test todo corre en un solo hilo, asi que el cerrojo de la cola no puede
 * estar cogido por nadie y xSemaphoreTake(.., portMAX_DELAY) entra siempre. La
 * espera CON plazo (la de s_despertar, "hasta que haya algo nuevo") es la que
 * usa el arnes para saber que el repartidor ya ha hecho todo lo que tenia que
 * hacer en esta pasada.
 */
#pragma once

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);

#ifdef __cplusplus
}
#endif
