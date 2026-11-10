/* freertos/task.h — DOBLE de las tareas para compilar main/net/viaje_cola.c en
 * el PC (ver test/viaje_cola_integracion.sh).
 *
 * Firmas de ESP-IDF 5.5 (freertos/FreeRTOS-Kernel/include/freertos/task.h):
 *
 *   typedef void (*TaskFunction_t)(void *);
 *   typedef void *TaskHandle_t;
 *   BaseType_t xTaskCreate(TaskFunction_t, const char *const, configSTACK_DEPTH_TYPE,
 *                          void *const, UBaseType_t, TaskHandle_t *const);
 *   void vTaskDelay(TickType_t);
 *
 * xTaskCreate NO crea ninguna tarea: guarda el puntero a la funcion para que el
 * arnes la ejecute cuando toque (ver dobles_tarea_pasada()). Es justo lo que
 * hace falta para probar la cola de forma determinista, sin hilos ni carreras.
 * vTaskDelay devuelve el control al arnes (el reloj del test corre solo).
 */
#pragma once

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char * const pcName,
                       const configSTACK_DEPTH_TYPE usStackDepth,
                       void * const pvParameters,
                       UBaseType_t uxPriority,
                       TaskHandle_t * const pxCreatedTask);

void vTaskDelay(const TickType_t xTicksToDelay);

#ifdef __cplusplus
}
#endif
