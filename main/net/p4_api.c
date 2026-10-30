/* p4_api.c — lo que la 3.5" MANDA a la P4 (Fase 4 del diseño, fase 1).
 *
 * Hasta ahora este aparato solo escuchaba (udp_rx.c). Esto es el camino de
 * vuelta: inicio y fin de viaje.
 *
 * Por que HTTP y no UDP como la telemetria que llega: por TCP se sabe con
 * CERTEZA que se entrego. Un apunte de viaje no se puede perder en silencio, y
 * con UDP habria que reinventar confirmaciones y reintentos. La telemetria si
 * puede perderse: llega otra al segundo siguiente.
 *
 * NO se llama desde la tarea de LVGL. Cada envio abre un socket y espera
 * respuesta, y bloquear ahi congelaria la pantalla varios segundos. Se hace en
 * una tarea corta de usar y tirar, y el resultado vuelve por lv_async_call.
 *
 * OJO: lv_async_call() por si solo NO es seguro llamado desde otra tarea en
 * esta version de LVGL (8.4) -- toca la lista global de timers sin ningun
 * lock propio, y lv_timer_handler() la recorre desde la tarea LVGL al mismo
 * tiempo. Por eso aqui se llama con lvgl_port_lock()/unlock() alrededor,
 * igual que ya hace el bucle principal con lv_timer_handler(). Detectado
 * auditando el 07-sep-2026.
 */
#include "p4_api.h"
#include "config_storage.h"
#include "p4_creds.h"   /* NVS y, si no hay, valores de fabrica */
#include "esp_http_client.h"
#include "esp_wifi.h"   /* p4_api_dev_id(): la MAC de esta pantalla */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lv_port_compat.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "p4_api";

#define P4_HOST       "http://192.168.4.1"
#define P4_URL        P4_HOST "/api/viaje"
#define P4_URL_ALARMA P4_HOST "/api/alarma"

/* La IP, para quien monte otras URLs suyas (desde el 17-oct-2026, la
 * actualizacion OTA pide el .bin al puerto 8081 de esta misma P4). Ver
 * p4_api.h. */
const char *p4_api_host(void)
{
    return P4_HOST;
}

/* 8 s: el AP esta a un metro, pero la P4 puede estar ocupada con la tarjeta
 * (el endpoint toma el cerrojo de la SD, hasta 3 s) y ademas puede tocarle
 * reasociarse. Corto se traduciria en fallos falsos. */
#define P4_TIMEOUT_MS 8000

/* Tamano maximo de un cuerpo de peticion a la P4.
 *
 * 224 y no 192 (auditoria del 16-oct-2026): los constructores de cuerpo
 * (p4_api_cuerpo_*) escriben en buffers de 224 -- el inicio con cuentakilometros
 * es el mas largo -- pero el trabajo que se lleva la tarea al heap copiaba a un
 * campo de 192, asi que un cuerpo de mas de 191 caracteres se TRUNCABA en
 * silencio: JSON roto, 400 de la P4 y el viaje sin empezar (o un apunte
 * perdido), sin mas pista en pantalla. Con este unico numero, los dos sitios
 * dicen lo mismo y el tope es el de verdad. */
#define P4_API_CUERPO_MAX 224

/* Trabajo de un envio. Se reserva en el heap y lo libera la tarea al terminar:
 * quien llama no espera y no puede ser el dueño de esta memoria. */
typedef struct {
    char           cuerpo[P4_API_CUERPO_MAX];
    /* Copia de la URL y no un puntero: el trabajo sobrevive a quien llama (la
     * tarea se lleva el struct al heap), asi que no puede depender de que el
     * texto siga vivo. */
    char           url[64];
    p4_api_done_cb cb;
    void          *user_data;   /* lo que paso quien lanzo, para el callback */
    bool           ok;
    int            estado;      /* codigo HTTP, o 0 si ni siquiera conecto */
    /* El principio del cuerpo de la respuesta: ahi viene " libre_mb=NNN" (el
     * hueco de la tarjeta de la P4) y, desde el 20-oct-2026, " token=<32 hex>"
     * (el emparejamiento de esta pantalla). 96 no bastaban para los dos con una
     * carpeta larga: el token se quedaba cortado y no se guardaba. */
    char           resp[192];
} trabajo_t;

/* Vuelta al hilo de LVGL para avisar del resultado. */
static void avisar_cb(void *arg)
{
    trabajo_t *t = (trabajo_t *)arg;
    if (t->cb) t->cb(t->ok, t->estado, t->resp, t->user_data);
    free(t);
}

/* ── Envio ─────────────────────────────────────────────────────────────────
 * Un solo camino de salida para todo lo que se manda a la P4: el POST contra su
 * portal, con Basic Auth y contando como entregado solo el 2xx. La orden de
 * silencio (p4_api_silenciar_alarma) usa el mismo, cambiando solo la URL: dos
 * copias de esto serian dos sitios donde arreglar el mismo fallo. */
static bool lanzar_a(const char *url, const char *cuerpo, p4_api_done_cb cb,
                     void *user_data);

/* El token, solo si tiene la forma buena (32 hex). Un token con comillas o con
 * otra longitud romperia el JSON y el envio no saldria; y sin token se manda
 * "" a proposito en el inicio, porque es lo que le dice a la P4 "emparejame". */
static const char *token_o_vacio(const char *t)
{
    if (!t || strlen(t) != 32) return "";
    for (int i = 0; i < 32; i++)
        if (!isxdigit((unsigned char)t[i])) return "";
    return t;
}

/* ── LA REGLA DE LOS LOGS CON CUERPO (una sola, y es esta) ──────────────────
 *
 * Los apuntes del viaje llevan dentro el TOKEN de la pantalla (32 hex) y el
 * "dev". Con ese par se puede cerrar el viaje que OTRA pantalla esta grabando,
 * que es justo lo que el token vino a impedir. Por eso:
 *
 *  1. LA COLA NO IMPRIME CUERPOS NUNCA. Solo "op=fin id=12" (ver resumen_apunte()
 *     en viaje_cola.c): sus cuerpos son los que llevan el token, y alli se decidio
 *     no gastar 896 B de pila en copiarlos para taparlos.
 *  2. EL CLIENTE DIRECTO (este fichero) SI IMPRIME EL CUERPO, porque es lo que
 *     hace falta para depurar un 400 de la P4, pero con los CAMPOS SECRETOS
 *     tapados. La lista de secretos esta AQUI y solo aqui: un campo nuevo que no
 *     pueda salir en un registro se anade a SECRETOS y queda tapado en todos los
 *     sitios que usan este cliente. Si el tapado se hiciera por fichero, el dia
 *     que alguien lea el otro no sabra cual es la regla -- y asi se colo el token
 *     la primera vez (auditoria del 27-oct-2026, cab5-net-01). */
static const char *const SECRETOS[] = { "token" };

/* Tapa el VALOR de cada campo secreto: "token":"abc..." -> "token":"***". Se
 * hace sobre una copia en el heap porque hay que ensuciarla, y esto corre una vez
 * por respuesta (no es un camino caliente). Si no hay memoria, se dice el tamano
 * y no se imprime: mejor perder una linea de log que el secreto. */
static void log_cuerpo(const char *cuerpo, int estado)
{
    if (!cuerpo) return;
    size_t n = strlen(cuerpo);
    char *t = malloc(n + 1);
    if (!t) {
        ESP_LOGW(TAG, "cuerpo de %u B (sin memoria para imprimirlo tapado)", (unsigned)n);
        return;
    }
    memcpy(t, cuerpo, n + 1);

    for (size_t k = 0; k < sizeof(SECRETOS) / sizeof(SECRETOS[0]); k++) {
        char patron[24];
        int pl = snprintf(patron, sizeof(patron), "\"%s\":\"", SECRETOS[k]);
        if (pl <= 0 || pl >= (int)sizeof(patron)) continue;
        char *p = t;
        while ((p = strstr(p, patron)) != NULL) {
            char *v = p + pl;                        /* primer caracter del valor */
            char *fin = strchr(v, '"');
            /* SIN COMILLA DE CIERRE (cuerpo cortado): se tapa hasta el final, que
             * es lo unico seguro; lo que no se puede es dejarlo a la vista. */
            if (!fin) fin = v + strlen(v);
            for (char *q = v; q < fin; q++) *q = '*';
            p = fin;
        }
    }
    ESP_LOGI(TAG, "%s -> HTTP %d", t, estado);
    free(t);
}

/* El POST de verdad. BLOQUEA hasta tener respuesta o agotar el plazo, asi que
 * NO se llama desde la tarea de LVGL: la usa el repartidor de la cola, que
 * tiene la suya. */
bool p4_api_post(const char *cuerpo, int *estado_out)
{
    return p4_api_post_url(P4_URL, cuerpo, estado_out, NULL, 0);
}

bool p4_api_post_resp(const char *cuerpo, int *estado_out, char *resp_out, size_t resp_n)
{
    return p4_api_post_url(P4_URL, cuerpo, estado_out, resp_out, resp_n);
}

/* Cabecera "Authorization: Basic xxx" montada a mano.
 *
 * POR QUE A MANO (7-oct-2026): con `auth_type = HTTP_AUTH_TYPE_BASIC`, el
 * cliente de IDF tiene que resolver el reto 401 de la P4 y NO LO HACE: en el
 * banco devolvia ESP_ERR_HTTP_MAX_REDIRECT (o EAGAIN) contra un servidor que
 * responde 401 correctamente — comprobado con un socket a pelo, que da 401 sin
 * credenciales y 200 con ellas. Mandando la cabecera nosotros, el cliente no
 * tiene que resolver ningun reto y contesta 200 (probado en la placa). */
static const char *B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void cabecera_basic(const char *user, const char *pass, char *out, size_t n)
{
    char plano[128];
    snprintf(plano, sizeof(plano), "%s:%s", user, pass);
    size_t lp = strlen(plano), k = 0;
    if (n < 16) { out[0] = '\0'; return; }
    k += snprintf(out + k, n - k, "Basic ");
    for (size_t i = 0; i < lp && k + 5 < n; i += 3) {
        uint32_t v = ((uint8_t)plano[i]) << 16;
        if (i + 1 < lp) v |= ((uint8_t)plano[i+1]) << 8;
        if (i + 2 < lp) v |= (uint8_t)plano[i+2];
        out[k++] = B64[(v >> 18) & 63];
        out[k++] = B64[(v >> 12) & 63];
        out[k++] = (i + 1 < lp) ? B64[(v >> 6) & 63] : '=';
        out[k++] = (i + 2 < lp) ? B64[v & 63] : '=';
    }
    out[k] = '\0';
}

/* Version publica de cabecera_basic() para el otro cliente HTTP del proyecto
 * (net/ota_rx.c). NO se duplica el Base64: ver el porque en p4_api.h. */
void p4_api_cabecera_basic(const char *user, const char *pass, char *out, size_t n)
{
    if (!out || n == 0) return;
    cabecera_basic(user, pass, out, n);
}

bool p4_api_post_url(const char *url, const char *cuerpo, int *estado_out,
                      char *resp_out, size_t resp_n)
{
    if (resp_out && resp_n) resp_out[0] = 0;
    if (!url || !cuerpo) return false;
    if (estado_out) *estado_out = 0;

    /* Las credenciales se leen en cada envio y no se cachean: el usuario puede
     * corregirlas en Ajustes entre un intento y el siguiente, y con una copia
     * en RAM seguiria fallando sin entender por que. */
    char user[33] = {0}, pass[65] = {0};
    bool hay_creds = p4_creds_cargar(user, sizeof(user), pass, sizeof(pass));

    char cab[192] = {0};
    if (hay_creds) cabecera_basic(user, pass, cab, sizeof(cab));

    esp_http_client_config_t cfg = {
        .url            = url,
        .method         = HTTP_METHOD_POST,
        .timeout_ms     = P4_TIMEOUT_MS,
        /* Sin auth_type: la cabecera va puesta a mano (ver cabecera_basic). */
        .auth_type      = HTTP_AUTH_TYPE_NONE,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        ESP_LOGE(TAG, "no puedo crear el cliente HTTP");
        return false;
    }
    esp_http_client_set_header(c, "Content-Type", "application/json");
    /* UNA SOLA VEZ (auditoria del 27-oct-2026, X4): esta linea estaba puesta
     * tambien antes del Content-Type, la misma llamada con el mismo valor. No
     * rompia nada, pero dejaba dos sitios que parecen independientes sobre el
     * unico punto donde la autenticacion se monta a mano (ver cabecera_basic). */
    if (hay_creds) esp_http_client_set_header(c, "Authorization", cab);

    /* ENVIO A MANO (7-oct-2026): con el camino normal (set_post_field +
     * perform) el POST no terminaba NUNCA contra la P4: el cliente se quedaba
     * esperando respuesta y el apunte no salia. Comprobado bajando un piso que
     * la autenticacion va bien (GET con cabecera = 200), asi que el problema
     * esta en como la biblioteca manda el cuerpo. Aqui se abre, se escribe el
     * cuerpo con su longitud explicita y se lee la respuesta: es el mismo camino
     * que el sondeo a pelo, que si funciona. */
    bool ok = false;
    int len = (int)strlen(cuerpo);
    if (esp_http_client_open(c, len) == ESP_OK) {
        int escritos = esp_http_client_write(c, cuerpo, len);
        if (escritos == len) {
            esp_http_client_fetch_headers(c);     /* lee cabeceras (bounded) */
            int estado = esp_http_client_get_status_code(c);
            if (estado_out) *estado_out = estado;
            ok = (estado >= 200 && estado < 300);
            log_cuerpo(cuerpo, estado);
            /* Drenar el cuerpo de la respuesta si lo hay, para poder reutilizar
             * o cerrar limpio. DE PASO SE GUARDA EL PRINCIPIO (14-oct-2026): la
             * P4 añade " libre_mb=NNN" al final de sus respuestas cortas, y ese
             * es el unico sitio por donde la pantalla puede saber cuanto queda en
             * su tarjeta. Se copia lo que quepa y se sigue drenando: si el cuerpo
             * fuera enorme, no se corta la lectura por quedarse sin hueco. */
            char resto[64];
            size_t copiado = 0;
            int r;
            while ((r = esp_http_client_read(c, resto, sizeof(resto))) > 0) {
                if (resp_out && resp_n > 1 && copiado < resp_n - 1) {
                    size_t libre = resp_n - 1 - copiado;
                    size_t n = (size_t)r < libre ? (size_t)r : libre;
                    memcpy(resp_out + copiado, resto, n);
                    copiado += n;
                    resp_out[copiado] = 0;
                }
            }
        } else {
            ESP_LOGW(TAG, "no se pudo escribir el cuerpo (%d de %d bytes)", escritos, len);
        }
        esp_http_client_close(c);
    } else {
        ESP_LOGW(TAG, "no se pudo abrir la conexion con el portal");
    }
    esp_http_client_cleanup(c);
    return ok;
}

static void envio_task(void *arg)
{
    trabajo_t *t = (trabajo_t *)arg;
    t->ok = p4_api_post_url(t->url, t->cuerpo, &t->estado,
                             t->resp, sizeof(t->resp));
    /* Ver el comentario de cabecera: lv_async_call() en si mismo no basta.
     *
     * Espera SIN LIMITE (lvgl_port_lock(0)): con un timeout de 1s, si la
     * tarea LVGL tardaba un pelin mas (una transicion de pantalla pesada,
     * p.ej.), el aviso se perdia EN SILENCIO -- ni exito ni fallo, el que
     * inicia/cierra un viaje se quedaba sin saber si se aplico, y de paso 't'
     * (calloc) se fugaba porque avisar_cb() -- el unico que lo libera -- no
     * llegaba a llamarse. Esta tarea es de usar y tirar, dedicada solo a este
     * envio: no pasa nada por esperar lo que haga falta, la tarea LVGL nunca
     * espera a su vez por esta (el aviso vuelve por lv_async_call, no hay
     * rendezvous sincrono que pueda formar un interbloqueo). Detectado
     * auditando el 08-sep-2026. */
    if (lvgl_port_lock(0)) {
        lv_async_call(avisar_cb, t);
        lvgl_port_unlock();
    }
    vTaskDelete(NULL);
}

static bool lanzar(const char *cuerpo, p4_api_done_cb cb)
{
    return lanzar_a(P4_URL, cuerpo, cb, NULL);
}

static bool lanzar_a(const char *url, const char *cuerpo, p4_api_done_cb cb,
                     void *user_data)
{
    trabajo_t *t = calloc(1, sizeof(trabajo_t));
    if (!t) return false;
    snprintf(t->cuerpo, sizeof(t->cuerpo), "%s", cuerpo);
    snprintf(t->url, sizeof(t->url), "%s", url);
    t->cb = cb;
    t->user_data = user_data;
    /* 6 KB: el cliente HTTP con su buffer de cabeceras no baja de unos 4. Con
     * menos se cuelga por desbordamiento de pila justo al conectar. */
    if (xTaskCreate(envio_task, "p4_api", 6144, t, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "sin memoria para la tarea de envio");
        free(t);
        return false;
    }
    return true;
}

/* DE QUE PANTALLA ES ESTE VIAJE (auditoria de comunicacion del 13-oct-2026).
 *
 * Se manda la MAC de esta placa en hex ("aabbccddeeff") en inicio, fin y
 * descartar. La P4 la guarda con el viaje abierto y, si otra pantalla intenta
 * empezar un viaje, contesta 412 en vez de 409: asi la segunda sabe que el viaje
 * abierto NO es suyo, y no le ofrece "apartarlo (era una prueba)" -- que
 * renombraba la carpeta del viaje REAL de la otra, que seguia grabando.
 *
 * Se lee una vez y se cachea: la MAC no cambia en caliente. Si no se puede leer,
 * se manda vacio y la P4 se comporta como siempre (409). */
const char *p4_api_dev_id(void)
{
    static char dev[13];
    if (!dev[0]) {
        uint8_t mac[6] = {0};
        if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
            snprintf(dev, sizeof(dev), "%02x%02x%02x%02x%02x%02x",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        } else {
            snprintf(dev, sizeof(dev), "desconocido");
        }
    }
    return dev;
}

void p4_api_cuerpo_inicio(char *out, size_t n, uint32_t id,
                          const char *destino, uint32_t fecha_dias,
                          uint32_t km_cuadro, const char *token)
{
    /* El token va el ULTIMO y siempre (vacio si no hay): es lo que le dice a la
     * P4 "emparejame" y lo que hace que su respuesta traiga el token. Una P4
     * anterior al 20-oct-2026 lo ignora. */
    const char *tok = token_o_vacio(token);
    /* El destino lo teclea el usuario, asi que se escapan comillas y barras
     * IGUAL QUE EN EL FIN (16-oct-2026: el fin ya lo hacia y el inicio no, y son
     * el mismo dato). Hoy no hay nada que escapar -- el campo solo admite
     * [A-Za-z0-9 -_] en las dos cabinas -- pero el dia que se amplie, sin esto
     * el JSON del inicio queda roto, la P4 contesta 400 y el viaje NO arranca,
     * sin mas pista que un "no se pudo empezar". */
    char limpio[80] = {0};
    if (destino && destino[0]) {
        size_t j = 0;
        for (size_t i = 0; destino[i] && j + 2 < sizeof(limpio); i++) {
            if (destino[i] == '"' || destino[i] == '\\') limpio[j++] = '\\';
            limpio[j++] = destino[i];
        }
        limpio[j] = 0;
    }

    /* El cuentakilometros va SOLO si hay lectura (16-oct-2026): con 0 no se
     * manda el campo, que es como se dice "no apuntado". Mandarlo a 0 seria
     * mandar un dato que el usuario no ha dado, y la P4 no podria distinguir
     * "el cuadro marcaba 0" de "no me lo han dicho" -- hoy los dos salen como
     * "no se apunto" en el resumen, pero no hay por que perder la diferencia. */
    if (km_cuadro > 0) {
        snprintf(out, n,
                 "{\"op\":\"inicio\",\"id\":%lu,\"destino\":\"%s\",\"fecha_dias\":%lu,"
                 "\"km_cuadro\":%lu,\"dev\":\"%s\",\"token\":\"%s\"}",
                 (unsigned long)id, limpio, (unsigned long)fecha_dias,
                 (unsigned long)km_cuadro, p4_api_dev_id(), tok);
    } else {
        snprintf(out, n,
                 "{\"op\":\"inicio\",\"id\":%lu,\"destino\":\"%s\",\"fecha_dias\":%lu,"
                 "\"dev\":\"%s\",\"token\":\"%s\"}",
                 (unsigned long)id, limpio, (unsigned long)fecha_dias,
                 p4_api_dev_id(), tok);
    }
}

/* 'destino' es el nombre DEFINITIVO del viaje, o NULL/"" si no ha cambiado.
 *
 * LO MANDA EL FIN Y NO EL CAMBIO (12-oct-2026): el usuario cambia el destino a
 * mitad de viaje y la pantalla lo enseña al momento, pero la carpeta de la P4
 * NO se renombra hasta que se termina el viaje. Mientras corre hay ficheros
 * escribiendose (ruta, telemetria) y renombrar por debajo obligaria a
 * cerrarlos y reabrirlos. Al cerrar ya se cierran de todas formas. */
void p4_api_cuerpo_fin(char *out, size_t n, uint32_t id, uint32_t eventos,
                       const char *destino, uint32_t km_cuadro)
{
    /* El destino lo teclea el usuario: se escapan comillas y barras, que si no
     * el JSON que se manda a la P4 queda roto y el cierre se pierde. */
    char limpio[80] = {0};
    if (destino && destino[0]) {
        size_t j = 0;
        for (size_t i = 0; destino[i] && j + 2 < sizeof(limpio); i++) {
            if (destino[i] == '"' || destino[i] == '\\') limpio[j++] = '\\';
            limpio[j++] = destino[i];
        }
        limpio[j] = 0;
    }

    /* Se arma a trozos y no con un snprintf de cuatro formatos: "destino" y
     * "km_cuadro" son opcionales por separado (hay cuatro combinaciones) y con
     * la cadena de formato variable los argumentos de las varargs dejaban de
     * cuadrar -- un %lu leyendo un char* es un fallo que aparece en la placa y
     * no al compilar. */
    /* eventos == 0 = NO SE SABE, y entonces la clave NO SE MANDA (tanda 2 de la
     * auditoria del 18-oct-2026): la P4, sin 'eventos', marca el viaje "SIN
     * VERIFICAR" en vez de darlo por completo. Antes se mandaba el 0 de una
     * lectura fallida como si fuera "ningun evento de mas". */
    int k;
    if (eventos)
        k = snprintf(out, n, "{\"op\":\"fin\",\"id\":%lu,\"eventos\":%lu,\"dev\":\"%s\"",
                     (unsigned long)id, (unsigned long)eventos, p4_api_dev_id());
    else
        k = snprintf(out, n, "{\"op\":\"fin\",\"id\":%lu,\"dev\":\"%s\"",
                     (unsigned long)id, p4_api_dev_id());
    if (k > 0 && (size_t)k < n && limpio[0]) {
        k += snprintf(out + k, n - (size_t)k, ",\"destino\":\"%s\"", limpio);
    }
    /* EL CUENTAKILOMETROS AL CERRAR (16-oct-2026): solo si hay lectura, como en
     * el inicio. La P4 lo guarda y lo escribe en resumen.txt junto al de la
     * salida, que es con quien se compara el recorrido del GPS. */
    if (k > 0 && (size_t)k < n && km_cuadro > 0) {
        k += snprintf(out + k, n - (size_t)k, ",\"km_cuadro\":%lu",
                      (unsigned long)km_cuadro);
    }
    if (k > 0 && (size_t)k < n - 1) snprintf(out + k, n - (size_t)k, "}");
}

/* AVISO A LA P4 DEL DESTINO NUEVO, SIN renombrar nada (13-oct-2026).
 *
 * Se manda EN CUANTO se cambia el nombre, no al cerrar: asi el nombre queda a
 * salvo en las TRES placas a la vez (la cabina lo guarda en su NVS y la P4 en
 * la suya). Si la cabina se queda sin NVS o se cambia por otra, la P4 lo aplica
 * igual al cerrar el viaje.
 *
 * Lleva "id" como las demas del satelite (ver contrato_app.c). */
void p4_api_cuerpo_destino(char *out, size_t n, uint32_t id, const char *destino)
{
    char limpio[80]; size_t j = 0;
    for (size_t i = 0; destino && destino[i] && j + 2 < sizeof(limpio); i++) {
        if (destino[i] == '"' || destino[i] == '\\') limpio[j++] = '\\';
        limpio[j++] = destino[i];
    }
    limpio[j] = 0;
    snprintf(out, n, "{\"op\":\"destino\",\"id\":%lu,\"destino\":\"%s\"}",
             (unsigned long)id, limpio);
}

void p4_api_cuerpo_fin_ajeno(char *out, size_t n, uint32_t id, int forzar,
                             const char *token)
{
    /* 'forzar' SOLO lo manda la pantalla "VIAJE EN LA P4", despues de que el
     * usuario confirme que cierra un viaje que empezo OTRA pantalla. La P4
     * (desde el 14-oct-2026) rechaza con 409 un cierre ajeno sin esta llave,
     * porque el dueño puede seguir grabando; con ella, deja hacerlo -- si no,
     * un viaje cuyo dueño ya no existe no se podria cerrar de ninguna forma. */
    const char *tok = forzar ? token_o_vacio(token) : "";
    if (tok[0])
        snprintf(out, n, "{\"op\":\"fin\",\"id\":%lu,\"dev\":\"%s\",\"forzar\":true,"
                         "\"token\":\"%s\"}",
                 (unsigned long)id, p4_api_dev_id(), tok);
    else
        snprintf(out, n, "{\"op\":\"fin\",\"id\":%lu,\"dev\":\"%s\"%s}",
                 (unsigned long)id, p4_api_dev_id(), forzar ? ",\"forzar\":true" : "");
}

void p4_api_cuerpo_descartar(char *out, size_t n, uint32_t id, int forzar,
                             const char *token)
{
    /* Ver p4_api_cuerpo_fin_ajeno: 'forzar' es la confirmacion explicita de que
     * se esta apartando un viaje que empezo otra pantalla. */
    const char *tok = forzar ? token_o_vacio(token) : "";
    if (tok[0])
        snprintf(out, n, "{\"op\":\"descartar\",\"id\":%lu,\"dev\":\"%s\","
                         "\"forzar\":true,\"token\":\"%s\"}",
                 (unsigned long)id, p4_api_dev_id(), tok);
    else
        snprintf(out, n, "{\"op\":\"descartar\",\"id\":%lu,\"dev\":\"%s\"%s}",
                 (unsigned long)id, p4_api_dev_id(), forzar ? ",\"forzar\":true" : "");
}

void p4_api_cuerpo_alarma(char *out, size_t n, uint8_t mask)
{
    p4_api_cuerpo_alarma_sentido(out, n, mask, 1);
}

/* Manda TAMBIEN el sentido: 1 = callar, 0 = volver a sonar.
 *
 * Antes solo iba la mascara, y la P4 entendia "callar" SIEMPRE: apagar el
 * altavoz desde la cabina no volvia a encender nada, el icono se quedaba
 * diciendo "sonido puesto" y la P4 seguia callada (auditoria del 13-oct-2026).
 * El campo es opcional en el otro lado, asi que una P4 con firmware viejo lo
 * ignora y se comporta como antes. */
void p4_api_cuerpo_alarma_sentido(char *out, size_t n, uint8_t mask, int silenciar)
{
    snprintf(out, n, "{\"alarmas\":%u,\"silencio\":%d}", (unsigned)mask,
             silenciar ? 1 : 0);
}

bool p4_api_silenciar_alarma(uint8_t mask, p4_api_done_cb cb, void *user_data)
{
    /* Compatibilidad: callar. Para volver a sonar, p4_api_alarma_sentido(). */
    return p4_api_alarma_sentido(mask, 1, cb, user_data);
}

bool p4_api_alarma_sentido(uint8_t mask, int silenciar, p4_api_done_cb cb,
                           void *user_data)
{
    char cuerpo[64];
    p4_api_cuerpo_alarma_sentido(cuerpo, sizeof(cuerpo), mask, silenciar);
    return lanzar_a(P4_URL_ALARMA, cuerpo, cb, user_data);
}

bool p4_api_viaje_inicio(uint32_t id, const char *destino, uint32_t fecha_dias,
                         uint32_t km_cuadro, const char *token, p4_api_done_cb cb)
{
    /* El inicio con cuentakilometros lleva 20 caracteres mas que el resto
     * ("km_cuadro":999999,) y con el tope de 6 cifras el cuerpo llega a ~130. */
    char cuerpo[P4_API_CUERPO_MAX];
    p4_api_cuerpo_inicio(cuerpo, sizeof(cuerpo), id, destino, fecha_dias, km_cuadro,
                         token);
    return lanzar(cuerpo, cb);
}


