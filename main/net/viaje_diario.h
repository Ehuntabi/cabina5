/* viaje_diario.h — copia DURADERA de cada apunte del viaje, en la flash de la
 * pantalla.
 *
 * POR QUE EXISTE (10-nov-2026). El fin de semana del 7-8-nov el usuario no pudo
 * grabar el viaje: la cola hacia la P4 se atasco y los apuntes se quedaron
 * dentro. La cola funciona (reintenta, distingue 6 motivos de atasco y sobrevive
 * reinicios), pero son **16 huecos de NVS** y no hay nada mas: si algo se
 * rechaza, se descarta o la cola se reinicia, el apunte no existe en ningun
 * sitio. Y el aviso de atasco vive en la pantalla de Informacion, asi que
 * conduciendo no se ve: el usuario se entera dias despues.
 *
 * QUE HACE ESTE MODULO. Antes de encolar un apunte, se escribe aqui, en una
 * particion propia de la flash. La cola pasa a ser la VENTANA DE ENVIO (que se
 * puede vaciar, atascar o perder) y el diario es la copia que manda:
 *   - el apunte sale del diario SOLO cuando la P4 responde 200 para ese id
 *     (o cuando el usuario lo descarta a mano, que entonces se marca como
 *     descartado a proposito, no se borra sin dejar rastro);
 *   - al arrancar y cada vez que la cola se queda vacia, el repartidor saca el
 *     pendiente mas antiguo del diario y lo encola: asi se recupera solo,
 *     aunque la cola se llenara o la NVS se reiniciara;
 *   - `viaje_diario_pendientes()` es el numero que la pantalla principal
 *     enseña: mientras no sea 0, el viaje NO esta grabado en la P4.
 *
 * FORMATO Y CORTES DE CORRIENTE. Registro append-only:
 *
 *     [magic 'VDA1'][id][len][flags][crc32 del cuerpo][cuerpo JSON][relleno a 4]
 *
 * Un corte a mitad de escritura deja un registro con CRC que no cuadra (o
 * corto): se descarta ESE y se para de leer ahi, de modo que todo lo anterior
 * (que si se escribio entero) sigue valiendo. Nada de sistema de ficheros: la
 * particion se escribe a pelo, que es lo unico que no depende de que el FAT
 * sobreviva a un apagon.
 *
 * La particion se parte en dos mitades. Cuando la activa se llena, los
 * pendientes se copian a la otra y ALLI se escribe la cabecera de commit al
 * final: si se corta la corriente a mitad de la copia, la mitad nueva no tiene
 * cabecera valida y al arrancar se sigue leyendo la vieja, que esta intacta.
 */
#ifndef VIAJE_DIARIO_H
#define VIAJE_DIARIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Particion (ver partitions.csv) y topes. */
#define VIAJE_DIARIO_PARTICION   "diario"
#define VIAJE_DIARIO_CUERPO_MAX  896     /* = CUERPO_MAX de viaje_cola.h */
#define VIAJE_DIARIO_MAX_PEND    128     /* apuntes sin confirmar que caben en RAM */

/* Lee la particion y deja montado el indice de pendientes. Una vez, al
 * arrancar. Si la particion no existe o no hay nada valido, queda vacio y la
 * pantalla sigue funcionando como antes (sin diario), avisando por el log. */
void viaje_diario_init(void);

/* Saca el id de un cuerpo JSON montado por apunte.c / p4_api.c.
 * Devuelve 0 si no lo encuentra (cuerpo invalido: no se anota). */
uint32_t viaje_diario_id_de(const char *cuerpo);

/* Anota el apunte. true si queda guardado en la flash. */
bool viaje_diario_anota(const char *cuerpo, uint32_t id);

/* Marca el apunte como resuelto: entregado (200 de la P4) o descartado por el
 * usuario. true si habia algo que marcar. */
bool viaje_diario_confirma(uint32_t id, bool descartado);

/* Apuntes pendientes (ni entregados ni descartados). */
size_t viaje_diario_pendientes(void);

/* true si ese id sigue pendiente en el diario. */
bool viaje_diario_contiene(uint32_t id);

/* El pendiente MAS ANTIGUO, para que el repartidor lo encole cuando la cola se
 * queda vacia. false si no hay ninguno. */
bool viaje_diario_siguiente(char *out, size_t n, uint32_t *id_out);

/* El pendiente numero 'k' contando desde el mas antiguo. La cola de envio es
 * SIEMPRE un prefijo de esta lista (ver viaje_cola.c), asi que el siguiente que
 * hay que encolar es el k = pendientes en la cola. */
bool viaje_diario_en(size_t k, char *out, size_t n, uint32_t *id_out);

/* true si la particion existe y el diario esta montado. Sin esto, la cola no
 * puede reconstruirse a partir del diario (se queda como estaba). */
bool viaje_diario_disponible(void);

/* El pendiente MAS NUEVO (el ultimo anotado). Lo necesita la reconciliacion del
 * arranque de la cola: "lo ultimo que se encolo fue el fin" tiene que valer
 * tambien cuando el fin vive solo en el diario. */
bool viaje_diario_ultimo(char *out, size_t n, uint32_t *id_out);

/* Solo para el test en el PC: fuerza que la proxima escritura no quepa, o que
 * falle, para poder probar los caminos malos sin llenar 1 MB. */
#ifdef VIAJE_DIARIO_TEST
void viaje_diario_test_falla(bool fallar);
void viaje_diario_test_borra_todo(void);
void viaje_diario_test_mitad(uint32_t bytes);
void viaje_diario_test_corrompe_pendiente(size_t i);
void viaje_diario_test_rompe_cabecera(uint32_t base);
void viaje_diario_test_falla_tras(int n);
uint32_t viaje_diario_test_base(void);
uint32_t viaje_diario_test_gen(void);
uint32_t viaje_diario_test_crc_vector(void);
uint16_t viaje_diario_test_marca_de(uint32_t id);
#endif

#ifdef __cplusplus
}
#endif

#endif /* VIAJE_DIARIO_H */
