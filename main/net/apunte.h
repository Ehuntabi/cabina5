/* apunte.h — montaje del JSON de un registro para la P4.
 *
 * Uso tipico:
 *     char b[384];
 *     size_t u = apunte_cabecera(b, sizeof b, id, "repostaje");
 *     u = apunte_campo_txt(b, sizeof b, u, "moneda", "EUR");
 *     u = apunte_campo_txt(b, sizeof b, u, "importe", "62.40");
 *     u = apunte_cerrar(b, sizeof b, u, "62.40 EUR, 41.2 L");
 *     viaje_cola_push(b, NULL);
 *
 * El orden de los campos IMPORTA: la P4 hace con ellos la cabecera del CSV.
 * Ver la cabecera del .c para el porque del sello de tiempo.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Abre el objeto y sella la hora. Devuelve los bytes usados. */
size_t apunte_cabecera(char *out, size_t n, uint32_t id, const char *tipo);

/* Añaden un campo a "datos". Devuelven los bytes usados, o **0 si no cabe** --
 * y ese 0 se arrastra hasta apunte_cerrar(). QUIEN LLAMA TIENE QUE MIRARLO: un
 * apunte que no cabe no se puede encolar, hay que avisar al usuario. */
size_t apunte_campo_txt(char *out, size_t n, size_t used,
                        const char *clave, const char *valor);
size_t apunte_campo_num(char *out, size_t n, size_t used,
                        const char *clave, long valor);

/* Cierra "datos" y añade el resumen de una linea que ira al diario del viaje
 * (eventos.csv). Lo monta la 3.5" y no la P4 porque es aqui donde se sabe que
 * significa cada casilla. Devuelve 0 si el apunte NO cabe: en ese caso el
 * buffer contiene un JSON cortado y NO se debe mandar. */
size_t apunte_cerrar(char *out, size_t n, size_t used, const char *resumen);

#ifdef __cplusplus
}
#endif
