/* dobles.h — lo que el arnes puede hacer con los dobles de
 * test/viaje_cola_integracion_arnes.c (la implementacion esta en dobles.c).
 *
 * Los dobles son las piezas de FUERA del fichero de produccion que se compila
 * tal cual: la NVS, FreeRTOS, el POST a la P4, el log y el reloj. Desde aqui se
 * les manda lo que en la placa pasaria solo (la P4 contesta 200, la NVS no
 * tiene sitio) y se mira lo que la cola ha hecho con ello (que cuerpos han
 * salido, que ha quedado grabado).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── La NVS (un mapa en RAM con las claves de la cola) ──────────────────── */

/* NVS de estreno: como una placa recien flasheada (sin "cabeza", sin "cola" ni
 * "q0"...). Se usa para simular que la cola se pierde. */
void dobles_nvs_borra(void);

bool        dobles_nvs_tiene(const char *clave);   /* ¿existe esa clave? */
const char *dobles_nvs_cadena(const char *clave);  /* valor de texto, o NULL */
uint32_t    dobles_nvs_numero(const char *clave, uint32_t por_defecto);
size_t      dobles_nvs_claves(void);               /* cuantas claves hay ahora */

/* A partir de aqui TODA escritura (cuerpo o indice) devuelve
 * ESP_ERR_NVS_NOT_ENOUGH_SPACE, como una NVS llena de verdad. */
void dobles_nvs_sin_espacio(bool activo);

/* Solo los indices (nvs_set_u32): es el caso del "apunte escrito pero
 * invisible" que la cola tiene que deshacer borrando el cuerpo. */
void dobles_nvs_indices_sin_espacio(bool activo);

/* Hace que nvs_open(NVS_READONLY) falle (ESP_ERR_NVS_NO_FREE_PAGES: un fallo de
 * APERTURA, no de dato) a partir de la apertura de solo lectura numero 'desde'
 * (1 = todas; 0 = desactivado). La cuenta empieza de cero en esta misma llamada.
 *
 * El 'desde' existe porque no todas las aperturas de solo lectura son lo mismo
 * para el codigo bajo prueba: viaje_cola_init() abre PRIMERO para leer los
 * indices (indices_leer) y DESPUES, dentro del bucle de rescate, una por apunte.
 * Para probar la rama "no puedo leer el apunte, NO reconstruyo la cola" hay que
 * fallar la SEGUNDA (desde = 2): si fallara la primera, el bucle de rescate ni
 * siquiera llegaria a ejecutarse. */
void dobles_nvs_open_ro_falla(unsigned desde);

/* ── La P4 (el POST HTTP) ───────────────────────────────────────────────── */

void   dobles_p4_limpia(void);              /* olvida los envios y pone estado 200 */
void   dobles_p4_estado(int estado);        /* codigo HTTP que "contesta" (0 = no hay) */
void   dobles_p4_respuesta(const char *resp);   /* cuerpo de la respuesta */
size_t dobles_p4_enviados(void);            /* cuantos POST ha recibido la P4 */
const char *dobles_p4_cuerpo(size_t i);     /* el i-esimo cuerpo, EN ORDEN DE ENVIO */

/* ── El repartidor (la tarea que en la placa crea viaje_cola_init) ──────── */

/* Ejecuta la tarea registrada por xTaskCreate. Una pasada la deja correr hasta
 * que se ha dormido DOBLES_DORMIDAS_PASADA veces (ver dobles.c): dentro de una
 * pasada el estado local del repartidor (los contadores de reintentos) se
 * conserva, que es lo que necesitan los umbrales de "3 intentos". Devuelve
 * false si la cola no ha registrado ninguna tarea. */
bool dobles_tarea_pasada(void);

/* ¿viaje_cola_init() ha registrado la tarea del repartidor? */
bool dobles_tarea_hay(void);

#ifdef __cplusplus
}
#endif
