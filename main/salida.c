/* salida.c — implementacion del estado de la salida. Ver salida.h para el
 * porque; aqui solo van los detalles que no se adivinan leyendo el .h.
 *
 * TODO EL ESTADO SE TOCA DESDE LA TAREA DE LVGL y solo desde ahi, asi que no
 * lleva mutex. La unica excepcion es la marca de vida, que la escribe una tarea
 * propia y NO comparte nada con el estado: es otra clave de NVS y una variable
 * que nadie mas lee.
 */
#include "salida.h"
#include "reloj.h"
#include "config_storage.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "salida";

/* Version del blob. Si algun dia cambia la forma de la estructura, subir esto
 * hace que lo viejo se descarte en vez de leerse torcido. */
#define SALIDA_BLOB_VERSION  2

typedef struct {
    uint8_t  version;
    uint8_t  tipo;                     /* salida_tipo_t */
    uint8_t  n_eventos;
    uint8_t  declarado;                /* solo se usa en SALIDA_PUNTUAL */
    uint32_t epoch_ini;
    char     nombre[SALIDA_NOMBRE_MAX];
    char     carpeta[SALIDA_CARPETA_MAX];
    salida_evento_t eventos[SALIDA_EVENTOS_MAX];
} salida_blob_t;

static salida_blob_t  s_st;
static salida_vista_t s_vista;

/* La marca de vida ANTERIOR a este arranque. Se lee una sola vez en
 * salida_init() y se guarda aqui, porque en cuanto la tarea de vida escriba la
 * primera marca el valor de NVS ya sera el de ahora y no el del apagon. */
static uint32_t s_vida_previa;
static bool     s_olvido_ya_contestado;

/* ── Persistencia ────────────────────────────────────────────────────────── */

static void guardar(void)
{
    s_st.version = SALIDA_BLOB_VERSION;
    esp_err_t err = save_salida_blob(&s_st, sizeof(s_st));
    if (err != ESP_OK) {
        /* No es cosmetico: si esto falla, al volver el contacto la pantalla no
         * sabra que dejo algo abierto y el apunte se pierde en silencio. */
        ESP_LOGE(TAG, "NO se pudo guardar el estado (%s): un apagon ahora "
                      "perderia la salida en curso", esp_err_to_name(err));
    }
}

static void refrescar_vista(void)
{
    s_vista.tipo      = (salida_tipo_t)s_st.tipo;
    s_vista.nombre    = s_st.nombre;
    s_vista.carpeta   = s_st.carpeta;
    s_vista.epoch_ini = s_st.epoch_ini;
    s_vista.n_eventos = s_st.n_eventos;
    s_vista.eventos   = s_st.eventos;
    s_vista.declarado = s_st.declarado != 0;
}

/* ── La marca de vida ────────────────────────────────────────────────────── */

static void vida_task(void *arg)
{
    (void)arg;
    /* La primera al minuto de arrancar y no de inmediato: acota el error al
     * principio del trayecto sin escribir en cada encendido fallido. */
    vTaskDelay(pdMS_TO_TICKS(60 * 1000));
    for (;;) {
        uint32_t ahora;
        if (reloj_ahora(&ahora)) save_salida_vida(ahora);
        vTaskDelay(pdMS_TO_TICKS(SALIDA_VIDA_PERIODO_MS));
    }
}

/* ── Arranque ────────────────────────────────────────────────────────────── */

void salida_init(void)
{
    /* ORDEN IMPORTANTE: leer la marca vieja ANTES de arrancar la tarea que la
     * pisa. Si se hace al reves se pierde la hora del apagon y con ella la
     * posibilidad de ofrecer la parada olvidada. */
    s_vida_previa = load_salida_vida();

    size_t len = sizeof(s_st);
    esp_err_t err = load_salida_blob(&s_st, &len);
    if (err != ESP_OK || len != sizeof(s_st) || s_st.version != SALIDA_BLOB_VERSION) {
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "estado guardado incompatible (version %u, %u bytes): se descarta",
                     (unsigned)s_st.version, (unsigned)len);
        }
        memset(&s_st, 0, sizeof(s_st));
        s_st.version = SALIDA_BLOB_VERSION;
        s_st.tipo    = SALIDA_NINGUNA;
    }

    /* Lo que sale de la NVS no se da por bueno aunque la version y el tamano
     * cuadren. Tres saneos que cuestan tres lineas:
     *  - Los textos, terminados a la fuerza: uno sin terminador se leeria mas
     *    alla de su sitio (aqui mismo se imprimen con %s, y el nombre acaba en
     *    la ruta de la carpeta del viaje en la SD de la P4).
     *  - El tipo, dentro del enum: un valor cualquiera haria creer que hay una
     *    salida en curso que no existe, y salida_hay() solo mira que no sea
     *    NINGUNA.
     *  - El numero de eventos, acotado, o los bucles leerian fuera del array. */
    s_st.nombre[SALIDA_NOMBRE_MAX - 1]   = '\0';
    s_st.carpeta[SALIDA_CARPETA_MAX - 1] = '\0';
    if (s_st.tipo > (uint8_t)SALIDA_PUNTUAL) {
        ESP_LOGW(TAG, "tipo de salida guardado fuera de rango (%u): lo descarto",
                 (unsigned)s_st.tipo);
        s_st.tipo = SALIDA_NINGUNA;
    }
    if (s_st.n_eventos > SALIDA_EVENTOS_MAX) s_st.n_eventos = SALIDA_EVENTOS_MAX;

    refrescar_vista();

    if (s_st.tipo == SALIDA_VIAJE) {
        ESP_LOGI(TAG, "viaje en curso: '%s' (%s), %u evento(s) abierto(s)",
                 s_st.nombre, s_st.carpeta, (unsigned)s_st.n_eventos);
    } else if (s_st.tipo == SALIDA_PUNTUAL) {
        ESP_LOGI(TAG, "salida puntual en curso: '%s' (%s), %s, %u evento(s) abierto(s)",
                 s_st.nombre, s_st.carpeta,
                 s_st.declarado ? "ya declarada" : "esperando llegar",
                 (unsigned)s_st.n_eventos);
    } else {
        ESP_LOGI(TAG, "sin salida en curso");
    }

    if (xTaskCreate(vida_task, "salida_vida", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(vida_task) fallo: sin marca de vida de la salida");
    }
}

const salida_vista_t *salida_get(void)
{
    return &s_vista;
}

/* ── Utilidades de fecha ─────────────────────────────────────────────────── */

uint32_t salida_noches(uint32_t epoch_ini, uint32_t epoch_fin)
{
    if (epoch_fin <= epoch_ini) return 0;
    /* Cambios de dia de calendario, no periodos de 24 h: llegas el viernes por
     * la tarde y te vas el sabado por la manana y eso es UNA noche. Los epoch
     * ya vienen desplazados a hora local, asi que el dia es una division. */
    return (epoch_fin / 86400) - (epoch_ini / 86400);
}

void salida_fecha_compacta(uint32_t epoch_local, char *buf, size_t n)
{
    if (!buf || n < 9) return;
    /* gmtime y no localtime: el epoch YA viene desplazado a la hora local de la
     * P4 (ver mini_proto.h), asi que aplicarle otro huso lo correria otra vez. */
    time_t t = (time_t)epoch_local;
    struct tm tmv;
    gmtime_r(&t, &tmv);

    /* Acotado a proposito: con un epoch corrupto, gmtime_r puede devolver un
     * anio de cinco cifras y la fecha se saldria del buffer. Ademas asi un
     * nombre de carpeta raro se nota (sale "00000101") en vez de reventar. */
    unsigned anio = (unsigned)(tmv.tm_year + 1900);
    unsigned mes  = (unsigned)(tmv.tm_mon + 1);
    unsigned dia  = (unsigned)tmv.tm_mday;
    if (anio > 9999) anio = 0;
    if (mes  > 12)   mes  = 1;
    if (dia  > 31)   dia  = 1;
    snprintf(buf, n, "%04u%02u%02u", anio, mes, dia);
}

bool parada_sitio_es_de_pago(uint8_t sitio)
{
    return sitio == SITIO_PARKING_PAGO ||
           sitio == SITIO_AREA_PAGO    ||
           sitio == SITIO_CAMPING;
}

/* ── Nombre de carpeta ───────────────────────────────────────────────────── */

/* Sanea el nombre tecleado para que valga como nombre de carpeta en la SD.
 *
 * Las vocales acentuadas y la ñ se MAPEAN en vez de tirarse: "Cataluña" tiene
 * que quedar "Cataluna" y no "Cataluа" ni "Catalua". Llegan en UTF-8, o sea en
 * dos bytes que empiezan por 0xC3.
 *
 * Devuelve la longitud escrita. */
static size_t sanear(const char *in, char *out, size_t n)
{
    static const char *const ACENTOS = "aeiouAEIOUnNcCuU";
    /* Segundo byte UTF-8 de: á é í ó ú Á É Í Ó Ú ñ Ñ ç Ç ü Ü */
    static const unsigned char SEGUNDO[] = {
        0xA1,0xA9,0xAD,0xB3,0xBA,0x81,0x89,0x8D,0x93,0x9A,0xB1,0x91,0xA7,0x87,0xBC,0x9C
    };

    size_t u = 0;
    bool ultimo_guion = true;   /* true al principio: evita empezar por '_' */

    for (const unsigned char *p = (const unsigned char *)in; *p && u + 1 < n; p++) {
        char c = 0;

        if (*p == 0xC3 && p[1]) {
            for (size_t i = 0; i < sizeof(SEGUNDO); i++) {
                if (p[1] == SEGUNDO[i]) { c = ACENTOS[i]; break; }
            }
            p++;                       /* consume el segundo byte pase lo que pase */
            if (!c) continue;
        } else if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                   (*p >= '0' && *p <= '9')) {
            c = (char)*p;
        } else if (*p == ' ' || *p == '-' || *p == '_') {
            if (ultimo_guion) continue;         /* no acumular separadores */
            c = '_';
        } else {
            continue;                            /* lo demas, fuera */
        }

        ultimo_guion = (c == '_');
        out[u++] = c;
    }

    while (u > 0 && out[u - 1] == '_') u--;      /* ni terminar en '_' */
    out[u] = 0;
    return u;
}

/* ── Abrir y cerrar la salida ────────────────────────────────────────────── */

bool salida_abrir_viaje(const char *nombre)
{
    uint32_t ahora;
    if (!reloj_ahora(&ahora)) {
        ESP_LOGW(TAG, "no se abre viaje: todavia no hay hora de la P4");
        return false;
    }

    char limpio[SALIDA_NOMBRE_MAX];
    if (sanear(nombre ? nombre : "", limpio, sizeof(limpio)) == 0) {
        ESP_LOGW(TAG, "no se abre viaje: el nombre queda vacio al sanearlo");
        return false;
    }

    char fecha[9];
    salida_fecha_compacta(ahora, fecha, sizeof(fecha));

    memset(&s_st, 0, sizeof(s_st));
    s_st.tipo      = SALIDA_VIAJE;
    s_st.epoch_ini = ahora;
    snprintf(s_st.nombre,  sizeof(s_st.nombre),  "%s", nombre ? nombre : "");
    snprintf(s_st.carpeta, sizeof(s_st.carpeta), "%s_%s", limpio, fecha);

    guardar();
    refrescar_vista();
    ESP_LOGI(TAG, "viaje abierto: '%s' -> carpeta '%s'", s_st.nombre, s_st.carpeta);
    return true;
}

bool salida_abrir_puntual(const char *nombre)
{
    uint32_t ahora;
    if (!reloj_ahora(&ahora)) {
        ESP_LOGW(TAG, "no se abre salida puntual: todavia no hay hora de la P4");
        return false;
    }

    char limpio[SALIDA_NOMBRE_MAX];
    if (sanear(nombre ? nombre : "", limpio, sizeof(limpio)) == 0) {
        ESP_LOGW(TAG, "no se abre salida puntual: el nombre queda vacio al sanearlo");
        return false;
    }

    char fecha[9];
    salida_fecha_compacta(ahora, fecha, sizeof(fecha));

    memset(&s_st, 0, sizeof(s_st));
    s_st.tipo      = SALIDA_PUNTUAL;
    s_st.epoch_ini = ahora;
    snprintf(s_st.nombre,  sizeof(s_st.nombre),  "%s", nombre ? nombre : "");
    snprintf(s_st.carpeta, sizeof(s_st.carpeta), "%s_%s", limpio, fecha);

    guardar();
    refrescar_vista();
    ESP_LOGI(TAG, "salida puntual abierta: '%s' -> carpeta '%s'", s_st.nombre, s_st.carpeta);
    return true;
}

void salida_puntual_marcar_declarada(void)
{
    s_st.declarado = 1;
    guardar();
    refrescar_vista();
    ESP_LOGI(TAG, "salida puntual declarada: '%s'", s_st.nombre);
}

void salida_puntual_desmarcar_declarada(void)
{
    s_st.declarado = 0;
    guardar();
    refrescar_vista();
    ESP_LOGI(TAG, "salida puntual desdeclarada (deshacer): '%s'", s_st.nombre);
}

void salida_cerrar(void)
{
    if (s_st.n_eventos > 0) {
        /* Quien llama deberia haberlos despachado. Si llegamos aqui con algo
         * abierto es un fallo de la pantalla, no del usuario: que se vea. */
        ESP_LOGE(TAG, "se cierra la salida con %u evento(s) abierto(s): se pierden",
                 (unsigned)s_st.n_eventos);
    }
    memset(&s_st, 0, sizeof(s_st));
    s_st.tipo    = SALIDA_NINGUNA;
    /* guardar() y no clear_salida_blob(): la version anterior ignoraba el
     * resultado del borrado -- si fallaba (NVS con problemas justo en ese
     * instante), la salida quedaba cerrada en memoria pero seguia abierta
     * de verdad en el disco, y "resucitaba" al siguiente arranque
     * (salida_init() carga lo que haya en NVS, no lo que se creyo cerrado).
     * guardar() escribe este mismo estado (ya a SALIDA_NINGUNA) por el
     * camino normal, que ya avisa por su cuenta si el commit falla.
     * Detectado el 09-sep-2026. */
    guardar();
    refrescar_vista();
    ESP_LOGI(TAG, "salida cerrada");
}

/* ── Eventos ─────────────────────────────────────────────────────────────── */

uint32_t salida_evento_abrir(evento_tipo_t tipo, uint8_t sub, uint8_t sub2)
{
    if (s_st.tipo == SALIDA_NINGUNA) {
        ESP_LOGW(TAG, "no se abre evento %d: no hay salida en curso", (int)tipo);
        return 0;
    }
    if (s_st.n_eventos >= SALIDA_EVENTOS_MAX) {
        ESP_LOGW(TAG, "no caben mas eventos abiertos (%d)", SALIDA_EVENTOS_MAX);
        return 0;
    }
    uint32_t ahora;
    if (!reloj_ahora(&ahora)) {
        ESP_LOGW(TAG, "no se abre evento %d: todavia no hay hora de la P4", (int)tipo);
        return 0;
    }

    /* El id se reserva AL ABRIR y no al mandar: asi el apunte lleva el mismo
     * numero aunque se reintente, que es de lo que se agarra la P4 para
     * descartar duplicados. */
    uint32_t id = next_trip_seq();

    salida_evento_t *e = &s_st.eventos[s_st.n_eventos++];
    e->tipo      = (uint8_t)tipo;
    e->sub       = sub;
    e->sub2      = sub2;
    e->contado   = 0;
    e->epoch_ini = ahora;
    e->id        = id;

    guardar();
    refrescar_vista();
    ESP_LOGI(TAG, "evento abierto: tipo=%d sub=%u sub2=%u id=%lu (%u abiertos)",
             (int)tipo, (unsigned)sub, (unsigned)sub2,
             (unsigned long)id, (unsigned)s_st.n_eventos);
    return id;
}

bool salida_evento_borrar(int idx)
{
    if (idx < 0 || idx >= (int)s_st.n_eventos) return false;
    for (int i = idx; i + 1 < (int)s_st.n_eventos; i++) {
        s_st.eventos[i] = s_st.eventos[i + 1];
    }
    s_st.n_eventos--;
    memset(&s_st.eventos[s_st.n_eventos], 0, sizeof(salida_evento_t));
    guardar();
    refrescar_vista();
    ESP_LOGI(TAG, "evento %d borrado a mano (%u abiertos)", idx, (unsigned)s_st.n_eventos);
    return true;
}

const salida_evento_t *salida_evento_en(int idx)
{
    if (idx < 0 || idx >= (int)s_st.n_eventos) return NULL;
    return &s_st.eventos[idx];
}

int salida_eventos_abiertos(void)
{
    return s_st.n_eventos;
}

const salida_evento_t *salida_evento_primero(void)
{
    return s_st.n_eventos > 0 ? &s_st.eventos[0] : NULL;
}

bool salida_evento_set_inicio(int idx, uint32_t epoch_local)
{
    if (idx < 0 || idx >= s_st.n_eventos) return false;
    s_st.eventos[idx].epoch_ini = epoch_local;
    guardar();
    refrescar_vista();
    return true;
}

bool salida_evento_marcar_contado(int idx)
{
    if (idx < 0 || idx >= (int)s_st.n_eventos) return false;
    if (s_st.eventos[idx].contado) return true;   /* ya estaba, nada que hacer */
    s_st.eventos[idx].contado = 1;
    guardar();
    return true;
}

/* ── La parada olvidada ──────────────────────────────────────────────────── */

bool salida_olvido_pendiente(uint32_t *segundos_out, uint32_t *epoch_apagado_out)
{
    if (s_olvido_ya_contestado)        return false;
    if (s_st.tipo != SALIDA_VIAJE)     return false;   /* solo tiene sentido en viaje */
    if (s_st.n_eventos > 0)            return false;   /* ya dejo algo abierto */
    if (s_vida_previa == 0)            return false;   /* nunca hubo marca */

    uint32_t ahora;
    if (!reloj_ahora(&ahora))          return false;
    if (ahora <= s_vida_previa)        return false;   /* reloj hacia atras: no inventar */

    uint32_t hueco = ahora - s_vida_previa;
    if (hueco < SALIDA_OLVIDO_MIN_S)   return false;

    if (segundos_out)      *segundos_out      = hueco;
    if (epoch_apagado_out) *epoch_apagado_out = s_vida_previa;
    return true;
}

void salida_olvido_descartar(void)
{
    s_olvido_ya_contestado = true;
}
