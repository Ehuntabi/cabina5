/* viaje_cola.h — apuntes pendientes de entregar a la P4, en NVS.
 * Ver la cabecera del .c: existe porque esta pantalla se apaga con el contacto
 * y los apuntes se hacen justo antes de apagar. */
#pragma once

/* Cuantos apuntes caben esperando. La pantalla de datos lo necesita para avisar
 * ANTES de llegar al tope; el porque de este numero esta en viaje_cola.c. */
#define VIAJE_COLA_CAPACIDAD  16

/* HUECOS RESERVADOS PARA LAS OPERACIONES DE CONTROL (auditoria del 14-oct-2026).
 *
 * El cierre del viaje (y el descarte) NO pueden competir por sitio con los
 * apuntes: con la cola llena, el `fin` se rechazaba y el viaje no se podia
 * cerrar desde la pantalla hasta hacer sitio o descartar a mano -- y la P4
 * apagada (que es lo normal con el vehiculo parado) es justo cuando la cola se
 * llena. Con esto los apuntes se limitan a APUNTES_MAX y el cierre siempre
 * entra. Coste: dos apuntes menos de capacidad, que con 2-6 apuntes por viaje
 * no se nota. */
#define VIAJE_COLA_RESERVA_CONTROL 2

/* Los apuntes que caben de verdad: lo que la pantalla usa para el aviso de
 * "casi llena" y para el de "llena". */
#define VIAJE_COLA_APUNTES_MAX (VIAJE_COLA_CAPACIDAD - VIAJE_COLA_RESERVA_CONTROL)

/* Tamano maximo de un cuerpo JSON de apunte. Publica (y no en viaje_cola.c)
 * porque view_registro.c necesita el MISMO numero para el buffer local de
 * apunte_encolar() -- un solo #define en vez de dos copias a mano evita que
 * diverjan en silencio. El porque de 896 esta en viaje_cola.c. Tambien tiene
 * que caber con margen en VIAJE_BODY_MAX del lado P4 (config_server_viaje.c
 * en victron), vigilado por el job de CI viaje_body_sync. */
#define CUERPO_MAX  896

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Se llama cada vez que cambia el numero de pendientes, DESDE LA TAREA DE
 * REPARTO (no desde LVGL): si toca widgets, hacerlo con lv_async_call. */
typedef void (*viaje_cola_cambio_cb)(size_t pendientes);

/* Por que ha fallado viaje_cola_push(), para poder avisar de verdad de lo
 * que pasa en vez de decir siempre "la cola esta llena": antes los dos casos
 * ensenaban el mismo cartel ("la P4 no la vacia, mira si tiene corriente"),
 * que es enganoso para un fallo de NVS (que no tiene nada que ver con que la
 * P4 este apagada). Detectado auditando el 07-sep-2026. */
typedef enum {
    VIAJE_COLA_ERR_NINGUNO = 0,
    VIAJE_COLA_ERR_LLENA,   /* CAPACIDAD alcanzada de verdad: la P4 no vacia */
    VIAJE_COLA_ERR_NVS,     /* fallo al escribir en NVS (cuerpo o indice) */
    VIAJE_COLA_ERR_LARGO,   /* el cuerpo no cabe en CUERPO_MAX (ver abajo) */
} viaje_cola_error_t;

/* Arranca el repartidor. Una vez, al iniciar. */
void viaje_cola_init(viaje_cola_cambio_cb cb);

/* Encola un cuerpo JSON ya montado. false si la cola esta llena o el apunte no
 * cabe: en ese caso hay que AVISAR al usuario, no callarse. 'motivo_out' es
 * opcional (NULL si no interesa distinguir el porque). */
bool viaje_cola_push(const char *cuerpo, viaje_cola_error_t *motivo_out);

size_t viaje_cola_pendientes(void);

/* Por que esta atascada la cola. Un solo motivo cada vez, y el que manda es el
 * del apunte de cabeza (que es el que no avanza). Antes esto eran dos booleanos
 * sueltos (409 y 401) y todo lo demas -- un 400 de la P4 con firmware viejo, un
 * 507 de tarjeta llena -- se veia igual que "la P4 esta apagada", que es un
 * diagnostico FALSO. Ver los comentarios de reparto_task() en viaje_cola.c. */
typedef enum {
    VIAJE_COLA_ATASCO_NO = 0,        /* la cola va sola */
    VIAJE_COLA_ATASCO_CLAVE,         /* 401: usuario/clave mal en Ajustes */
    VIAJE_COLA_ATASCO_NUMERACION,    /* 409 sostenido: choque de numeracion */
    VIAJE_COLA_ATASCO_RECHAZO,       /* la P4 rechaza el apunte (400 y otros 4xx) */
    VIAJE_COLA_ATASCO_P4_ESCRITURA,  /* 5xx: la P4 vive pero no puede escribir */
    VIAJE_COLA_ATASCO_SIN_RESPUESTA, /* estado 0 sostenido: la P4 no contesta (apagada
                                      * o fuera de su Wi-Fi). Se marca solo tras 3
                                      * intentos y se limpia solo cuando vuelve. Existe
                                      * para que con la cola llena el usuario vea POR QUE
                                      * y tenga la salida de apartar el mas antiguo. */
    VIAJE_COLA_ATASCO_REZAGADO,      /* 409 del APUNTE: la P4 lo dice en el cuerpo --
                                      * "es de un viaje ya cerrado" (ver id_rezagado()
                                      * en op_registro). NO es un choque de numeracion:
                                      * la numeracion esta bien, lo que pasa es que ese
                                      * apunte es del viaje ANTERIOR y no va a entrar
                                      * nunca en el que hay abierto. Al final del enum
                                      * a proposito: los valores de arriba no se mueven. */
} viaje_cola_atasco_t;

viaje_cola_atasco_t viaje_cola_atasco(void);

/* true si el apunte de cabeza lleva mucho rato recibiendo 409 de la P4 sin
 * avanzar (ver el comentario de INTENTOS_409_ATASCO en el .c): o es un choque de
 * numeracion entre los dos dispositivos, o un apunte del viaje anterior
 * (VIAJE_COLA_ATASCO_REZAGADO). Lo que NO significa es que la P4 este apagada.
 * No se pierde el apunte: sigue en cola y se reintenta igual, esto es solo para
 * poder avisar de verdad en vez de dejarlo mudo. */
bool viaje_cola_bloqueada(void);

/* true si el ultimo tramo de intentos viene fallando con 401 (credenciales
 * del portal mal puestas en Ajustes): a diferencia de "la P4 esta apagada",
 * esto NO se arregla solo, hace falta corregir usuario/clave. */
bool viaje_cola_credenciales_mal(void);

/* Tira el apunte de CABEZA a mano (el que la P4 no acepta) y deja seguir a los
 * de detras. Devuelve si habia algo que descartar.
 *
 * Existe porque la cola no descarta NUNCA por un codigo de respuesta -- a
 * proposito: un 400 casi siempre es "la P4 es mas vieja" y el apunte entra solo
 * al actualizarla --, pero si la P4 no lo va a aceptar nunca, la cola entera se
 * queda atascada detras y a los 16 apuntes ya no se puede ni anotar ni cerrar
 * el viaje. Antes no habia forma de quitar una entrada encolada: la lista de
 * "sin cerrar" son los eventos de la SALIDA, no la cola. Se llama desde la
 * pantalla, CON confirmacion: lo decide el usuario, no un codigo de respuesta.
 * Auditoria del 13-oct-2026. */
bool viaje_cola_descartar_cabeza(void);

/* Huecos de apunte libres ahora mismo (los de control no cuentan). */
size_t viaje_cola_apuntes_libres(void);

/* true si hay encolado algun apunte de esa operacion ("fin", "inicio"...).
 * OJO: NO sirve para decidir si hay que CERRAR al arrancar -- un "fin" viejo de
 * otro viaje tambien cuenta, que es justo lo que avisa el comentario de al lado.
 * Por eso la reconciliacion usa viaje_cola_ultima_es_op() desde la auditoria del
 * 27-oct-2026 (hallazgo cab5-core-04); hoy esta funcion no la llama nadie y se
 * queda por si hace falta para diagnostico (hallazgo M5 del 13-oct-2026). */
bool viaje_cola_tiene_op_pendiente(const char *op);

/* true si el ULTIMO apunte de la cola (el mas reciente) es de esa operacion.
 *
 * Existe para la reconciliacion del arranque (auditoria del 14-oct-2026): un
 * "fin" encolado puede ser el de ESTE viaje (corte entre encolarlo y cerrarlo
 * aqui: hay que cerrar) o el de un viaje ANTERIOR cuyo 200 se perdio (el nuevo
 * viaje va por detras: NO hay que tocar nada). Lo que los distingue es que el
 * fin del viaje que se esta cerrando es el ultimo que se encolo. */
bool viaje_cola_ultima_es_op(const char *op);

/* Cuantos apuntes se han tenido que tirar AL ARRANCAR (la cola se reinicio
 * porque los indices no cuadraban o cambio la capacidad). La pantalla lo avisa
 * una vez: los apuntes que el usuario vio guardados ya no estan y hay que
 * decirlo (auditoria del 13-oct-2026, hallazgo B2). */
int viaje_cola_perdidos_al_arrancar(void);

/* Cuanto queda en la tarjeta de la P4, tal y como lo dijo ella en el cuerpo de
 * la ultima respuesta (" libre_mb=NNN", 14-oct-2026). -1 si no se sabe (P4
 * vieja, sin respuesta todavia, o el dato es viejo): la pantalla lo enseña solo
 * cuando lo sabe, y NUNCA decide con el si un apunte se guardo. */
int viaje_cola_libre_mb(void);

/* true si el ultimo 507 dijo que lo que se ha llenado es la MEMORIA de la P4 y
 * no su tarjeta. El aviso de la pantalla tiene que decir cual de las dos. */
bool viaje_cola_p4_nvs_llena(void);

#ifdef __cplusplus
}
#endif
