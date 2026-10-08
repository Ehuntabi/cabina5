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
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lv_port_compat.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "p4_api";

#define P4_HOST       "http://192.168.4.1"
#define P4_URL        P4_HOST "/api/viaje"
#define P4_URL_ALARMA P4_HOST "/api/alarma"
/* 8 s: el AP esta a un metro, pero la P4 puede estar ocupada con la tarjeta
 * (el endpoint toma el cerrojo de la SD, hasta 3 s) y ademas puede tocarle
 * reasociarse. Corto se traduciria en fallos falsos. */
#define P4_TIMEOUT_MS 8000

/* Trabajo de un envio. Se reserva en el heap y lo libera la tarea al terminar:
 * quien llama no espera y no puede ser el dueño de esta memoria. */
typedef struct {
    char           cuerpo[192];
    /* Copia de la URL y no un puntero: el trabajo sobrevive a quien llama (la
     * tarea se lleva el struct al heap), asi que no puede depender de que el
     * texto siga vivo. */
    char           url[64];
    p4_api_done_cb cb;
    void          *user_data;   /* lo que paso quien lanzo, para el callback */
    bool           ok;
    int            estado;      /* codigo HTTP, o 0 si ni siquiera conecto */
} trabajo_t;

/* Vuelta al hilo de LVGL para avisar del resultado. */
static void avisar_cb(void *arg)
{
    trabajo_t *t = (trabajo_t *)arg;
    if (t->cb) t->cb(t->ok, t->estado, t->user_data);
    free(t);
}

/* ── Envio ─────────────────────────────────────────────────────────────────
 * Un solo camino de salida para todo lo que se manda a la P4: el POST contra su
 * portal, con Basic Auth y contando como entregado solo el 2xx. La orden de
 * silencio (p4_api_silenciar_alarma) usa el mismo, cambiando solo la URL: dos
 * copias de esto serian dos sitios donde arreglar el mismo fallo. */
static bool lanzar_a(const char *url, const char *cuerpo, p4_api_done_cb cb,
                     void *user_data);

/* El POST de verdad. BLOQUEA hasta tener respuesta o agotar el plazo, asi que
 * NO se llama desde la tarea de LVGL: la usa el repartidor de la cola, que
 * tiene la suya. */
bool p4_api_post(const char *cuerpo, int *estado_out)
{
    return p4_api_post_url(P4_URL, cuerpo, estado_out);
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

bool p4_api_post_url(const char *url, const char *cuerpo, int *estado_out)
{
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
    if (hay_creds) esp_http_client_set_header(c, "Authorization", cab);

    esp_http_client_set_header(c, "Content-Type", "application/json");
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
            ESP_LOGI(TAG, "%s -> HTTP %d", cuerpo, estado);
            /* Drenar el cuerpo de la respuesta si lo hay, para poder reutilizar
             * o cerrar limpio. */
            char resto[64];
            while (esp_http_client_read(c, resto, sizeof(resto)) > 0) { }
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
    t->ok = p4_api_post_url(t->url, t->cuerpo, &t->estado);
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

void p4_api_cuerpo_inicio(char *out, size_t n, uint32_t id,
                          const char *destino, uint32_t fecha_dias)
{
    snprintf(out, n,
             "{\"op\":\"inicio\",\"id\":%lu,\"destino\":\"%s\",\"fecha_dias\":%lu}",
             (unsigned long)id, destino, (unsigned long)fecha_dias);
}

void p4_api_cuerpo_fin(char *out, size_t n, uint32_t id, uint32_t eventos)
{
    snprintf(out, n, "{\"op\":\"fin\",\"id\":%lu,\"eventos\":%lu}",
             (unsigned long)id, (unsigned long)eventos);
}

void p4_api_cuerpo_fin_ajeno(char *out, size_t n, uint32_t id)
{
    snprintf(out, n, "{\"op\":\"fin\",\"id\":%lu}", (unsigned long)id);
}

void p4_api_cuerpo_descartar(char *out, size_t n, uint32_t id)
{
    snprintf(out, n, "{\"op\":\"descartar\",\"id\":%lu}", (unsigned long)id);
}

void p4_api_cuerpo_alarma(char *out, size_t n, uint8_t mask)
{
    snprintf(out, n, "{\"alarmas\":%u}", (unsigned)mask);
}

bool p4_api_silenciar_alarma(uint8_t mask, p4_api_done_cb cb, void *user_data)
{
    char cuerpo[48];
    p4_api_cuerpo_alarma(cuerpo, sizeof(cuerpo), mask);
    return lanzar_a(P4_URL_ALARMA, cuerpo, cb, user_data);
}

bool p4_api_viaje_inicio(uint32_t id, const char *destino, uint32_t fecha_dias,
                         p4_api_done_cb cb)
{
    char cuerpo[192];
    p4_api_cuerpo_inicio(cuerpo, sizeof(cuerpo), id, destino, fecha_dias);
    return lanzar(cuerpo, cb);
}


