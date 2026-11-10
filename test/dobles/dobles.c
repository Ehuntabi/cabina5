/* dobles.c — las piezas de FUERA que necesita main/net/viaje_cola.c para
 * compilarse y correr en el PC (ver test/viaje_cola_integracion.sh).
 *
 * Que hay aqui y que NO hay aqui:
 *   - NVS: un mapa en RAM con las claves de la cola. No imita la paginacion de
 *     la NVS de verdad (eso ya se prueba en la placa y no es lo que se esta
 *     probando), pero si se niega a escribir cuando se le pide, que es el unico
 *     fallo de la NVS del que depende la logica de la cola.
 *   - FreeRTOS: semaforos de mentira (el test es de un solo hilo) y la tarea del
 *     repartidor REGISTRADA pero NO creada: la llama el arnes.
 *   - p4_api: se guarda cada cuerpo enviado, en orden, y se contesta el codigo
 *     HTTP que diga el arnes. La cabecera NO se dobla: se incluye la REAL
 *     (main/net/p4_api.h), asi que si cambia la firma el test no compila.
 *   - esp_timer y esp_err_to_name: lo minimo para enlazar.
 *
 * LOS MENSAJES DE LOG VAN A stderr (ver test/dobles/esp_log.h) para que stdout
 * sea solo el informe del test.
 */
#include "dobles.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "p4_api.h"
#include "viaje_cola.h"   /* CUERPO_MAX: mismo tope que el cuerpo de un apunte */

/* ── NVS simulada ─────────────────────────────────────────────────────────── */

/* El unico namespace que usa la cola (NS en viaje_cola.c). Los accesos de
 * conveniencia del arnes (dobles_nvs_cadena y compañia) miran solo aqui. */
#define NVS_NS_COLA     "vcola"
#define NVS_NS_MAX      12
#define NVS_NS_CUANTOS  8
#define NVS_CLAVE_MAX   16
#define NVS_CLAVES_MAX  40          /* 16 de la cola + cabeza/cola/capacidad, con margen */
#define NVS_VALOR_MAX   (CUERPO_MAX + 1)
#define NVS_HANDLES_MAX 16

typedef struct {
    bool     usado;
    char     ns[NVS_NS_MAX];
    char     clave[NVS_CLAVE_MAX];
    bool     es_numero;
    uint32_t numero;
    char     cadena[NVS_VALOR_MAX];
} nvs_entrada_t;

typedef struct {
    bool abierto;
    bool escritura;
    char ns[NVS_NS_MAX];
} nvs_handle_estado_t;

static nvs_entrada_t      s_nvs[NVS_CLAVES_MAX];
static nvs_handle_estado_t s_handles[NVS_HANDLES_MAX];
static char               s_ns[NVS_NS_CUANTOS][NVS_NS_MAX];
static size_t             s_ns_n;
static bool               s_sin_espacio;
static bool               s_indices_sin_espacio;

/* Copia sin el aviso de truncado de snprintf: lo que sobra se corta y punto
 * (los cuerpos que se prueban caben de sobra). */
static void copia(char *dst, size_t n, const char *src)
{
    size_t k = src ? strlen(src) : 0;
    if (k >= n) k = n - 1;
    if (k) memcpy(dst, src, k);
    dst[k] = 0;
}

/* El namespace pasa a existir en cuanto se le escribe algo (la NVS de verdad lo
 * crea con el primer set+commit). Antes de eso, abrirlo de solo lectura da
 * ESP_ERR_NVS_NOT_FOUND, que es lo que veria la placa en una NVS de estreno. */
static void marca_ns(const char *ns)
{
    for (size_t i = 0; i < s_ns_n; i++)
        if (strcmp(s_ns[i], ns) == 0) return;
    if (s_ns_n >= NVS_NS_CUANTOS) return;
    copia(s_ns[s_ns_n], NVS_NS_MAX, ns);
    s_ns_n++;
}

static bool ns_existe(const char *ns)
{
    for (size_t i = 0; i < s_ns_n; i++)
        if (strcmp(s_ns[i], ns) == 0) return true;
    return false;
}

static nvs_entrada_t *nvs_busca(const char *ns, const char *clave)
{
    if (!ns || !clave) return NULL;
    for (size_t i = 0; i < NVS_CLAVES_MAX; i++) {
        if (s_nvs[i].usado && strcmp(s_nvs[i].ns, ns) == 0 &&
            strcmp(s_nvs[i].clave, clave) == 0)
            return &s_nvs[i];
    }
    return NULL;
}

/* La entrada de esa clave, creandola si no estaba. NULL si la tabla esta llena
 * (que es el otro "no queda sitio" de la NVS). */
static nvs_entrada_t *nvs_hueco(const char *ns, const char *clave)
{
    nvs_entrada_t *e = nvs_busca(ns, clave);
    if (e) return e;
    for (size_t i = 0; i < NVS_CLAVES_MAX; i++) {
        if (!s_nvs[i].usado) {
            s_nvs[i].usado = true;
            copia(s_nvs[i].ns, NVS_NS_MAX, ns);
            copia(s_nvs[i].clave, NVS_CLAVE_MAX, clave);
            s_nvs[i].es_numero = false;
            s_nvs[i].numero = 0;
            s_nvs[i].cadena[0] = 0;
            return &s_nvs[i];
        }
    }
    return NULL;
}

static nvs_handle_estado_t *handle_de(nvs_handle_t h)
{
    if (h == 0 || h > NVS_HANDLES_MAX) return NULL;
    nvs_handle_estado_t *s = &s_handles[h - 1];
    return s->abierto ? s : NULL;
}

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
                   nvs_handle_t *out_handle)
{
    if (!namespace_name || !out_handle) return ESP_ERR_INVALID_ARG;
    if (open_mode == NVS_READONLY && !ns_existe(namespace_name))
        return ESP_ERR_NVS_NOT_FOUND;

    for (int i = 0; i < NVS_HANDLES_MAX; i++) {
        if (!s_handles[i].abierto) {
            s_handles[i].abierto = true;
            s_handles[i].escritura = (open_mode == NVS_READWRITE);
            copia(s_handles[i].ns, NVS_NS_MAX, namespace_name);
            *out_handle = (nvs_handle_t)(i + 1);
            return ESP_OK;
        }
    }
    return ESP_ERR_NVS_NO_FREE_PAGES;
}

void nvs_close(nvs_handle_t handle)
{
    nvs_handle_estado_t *s = handle_de(handle);
    if (s) s->abierto = false;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    nvs_handle_estado_t *s = handle_de(handle);
    if (!s) return ESP_ERR_NVS_INVALID_HANDLE;
    if (!s->escritura) return ESP_ERR_NVS_READ_ONLY;
    return ESP_OK;   /* el "no cabe" ya lo devuelve el set, como en la NVS real */
}

esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value)
{
    nvs_handle_estado_t *s = handle_de(handle);
    if (!s) return ESP_ERR_NVS_INVALID_HANDLE;
    if (!s->escritura) return ESP_ERR_NVS_READ_ONLY;
    if (s_sin_espacio || s_indices_sin_espacio) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    nvs_entrada_t *e = nvs_hueco(s->ns, key);
    if (!e) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    e->es_numero = true;
    e->numero = value;
    e->cadena[0] = 0;
    marca_ns(s->ns);
    return ESP_OK;
}

esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value)
{
    nvs_handle_estado_t *s = handle_de(handle);
    if (!s) return ESP_ERR_NVS_INVALID_HANDLE;
    if (!out_value) return ESP_ERR_INVALID_ARG;

    nvs_entrada_t *e = nvs_busca(s->ns, key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    if (!e->es_numero) return ESP_ERR_NVS_TYPE_MISMATCH;
    *out_value = e->numero;
    return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    nvs_handle_estado_t *s = handle_de(handle);
    if (!s) return ESP_ERR_NVS_INVALID_HANDLE;
    if (!s->escritura) return ESP_ERR_NVS_READ_ONLY;
    if (!value) return ESP_ERR_INVALID_ARG;
    if (s_sin_espacio) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    if (strlen(value) + 1 > NVS_VALOR_MAX) return ESP_ERR_NVS_VALUE_TOO_LONG;

    nvs_entrada_t *e = nvs_hueco(s->ns, key);
    if (!e) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    e->es_numero = false;
    e->numero = 0;
    copia(e->cadena, NVS_VALOR_MAX, value);
    marca_ns(s->ns);
    return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value,
                      size_t *length)
{
    nvs_handle_estado_t *s = handle_de(handle);
    if (!s) return ESP_ERR_NVS_INVALID_HANDLE;

    nvs_entrada_t *e = nvs_busca(s->ns, key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    if (e->es_numero) return ESP_ERR_NVS_TYPE_MISMATCH;

    size_t necesito = strlen(e->cadena) + 1;
    /* El modo documentado de "¿existe y cuanto mide?": out_value NULL. */
    if (!out_value) {
        if (length) *length = necesito;
        return ESP_OK;
    }
    if (!length) return ESP_ERR_INVALID_ARG;
    if (*length < necesito) {
        *length = necesito;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out_value, e->cadena, necesito);
    *length = necesito;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    nvs_handle_estado_t *s = handle_de(handle);
    if (!s) return ESP_ERR_NVS_INVALID_HANDLE;
    if (!s->escritura) return ESP_ERR_NVS_READ_ONLY;

    nvs_entrada_t *e = nvs_busca(s->ns, key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    memset(e, 0, sizeof *e);
    return ESP_OK;
}

/* ── Lo que el arnes maneja de la NVS ───────────────────────────────────── */

void dobles_nvs_borra(void)
{
    memset(s_nvs, 0, sizeof s_nvs);
    memset(s_handles, 0, sizeof s_handles);
    memset(s_ns, 0, sizeof s_ns);
    s_ns_n = 0;
    s_sin_espacio = false;
    s_indices_sin_espacio = false;
}

bool dobles_nvs_tiene(const char *clave)
{
    return nvs_busca(NVS_NS_COLA, clave) != NULL;
}

const char *dobles_nvs_cadena(const char *clave)
{
    nvs_entrada_t *e = nvs_busca(NVS_NS_COLA, clave);
    return (e && !e->es_numero) ? e->cadena : NULL;
}

uint32_t dobles_nvs_numero(const char *clave, uint32_t por_defecto)
{
    nvs_entrada_t *e = nvs_busca(NVS_NS_COLA, clave);
    return (e && e->es_numero) ? e->numero : por_defecto;
}

size_t dobles_nvs_claves(void)
{
    size_t n = 0;
    for (size_t i = 0; i < NVS_CLAVES_MAX; i++)
        if (s_nvs[i].usado && strcmp(s_nvs[i].ns, NVS_NS_COLA) == 0) n++;
    return n;
}

void dobles_nvs_sin_espacio(bool activo) { s_sin_espacio = activo; }
void dobles_nvs_indices_sin_espacio(bool activo) { s_indices_sin_espacio = activo; }

/* ── La P4 simulada ───────────────────────────────────────────────────────── */

#define P4_MAX_ENVIOS 64

static char   s_p4_cuerpos[P4_MAX_ENVIOS][CUERPO_MAX];
static size_t s_p4_n;
static int    s_p4_estado = 200;
static char   s_p4_respuesta[160];

void dobles_p4_limpia(void)
{
    s_p4_n = 0;
    s_p4_estado = 200;
    s_p4_respuesta[0] = 0;
}

void dobles_p4_estado(int estado) { s_p4_estado = estado; }

void dobles_p4_respuesta(const char *resp)
{
    copia(s_p4_respuesta, sizeof s_p4_respuesta, resp ? resp : "");
}

size_t dobles_p4_enviados(void) { return s_p4_n; }

const char *dobles_p4_cuerpo(size_t i)
{
    return (i < s_p4_n) ? s_p4_cuerpos[i] : "";
}

/* El UNICO simbolo de p4_api que usa viaje_cola.c. El 'ok' es true solo con 2xx,
 * como dice la cabecera real. */
bool p4_api_post_resp(const char *cuerpo, int *estado_out, char *resp_out,
                      size_t resp_n)
{
    /* Tope de seguridad: si el repartidor se quedara en bucle, el test dice que
     * no puede conectar en vez de colgarse (la pasada acaba en la siguiente
     * espera, ver dormir()). */
    if (s_p4_n >= P4_MAX_ENVIOS) {
        if (estado_out) *estado_out = 0;
        if (resp_out && resp_n) resp_out[0] = 0;
        return false;
    }
    copia(s_p4_cuerpos[s_p4_n], CUERPO_MAX, cuerpo ? cuerpo : "");
    s_p4_n++;

    if (estado_out) *estado_out = s_p4_estado;
    if (resp_out && resp_n) copia(resp_out, resp_n, s_p4_respuesta);
    return s_p4_estado >= 200 && s_p4_estado < 300;
}

/* ── FreeRTOS simulado ───────────────────────────────────────────────────── */

/* CUANTAS VECES SE DUERME EN UNA PASADA. Cada vez que el repartidor se va a
 * dormir, el doble le deja seguir como si el reloj hubiera corrido en el acto;
 * al llegar a este numero, se devuelve el control al arnes (longjmp) y la
 * pasada termina. Asi el estado LOCAL de reparto_task (los contadores de
 * "intentos seguidos") SI se conserva dentro de la pasada, que es lo que
 * necesitan los umbrales de 3 intentos; y el test nunca se queda esperando.
 * Se reinicia en cada pasada. */
#define DOBLES_DORMIDAS_PASADA 4

static jmp_buf        s_rendicion;      /* donde espera el arnes */
static bool           s_en_tarea;       /* estamos dentro de reparto_task */
static int            s_dormidas;
static TaskFunction_t s_tarea;

static int s_sem_mutex_uno;
static int s_sem_binario_uno;

/* La tarea se va a dormir: o sigue (el reloj del test corre solo) o se acaba la
 * pasada. NO se coge ningun cerrojo al llegar aqui (el repartidor nunca llama a
 * vTaskDelay con el mutex cogido), asi que salir con longjmp es seguro. */
static void dormir(void)
{
    if (!s_en_tarea) return;
    if (++s_dormidas < DOBLES_DORMIDAS_PASADA) return;
    longjmp(s_rendicion, 1);
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)&s_sem_mutex_uno; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return (SemaphoreHandle_t)&s_sem_binario_uno; }

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait)
{
    if (!xSemaphore) return pdFALSE;
    if (xTicksToWait == portMAX_DELAY) {
        return pdTRUE;    /* un solo hilo: el cerrojo siempre esta libre */
    }
    dormir();             /* la espera con plazo de s_despertar */
    return pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
    return xSemaphore ? pdTRUE : pdFALSE;
}

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode, const char * const pcName,
                       const configSTACK_DEPTH_TYPE usStackDepth,
                       void * const pvParameters, UBaseType_t uxPriority,
                       TaskHandle_t * const pxCreatedTask)
{
    (void)pcName;
    (void)usStackDepth;
    (void)pvParameters;
    (void)uxPriority;
    if (pxCreatedTask) *pxCreatedTask = NULL;
    /* NO se crea ninguna tarea: se apunta quien la ejecutaria para que el arnes
     * la llame cuando toque (ver dobles_tarea_pasada). */
    s_tarea = pxTaskCode;
    return pdPASS;
}

void vTaskDelay(const TickType_t xTicksToDelay)
{
    (void)xTicksToDelay;
    dormir();
}

bool dobles_tarea_hay(void) { return s_tarea != NULL; }

bool dobles_tarea_pasada(void)
{
    if (!s_tarea) return false;
    s_dormidas = 0;
    if (setjmp(s_rendicion) == 0) {
        s_en_tarea = true;
        s_tarea(NULL);          /* deberia dormir antes de volver; si vuelve, fin */
        s_en_tarea = false;
    }
    s_en_tarea = false;
    return true;
}

/* ── Reloj y nombres de error ────────────────────────────────────────────── */

static int64_t s_reloj;

int64_t esp_timer_get_time(void)
{
    s_reloj += 1000000;         /* un segundo por llamada: solo lo usa la caducidad */
    return s_reloj;
}

const char *esp_err_to_name(esp_err_t code)
{
    switch (code) {
    case ESP_OK:                        return "ESP_OK";
    case ESP_FAIL:                      return "ESP_FAIL";
    case ESP_ERR_INVALID_ARG:           return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_NVS_NOT_FOUND:         return "ESP_ERR_NVS_NOT_FOUND";
    case ESP_ERR_NVS_NOT_ENOUGH_SPACE:  return "ESP_ERR_NVS_NOT_ENOUGH_SPACE";
    case ESP_ERR_NVS_INVALID_HANDLE:    return "ESP_ERR_NVS_INVALID_HANDLE";
    case ESP_ERR_NVS_READ_ONLY:         return "ESP_ERR_NVS_READ_ONLY";
    case ESP_ERR_NVS_TYPE_MISMATCH:     return "ESP_ERR_NVS_TYPE_MISMATCH";
    case ESP_ERR_NVS_VALUE_TOO_LONG:    return "ESP_ERR_NVS_VALUE_TOO_LONG";
    case ESP_ERR_NVS_INVALID_LENGTH:    return "ESP_ERR_NVS_INVALID_LENGTH";
    case ESP_ERR_NVS_NO_FREE_PAGES:     return "ESP_ERR_NVS_NO_FREE_PAGES";
    default:                            return "ESP_ERR?";
    }
}
