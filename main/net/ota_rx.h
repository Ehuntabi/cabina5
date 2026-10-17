/* ota_rx.h — actualizacion del firmware de ESTA pantalla, pedida a la P4.
 *
 * Lo pidio el dueño el 17-oct-2026: "¿es posible implementar una actualizacion
 * por OTA en la 5 y 3,5?" -> "dale". El reparto es: la P4 sirve el fichero (su
 * contrato, que NO se cambia desde aqui: ver la cabecera de ota_rx.c) y esta
 * pantalla se lo baja, lo comprueba y arranca con el.
 *
 * COMO SE USA DESDE LA UI (Ajustes -> "Actualizar pantalla"):
 *
 *   1. ota_rx_comprobar()   lanza la consulta. Vuelve AL INSTANTE.
 *   2. cada 500 ms, ota_rx_estado() dice en que va: comprobando / hay version
 *      nueva / al dia / descargando con % / listo / error.
 *   3. ota_rx_actualizar()  descarga e instala. Vuelve AL INSTANTE; si sale
 *      bien, la pantalla se reinicia sola 3 s despues.
 *
 * NADA DE ESTO SE PUEDE LLAMAR ESPERANDO RESPUESTA: el trabajo lo hacen tareas
 * propias (ver el porque en ota_rx.c). Es seguro llamarlas desde la tarea de
 * LVGL: solo tocan un struct pequeño protegido por un cerrojo corto.
 *
 * Semantica de los codigos de inicio: el 0 (OTA_RX_INICIO_OK) es "lanzado"; con
 * cualquier otro NO se ha lanzado nada y la UI tiene que enseñar el motivo. En
 * particular OTA_RX_INICIO_VIAJE es el "termina el viaje antes": no se actualiza
 * con un viaje abierto.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_RX_QUIETO = 0,   /* no hay nada en marcha ni que contar */
    OTA_RX_COMPROBANDO,  /* preguntando la version a la P4 */
    OTA_RX_HAY_NUEVA,    /* la P4 tiene otra version mas nueva; la UI pregunta */
    OTA_RX_AL_DIA,       /* la P4 tiene la misma (o no tiene nada mas nuevo) */
    OTA_RX_DESCARGANDO,  /* bajando el .bin y escribiendolo en la flash */
    OTA_RX_LISTO,        /* instalado y marcado para arrancar; reinicio en 3 s */
    OTA_RX_ERROR,        /* algo fallo; 'motivo' lo dice en castellano */
} ota_rx_estado_t;

/* Copia del estado para pintarlo. 'ofrecida' solo tiene sentido con HAY_NUEVA /
 * DESCARGANDO; 'motivo' solo con ERROR. Los textos son cortos a proposito: se
 * pintan en una linea de la pantalla de Ajustes.
 *
 * Se copia con cerrojo, asi que la UI puede llamarla desde su tarea sin miedo a
 * leer medio texto de una version y medio de otra. */
typedef struct {
    ota_rx_estado_t estado;
    int             pct;          /* 0..100, solo con OTA_RX_DESCARGANDO */
    char            motivo[96];   /* ERROR: por que no se pudo */
    char            ofrecida[16]; /* HAY_NUEVA/DESCARGANDO: "v1.31" */
} ota_rx_vista_t;

/* Por que no se ha podido EMPEZAR (ver ota_rx_comprobar/actualizar). */
typedef enum {
    OTA_RX_INICIO_OK = 0,
    OTA_RX_INICIO_VIAJE,     /* hay un viaje (o un apunte) abierto */
    OTA_RX_INICIO_OCUPADO,   /* ya hay una comprobacion o descarga en marcha */
    OTA_RX_INICIO_SIN_DATOS, /* actualizar sin haber comprobado antes */
    OTA_RX_INICIO_SIN_MEM,   /* no se pudo crear la tarea o reservar el buffer */
} ota_rx_inicio_t;

/* UNA vez en setup(), DESPUES de udp_rx_start() (hace falta la netif y el
 * enlace). Tres cosas, todas de arranque:
 *   - deja en el log de que particion esta corriendo y cual es la siguiente;
 *   - si la version que corre esta EN PRUEBAS (rollback armado), arranca la
 *     tarea que la valida cuando la P4 esta enganchada y estable;
 *   - si el bootloader es viejo (sin rollback), lo dice: el anti-ladrillo de
 *     verdad empieza a contar con el primer grabado por cable. */
void ota_rx_arranque(void);

/* Pregunta a la P4 que version tiene para NUESTRO modelo (OTA_MODELO, definido
 * en main/CMakeLists.txt) y la compara con la que corre. */
ota_rx_inicio_t ota_rx_comprobar(void);

/* Descarga el .bin anunciado, lo comprueba y lo deja listo para arrancar. El
 * reinicio lo pide la propia tarea 3 s despues, NUNCA la tarea de LVGL. */
ota_rx_inicio_t ota_rx_actualizar(void);

/* La UI cierra el cartel de "hay version nueva" sin actualizar. Vuelve a
 * QUIETO para que la proxima pulsacion del boton no se encuentre el cartel
 * "ya preguntado". NO hace nada si hay una descarga en marcha. */
void ota_rx_olvidar(void);

/* Copia el estado actual. Nunca bloquea. */
void ota_rx_estado(ota_rx_vista_t *out);

/* ── Las decisiones, expuestas para probarlas SIN PLACA ─────────────────────
 *
 * test/ota_version.sh saca estas funciones TAL CUAL de ota_rx.c y las compila
 * en el PC (mismo metodo que los tests del viaje). Son las que deciden si un
 * fichero se instala o no: si esto se equivoca, la pantalla se queda con un
 * firmware que no arranca, y eso no se puede probar a base de placas. */

/* Comparacion NUMERICA de versiones "vX.Y[.Z]": "v1.9" es MAS VIEJA que
 * "v1.10" aunque como texto vaya despues. false si alguna no tiene esa forma
 * (un build sin tags da "v?"): entonces NO se actualiza, porque no hay forma de
 * saber cual es mas nueva. */
bool ota_rx_version_es_mas_nueva(const char *nueva, const char *actual);

/* true solo si se puede instalar: la version anunciada es MAS NUEVA que la que
 * corre Y es exactamente la que trae el fichero descargado. Las tres cosas
 * tienen que cuadrar; con "igual", "mas vieja" o un .bin que no es el anunciado,
 * no se toca la flash. */
bool ota_rx_version_aceptable(const char *anunciada, const char *actual,
                              const char *embebida);

/* Compara el sha256 calculado (32 bytes) con el que anuncia la P4 (64 hex, en
 * mayusculas o minusculas). Un solo digito distinto -> false. */
bool ota_rx_sha_coincide(const uint8_t calculado[32], const char *hex64);

/* true si el .bin es de ESTA pantalla: el nombre de proyecto que lleva dentro
 * (esp_app_desc_t.project_name) es el modelo con el que se compilo. Es lo unico
 * que lo dice cuando las dos cabinas llevan la misma version; sin esto, un
 * fichero cruzado en la P4 dejaria la pantalla con el firmware de la otra. */
bool ota_rx_modelo_coincide(const char *embebido, const char *modelo);

#ifdef __cplusplus
}
#endif
