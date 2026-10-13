// config_storage.c
#include "config_storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "config_storage";

#define BRIGHTNESS_NAMESPACE "display"
#define BRIGHTNESS_KEY       "brightness"
#define SALIDA_NAMESPACE      "salida"
#define SALIDA_ESTADO_KEY     "estado"
#define SALIDA_VIDA_KEY       "vida"
#define SALIDA_KM_KEY         "ult_km"
#define TILT_NAMESPACE        "tilt"
#define TILT_PITCH_KEY        "pitch_off"
#define TILT_ROLL_KEY         "roll_off"
#define WIFI_NAMESPACE        "wifi"
#define WIFI_SSID_KEY         "ssid"
#define WIFI_PASS_KEY         "password"
#define TRIP_NAMESPACE        "viaje"
#define TRIP_BLOB_KEY         "estado"   /* activo+destino+n_eventos, ver save_trip_inicio() */
#define TRIP_SEQ_KEY          "seq"      /* fuera del blob: propio, ya atomico (ver next_trip_seq) */
/* Credenciales del PORTAL de la P4 (no del Wi-Fi): desde el 21-ago-2026 el
 * portal exige Basic Auth tambien en el nivel abierto, y el satelite escribe
 * en el. Se tecleaan una vez en Ajustes; se leen en la P4, Ajustes -> Wi-Fi. */
#define PORTAL_USER_KEY       "http_user"
#define PORTAL_PASS_KEY       "http_pass"
#define PARADA_NAMESPACE      "parada"
#define PARADA_BLOB_KEY       "estado"   /* blob unico, ver el comentario de save_parada_abierta() */

esp_err_t load_brightness(uint8_t *brightness_out) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(BRIGHTNESS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_u8(h, BRIGHTNESS_KEY, brightness_out);
    if (err != ESP_OK) {
        /* 100 = BRILLO_ALTO (main/brillo.h): este componente no ve los headers
         * de main, asi que el valor va aqui a pelo -- si cambia BRILLO_ALTO,
         * cambia esto tambien. Antes se grababa 5 (el arranque del fork viejo),
         * que brillo.c rechaza y reescribe: dejaba la pantalla casi apagada. */
        *brightness_out = 100; // default: BRILLO_ALTO
        nvs_set_u8(h, BRIGHTNESS_KEY, *brightness_out);
        nvs_commit(h);
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t save_brightness(uint8_t brightness) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(BRIGHTNESS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, BRIGHTNESS_KEY, brightness);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

/* === Salida en curso ====================================================== */

/* Un unico blob con todo el estado. De una sola escritura: si se va la
 * corriente a mitad, NVS deja el valor anterior entero en vez de un estado
 * mezclado (media salida con eventos de la otra). Con una clave por campo eso
 * no estaria garantizado, y aqui la corriente se va constantemente. */
esp_err_t load_salida_blob(void *out, size_t *len)
{
    if (!out || !len) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(SALIDA_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_blob(h, SALIDA_ESTADO_KEY, out, len);
    nvs_close(h);
    return err;
}

esp_err_t save_salida_blob(const void *data, size_t len)
{
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(SALIDA_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, SALIDA_ESTADO_KEY, data, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

uint32_t load_ultimo_km(void)
{
    nvs_handle_t h;
    uint32_t v = 0;
    if (nvs_open(SALIDA_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return 0;
    nvs_get_u32(h, SALIDA_KM_KEY, &v);
    nvs_close(h);
    return v;
}

esp_err_t save_ultimo_km(uint32_t km)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SALIDA_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(h, SALIDA_KM_KEY, km);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

uint32_t load_salida_vida(void)
{
    nvs_handle_t h;
    uint32_t v = 0;
    if (nvs_open(SALIDA_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return 0;
    nvs_get_u32(h, SALIDA_VIDA_KEY, &v);
    nvs_close(h);
    return v;
}

esp_err_t save_salida_vida(uint32_t epoch_local)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SALIDA_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(h, SALIDA_VIDA_KEY, epoch_local);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t load_tilt_calibration(int16_t *pitch_offset_centi, int16_t *roll_offset_centi)
{
    if (!pitch_offset_centi || !roll_offset_centi) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(TILT_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        /* Namespace aun no existe -> sin calibrar todavia, no es un error. */
        *pitch_offset_centi = 0;
        *roll_offset_centi  = 0;
        return ESP_OK;
    }

    int16_t p = 0, r = 0;
    nvs_get_i16(h, TILT_PITCH_KEY, &p);
    nvs_get_i16(h, TILT_ROLL_KEY, &r);
    nvs_close(h);

    *pitch_offset_centi = p;
    *roll_offset_centi  = r;
    return ESP_OK;
}

esp_err_t save_tilt_calibration(int16_t pitch_offset_centi, int16_t roll_offset_centi)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(TILT_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_i16(h, TILT_PITCH_KEY, pitch_offset_centi);
    if (err == ESP_OK) err = nvs_set_i16(h, TILT_ROLL_KEY, roll_offset_centi);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

/* activo+destino+n_eventos en un blob unico: antes eran 3 claves sueltas
 * (activo/destino/n_ev) con su propio nvs_commit cada una, y arrancar o
 * terminar un viaje encadenaba 2-3 de esas llamadas seguidas. Un apagon entre
 * medias -- el contacto se corta a menudo con la 3.5" en marcha -- podia dejar
 * activo=true con destino="" (o al reves): el arranque siguiente pintaba un
 * viaje fantasma o perdia uno real. Con el blob, iniciar/terminar es una
 * escritura sola. seq queda FUERA a proposito (ver next_trip_seq): se llama
 * por cada apunte, mucho mas a menudo que inicio/fin, y ya era atomico por si
 * solo -- meterlo aqui multiplicaria las escrituras sin arreglar nada.
 * Detectado auditando el 07-sep-2026. */
typedef struct {
    uint8_t  activo;
    char     destino[24];
    uint32_t n_eventos;
    /* DESTINO NUEVO, pedido A MITAD DE VIAJE (13-oct-2026). Vacio = no se ha
     * cambiado. Va en el MISMO blob a proposito: asi se guarda y se lee con el
     * resto del estado del viaje y no puede quedar descuadrado con el.
     *
     * OJO AL AMPLIAR EL BLOB: un blob viejo (mas corto) se lee bien porque
     * trip_blob_load() pone a cero antes y NVS devuelve el tamano guardado; el
     * campo nuevo sale vacio, que es justo lo que significa "sin cambio". */
    char     destino_nuevo[24];
    /* VERSION AL FINAL, y no al principio, a proposito: un blob de antes de
     * hoy empieza por 'activo' (0 o 1), asi que un campo de version delante se
     * leeria como una version inventada. Al final, un blob viejo (mas corto) se
     * lee con la version a 0 = "de antes de que hubiera version", que es la
     * verdad. Ver save_trip_destino_nuevo() para el otro cabo. */
    uint8_t  version;
} trip_blob_t;

#define TRIP_BLOB_VERSION 1

/* Devuelve si se ha podido LEER el estado de verdad.
 *
 * Antes esto se tragaba cualquier error y devolvia ESP_OK con todo a cero, y
 * eso era peligroso: un blob ilegible (p.ej. INVALID_LENGTH por bajar de
 * firmware, que deja un blob mas largo del que cabe en esta struct) se
 * traducia en "no hay viaje, cero eventos", y el siguiente
 * save_trip_destino_nuevo() GRABABA esos ceros encima -- con el blob de
 * 'salida' diciendo que el viaje sigue en marcha. Entonces el fin mandaba
 * eventos:1 y la P4 daba el viaje por COMPLETO sin serlo (auditoria del
 * 13-oct-2026, hallazgo M7). */
static esp_err_t trip_blob_load(trip_blob_t *out)
{
    memset(out, 0, sizeof(*out));
    out->version = TRIP_BLOB_VERSION;
    nvs_handle_t h;
    esp_err_t err = nvs_open(TRIP_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;   /* sin viaje: todo a cero */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "trip_blob: no puedo abrir la NVS (%s)", esp_err_to_name(err));
        return err;
    }
    size_t len = sizeof(*out);
    esp_err_t g = nvs_get_blob(h, TRIP_BLOB_KEY, out, &len);
    nvs_close(h);
    if (g == ESP_ERR_NVS_NOT_FOUND) {                  /* primera vez: sin viaje */
        memset(out, 0, sizeof(*out));
        out->version = TRIP_BLOB_VERSION;
        return ESP_OK;
    }
    if (g != ESP_OK) {
        ESP_LOGE(TAG, "trip_blob: no puedo leer el estado del viaje (%s): NO lo "
                      "tomo por vacio, que grabarlo encima perderia la cuenta de "
                      "eventos", esp_err_to_name(g));
        memset(out, 0, sizeof(*out));
        return g;
    }
    /* LA VERSION SE DEDUCE DEL TAMANO LEIDO, no se pone a 1 antes de leer: un
     * blob guardado por un firmware anterior es mas CORTO, y nvs_get_blob solo
     * copia sus bytes -- el campo del final se quedaba con lo que hubiera puesto
     * el memset (o sea TRIP_BLOB_VERSION), asi que un blob viejo se declaraba
     * "de la version nueva". Revision del 13-oct-2026; hoy nadie lee el campo,
     * pero es la trampa que se pondria el dia que alguien amplie el blob. */
    out->version = (len >= sizeof(*out)) ? TRIP_BLOB_VERSION : 0;
    return ESP_OK;
}

static esp_err_t trip_blob_save(const trip_blob_t *in)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(TRIP_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, TRIP_BLOB_KEY, in, sizeof(*in));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

/* Destino del viaje en curso. Es lo que da nombre a la carpeta en la SD de la
 * P4, asi que se guarda aqui tambien: si la 3.5" se reinicia a media entrega,
 * tiene que poder repetir el mismo nombre y no crear una carpeta nueva. */
esp_err_t load_trip_destino(char *out, size_t *len)
{
    if (!out || !len) return ESP_ERR_INVALID_ARG;
    trip_blob_t b;
    trip_blob_load(&b);
    snprintf(out, *len, "%s", b.destino);
    *len = strlen(out) + 1;
    return ESP_OK;
}

/* Destino nuevo pedido a mitad de viaje. Se guarda en cuanto se teclea, para
 * que un apagon (de la cabina o de la P4) no se lleve el nombre por delante. */
esp_err_t save_trip_destino_nuevo(const char *destino)
{
    trip_blob_t b;
    /* SI NO SE HA PODIDO LEER, NO SE GRABA: guardar aqui seria escribir ceros
     * (activo=0, n_eventos=0) sobre un viaje que sigue abierto, y la P4 daria
     * el viaje por completo con eventos:1. Antes se hacia justo eso. */
    esp_err_t e = trip_blob_load(&b);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "no guardo el destino nuevo: el estado del viaje no se ha "
                      "podido leer");
        return e;
    }
    snprintf(b.destino_nuevo, sizeof(b.destino_nuevo), "%s", destino ? destino : "");
    b.version = TRIP_BLOB_VERSION;
    return trip_blob_save(&b);
}

esp_err_t load_trip_destino_nuevo(char *out, size_t len)
{
    if (!out || !len) return ESP_ERR_INVALID_ARG;
    trip_blob_t b;
    trip_blob_load(&b);
    snprintf(out, len, "%s", b.destino_nuevo);
    return ESP_OK;
}

esp_err_t save_trip_inicio(const char *destino)
{
    trip_blob_t b = {0};
    b.activo = 1;
    snprintf(b.destino, sizeof(b.destino), "%s", destino ? destino : "");
    b.destino_nuevo[0] = 0;   /* el cambio de destino es del viaje que empieza */
    b.n_eventos = 1;   /* el inicio ya cuenta */
    return trip_blob_save(&b);
}

esp_err_t save_trip_fin(void)
{
    trip_blob_t b;
    /* SI NO SE HA PODIDO LEER, NO SE GRABA: escribir aqui seria dejar el estado
     * del viaje a ceros (activo=0, n_eventos=0) sobre un viaje que sigue
     * abierto. La P4 daria el viaje por COMPLETO con eventos:1. Revision del
     * 13-oct-2026: el arreglo se habia quedado solo en
     * save_trip_destino_nuevo(). */
    esp_err_t leido = trip_blob_load(&b);
    if (leido != ESP_OK) {
        ESP_LOGE(TAG, "%s: no grabo, el estado del viaje no se ha podido leer", __func__);
        return leido;
    }   /* conservar n_eventos: lo resetea el proximo inicio */
    b.activo = 0;
    b.destino[0] = '\0';
    return trip_blob_save(&b);
}

/* Contador de apuntes, CRECIENTE Y SIN HUECOS: es lo que permite a la P4
 * descartar duplicados cuando un reintento llega dos veces. Persistente porque
 * la 3.5" se apaga con el contacto constantemente. Fuera del blob de arriba
 * (ver el comentario de trip_blob_t). */
uint32_t next_trip_seq(void)
{
    nvs_handle_t h;
    uint32_t v = 0;
    /* REINTENTO, y el 0 deja de ser un id valido (auditoria del 13-oct-2026).
     * Un fallo puntual de nvs_open devolvia 0, y con id 0 la P4 se SALTABA la
     * comprobacion de duplicados (id_ya_aplicado solo mira ids != 0): si se
     * perdia el 200, el reintento volvia a aplicarse y el importe se sumaba
     * DOS veces. Los ids de verdad empiezan en 1. */
    esp_err_t abierto = ESP_FAIL;
    for (int i = 0; i < 3 && abierto != ESP_OK; i++) {
        abierto = nvs_open(TRIP_NAMESPACE, NVS_READWRITE, &h);
        if (abierto != ESP_OK) vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (abierto != ESP_OK) {
        ESP_LOGE(TAG, "next_trip_seq: no puedo abrir la NVS (%s): devuelvo 0, "
                      "que NO es un id valido y la P4 rechazara",
                 esp_err_to_name(abierto));
        return 0;
    }
    nvs_get_u32(h, TRIP_SEQ_KEY, &v);
    v++;
    esp_err_t err = nvs_set_u32(h, TRIP_SEQ_KEY, v);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    /* Si esto falla, el numero devuelto NO ha quedado grabado: un apagon
     * ahora mismo (el contacto se corta a menudo con la 3.5" en marcha) hace
     * que el siguiente arranque vuelva a repartir este mismo id. No hay forma
     * barata de evitarlo del todo aqui, pero al menos que quede en el log en
     * vez de fallar en silencio -- es lo que explica un "duplicado" raro en
     * la P4 semanas despues. */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "next_trip_seq: no se pudo grabar %lu (%s) -- riesgo de "
                      "id repetido si hay un apagon antes del proximo commit",
                 (unsigned long)v, esp_err_to_name(err));
    }
    return v;
}

/* Cuantos apuntes ha GENERADO este viaje, contando el inicio.
 *
 * Se cuenta lo generado y NO lo entregado, y esa es la clave: si un apunte no
 * llega a encolarse (cola llena, fallo de NVS), el contador sube igual y la P4
 * vera que le faltan. Si contaramos solo lo encolado, un apunte perdido cuadraria
 * las cuentas y el viaje se daria por completo sin serlo -- justo lo que este
 * contador existe para impedir. */
uint32_t trip_eventos_get(void)
{
    trip_blob_t b;
    trip_blob_load(&b);
    return b.n_eventos;
}

uint32_t trip_eventos_inc(void)
{
    trip_blob_t b;
    /* NO SE INCREMENTA A CIEGAS: si el estado no se ha podido leer, grabar el
     * incremento dejaria el blob a ceros con n_eventos=1, y el fin mandaria
     * "eventos:1" -- la P4 daria el viaje por COMPLETO sin serlo (revision del
     * 13-oct-2026: el arreglo se habia quedado solo en save_trip_destino_nuevo).
     * Se devuelve lo que haya (0 si no se pudo leer) y se avisa. */
    esp_err_t leido = trip_blob_load(&b);
    if (leido != ESP_OK) {
        ESP_LOGE(TAG, "trip_eventos_inc: estado del viaje ilegible (%s): NO "
                      "cuento este apunte; la P4 vera el viaje corto",
                 esp_err_to_name(leido));
        return 0;
    }
    b.n_eventos++;
    esp_err_t err = trip_blob_save(&b);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "trip_eventos_inc: no se pudo grabar %lu (%s) -- el "
                      "'esperados' que vera la P4 puede quedar corto",
                 (unsigned long)b.n_eventos, esp_err_to_name(err));
    }
    return b.n_eventos;
}

esp_err_t load_portal_creds(char *user_out, size_t *user_len,
                            char *pass_out, size_t *pass_len)
{
    if (!user_out || !user_len || !pass_out || !pass_len) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_str(h, PORTAL_USER_KEY, user_out, user_len);
    if (err == ESP_OK) err = nvs_get_str(h, PORTAL_PASS_KEY, pass_out, pass_len);
    nvs_close(h);
    return err;
}

esp_err_t save_portal_creds(const char *user, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, PORTAL_USER_KEY, user ? user : "");
    if (err == ESP_OK) err = nvs_set_str(h, PORTAL_PASS_KEY, pass ? pass : "");
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t load_parada_abierta(parada_abierta_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    nvs_handle_t h;
    esp_err_t err = nvs_open(PARADA_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return ESP_OK;   /* nunca hubo parada; no es un error */

    size_t len = sizeof(*out);
    parada_abierta_t tmp;
    if (nvs_get_blob(h, PARADA_BLOB_KEY, &tmp, &len) == ESP_OK && len == sizeof(tmp)) {
        *out = tmp;
    }
    nvs_close(h);

    /* Sin hora de inicio no hay forma de contar el tiempo, asi que una parada
     * asi se da por no abierta en vez de quedarse colgada para siempre
     * preguntando lo que no se puede responder. */
    out->abierta = out->abierta && (out->epoch_inicio > 0);
    return ESP_OK;
}

/* Blob unico y un solo commit, no 5 claves sueltas con 5 escrituras
 * independientes: un apagon a mitad (el contacto se corta a menudo con la
 * 3.5" en marcha) podia dejar 'abierta' y 'epoch_inicio' grabados pero
 * 'cobro'/'moneda'/'precio' todavia con los de la parada ANTERIOR -- una
 * parada que mezclaba datos de dos paradas distintas sin ningun aviso. El
 * proyecto ya usa este patron para "salida" (ver save_salida_blob());
 * aqui no se habia aplicado. Detectado auditando el 07-sep-2026. */
esp_err_t save_parada_abierta(const parada_abierta_t *p)
{
    if (!p) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(PARADA_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, PARADA_BLOB_KEY, p, sizeof(*p));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t clear_parada_abierta(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(PARADA_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(h, PARADA_BLOB_KEY);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t load_wifi_config(char *ssid_out, size_t *ssid_len,
                           char *pass_out, size_t *pass_len)
{
    if (!ssid_out || !ssid_len || !pass_out || !pass_len) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        /* Namespace aun no existe -> primer arranque, NVS vacia. */
        return err;
    }

    err = nvs_get_str(h, WIFI_SSID_KEY, ssid_out, ssid_len);
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }
    err = nvs_get_str(h, WIFI_PASS_KEY, pass_out, pass_len);
    nvs_close(h);
    return err;
}

esp_err_t save_wifi_config(const char *ssid, const char *pass)
{
    if (!ssid || !pass) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, WIFI_SSID_KEY, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, WIFI_PASS_KEY, pass);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
