/* freertos/FreeRTOS.h — DOBLE de FreeRTOS para compilar main/net/viaje_cola.c
 * en el PC (ver test/viaje_cola_integracion.sh).
 *
 * Solo los tipos y las constantes que usa la cola. Los valores son los de
 * ESP-IDF 5.5 (freertos/FreeRTOS-Kernel/include/freertos/projdefs.h y
 * portable/xtensa/include/freertos/portmacro.h):
 *
 *   BaseType_t = int, UBaseType_t = unsigned, TickType_t = uint32_t,
 *   configTICK_RATE_HZ = 100 (CONFIG_FREERTOS_HZ del sdkconfig),
 *   portMAX_DELAY = 0xFFFFFFFF, pdMS_TO_TICKS(ms) = ms * 100 / 1000.
 *
 * QUE HACE DE VERDAD EL DOBLE (ver test/dobles/dobles.c): los semaforos no
 * bloquean (el test es de un solo hilo: el cerrojo siempre esta libre), y las
 * esperas sirven para DEVOLVER EL CONTROL al arnes, que es quien decide cuando
 * corre el repartidor. xTaskCreate NO crea la tarea: la registra para que el
 * arnes la llame.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int          BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t     TickType_t;
typedef uint32_t     configSTACK_DEPTH_TYPE;

#define pdTRUE   ((BaseType_t)1)
#define pdFALSE  ((BaseType_t)0)
#define pdPASS   pdTRUE
#define pdFAIL   pdFALSE

#define configTICK_RATE_HZ  100u
#define portMAX_DELAY       ((TickType_t)0xFFFFFFFFu)

#define pdMS_TO_TICKS(xTimeInMs) \
    ((TickType_t)(((TickType_t)(xTimeInMs) * (TickType_t)configTICK_RATE_HZ) / 1000u))

#ifdef __cplusplus
}
#endif
