/* p4_api.h — envios de la 3.5" a la P4. Ver la cabecera del .c para el porque
 * de HTTP y de que no se pueda llamar desde la tarea de LVGL... salvo que SI:
 * estas funciones vuelven al instante, el trabajo lo hace una tarea aparte y el
 * resultado llega por 'cb', ya en el hilo de LVGL (seguro para tocar widgets).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 'ok' es true solo con respuesta 2xx. 'estado' es el codigo HTTP, o 0 si no se
 * llego a conectar (P4 apagada o fuera de alcance). 401 = credenciales del
 * portal mal puestas en Ajustes. 'user_data' es lo que se paso al lanzar el
 * envio, reenviado tal cual: sirve para distinguir de que era la respuesta. */
typedef void (*p4_api_done_cb)(bool ok, int estado, void *user_data);

/* Devuelven false si ni siquiera se pudo lanzar el envio (sin memoria); en ese
 * caso 'cb' NO se llama. */
/* 'km_cuadro' = lo que marca el cuentakilometros del vehiculo en el momento de
 * empezar (0 = no se apunto, y entonces el campo no viaja). Lo teclea el usuario
 * y es OPCIONAL: la 3,5" no lo pide, asi que su inicio tiene que seguir siendo
 * valido tal cual. La P4 lo guarda con el viaje y lo escribe en resumen.txt. */
bool p4_api_viaje_inicio(uint32_t id, const char *destino, uint32_t fecha_dias,
                         uint32_t km_cuadro, p4_api_done_cb cb);

/* El POST crudo, SINCRONO: bloquea hasta la respuesta. Lo usa el repartidor de
 * la cola (viaje_cola.c), que tiene su propia tarea. NO llamar desde LVGL.
 * 'estado_out' recibe el codigo HTTP, o 0 si no se llego a conectar. */
bool p4_api_post(const char *cuerpo, int *estado_out);

/* Igual, pero deja en 'resp_out' los primeros bytes del CUERPO de la respuesta.
 *
 * Existe por el hueco de la tarjeta de la P4 (14-oct-2026): la P4 añade
 * " libre_mb=NNN" al cuerpo de las respuestas de /api/viaje, y ese es el unico
 * canal que la cabina tiene para enterarse sin tocar el struct de telemetria (40
 * bytes, compartido por las tres placas, que no se puede cambiar sin grabarlas
 * todas a la vez). Se queda con el principio: el sufijo va al final de un texto
 * corto. */
bool p4_api_post_resp(const char *cuerpo, int *estado_out, char *resp_out, size_t resp_n);

/* El POST a una URL concreta (mismo camino que los viajes). Lo usa el auto-test
 * para probar /api/alarma; el resto usa p4_api_post. SINCRONO: no llamar desde
 * la tarea de LVGL. */
bool p4_api_post_url(const char *url, const char *cuerpo, int *estado_out,
                      char *resp_out, size_t resp_n);

/* Montadores del cuerpo JSON, para encolar sin enviar. */
/* Identidad de esta pantalla (su MAC en hex) tal y como viaja en los cuerpos:
 * la P4 la usa para saber de quien es el viaje abierto. Ver p4_api.c. */
const char *p4_api_dev_id(void);

void p4_api_cuerpo_inicio(char *out, size_t n, uint32_t id,
                          const char *destino, uint32_t fecha_dias,
                          uint32_t km_cuadro);
/* 'eventos' es cuantos apuntes ha generado el viaje, el inicio incluido: con
 * eso la P4 sabe si le falta alguno y marca el viaje como incompleto.
 *
 * 'km_cuadro' = lo que marca el cuentakilometros del vehiculo AL CERRAR (0 = no
 * se apunto, y entonces el campo no viaja). Con el de la salida, la P4 escribe
 * en el resumen lo que ha hecho el viaje de verdad y lo compara con el GPS. */
void p4_api_cuerpo_fin(char *out, size_t n, uint32_t id, uint32_t eventos,
                       const char *destino, uint32_t km_cuadro);

/* Fin SIN el recuento de apuntes. Para cerrar un viaje que esta abierto en la
 * P4 pero no en esta pantalla (se empezo desde otro sitio, o se perdio el
 * estado): no sabemos cuantos apuntes generó, y mandar un numero inventado
 * marcaria el viaje como INCOMPLETO sin serlo. Sin el campo, la P4 lo da por
 * bueno -- ver comprobar_completo() en config_server_viaje.c. */
void p4_api_cuerpo_fin_ajeno(char *out, size_t n, uint32_t id, int forzar);

/* Avisa a la P4 del destino nuevo SIN renombrar nada: ella lo apunta y lo
 * aplica al cerrar el viaje. Ver p4_api_cuerpo_destino en el .c. */
void p4_api_cuerpo_destino(char *out, size_t n, uint32_t id, const char *destino);

/* APARTAR el viaje abierto en la P4 en vez de cerrarlo: su carpeta pasa a
 * llamarse DESCARTADO_<nombre> y deja de contar como viaje. Para el que se
 * empezo por error. No borra nada. */
void p4_api_cuerpo_descartar(char *out, size_t n, uint32_t id, int forzar);

/* ── Silenciar una alarma de la P4 (14-sep-2026) ────────────────────────────
 *
 * El pitido lo hace la P4 (lleva altavoz) y esta pantalla no, asi que callarlo
 * desde el asiento del conductor es mandarle una orden. Va por el mismo camino
 * que los apuntes y por el mismo motivo: por TCP se sabe que llego. Un silencio
 * perdido seria una alarma pitando y un conductor convencido de haberla
 * callado.
 *
 * 'mask' es el bitmask MINI_ALARM_* (main/net/mini_proto.h) de la alarma que se
 * quiere callar: se manda el mismo byte que acaba de llegar en la telemetria, o
 * sea que se calla la que la pantalla esta enseñando.
 *
 * 'cb' es opcional (NULL si no se quiere saber el resultado). Como las demas
 * funciones de aqui, vuelve al instante: el envio lo hace una tarea aparte y el
 * resultado llega ya en el hilo de LVGL. 'user_data' se reenvia tal cual al
 * callback (ver p4_api_done_cb). Devuelve false solo si no se pudo lanzar el
 * envio (sin memoria). */
bool p4_api_silenciar_alarma(uint8_t mask, p4_api_done_cb cb, void *user_data);

/* Manda el estado DESEADO del altavoz: silenciar=0 vuelve a habilitar el sonido,
 * que antes era imposible desde la pantalla (la P4 solo sabia callar). */
bool p4_api_alarma_sentido(uint8_t mask, int silenciar, p4_api_done_cb cb,
                           void *user_data);

/* Monta el cuerpo JSON, expuesto aparte para poder comprobarlo sin red. */
void p4_api_cuerpo_alarma(char *out, size_t n, uint8_t mask);

/* Igual, pero diciendo si se quiere CALLAR (silenciar=1) o VOLVER A SONAR (0).
 * p4_api_cuerpo_alarma() es esta misma con 1. */
void p4_api_cuerpo_alarma_sentido(char *out, size_t n, uint8_t mask, int silenciar);

#ifdef __cplusplus
}
#endif
