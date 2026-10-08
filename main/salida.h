/* salida.h — la salida en curso y lo que dejo abierto, a prueba de apagones.
 *
 * Esta pantalla se apaga al quitar el contacto y arranca al ponerlo. Todo el
 * diseno del cuaderno de viaje se apoya en eso:
 *
 *     DECLARAS AL LLEGAR, RELLENAS AL SALIR.
 *
 * Con el motor todavia en marcha pulsas "Repostaje" (se ABRE un evento). Echas
 * gasolina, la pantalla muere con el contacto. Al girar la llave arranca, ve
 * que dejo un repostaje abierto y te pide importe, litros y kilometros -- que
 * es justo cuando ya los sabes.
 *
 * De ahi salen dos cosas que no hay que programar, vienen solas: no hay nada
 * que acordarse de cerrar (lo pregunta el vehiculo al arrancar) y las horas de
 * inicio y fin son reales, no tecleadas.
 *
 * Por eso TODO lo de aqui vive en NVS: entre abrir un evento y cerrarlo, el
 * aparato ha estado sin corriente.
 *
 * Diseno completo en docs/superpowers/specs/2026-08-23-pantalla-registros-salidas-design.md
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Tipo de salida ------------------------------------------------------ */
typedef enum {
    SALIDA_NINGUNA = 0,
    SALIDA_VIAJE,      /* dura dias, tiene nombre y carpeta propia */
    SALIDA_PUNTUAL,    /* sales a repostar / ITV / bombona / taller y vuelves */
} salida_tipo_t;

/* --- Que se puede anotar ------------------------------------------------- */
typedef enum {
    EV_PARADA = 0,
    EV_AGUAS,        /* wc, vaciado de grises, llenado de agua */
    EV_REPOSTAJE,
    EV_PEAJE,        /* el unico que se rellena en el momento: lo hace el copiloto */
    EV_BOMBONA,
    EV_AVERIA,       /* averia o mantenimiento */
    EV_ITV,          /* solo en salida puntual */
    EV_COUNT
} evento_tipo_t;

/* Subtipo de EV_PARADA: por que paras. */
typedef enum {
    MOTIVO_VISITA = 0,
    MOTIVO_DESCANSO,
    MOTIVO_COMER,
    MOTIVO_CENAR,
    MOTIVO_COMPRAS,
    MOTIVO_PERNOCTA,   /* el unico con cola: sitio, servicios, precio, valoracion */
    MOTIVO_COUNT
} parada_motivo_t;

/* Segundo subtipo de EV_PARADA, solo si el motivo es MOTIVO_PERNOCTA.
 * El orden importa: parada_sitio_es_de_pago() lo usa. */
typedef enum {
    SITIO_PARKING_GRATIS = 0,
    SITIO_PARKING_PAGO,
    SITIO_AREA_GRATIS,
    SITIO_AREA_PAGO,
    SITIO_CAMPING,     /* siempre de pago */
    SITIO_COUNT
} pernocta_sitio_t;

bool parada_sitio_es_de_pago(uint8_t sitio);

/* --- Un evento abierto --------------------------------------------------- */
typedef struct {
    uint8_t  tipo;        /* evento_tipo_t */
    uint8_t  sub;         /* parada_motivo_t, o 0 */
    uint8_t  sub2;        /* pernocta_sitio_t, o 0 */
    /* 1 = ya se llamo trip_eventos_inc() para este evento. El id se reserva al
     * ABRIR justo para que un reintento (push a la cola fallado) lleve el
     * mismo numero y la P4 pueda deduplicar -- pero "contar lo generado" solo
     * debe pasar UNA VEZ por evento, no en cada reintento del mismo cierre.
     * Sin esto, un fallo de encolado + reintento contaba el mismo evento dos
     * veces y la P4 veia "eventos" de mas (INCOMPLETO falso). Detectado
     * auditando el 07-sep-2026. */
    uint8_t  contado;
    uint32_t epoch_ini;   /* hora local de la P4 al declararlo */
    uint32_t id;          /* reservado al ABRIR: id estable aunque se reintente */
} salida_evento_t;

/* Cuantos eventos pueden estar abiertos a la vez.
 *
 * Cuatro y no uno porque en una estancia larga conviven: pernoctas cinco dias
 * en un camping (parada abierta) y por el medio sales a repostar, compras una
 * bombona y pagas un peaje. Con uno solo, los del medio se perderian.
 *
 * Cuatro y no mas porque cada uno hay que cerrarlo a mano al arrancar, y una
 * cola mas larga que eso no se despacha: seria una pantalla de preguntas. */
#define SALIDA_EVENTOS_MAX  4

#define SALIDA_NOMBRE_MAX   25   /* lo que se teclea */
#define SALIDA_CARPETA_MAX  40   /* nombre saneado + "_AAAAMMDD" */

typedef struct {
    salida_tipo_t tipo;
    const char   *nombre;    /* "" si no hay salida */
    const char   *carpeta;   /* "" si no hay salida */
    uint32_t      epoch_ini;
    uint8_t       n_eventos;
    const salida_evento_t *eventos;
    /* Solo tiene sentido en SALIDA_PUNTUAL: si ya se ha declarado la unica
     * parada que le corresponde (se llego al sitio de la accion). Antes de
     * eso solo se ha abierto la carpeta en la P4, sin apuntar nada todavia.
     * Un viaje real no la usa -- admite cualquier numero de paradas y no
     * necesita saber si ya hubo alguna. */
    bool          declarado;
} salida_vista_t;

/* --- Ciclo de vida ------------------------------------------------------- */

/* Carga el estado de NVS y arranca la marca de vida. Llamar UNA vez al
 * arrancar, despues de nvs_flash_init(). No necesita que haya hora todavia. */
void salida_init(void);

/* Lo que hay ahora mismo. Nunca NULL. */
const salida_vista_t *salida_get(void);

static inline bool salida_hay(void) { return salida_get()->tipo != SALIDA_NINGUNA; }

/* --- Abrir y cerrar la salida -------------------------------------------- */

/* Crea la carpeta "<nombre saneado>_AAAAMMDD" a partir de la hora de la P4.
 * false si no hay hora (sin ella no hay fecha que poner) o si el nombre queda
 * vacio al sanearlo. */
bool salida_abrir_viaje(const char *nombre);

/* Una gestion y vuelta (repostar, ITV, bombona, taller). Se trata como un
 * viaje de una sola parada: crea la misma carpeta "<nombre>_AAAAMMDD" que
 * salida_abrir_viaje (la P4 la antepone ella sola, ver op_inicio), asi que
 * queda descargable igual que un viaje. Se llama DESPUES de que la P4 ya
 * haya confirmado el inicio -- igual que salida_abrir_viaje. */
bool salida_abrir_puntual(const char *nombre);

/* Marca la salida puntual en curso como ya declarada (ver el campo
 * 'declarado' de salida_vista_t). Llamar justo antes de declarar su unica
 * parada -- de ahi en adelante no se vuelve a ofrecer declararla otra vez. */
void salida_puntual_marcar_declarada(void);

/* Deshace la marca anterior. Solo tiene sentido en un sitio: si justo el
 * apunte que se acaba de declarar se deshace con "Deshacer" en el cartel de
 * "Anotado" (mismo dialogo modal, sin nada de por medio), la puntual se
 * queda "declarada" pero sin ningun apunte dentro -- el mismo estado
 * muerto que evita salida_puntual_marcar_declarada, por el otro lado.
 * Detectado el 09-sep-2026. */
void salida_puntual_desmarcar_declarada(void);

/* Cierra la salida y OLVIDA los eventos que quedasen abiertos. Quien llama
 * tiene que haberlos despachado antes (salida_eventos_abiertos() == 0). */
void salida_cerrar(void);

/* --- Abrir y cerrar eventos ---------------------------------------------- */

/* Devuelve el id reservado, o 0 si no cabe otro / no hay hora. */
uint32_t salida_evento_abrir(evento_tipo_t tipo, uint8_t sub, uint8_t sub2);

int salida_eventos_abiertos(void);

/* El primero de la cola (el que lleva mas tiempo abierto), o NULL. Es el orden
 * en que se preguntan al arrancar. */
const salida_evento_t *salida_evento_primero(void);

/* Uno cualquiera, por su sitio en la cola. Existe para DESHACER: el evento se
 * abre de un toque y sin esto un dedo equivocado no tendria vuelta atras.
 * false si el indice no existe. */
bool salida_evento_borrar(int idx);

/* El de la cola, por su sitio. NULL si no existe. Para pintar la lista de lo
 * que queda abierto. */
const salida_evento_t *salida_evento_en(int idx);

/* Cambia la hora de inicio de un evento ya abierto. Existe para LA PARADA QUE
 * SE ANOTA A POSTERIORI: al volver a dar el contacto, la pantalla puede haber
 * estado apagada horas, y esa parada no empieza ahora sino cuando se fue la luz
 * (ver salida_olvido_pendiente).
 *
 * Por indice y no "el primero" a proposito: entre que se ofrece anotarla y se
 * elige el motivo puede haberse declarado otra cosa, y entonces la nuestra ya
 * no es la primera de la cola. false si el indice no existe. */
bool salida_evento_set_inicio(int idx, uint32_t epoch_local);

/* Marca un evento como ya contado en trip_eventos_inc() (ver el comentario del
 * campo 'contado' arriba). Idempotente: llamarla varias veces no vuelve a
 * incrementar nada por si sola, solo deja la marca puesta. */
bool salida_evento_marcar_contado(int idx);

/* --- La marca de vida y el olvido ---------------------------------------- */

/* Cada cuanto se deja la marca. Diez minutos: para decir "estuviste parado
 * desde las 19:40" sobra, y son ~6 escrituras a la hora de contacto puesto,
 * que la flash aguanta de sobra. */
#define SALIDA_VIDA_PERIODO_MS  (10 * 60 * 1000)

/* A partir de cuanto tiempo apagada se considera que hubo una parada que
 * merece la pena ofrecer. Media hora: parar veinte minutos a por el pan no es
 * algo que uno quiera anotar, y preguntarlo cada vez cansa. */
#define SALIDA_OLVIDO_MIN_S     (30 * 60)

/* Cuanto estuvo apagada la pantalla, en segundos, si merece ofrecer anotar una
 * parada olvidada. Devuelve false si no procede: no hay hora, no hay salida en
 * marcha, ya habia algo abierto, no hay marca previa o el hueco es corto.
 *
 * Solo dice que si UNA vez por arranque: en cuanto se contesta (anotando o
 * descartando) hay que llamar a salida_olvido_descartar(). */
bool salida_olvido_pendiente(uint32_t *segundos_out, uint32_t *epoch_apagado_out);
void salida_olvido_descartar(void);

/* --- Utilidades ---------------------------------------------------------- */

/* Noches entre dos instantes, contando CAMBIOS DE DIA de calendario y no
 * periodos de 24 h: llegas el viernes por la tarde y te vas el sabado por la
 * manana y eso es UNA noche. Los dos epoch son hora local (ver mini_proto.h),
 * asi que el dia sale con una division entera. */
uint32_t salida_noches(uint32_t epoch_ini, uint32_t epoch_fin);

/* "AAAAMMDD" a partir de un epoch local. buf de 9 bytes minimo. */
void salida_fecha_compacta(uint32_t epoch_local, char *buf, size_t n);

#ifdef __cplusplus
}
#endif
