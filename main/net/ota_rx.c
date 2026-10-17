/* ota_rx.c — LA PANTALLA SE ACTUALIZA CON EL FIRMWARE QUE LE DA LA P4.
 *
 * Lo pidio el dueño el 17-oct-2026: "¿es posible implementar una actualizacion
 * por OTA en la 5 y 3,5?" -> "dale". Esta es la mitad que corre en las dos
 * pantallas; la otra mitad (servir los ficheros) la implementa la P4. EL
 * CONTRATO NO SE CAMBIA DESDE AQUI:
 *
 *   GET http://192.168.4.1/api/pantalla/info            (puerto 80, Basic)
 *     {"cabina5":{"ver":"v1.30","bytes":2214608,"sha256":"<64 hex>"},"35cabina":{...}}
 *     Un modelo sin fichero puede venir null o no venir: las dos cosas son "la
 *     P4 no tiene esta pantalla" y NO son un fallo de red (se avisa distinto).
 *
 *   GET http://192.168.4.1:8081/api/pantalla/bin?modelo=<modelo>
 *     el .bin ENTERO, con Content-Length. El 8081 es el puerto "pesado" de la
 *     P4: separa la descarga de 2 MB del portal, para que bajarse una pantalla
 *     no atasque las respuestas de los viajes.
 *
 * QUIEN HACE EL TRABAJO: tareas propias de usar y tirar. Descargar son ~2 MB y
 * escribir la flash son varios segundos con la CPU ocupada; hacerlo en la tarea
 * de LVGL congela la pantalla, y con el Task WDT encendido (CONFIG_ESP_TASK_WDT_
 * PANIC) eso es un reinicio en mitad de la descarga. La UI SOLO mira el estado
 * (ota_rx_estado), nunca espera a la red.
 *
 * ANTI-LADRILLO, en tres capas:
 *   1. El .bin se escribe en la particion que NO se esta usando
 *      (esp_ota_get_next_update_partition): la version que funciona sigue
 *      intacta hasta que la nueva arranca de verdad.
 *   2. Antes de marcar nada se comprueba, en este orden: que llego ENTERO
 *      (bytes anunciados), que su sha256 al vuelo es el anunciado, que la
 *      version EMBEBIDA en el fichero es la anunciada y MAS NUEVA que la que
 *      corre, y que esp_ota_end la da por valida (comprueba la imagen entera).
 *      Si algo de eso falla, no se toca el arranque y se sigue con la de antes.
 *   3. El bootloader lleva CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE: la version
 *      nueva arranca EN PRUEBAS y solo se da por buena cuando la pantalla
 *      demuestra que engancha con la P4 (asociada + IP + 8 s de margen, ver
 *      tarea_validar). Si la nueva se queda colgada o sin red, al siguiente
 *      encendido el bootloader vuelve sola a la anterior.
 *      OJO: eso lo hace el BOOTLOADER, y una OTA solo cambia la app. En una
 *      placa que todavia tenga el bootloader viejo, la capa 3 no existe hasta
 *      el primer grabado POR CABLE; el log lo dice en cada arranque para que
 *      nadie se crea protegido sin estarlo.
 *
 * LO QUE NO HACE: actualizar con un viaje abierto. El reinicio cae en mitad de
 * una jornada y el apunte que se esta escribiendo se queda a medias, asi que se
 * devuelve OTA_RX_INICIO_VIAJE y la UI dice "Termina el viaje antes".
 *
 * AUTOMATICO (17-oct-2026). El dueño sube el .bin desde el movil al portal de la
 * P4 y quiere que la pantalla se entere SOLA, sin ir a tocarla (es lo que ya
 * hace la P4). Un temporizador de 60 s, en la tarea de esp_timer y NO en la de
 * LVGL, pregunta la version solo si se cumplen TODAS estas condiciones:
 *
 *   - hay enlace con la P4 (asociada + IP; lo mismo que valida el arranque),
 *   - no hay viaje ni apunte abierto,
 *   - no hay una comprobacion o descarga en marcha,
 *   - no hay editor a pantalla completa ni dialogo abierto (lo avisa la UI).
 *
 * Si hay version nueva la instala SIN preguntar. Antes de arrancar la descarga
 * se vuelven a mirar viaje/editor/dialogo: entre la respuesta de la P4 y la
 * tarea de descarga pasa una ventana en la que el usuario puede haber empezado
 * a escribir. Un fallo NO se reintenta en bucle: se deja para la comprobacion
 * del minuto siguiente (el boton manual sigue igual, para forzar).
 *
 * EL INTERRUPTOR: NVS, namespace "ota", clave "ota_auto" (1 = si, por defecto).
 * Se lee UNA vez en ota_rx_arranque(); con 0 no se crea ni el temporizador.
 */
#include "ota_rx.h"

#include "p4_api.h"      /* p4_api_cabecera_basic() y p4_api_host(): UN solo sitio para la IP */
#include "p4_creds.h"    /* usuario/clave del PORTAL de la P4 (los de Ajustes) */
#include "udp_rx.h"      /* udp_rx_enlace(): el enlace que valida el arranque */
#include "salida.h"      /* salida_hay()/salida_eventos_abiertos(): viaje abierto */
#include "diag_reset.h"  /* el reinicio de la OTA no es un fallo: no debe salir en rojo */

#include "esp_app_desc.h"    /* esp_app_get_description(): la version que corre */
#include "esp_heap_caps.h"   /* el buffer de 4 KB, de PSRAM si se puede */
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"       /* la IP de la STA, para validar el arranque */
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"     /* esp_ota_*: escribir y marcar la particion */
#include "esp_partition.h"
#include "esp_system.h"      /* esp_restart */
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"  /* sha256 AL VUELO, sin guardar 2 MB en ningun sitio */
#include "nvs.h"             /* el interruptor ota_auto */

#include <inttypes.h>
#include <stdatomic.h>       /* el aviso de la UI cruza tareas */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ota_rx";

/* EL MODELO VA EN LA COMPILACION (main/CMakeLists.txt), no en el .c: es el
 * mismo fuente en las dos cabinas y el unico dato que cambia. Sin esto, la 5"
 * se bajaria el firmware de la 3,5" y la dejaria inservible: por eso es un
 * error de compilacion y no un valor por defecto. */
#ifndef OTA_MODELO
#error "OTA_MODELO no esta definido: lo pone main/CMakeLists.txt (cabina5 o 35cabina)"
#endif

/* Los plazos. La P4 esta a un metro por Wi-Fi, pero puede estar ocupada con la
 * tarjeta; corto se traduce en fallos falsos. Para el .bin, 20 s es lo que
 * puede tardar en llegar un trozo si la red se atasca, NO la descarga entera. */
#define OTA_TIMEOUT_INFO_MS   8000
/* 60 s y no 20 (17-oct-2026): la P4 lee el .bin de su tarjeta mientras la
 * camara y el datalogger comparten ese bus, y un paron largo de la SD hacia que
 * la lectura caducara a los 20 s ("Se corto la descarga") con la descarga ya
 * avanzada. El timeout es POR OPERACION: con 60 s un paron no la mata y, si de
 * verdad no hay red, el error sale igual (antes o despues). */
#define OTA_TIMEOUT_BIN_MS   60000

/* 4 KB es lo que pide el contrato para escribir a trozos. Ni uno mas: en esta
 * placa la RAM INTERNA es la escasa (Wi-Fi + LVGL ya se han llevado lo suyo),
 * por eso el buffer se intenta reservar en PSRAM (ver reservar_buffer). */
#define OTA_BUFFER            4096

/* La respuesta de /api/pantalla/info son ~250 bytes. 1 KB da aire de sobra y,
 * si llegara mas, es que no es la respuesta del contrato: se rechaza. */
#define OTA_INFO_MAX          1024

/* Un .bin de esta pantalla pesa ~1,94 MB. Los topes no son para el usuario:
 * son la red que evita que un "bytes":0 o un numero absurdo acaben en una
 * division por cero o en un esp_ota_begin con un tamaño imposible. */
#define OTA_APP_MIN           65536u
#define OTA_APP_MAX           (8u * 1024u * 1024u)

/* Margen antes de dar por buena la version nueva: asociada + IP no basta, hay
 * que seguir enganchado unos segundos (una asociacion que se cae a los 200 ms
 * no ha demostrado nada). */
#define OTA_VALIDAR_ESPERA_MS 8000
#define OTA_VALIDAR_POLL_MS   1000

/* Margen entre "instalado" y el reinicio: lo justo para que la pantalla pinte
 * "Listo, reiniciando" y el usuario lo lea. */
#define OTA_REINICIO_MS       3000

/* Cada cuanto mira sola si la P4 tiene una version nueva. 60 s es lo que pidio
 * el dueño: subir el .bin desde el movil y que la pantalla se entere sin ir a
 * tocarla. Ni mas seguido (la P4 tambien atiende los viajes) ni menos (se
 * haria esperar a quien acaba de subir el fichero). */
#define OTA_AUTO_PERIODO_MS   60000

/* El interruptor de la red de seguridad. Va en NVS y NO en una pantalla nueva:
 * el dueño lo pidio como "por si no quiero reinicios sin avisar". Se lee una
 * vez al arrancar; con 0 desaparece el temporizador y queda solo el boton. */
#define OTA_AUTO_NS           "ota"
#define OTA_AUTO_CLAVE        "ota_auto"

/* ── Estado compartido con la UI ───────────────────────────────────────────
 *
 * Un cerrojo de interrupciones (portMUX) y no un mutex: las copias son de 100
 * bytes y duran microsegundos, y asi no hay que crear ni destruir nada ni
 * preocuparse del orden de arranque. La regla: DENTRO del cerrojo solo se
 * copia; montar textos (snprintf) se hace fuera. */
static portMUX_TYPE       s_mtx = portMUX_INITIALIZER_UNLOCKED;
static ota_rx_vista_t     s_vista;       /* lo que lee la UI */
static bool               s_trabajando;  /* hay una tarea de red viva */

/* Lo que anuncio la P4 y hay que instalar. Lo escribe tarea_comprobar y lo lee
 * tarea_actualizar; no hace falta cerrojo entre las dos porque SOLO hay una
 * tarea de red a la vez (s_trabajando). En manual, la segunda se lanza cuando la
 * UI confirma HAY_NUEVA; en automatico, tarea_comprobar deja DESCARGANDO y
 * arranca ella misma la descarga sin soltar el turno. Se guarda aparte de
 * s_vista porque la UI no tiene por que ver el sha. */
typedef struct {
    uint32_t bytes;
    char     ver[16];
    char     sha[65];
} oferta_t;
static oferta_t s_oferta;

/* Lo avisa la tarea de LVGL (editor/dialogo abierto). Es un atomico porque lo
 * leen el temporizador automatico y las tareas de red, que NO son la de LVGL:
 * nunca se tocan objetos de LVGL desde aqui. */
static _Atomic bool s_ui_ocupada;

/* Interruptor leido de NVS en ota_rx_arranque(). Se crea el temporizador
 * DESPUES de fijarlo, asi que la tarea de esp_timer ya lo ve definitivo. */
static bool s_auto_activo = true;

/* Ultimo aviso del automatico. En modo manual no se usa. Sirve para no repetir
 * el mismo WARN cada 60 s (una P4 sin fichero para esta pantalla, por ejemplo);
 * la primera vez se cuenta y las siguientes quedan en DEBUG. */
static char s_auto_ultimo_aviso[96];

/* El temporizador automatico se crea al final del arranque; la definicion esta
 * en la seccion "AUTOMATICO", al final del fichero. */
static void auto_arrancar(void);
/* tarea_comprobar arranca la descarga directamente en modo automatico. */
static void tarea_actualizar(void *arg);

/* ── Las decisiones, probadas sin placa (ver test/ota_version.sh) ───────────
 *
 */

/* INICIO BLOQUE PRUEBA (lo saca test/ota_version.sh y lo compila en el PC)
 * Estas funciones NO tocan nada de ESP-IDF a proposito: el test las saca de
 * aqui tal cual. Si se metiera un ESP_LOG dentro, el test dejaria de compilar;
 * y si se cambia la logica, el test lo canta. */

/* Valor de un digito hexadecimal, o -1 si no lo es. */
static int nibble_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "vX.Y" o "vX.Y.Z" (una "v" delante, digitos y puntos) -> tres numeros.
 *
 * Devuelve false con cualquier otra cosa ("v?", "v1.30-dirty", ""): un build
 * sin tag no se puede comparar con nada, y comparar texto daria "v1.9" >
 * "v1.10", que es justo al reves. */
static bool version_partir(const char *v, unsigned *a, unsigned *b, unsigned *c)
{
    if (!v || (*v != 'v' && *v != 'V')) return false;
    v++;
    unsigned num[3] = { 0, 0, 0 };
    int campo = 0;
    bool hay_digito = false;
    for (; *v; v++) {
        if (*v >= '0' && *v <= '9') {
            if (num[campo] > 100000u) return false;   /* absurdo: no es una version */
            num[campo] = num[campo] * 10u + (unsigned)(*v - '0');
            hay_digito = true;
        } else if (*v == '.' && campo < 2 && hay_digito) {
            campo++;
            hay_digito = false;
        } else {
            return false;
        }
    }
    if (!hay_digito) return false;
    *a = num[0];
    *b = num[1];
    *c = num[2];
    return true;
}

bool ota_rx_version_es_mas_nueva(const char *nueva, const char *actual)
{
    unsigned na, nb, nc, aa, ab, ac;
    if (!version_partir(nueva, &na, &nb, &nc)) return false;
    if (!version_partir(actual, &aa, &ab, &ac)) return false;
    if (na != aa) return na > aa;
    if (nb != ab) return nb > ab;
    return nc > ac;
}

bool ota_rx_version_aceptable(const char *anunciada, const char *actual,
                              const char *embebida)
{
    /* Las TRES cosas y en este orden: el fichero tiene que ser el anunciado (si
     * no, la P4 sirvio otro) y ademas mas nuevo que el que corre. "Igual" no se
     * instala (no arregla nada y gasta un hueco de flash) y "mas viejo" tampoco
     * (quitaria lo que ya funcionaba). */
    if (!anunciada || !embebida) return false;
    if (strcmp(anunciada, embebida) != 0) return false;
    return ota_rx_version_es_mas_nueva(anunciada, actual);
}

/* 64 digitos hex -> 32 bytes. false si no son exactamente 64 o hay algo que no
 * es hex: asi el sha anunciado se valida ANTES de descargar 2 MB. */
static bool sha_hex_a_bytes(const char *hex, uint8_t out[32])
{
    if (!hex || strlen(hex) != 64) return false;
    for (int i = 0; i < 32; i++) {
        int hi = nibble_hex(hex[2 * i]);
        int lo = nibble_hex(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

bool ota_rx_modelo_coincide(const char *embebido, const char *modelo)
{
    /* El nombre del proyecto va DENTRO de la cabecera de la app
     * (esp_app_desc_t.project_name) y es lo unico que dice de que pantalla es el
     * fichero cuando las dos llevan la MISMA version. Sin esta comprobacion, un
     * .bin cruzado en la P4 (el de la 3,5" publicado como cabina5, con el mismo
     * numero de version) se instalaria y la 5" se quedaria con un firmware de
     * otra pantalla: otra UI y otro driver de panel. */
    if (!embebido || !modelo) return false;
    return strcmp(embebido, modelo) == 0;
}

bool ota_rx_sha_coincide(const uint8_t calculado[32], const char *hex64)
{
    uint8_t esperado[32];
    if (!calculado || !sha_hex_a_bytes(hex64, esperado)) return false;
    /* Se compara en BYTES, no el texto: da igual que la P4 mande el sha en
     * mayusculas o en minusculas, y un solo digito distinto (un byte que se
     * corrompio por el camino) rechaza el fichero. Se miran los 32 sin cortar
     * en el primero que falle: el tiempo no depende de donde este el fallo. */
    uint8_t dif = 0;
    for (int i = 0; i < 32; i++) dif |= (uint8_t)(calculado[i] ^ esperado[i]);
    return dif == 0;
}

/* Las condiciones del automatico, ya masticadas: la red, el viaje y la UI se
 * consultan fuera (ESP-IDF/LVGL no se pueden meter en un test de host) y aqui
 * solo queda la DECISION. Es justo la parte que se puede equivocar en silencio:
 * reiniciar la pantalla en mitad de un viaje o de una edicion, o preguntar a la
 * P4 cada minuto sin red. */
typedef struct {
    bool auto_activo;  /* NVS ota/ota_auto */
    bool red;          /* asociada a la P4 Y con IP */
    bool viaje;        /* viaje o apunte abierto */
    bool trabajando;   /* comprobacion o descarga en marcha */
    bool ui_ocupada;   /* editor a pantalla completa o dialogo abierto */
} ota_rx_condiciones_t;

/* ¿Toca preguntar a la P4 ahora? TODAS las condiciones, y en este orden: sin
 * red o con viaje no se insiste (se mira al minuto siguiente, sin llenar el
 * log), y con algo en marcha o el usuario escribiendo no se interrumpe. */
static bool ota_rx_auto_toca_comprobar(const ota_rx_condiciones_t *c)
{
    if (!c) return false;
    if (!c->auto_activo) return false;
    if (!c->red) return false;
    if (c->viaje) return false;
    if (c->trabajando) return false;
    if (c->ui_ocupada) return false;
    return true;
}

/* Justo antes de descargar hay que volver a mirar: la respuesta de la P4 tarda
 * hasta 8 s y en ese rato puede haber empezado un viaje, o el usuario puede
 * haber abierto el editor o un dialogo. La red y "trabajando" NO se miran aqui
 * a proposito: esta funcion corre DENTRO de la comprobacion (trabajando es
 * true por definicion) y la respuesta de la P4 acaba de demostrar la red. */
static bool ota_rx_auto_puede_instalar(const ota_rx_condiciones_t *c)
{
    if (!c) return false;
    if (!c->auto_activo) return false;
    if (c->viaje) return false;
    if (c->ui_ocupada) return false;
    return true;
}

/* FIN BLOQUE PRUEBA */

/* ── Lector MINIMO de JSON ──────────────────────────────────────────────────
 *
 * La P4 manda UNA linea con esta forma exacta, que es contrato:
 *   {"cabina5":{"ver":"v1.30","bytes":2214608,"sha256":"<64 hex>"},...}
 *
 * Un lector completo (cJSON) seria un componente mas para leer TRES campos de
 * un objeto de un nivel. Aqui se busca el objeto del modelo y, DENTRO de sus
 * llaves, cada campo. Si algo no cuadra se dice "rara" y NO se actualiza: es
 * mucho mejor no actualizar que instalar un fichero que no es el que se cree.
 *
 * Se respetan las comillas: una llave o una coma DENTRO de una cadena no
 * cuentan. Hoy no hay cadenas con llaves, pero un mensaje de error de la P4
 * podria llevarlas y descuadraria el recorte del objeto. */

/* Puntero al VALOR de "clave":..., dentro de [ini, fin). NULL si no esta. */
static const char *json_valor(const char *ini, const char *fin, const char *clave)
{
    char aguja[40];
    int n = snprintf(aguja, sizeof aguja, "\"%s\"", clave);
    if (n <= 0 || (size_t)n >= sizeof aguja) return NULL;

    const char *p = ini;
    while (p && p < fin) {
        p = strstr(p, aguja);
        if (!p || p >= fin) return NULL;
        const char *q = p + strlen(aguja);
        while (q < fin && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
        if (q < fin && *q == ':') {
            q++;
            while (q < fin && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
            return (q < fin) ? q : NULL;
        }
        /* La clave aparecia dentro de un texto, no como campo: se sigue
         * buscando detras de ella. */
        p = q > p ? q : p + strlen(aguja);
    }
    return NULL;
}

/* Recorta el objeto de "clave":{...}. false si la clave no esta, es null (un
 * modelo sin fichero puede venir asi, y NO es un error de red) o no es un
 * objeto. */
static bool json_objeto(const char *json, const char *clave,
                        const char **ini, const char **fin)
{
    const char *p = json_valor(json, json + strlen(json), clave);
    if (!p) return false;
    if (strncmp(p, "null", 4) == 0) return false;
    if (*p != '{') return false;

    int nivel = 0;
    bool cadena = false;
    for (const char *q = p; *q; q++) {
        char c = *q;
        if (cadena) {
            if (c == '\\' && q[1]) { q++; continue; }
            if (c == '"') cadena = false;
        } else if (c == '"') {
            cadena = true;
        } else if (c == '{') {
            nivel++;
        } else if (c == '}') {
            nivel--;
            if (nivel == 0) { *ini = p; *fin = q; return true; }
        }
    }
    return false;
}

/* Copia el texto de "clave":"..." (sin escapes: el contrato no los usa, y si
 * aparecieran es que no es la respuesta esperada). */
static bool json_cadena(const char *ini, const char *fin, const char *clave,
                        char *out, size_t n)
{
    const char *v = json_valor(ini, fin, clave);
    if (!v || *v != '"' || n == 0) return false;
    size_t k = 0;
    for (const char *q = v + 1; q < fin && *q; q++) {
        if (*q == '\\') return false;
        if (*q == '"') {
            if (k == 0 || k >= n) return false;
            out[k] = '\0';
            return true;
        }
        if (k + 1 >= n) return false;
        out[k++] = *q;
    }
    return false;
}

/* Numero entero sin decimales ni signo. Se rechaza a partir de OTA_APP_MAX. */
static bool json_entero(const char *ini, const char *fin, const char *clave, uint32_t *out)
{
    const char *v = json_valor(ini, fin, clave);
    if (!v || *v < '0' || *v > '9') return false;
    uint64_t x = 0;
    for (const char *q = v; q < fin && *q >= '0' && *q <= '9'; q++) {
        x = x * 10u + (unsigned)(*q - '0');
        if (x > OTA_APP_MAX) return false;
    }
    *out = (uint32_t)x;
    return true;
}

/* ── Estado ──────────────────────────────────────────────────────────────── */

/* Monta la vista fuera del cerrojo y la asigna dentro (ver la regla de s_mtx). */
static void fijar_estado(ota_rx_estado_t est, int pct, const char *motivo)
{
    ota_rx_vista_t v;
    taskENTER_CRITICAL(&s_mtx);
    v = s_vista;
    taskEXIT_CRITICAL(&s_mtx);

    v.estado = est;
    v.pct = (pct < 0) ? 0 : (pct > 100) ? 100 : pct;
    if (motivo) snprintf(v.motivo, sizeof v.motivo, "%s", motivo);
    else        v.motivo[0] = '\0';

    taskENTER_CRITICAL(&s_mtx);
    s_vista = v;
    taskEXIT_CRITICAL(&s_mtx);
}

static void fijar_progreso(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    taskENTER_CRITICAL(&s_mtx);
    if (s_vista.pct != pct) {
        s_vista.pct = pct;
        s_vista.estado = OTA_RX_DESCARGANDO;
    }
    taskEXIT_CRITICAL(&s_mtx);
}

static void terminar_trabajo(void)
{
    taskENTER_CRITICAL(&s_mtx);
    s_trabajando = false;
    taskEXIT_CRITICAL(&s_mtx);
}

void ota_rx_estado(ota_rx_vista_t *out)
{
    if (!out) return;
    taskENTER_CRITICAL(&s_mtx);
    *out = s_vista;
    taskEXIT_CRITICAL(&s_mtx);
}

/* Ver ota_rx.h: lo llama la tarea de LVGL cada 500 ms. */
void ota_rx_ui_ocupada(bool ocupada)
{
    atomic_store(&s_ui_ocupada, ocupada);
}

static bool ui_ocupada_ahora(void)
{
    return atomic_load(&s_ui_ocupada);
}

static bool trabajando_ahora(void)
{
    bool t;
    taskENTER_CRITICAL(&s_mtx);
    t = s_trabajando;
    taskEXIT_CRITICAL(&s_mtx);
    return t;
}

/* Un aviso del automatico: WARN la primera vez y DEBUG si se repite igual.
 * Sin esto, una P4 sin fichero para esta pantalla llenaria el log cada minuto,
 * que es justo lo que el dueño pidio evitar ("que no sature"). La UI sigue
 * viendo el ERROR; esto solo calla el log. */
static void auto_aviso(const char *texto)
{
    if (!texto) texto = "";
    if (strcmp(s_auto_ultimo_aviso, texto) == 0) {
        ESP_LOGD(TAG, "auto: sigue igual: %s", texto);
        return;
    }
    snprintf(s_auto_ultimo_aviso, sizeof s_auto_ultimo_aviso, "%s", texto);
    ESP_LOGW(TAG, "auto: %s", texto);
}

/* La comprobacion salio bien (o instalo): el proximo fallo vuelve a contarse. */
static void auto_aviso_limpiar(void)
{
    s_auto_ultimo_aviso[0] = '\0';
}

/* Un viaje abierto, o un apunte sin cerrar. Se miran las dos cosas: un apunte
 * abierto es alguien escribiendo en el cuaderno justo ahora, y reiniciar la
 * pantalla en ese momento es perder lo que estaba haciendo. */
static bool hay_viaje_abierto(void)
{
    return salida_hay() || salida_eventos_abiertos() > 0;
}

/* ── Red ─────────────────────────────────────────────────────────────────── */

/* Credenciales del portal y la cabecera "Authorization: Basic ..." ya montada.
 * Se piden en cada intento y NO se cachean: si el usuario las corrige en Ajustes
 * entre un intento y el siguiente, con una copia en RAM seguiria fallando sin
 * decir por que (el mismo motivo que en p4_api.c). */
static bool creds_cabecera(char *cab, size_t n)
{
    char user[33] = {0}, pass[65] = {0};
    if (!cab || n == 0) return false;
    cab[0] = '\0';
    if (!p4_creds_cargar(user, sizeof user, pass, sizeof pass)) return false;
    p4_api_cabecera_basic(user, pass, cab, n);
    return cab[0] != '\0';
}

/* GET corto con la cabecera puesta a mano y el cuerpo en 'cuerpo' (NUL al
 * final). Deja el codigo HTTP en 'estado' (0 si ni conecto) y devuelve true solo
 * con 2xx y el cuerpo leido entero. */
static bool http_get_corto(const char *url, char *cuerpo, size_t n, int *estado)
{
    *estado = 0;
    cuerpo[0] = '\0';

    char cab[192];
    bool hay_creds = creds_cabecera(cab, sizeof cab);

    esp_http_client_config_t cfg = {
        .url            = url,
        .method         = HTTP_METHOD_GET,
        .timeout_ms     = OTA_TIMEOUT_INFO_MS,
        /* Sin auth_type: la cabecera va a mano, igual que en p4_api.c. Con
         * HTTP_AUTH_TYPE_BASIC el cliente de IDF intenta resolver el reto 401 de
         * la P4 y NO LO HACE (devuelve ESP_ERR_HTTP_MAX_REDIRECT). */
        .auth_type      = HTTP_AUTH_TYPE_NONE,
        .keep_alive_enable = false,
        .buffer_size    = 1024,
        .buffer_size_tx = 512,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    if (hay_creds) esp_http_client_set_header(c, "Authorization", cab);

    bool ok = false;
    size_t copiado = 0;
    if (esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        *estado = esp_http_client_get_status_code(c);
        if (*estado >= 200 && *estado < 300) {
            bool error = false;
            for (;;) {
                int r = esp_http_client_read(c, cuerpo + copiado,
                                             (int)(n - 1 - copiado));
                if (r < 0) { error = true; break; }
                if (r == 0) break;
                copiado += (size_t)r;
                if (copiado >= n - 1) break;   /* no cabe: no es nuestra respuesta */
            }
            cuerpo[copiado] = '\0';
            ok = !error;
        }
        esp_http_client_close(c);
    }
    esp_http_client_cleanup(c);

    /* Truncado = no es la respuesta del contrato. Mejor decir "rara" que
     * interpretar medio JSON. */
    if (ok && copiado >= n - 1) ok = false;
    return ok;
}

/* Pregunta la version de NUESTRO modelo. Devuelve false y deja 'motivo'. */
static bool pedir_info(char *ver, size_t nver, uint32_t *bytes, char *sha, size_t nsha,
                       char *motivo, size_t nmotivo)
{
    char url[96];
    snprintf(url, sizeof url, "%s/api/pantalla/info", p4_api_host());

    char json[OTA_INFO_MAX];
    int estado = 0;
    if (!http_get_corto(url, json, sizeof json, &estado)) {
        if (estado == 0)        snprintf(motivo, nmotivo, "Sin red con la P4");
        else if (estado == 401) snprintf(motivo, nmotivo, "Credenciales del portal (401)");
        else                    snprintf(motivo, nmotivo, "La P4 responde %d", estado);
        return false;
    }

    const char *oi = NULL, *of = NULL;
    if (!json_objeto(json, OTA_MODELO, &oi, &of)) {
        /* null o ausente: la P4 esta, pero no tiene fichero para esta pantalla.
         * No es un fallo de red y se dice distinto. */
        snprintf(motivo, nmotivo, "La P4 no tiene esta pantalla");
        return false;
    }
    if (!json_cadena(oi, of, "ver", ver, nver) ||
        !json_entero(oi, of, "bytes", bytes) ||
        !json_cadena(oi, of, "sha256", sha, nsha)) {
        snprintf(motivo, nmotivo, "Respuesta de la P4 rara");
        return false;
    }

    /* Se valida TODO antes de enseñar nada y, sobre todo, antes de tocar la
     * flash: si la version no se puede comparar o el sha no son 64 hex, es que
     * esto no es la respuesta del contrato. */
    unsigned a, b, c;
    if (!version_partir(ver, &a, &b, &c)) {
        snprintf(motivo, nmotivo, "Version anunciada rara");
        return false;
    }
    uint8_t basura[32];
    if (!sha_hex_a_bytes(sha, basura)) {
        snprintf(motivo, nmotivo, "sha256 anunciado raro");
        return false;
    }
    if (*bytes < OTA_APP_MIN) {
        snprintf(motivo, nmotivo, "Tamano anunciado raro");
        return false;
    }
    return true;
}

/* ── Tarea: comprobar la version ─────────────────────────────────────────── */

static void tarea_comprobar(void *arg)
{
    /* El argumento dice quien la lanzo: NULL = el boton de Ajustes (pregunta y
     * deja HAY_NUEVA), no NULL = el temporizador automatico (si hay nueva, la
     * instala sin preguntar). */
    bool automatica = (arg != NULL);
    char motivo[96] = "";
    char ver[16] = "";
    char sha[65] = "";
    uint32_t bytes = 0;

    const esp_app_desc_t *app = esp_app_get_description();
    const char *actual = (app && app->version[0]) ? app->version : "v?";
    unsigned a, b, c;

    if (!pedir_info(ver, sizeof ver, &bytes, sha, sizeof sha,
                    motivo, sizeof motivo)) {
        if (automatica) auto_aviso(motivo);
        else            ESP_LOGE(TAG, "no puedo comprobar la version: %s", motivo);
        fijar_estado(OTA_RX_ERROR, 0, motivo);
        terminar_trabajo();
        vTaskDelete(NULL);
        return;
    }
    if (automatica) {
        ESP_LOGD(TAG, "auto: la P4 ofrece %s (%" PRIu32 " bytes) y corre %s",
                 ver, bytes, actual);
    } else {
        ESP_LOGI(TAG, "la P4 ofrece %s (%" PRIu32 " bytes) y corre %s",
                 ver, bytes, actual);
    }

    /* Si la version instalada no se puede comparar (build sin tags: "v?"), no se
     * actualiza: no hay forma de saber si la de la P4 es mas nueva. */
    if (!version_partir(actual, &a, &b, &c)) {
        if (automatica) auto_aviso("Version instalada rara (build sin tag)");
        fijar_estado(OTA_RX_ERROR, 0, "Version instalada rara (build sin tag)");
        terminar_trabajo();
        vTaskDelete(NULL);
        return;
    }

    if (!ota_rx_version_es_mas_nueva(ver, actual)) {
        auto_aviso_limpiar();
        if (automatica)
            ESP_LOGD(TAG, "auto: estas al dia (%s)", actual);
        else
            ESP_LOGI(TAG, "estas al dia: la P4 tiene %s y corre %s", ver, actual);
        fijar_estado(OTA_RX_AL_DIA, 0, "");
        terminar_trabajo();
        vTaskDelete(NULL);
        return;
    }

    /* Hay version nueva. Primero se monta todo fuera del cerrojo. */
    oferta_t of;
    of.bytes = bytes;
    snprintf(of.ver, sizeof of.ver, "%s", ver);
    snprintf(of.sha, sizeof of.sha, "%s", sha);

    ota_rx_vista_t v = { 0 };
    v.pct = 0;
    snprintf(v.ofrecida, sizeof v.ofrecida, "%s", ver);
    v.automatica = automatica;

    if (automatica) {
        /* LA VENTANA: entre la respuesta de la P4 y la descarga pueden pasar
         * segundos. Se vuelve a mirar viaje/editor/dialogo, y si algo se ha
         * abierto NO se instala: se deja QUIETO y se vuelve a mirar al minuto
         * siguiente. No se le pregunta nada al usuario (no hay dialogo). */
        ota_rx_condiciones_t cond = {
            .auto_activo = s_auto_activo,
            .red         = true,          /* la P4 acaba de responder */
            .viaje       = hay_viaje_abierto(),
            .trabajando  = true,          /* esta tarea */
            .ui_ocupada  = ui_ocupada_ahora(),
        };
        if (!ota_rx_auto_puede_instalar(&cond)) {
            ESP_LOGW(TAG, "auto: hay %s; dejo %s para la proxima comprobacion",
                     cond.viaje ? "un viaje abierto" : "un editor o dialogo abierto",
                     ver);
            fijar_estado(OTA_RX_QUIETO, 0, "");
            terminar_trabajo();
            vTaskDelete(NULL);
            return;
        }

        /* Se pasa a DESCARGANDO en el MISMO cerrojo y s_trabajando se queda a
         * true: la tarea de descarga hereda el turno. Asi el boton manual no
         * puede colarse en el hueco y arrancar una segunda descarga. */
        v.estado = OTA_RX_DESCARGANDO;
        taskENTER_CRITICAL(&s_mtx);
        s_oferta = of;
        s_vista = v;
        taskEXIT_CRITICAL(&s_mtx);

        auto_aviso_limpiar();
        if (xTaskCreate(tarea_actualizar, "ota_upd", 8192, NULL, 3, NULL) != pdPASS) {
            terminar_trabajo();
            fijar_estado(OTA_RX_ERROR, 0, "Sin memoria para actualizar");
        } else {
            ESP_LOGI(TAG, "version nueva %s (instalada %s): la instalo sola, sin preguntar",
                     ver, actual);
        }
        vTaskDelete(NULL);
        return;
    }

    /* Manual: se publica HAY_NUEVA y, en el MISMO cerrojo, los datos de la
     * oferta. Asi, cuando la UI vea HAY_NUEVA, s_oferta ya esta completa. */
    v.estado = OTA_RX_HAY_NUEVA;
    taskENTER_CRITICAL(&s_mtx);
    s_oferta = of;
    s_vista = v;
    taskEXIT_CRITICAL(&s_mtx);

    ESP_LOGI(TAG, "hay version nueva: %s (instalada %s)", ver, actual);
    terminar_trabajo();
    vTaskDelete(NULL);
}

/* ── Tarea: descargar e instalar ─────────────────────────────────────────── */

/* El buffer de la descarga. En PSRAM si la placa la tiene: 4 KB de RAM interna
 * son los que le hacen falta al Wi-Fi o a LVGL para no quedarse sin sitio, y el
 * contenido no necesita ninguna prisa. Si no hay PSRAM, se cae a la interna. */
static uint8_t *reservar_buffer(size_t n)
{
    uint8_t *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!p) p = malloc(n);
    return p;
}

/* Instala la version anunciada. Todo por un solo camino de salida ('salir')
 * para que ninguna fuga ni ningun handle abierto se quede por el camino: esta
 * funcion puede fallar por diez sitios distintos y la flash tiene que quedar
 * igual de sana en todos. */
/* ── EL TASK WDT DURANTE LA VENTANA DE FLASH ──────────────────────────────
 *
 * Durante el borrado y la escritura de la particion la cache se apaga y NINGUNA
 * tarea que ejecute desde la flash puede latir: ni app_main ni el supervisor de
 * LVGL, que son las dos que estan suscritas al Task WDT (10 s, CONFIG_ESP_TASK_
 * WDT_PANIC=y). La OTA tarda mas que eso -- el borrado del hueco de 4 MB son
 * varios segundos y escribir 2,2 MB por Wi-Fi, otros quince --, asi que el WDT
 * reiniciaba la placa A MITAD de la actualizacion. Visto en la placa el
 * 17-oct-2026 probando la OTA desde el movil ("se reinicio sola, watchdog").
 *
 * No se desuscriben las tareas una a una (habria que conocer sus handles y en
 * que nucleo corre cada una, y una OTA es puntual): se alarga el plazo del WDT
 * mientras dura la ventana de flash y se devuelve al terminar, pase lo que pase.
 * El plazo de 90 s es de sobra para el peor caso (tarjeta lenta + Wi-Fi malo) y,
 * si de verdad se cuelga algo, el WDT sigue saltando, solo que mas tarde. */
#define OTA_TWDT_FLASH_MS 90000
static void ota_wdt_ventana(bool flash)
{
    esp_task_wdt_config_t cfg = {
        .timeout_ms     = flash ? OTA_TWDT_FLASH_MS : 10000,
        .idle_core_mask = 0,        /* como esta configurado: las IDLE no se vigilan */
        .trigger_panic  = true,     /* igual que sdkconfig */
    };
    esp_err_t e = esp_task_wdt_reconfigure(&cfg);
    if (e != ESP_OK)
        ESP_LOGW(TAG, "no he podido %s el Task WDT (%s): la flash puede reiniciar "
                      "la placa a mitad", flash ? "alargar" : "devolver",
                 esp_err_to_name(e));
}

static void tarea_actualizar(void *arg)
{
    (void)arg;

    char motivo[96] = "";
    esp_ota_handle_t ota = 0;
    esp_http_client_handle_t cli = NULL;
    uint8_t *buf = NULL;
    mbedtls_sha256_context sha;
    bool sha_iniciado = false;
    bool ota_abierta = false;
    bool cli_abierto = false;
    bool wdt_abierto = false;
    bool diferido = false;
    uint32_t escrito = 0;

    /* Copia local de lo anunciado (ver el comentario de s_oferta). */
    uint32_t esperado;
    char ver[16];
    char sha_hex[65];
    bool automatica;
    taskENTER_CRITICAL(&s_mtx);
    esperado = s_oferta.bytes;
    memcpy(ver, s_oferta.ver, sizeof ver);
    memcpy(sha_hex, s_oferta.sha, sizeof sha_hex);
    automatica = s_vista.automatica;
    taskEXIT_CRITICAL(&s_mtx);

    /* Ultima comprobacion ANTES de tocar la flash: la UI ya pregunto, pero entre
     * el "si" y esta linea pueden pasar segundos y puede haber empezado un
     * viaje. Es el mismo codigo que devuelve ota_rx_actualizar(), pero aqui es
     * el que manda: es el que corre en la tarea. */
    if (hay_viaje_abierto()) {
        snprintf(motivo, sizeof motivo, "Termina el viaje antes");
        diferido = automatica;   /* en automatico no es un fallo: se deja pasar */
        goto salir;
    }
    /* Y la ventana del automatico: entre la comprobacion y esta tarea el usuario
     * puede haber abierto el editor o un dialogo. Se mira otra vez aqui, que es
     * el ultimo momento antes de tocar la flash. */
    if (automatica && ui_ocupada_ahora()) {
        snprintf(motivo, sizeof motivo, "Hay un editor o un dialogo abierto");
        diferido = true;
        goto salir;
    }

    const esp_partition_t *parte = esp_ota_get_next_update_partition(NULL);
    if (!parte) {
        snprintf(motivo, sizeof motivo, "No hay particion de actualizacion");
        goto salir;
    }
    if (esperado == 0 || esperado > parte->size) {
        snprintf(motivo, sizeof motivo, "El fichero no cabe en la particion");
        goto salir;
    }

    buf = reservar_buffer(OTA_BUFFER);
    if (!buf) {
        snprintf(motivo, sizeof motivo, "Sin memoria para la descarga");
        goto salir;
    }

    char cab[192];
    bool hay_creds = creds_cabecera(cab, sizeof cab);

    char url[128];
    snprintf(url, sizeof url, "%s:8081/api/pantalla/bin?modelo=%s",
             p4_api_host(), OTA_MODELO);

    esp_http_client_config_t cfg = {
        .url            = url,
        .method         = HTTP_METHOD_GET,
        .timeout_ms     = OTA_TIMEOUT_BIN_MS,
        .auth_type      = HTTP_AUTH_TYPE_NONE,   /* la cabecera va a mano */
        .keep_alive_enable = false,
        .buffer_size    = 1024,
        .buffer_size_tx = 512,
    };
    cli = esp_http_client_init(&cfg);
    if (!cli) {
        snprintf(motivo, sizeof motivo, "Sin memoria para el cliente HTTP");
        goto salir;
    }
    if (hay_creds) esp_http_client_set_header(cli, "Authorization", cab);

    if (esp_http_client_open(cli, 0) != ESP_OK) {
        snprintf(motivo, sizeof motivo, "Sin red con la P4");
        goto salir;
    }
    cli_abierto = true;
    esp_http_client_fetch_headers(cli);
    int estado = esp_http_client_get_status_code(cli);
    if (estado == 401) {
        snprintf(motivo, sizeof motivo, "Credenciales del portal (401)");
        goto salir;
    }
    if (estado < 200 || estado >= 300) {
        snprintf(motivo, sizeof motivo, "La P4 responde %d", estado);
        goto salir;
    }
    /* Si la P4 dice un tamaño distinto del que anuncio, se corta ANTES de
     * escribir: aunque luego el sha lo cazaria, no se gasta el tiempo (ni el
     * desgaste de flash) en un fichero que ya sabemos que no es. */
    int64_t largo = esp_http_client_get_content_length(cli);
    if (largo > 0 && largo != (int64_t)esperado) {
        snprintf(motivo, sizeof motivo, "La P4 sirve %" PRId64 " bytes, no %" PRIu32,
                 largo, esperado);
        goto salir;
    }

    /* esp_ota_begin borra la particion que NO se esta usando. Si falla, la
     * version que corre no se ha tocado. */
    /* La flash se queda sin cache varios segundos: ver ota_wdt_ventana(). */
    ota_wdt_ventana(true);
    wdt_abierto = true;

    esp_err_t err = esp_ota_begin(parte, esperado, &ota);
    if (err != ESP_OK) {
        snprintf(motivo, sizeof motivo, "No se pudo preparar la particion (%s)",
                 esp_err_to_name(err));
        goto salir;
    }
    ota_abierta = true;

    mbedtls_sha256_init(&sha);
    sha_iniciado = true;
    if (mbedtls_sha256_starts(&sha, 0) != 0) {
        snprintf(motivo, sizeof motivo, "Sin memoria para el sha256");
        goto salir;
    }

    fijar_progreso(0);
    for (;;) {
        int leido = esp_http_client_read(cli, (char *)buf, OTA_BUFFER);
        if (leido < 0) {
            snprintf(motivo, sizeof motivo, "Se corto la descarga");
            goto salir;
        }
        if (leido == 0) break;

        /* Mas bytes que los anunciados = la P4 sirve otro fichero (o el mismo
         * con otro tamaño). Se para aqui y no se escribe ni uno mas. */
        if ((uint32_t)leido > esperado - escrito) {
            snprintf(motivo, sizeof motivo, "El fichero es mas grande de lo anunciado");
            goto salir;
        }

        err = esp_ota_write(ota, buf, (size_t)leido);
        if (err != ESP_OK) {
            snprintf(motivo, sizeof motivo, "Fallo al escribir la flash (%s)",
                     esp_err_to_name(err));
            goto salir;
        }
        mbedtls_sha256_update(&sha, buf, (size_t)leido);
        escrito += (uint32_t)leido;
        fijar_progreso((int)(((uint64_t)escrito * 100u) / esperado));
    }

    if (escrito != esperado) {
        snprintf(motivo, sizeof motivo, "Fichero corto (%" PRIu32 " de %" PRIu32 " bytes)",
                 escrito, esperado);
        goto salir;
    }

    uint8_t hash[32];
    if (mbedtls_sha256_finish(&sha, hash) != 0) {
        snprintf(motivo, sizeof motivo, "No pude cerrar el sha256");
        goto salir;
    }
    sha_iniciado = false;
    if (!ota_rx_sha_coincide(hash, sha_hex)) {
        snprintf(motivo, sizeof motivo, "sha256 no cuadra");
        goto salir;
    }

    /* esp_ota_end valida la imagen ENTERA (cabecera, segmentos, checksum). Si
     * esto pasa, el fichero esta bien escrito en la flash. Libera el handle
     * siempre, salga bien o mal. */
    err = esp_ota_end(ota);
    ota_abierta = false;
    if (err != ESP_OK) {
        snprintf(motivo, sizeof motivo, "La imagen no es valida (%s)",
                 esp_err_to_name(err));
        goto salir;
    }

    /* Y la ultima: que lo que se ha escrito DIGA la version que la P4 anuncia y
     * que sea mas nueva que la que corre. Esto es lo que impide que una P4 con
     * un fichero viejo (o cruzado con el de la otra pantalla) deje esta placa
     * con una version anterior sin enterarse. */
    esp_app_desc_t embebida;
    if (esp_ota_get_partition_description(parte, &embebida) != ESP_OK) {
        snprintf(motivo, sizeof motivo, "No puedo leer la version del fichero");
        goto salir;
    }
    /* Primero, que el fichero sea de ESTA pantalla. Va antes que la version
     * porque es el error mas gordo: un firmware de la otra cabina no arranca
     * aqui (otro panel, otra UI) y dejaria la pantalla muerta. */
    if (!ota_rx_modelo_coincide(embebida.project_name, OTA_MODELO)) {
        snprintf(motivo, sizeof motivo, "El fichero es de otra pantalla (%s)",
                 embebida.project_name);
        goto salir;
    }

    const esp_app_desc_t *app = esp_app_get_description();
    const char *actual = (app && app->version[0]) ? app->version : "v?";
    if (!ota_rx_version_aceptable(ver, actual, embebida.version)) {
        if (strcmp(ver, embebida.version) != 0)
            snprintf(motivo, sizeof motivo, "El fichero trae %s, no %s",
                     embebida.version, ver);
        else
            snprintf(motivo, sizeof motivo, "Version no mas nueva (el fichero es %s)",
                     embebida.version);
        goto salir;
    }

    err = esp_ota_set_boot_partition(parte);
    if (err != ESP_OK) {
        snprintf(motivo, sizeof motivo, "No pude marcar el arranque (%s)",
                 esp_err_to_name(err));
        goto salir;
    }

    ESP_LOGI(TAG, "actualizacion lista: %s -> %s en '%s' (0x%08" PRIx32 "). "
                  "Ya se puede arrancar de ahi; reinicio en %d s",
             actual, ver, parte->label, parte->address, OTA_REINICIO_MS / 1000);
    fijar_estado(OTA_RX_LISTO, 100, "");

    /* ESTE reinicio es a proposito y NO es un fallo: sin esta marca, a la
     * vuelta la tarjeta de Ajustes diria "Se reinicio sola" en rojo (el chip
     * solo sabe decir "reinicio por software"). */
    diag_reset_marcar_sw_bueno("actualizacion de pantalla");

    /* El reinicio se pide DESDE ESTA TAREA, nunca desde la de LVGL: esp_restart()
     * alli dentro dejaria la pantalla a medias y el reinicio caeria sin que el
     * usuario hubiera leido nada. 3 s dan para pintar "Listo, reiniciando". */
    vTaskDelay(pdMS_TO_TICKS(OTA_REINICIO_MS));
    esp_restart();

salir:
    /* El plazo del WDT se devuelve SIEMPRE, salga bien o mal. */
    if (wdt_abierto) ota_wdt_ventana(false);
    if (sha_iniciado) mbedtls_sha256_free(&sha);
    if (ota_abierta) esp_ota_abort(ota);   /* libera el handle SIN marcar nada */
    if (cli) {
        if (cli_abierto) esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
    }
    free(buf);
    if (motivo[0]) {
        if (diferido) {
            /* No es un fallo de la descarga: es que ya no es buen momento. La
             * pantalla no se toca y se vuelve a mirar al minuto siguiente. */
            ESP_LOGW(TAG, "auto: %s; lo dejo para la proxima comprobacion", motivo);
            fijar_estado(OTA_RX_QUIETO, 0, "");
        } else if (automatica) {
            /* Un fallo NO se reintenta en bucle: la tarea muere y el
             * temporizador volvera a preguntar dentro de 60 s. El aviso se
             * repite solo si cambia (ver auto_aviso). */
            auto_aviso(motivo);
            fijar_estado(OTA_RX_ERROR, 0, motivo);
        } else {
            ESP_LOGE(TAG, "no se actualizo: %s", motivo);
            fijar_estado(OTA_RX_ERROR, 0, motivo);
        }
    }
    terminar_trabajo();
    vTaskDelete(NULL);
}

/* ── Arranque: validar la version nueva y decir de donde corre ────────────── */

/* Asociada a la P4 Y con IP. Las dos cosas: una asociacion a medias no ha
 * enganchado nada, y una IP sin datos tampoco. Si 'ip' viene, se copia. */
static bool enlace_listo(char *ip, size_t n)
{
    char ssid[33] = {0};
    bool asociado = false;
    int rssi = 0, sin_datos = -1;
    udp_rx_enlace(ssid, sizeof ssid, &asociado, &rssi, &sin_datos);
    if (!asociado) return false;

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return false;
    esp_netif_ip_info_t info = { 0 };
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) return false;

    if (ip && n) snprintf(ip, n, IPSTR, IP2STR(&info.ip));
    return true;
}

/* La version que acaba de arrancar esta EN PRUEBAS. Se da por buena solo con la
 * P4 enganchada y ESTABLE: asociada + IP + 8 s de margen, y se vuelve a mirar
 * justo antes de marcar. Si no llega, NO se marca: el bootloader volvera sola a
 * la version anterior en el siguiente encendido, que es exactamente lo que se
 * quiere si la nueva no engancha. */
static void tarea_validar(void *arg)
{
    (void)arg;
    int64_t estable_desde_ms = 0;
    int64_t ultimo_log_ms = 0;

    for (;;) {
        char ip[16] = "";
        if (enlace_listo(ip, sizeof ip)) {
            int64_t ahora_ms = esp_timer_get_time() / 1000;
            if (estable_desde_ms == 0) {
                estable_desde_ms = ahora_ms;
                ESP_LOGI(TAG, "enlace con la P4 en %s: %d s de margen antes de dar "
                              "la version por buena", ip, OTA_VALIDAR_ESPERA_MS / 1000);
            } else if (ahora_ms - estable_desde_ms >= OTA_VALIDAR_ESPERA_MS) {
                /* Se vuelve a mirar JUSTO antes de marcar: si el enlace se cayo en
                 * estos 8 s, la version nueva no ha demostrado nada. */
                if (!enlace_listo(NULL, 0)) {
                    ESP_LOGW(TAG, "se cayo el enlace antes de validar: vuelvo a contar");
                    estable_desde_ms = 0;
                    vTaskDelay(pdMS_TO_TICKS(OTA_VALIDAR_POLL_MS));
                    continue;
                }
                esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "version validada: el bootloader ya no volvera a la anterior");
                } else {
                    ESP_LOGE(TAG, "no pude validar la version (%s): %s",
                             esp_err_to_name(err),
                             (err == ESP_ERR_NOT_SUPPORTED)
                                 ? "el bootloader no lleva rollback: hace falta el grabado por cable"
                                 : "si la placa se reinicia, el bootloader volvera a la anterior");
                }
                esp_ota_img_states_t est;
                const esp_partition_t *p = esp_ota_get_running_partition();
                if (p && esp_ota_get_state_partition(p, &est) == ESP_OK) {
                    ESP_LOGI(TAG, "estado de '%s' despues de validar: %d",
                             p->label, (int)est);
                }
                vTaskDelete(NULL);
            }
        } else {
            if (estable_desde_ms != 0) {
                ESP_LOGW(TAG, "se cayo el enlace antes de validar: vuelvo a contar");
                estable_desde_ms = 0;
            }
            int64_t ahora_ms = esp_timer_get_time() / 1000;
            if (ahora_ms - ultimo_log_ms >= 10000) {
                ultimo_log_ms = ahora_ms;
                ESP_LOGW(TAG, "la version nueva sigue EN PRUEBAS: esperando a la P4 "
                              "(sin asociar o sin IP)");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(OTA_VALIDAR_POLL_MS));
    }
}

void ota_rx_arranque(void)
{
    const esp_partition_t *corre = esp_ota_get_running_partition();
    const esp_partition_t *prox = esp_ota_get_next_update_partition(NULL);

    /* QUE PARTICION CORRE, en cada arranque: es el primer dato para saber si una
     * actualizacion se aplico de verdad (y si el bootloader hizo rollback). */
    if (corre) {
        ESP_LOGI(TAG, "corriendo desde '%s' (0x%08" PRIx32 ", %" PRIu32 " KB); la "
                      "siguiente actualizacion escribira en '%s' (%" PRIu32 " KB)",
                 corre->label, corre->address, corre->size / 1024,
                 prox ? prox->label : "ninguna",
                 prox ? prox->size / 1024 : 0);
    } else {
        ESP_LOGE(TAG, "no se cual es la particion que corre");
    }

    if (prox && corre && prox == corre) {
        /* No deberia pasar con esta tabla, pero si pasara, esp_ota_begin
         * rechazaria la particion (PARTITION_CONFLICT) y la actualizacion no
         * arrancaria nunca sin decir por que. */
        ESP_LOGE(TAG, "la particion de actualizacion es la que corre: revisa partitions.csv");
    }

    esp_ota_img_states_t estado;
    if (corre && esp_ota_get_state_partition(corre, &estado) == ESP_OK) {
        if (estado == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGW(TAG, "esta version esta EN PRUEBAS (rollback armado): se marcara "
                          "valida cuando la P4 este enganchada y estable");
            if (xTaskCreate(tarea_validar, "ota_validar", 4096, NULL, 2, NULL) != pdPASS) {
                ESP_LOGE(TAG, "sin tarea de validacion: si la placa se reinicia, el "
                              "bootloader volvera a la version anterior");
            }
        } else {
            ESP_LOGI(TAG, "la app no tiene que validarse (estado %d): arranque normal",
                     (int)estado);
        }
    }

    /* Si el bootloader instalado es de antes del 17-oct-2026 (sin
     * CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE), esp_ota_get_state_partition no
     * devuelve PENDING_VERIFY nunca y aqui arriba no se hace nada. El
     * anti-ladrillo de verdad empieza a contar con el primer grabado POR CABLE;
     * esto se dice en el log para que nadie se crea protegido sin estarlo. */

    /* Y el temporizador que mira sola la version cada 60 s (ver la cabecera).
     * Va al final del arranque porque necesita el enlace (udp_rx_start) y la
     * NVS ya inicializada; la primera consulta no sale hasta dentro de un
     * minuto, que es de sobra para que la UI exista y pueda avisar de un
     * editor abierto. */
    auto_arrancar();
}

/* ── Lo que llama la UI ──────────────────────────────────────────────────── */

/* Comun al boton y al temporizador. 'automatica' se le pasa a la tarea como
 * argumento (NULL = manual): es lo unico que cambia entre los dos caminos. */
static ota_rx_inicio_t comprobar_lanzar(bool automatica)
{
    /* Con un viaje abierto no se actualiza (ver la cabecera). Se comprueba ANTES
     * de lanzar la tarea: la UI recibe el codigo al instante y lo puede decir. */
    if (hay_viaje_abierto()) return OTA_RX_INICIO_VIAJE;

    /* En automatico tambien se respeta al usuario que esta escribiendo: si el
     * editor o un dialogo estan abiertos, no se lanza ni la consulta. */
    if (automatica && ui_ocupada_ahora()) return OTA_RX_INICIO_OCUPADO;

    taskENTER_CRITICAL(&s_mtx);
    if (s_trabajando) {
        taskEXIT_CRITICAL(&s_mtx);
        return OTA_RX_INICIO_OCUPADO;
    }
    s_trabajando = true;
    taskEXIT_CRITICAL(&s_mtx);

    fijar_estado(OTA_RX_COMPROBANDO, 0, "");

    if (xTaskCreate(tarea_comprobar, "ota_comp", 6144,
                    (void *)(uintptr_t)automatica, 3, NULL) != pdPASS) {
        terminar_trabajo();
        fijar_estado(OTA_RX_ERROR, 0, "Sin memoria para comprobar");
        return OTA_RX_INICIO_SIN_MEM;
    }
    return OTA_RX_INICIO_OK;
}

ota_rx_inicio_t ota_rx_comprobar(void)
{
    return comprobar_lanzar(false);
}

ota_rx_inicio_t ota_rx_actualizar(void)
{
    /* Mismo codigo claro que en comprobar: "termina el viaje antes". */
    if (hay_viaje_abierto()) return OTA_RX_INICIO_VIAJE;

    taskENTER_CRITICAL(&s_mtx);
    bool hay_oferta = (s_vista.estado == OTA_RX_HAY_NUEVA) && !s_trabajando;
    bool ocupado = s_trabajando;
    if (hay_oferta) {
        s_trabajando = true;
        s_vista.estado = OTA_RX_DESCARGANDO;
        s_vista.pct = 0;
        s_vista.automatica = false;   /* la pidio el usuario, no el temporizador */
        s_vista.motivo[0] = '\0';
    }
    taskEXIT_CRITICAL(&s_mtx);

    if (ocupado) return OTA_RX_INICIO_OCUPADO;
    /* Sin comprobacion previa no hay nada que instalar (y no se puede inventar
     * el sha ni el tamaño que hay que comprobar). */
    if (!hay_oferta) return OTA_RX_INICIO_SIN_DATOS;

    if (xTaskCreate(tarea_actualizar, "ota_upd", 8192, NULL, 3, NULL) != pdPASS) {
        terminar_trabajo();
        fijar_estado(OTA_RX_ERROR, 0, "Sin memoria para actualizar");
        return OTA_RX_INICIO_SIN_MEM;
    }
    return OTA_RX_INICIO_OK;
}

void ota_rx_olvidar(void)
{
    taskENTER_CRITICAL(&s_mtx);
    /* Solo se olvida un cartel que NO tiene trabajo detras. Con una descarga en
     * marcha esto no toca nada: el estado lo lleva la tarea. */
    if (s_vista.estado == OTA_RX_HAY_NUEVA ||
        s_vista.estado == OTA_RX_AL_DIA ||
        s_vista.estado == OTA_RX_ERROR) {
        s_vista.estado = OTA_RX_QUIETO;
        s_vista.pct = 0;
        s_vista.automatica = false;
        s_vista.motivo[0] = '\0';
        s_vista.ofrecida[0] = '\0';
    }
    taskEXIT_CRITICAL(&s_mtx);
}

/* ── AUTOMATICO: preguntar sola cada 60 s e instalar sin preguntar ──────────
 *
 * Lo pidio el dueño: sube el .bin desde el navegador del movil al portal de la
 * P4 y la pantalla se actualiza sola, como ya hace la P4. El temporizador es de
 * esp_timer (NO de LVGL) porque la consulta es red y puede tardar hasta 8 s:
 * hacerla en la tarea de dibujo congelaria la pantalla y el watchdog de tareas
 * reiniciaria la placa a mitad. La decision pura esta en el bloque de pruebas
 * de arriba (ota_rx_auto_toca_comprobar / ota_rx_auto_puede_instalar).
 *
 * ORDEN DE LAS COSAS, a proposito:
 *   1. el temporizador mira red/viaje/trabajo/UI sin lanzar nada;
 *   2. tarea_comprobar pregunta la version;
 *   3. si hay nueva, tarea_comprobar vuelve a mirar viaje/UI y, si esta bien,
 *      deja DESCARGANDO y arranca tarea_actualizar SIN soltar s_trabajando
 *      (el boton manual no puede colarse);
 *   4. tarea_actualizar mira una ultima vez viaje/UI antes de tocar la flash.
 * Un fallo deja el estado en ERROR y la tarea muere: no hay bucle, el
 * temporizador volvera a preguntar dentro de 60 s. Si el fallo es "ya no es
 * buen momento" (viaje/editor), se deja QUIETO y se dice en el log.
 */
static ota_rx_inicio_t ota_rx_comprobar_auto(void)
{
    return comprobar_lanzar(true);
}

/* El temporizador. Corre en la tarea de esp_timer: aqui SOLO se miran cosas
 * baratas y se lanza la comprobacion, que hace el trabajo en su propia tarea. */
static void auto_timer_cb(void *arg)
{
    (void)arg;
    ota_rx_condiciones_t c = {
        .auto_activo = s_auto_activo,
        .red         = enlace_listo(NULL, 0),
        .viaje       = hay_viaje_abierto(),
        .trabajando  = trabajando_ahora(),
        .ui_ocupada  = ui_ocupada_ahora(),
    };

    if (!ota_rx_auto_toca_comprobar(&c)) {
        /* Sin red o con viaje no se insiste: el siguiente minuto lo vuelve a
         * mirar. Solo DEBUG, para no llenar el log de "no puedo" cada 60 s. */
        if (!c.red)            ESP_LOGD(TAG, "auto: sin enlace con la P4; lo miro al minuto");
        else if (c.viaje)      ESP_LOGD(TAG, "auto: viaje abierto; lo miro al minuto");
        else if (c.trabajando) ESP_LOGD(TAG, "auto: ya hay una comprobacion o descarga en marcha");
        else if (c.ui_ocupada) ESP_LOGD(TAG, "auto: editor o dialogo abierto; lo miro al minuto");
        return;
    }

    ESP_LOGD(TAG, "auto: preguntando la version a la P4");
    ota_rx_inicio_t r = ota_rx_comprobar_auto();
    if (r != OTA_RX_INICIO_OK) {
        ESP_LOGD(TAG, "auto: no he lanzado la comprobacion (%d)", (int)r);
    }
}

/* Lee el interruptor de NVS. Por defecto ENCENDIDO: es lo que pidio el dueño y
 * la red de seguridad solo tiene que servir para apagarlo. */
static void auto_leer_interruptor(void)
{
    nvs_handle_t h;
    uint8_t v = 1;
    esp_err_t err = nvs_open(OTA_AUTO_NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        esp_err_t r = nvs_get_u8(h, OTA_AUTO_CLAVE, &v);
        nvs_close(h);
        if (r == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "sin ajuste %s/%s: actualizacion automatica ENCENDIDA "
                          "por defecto", OTA_AUTO_NS, OTA_AUTO_CLAVE);
        } else if (r != ESP_OK) {
            ESP_LOGW(TAG, "no pude leer %s/%s (%s): automatica ENCENDIDA por defecto",
                     OTA_AUTO_NS, OTA_AUTO_CLAVE, esp_err_to_name(r));
        }
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* NVS vacia (namespace nuevo): se usa el valor de fabrica. */
        ESP_LOGI(TAG, "sin NVS %s/%s: automatica ENCENDIDA por defecto",
                 OTA_AUTO_NS, OTA_AUTO_CLAVE);
    } else {
        ESP_LOGW(TAG, "no pude abrir la NVS %s (%s): automatica ENCENDIDA por defecto",
                 OTA_AUTO_NS, esp_err_to_name(err));
    }
    s_auto_activo = (v != 0);
}

/* PARA APAGARLA (red de seguridad; no hay boton ni pantalla nueva): escribir 0
 * en NVS, namespace "ota", clave "ota_auto", y reiniciar. Desde el propio
 * firmware, una vez:
 *
 *   nvs_handle_t h;
 *   if (nvs_open("ota", NVS_READWRITE, &h) == ESP_OK) {
 *       nvs_set_u8(h, "ota_auto", 0);
 *       nvs_commit(h);
 *       nvs_close(h);
 *   }
 *
 * o con una imagen NVS generada por nvs_partition_gen. Con 0 no se crea ni el
 * temporizador: queda solo el boton "Actualizar pantalla" de Ajustes. */
static void auto_arrancar(void)
{
    auto_leer_interruptor();
    if (!s_auto_activo) {
        ESP_LOGW(TAG, "actualizacion automatica APAGADA (NVS %s/%s=0): solo el "
                      "boton de Ajustes", OTA_AUTO_NS, OTA_AUTO_CLAVE);
        return;
    }

    esp_timer_create_args_t cfg = {
        .callback = auto_timer_cb,
        .name     = "ota_auto",
    };
    esp_timer_handle_t t = NULL;
    esp_err_t err = esp_timer_create(&cfg, &t);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(t, (uint64_t)OTA_AUTO_PERIODO_MS * 1000u);
    }
    if (err != ESP_OK) {
        if (t) esp_timer_delete(t);
        ESP_LOGE(TAG, "sin temporizador automatico (%s): queda el boton de Ajustes",
                 esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "actualizacion automatica ENCENDIDA: cada %d s pregunta sola y, "
                  "si hay version nueva, la instala sin preguntar",
             OTA_AUTO_PERIODO_MS / 1000);
}
