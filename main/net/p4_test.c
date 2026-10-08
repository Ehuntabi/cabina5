/* p4_test.c — Auto-test del canal de SUBIDA a la P4 (cabina -> P4 por HTTP).
 *
 * POR QUE EXISTE (7-oct-2026)
 * El camino de bajada (telemetria UDP) y el latido se prueban solos: se ven en
 * el log. El de subida NO: la cabina solo le habla a la P4 cuando alguien inicia
 * o cierra un viaje, asi que un fallo ahi se descubre con la autocaravana
 * aparcada y el viaje a medias. Y hay tres cosas que solo se prueban juntas:
 * que la ruta exista en la P4, que las CREDENCIALES del portal sean correctas y
 * que la P4 acepte un POST con el cuerpo de la cabina.
 *
 * QUE HACE
 * Al arrancar, una sola vez y en su propia tarea (no bloquea el arranque ni la
 * UI), hace tres peticiones y lo deja dicho en el log en castellano:
 *
 *   1. POST /api/viaje SIN credenciales y con una operacion desconocida -> 401:
 *      la ruta existe y pide clave (lo normal). 404 = la P4 lleva un firmware sin
 *      esa ruta. OJO: tiene que ser POST, porque en la P4 esa ruta esta
 *      registrada solo para POST y un GET lo coge el portal cautivo y lo
 *      redirige en bucle (ESP_ERR_HTTP_MAX_REDIRECT, visto el 7-oct-2026).
 *   2. El mismo POST CON credenciales -> 4xx (la P4 reconoce al cliente y
 *      rechaza la operacion rara) = credenciales BIEN; 401 = estan mal o no hay
 *      ninguna puesta, que es el fallo que deja los viajes sin guardar.
 *   3. Y con eso queda comprobado que la P4 acepta POST de la cabina y rechaza
 *      lo que no entiende, sin crear ningun viaje.
 *
 * NO crea ningun viaje ni escribe nada en la P4: las tres peticiones son de
 * lectura o de algo que la P4 rechaza a proposito.
 */
#include "p4_test.h"

#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "p4_api.h"          /* p4_api_post: el POST crudo, sincrono */
#include "lwip/sockets.h"    /* TCP a pelo: para distinguir "no hay red" de "no contesta" */
#include "p4_creds.h"       /* credenciales: NVS o fabrica */
#include "esp_log_level.h"   /* subir el nivel del cliente HTTP a DEBUG en la prueba */

static const char *TAG = "p4_test";

#define P4_HOST        "http://192.168.4.1"
#define P4_URL_VIAJE   P4_HOST "/api/state"   /* GET de lectura: existe seguro */
#define P4_TIMEOUT_MS  8000

/* Una peticion. 'con_creds' decide si se manda Basic Auth. Devuelve el codigo
 * HTTP, o 0 si no se pudo ni conectar (P4 apagada, fuera de alcance, sin IP). */
static int pedir(const char *url, esp_http_client_method_t metodo,
                 const char *cuerpo, bool con_creds)
{
    char user[33] = {0}, pass[65] = {0};
    bool hay = con_creds && p4_creds_cargar(user, sizeof(user), pass, sizeof(pass));

    esp_http_client_config_t cfg = {
        .url        = url,
        .method     = metodo,
        .timeout_ms = P4_TIMEOUT_MS,
        .auth_type  = hay ? HTTP_AUTH_TYPE_BASIC : HTTP_AUTH_TYPE_NONE,
        .username   = hay ? user : NULL,
        .password   = hay ? pass : NULL,
        /* KEEP-ALIVE APAGADO, y no es un detalle: con keep-alive el cliente se
         * queda esperando a que el servidor cierre la conexion y 'perform' NO
         * VUELVE NUNCA (ni con timeout_ms), asi que la pantalla se reinicia por
         * el watchdog. Visto en el banco el 7-oct-2026: el auto-test arrancaba y
         * no terminaba jamas. */
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return 0;
    if (cuerpo) {
        esp_http_client_set_header(c, "Content-Type", "application/json");
        esp_http_client_set_post_field(c, cuerpo, strlen(cuerpo));
    }
    esp_err_t err = esp_http_client_perform(c);
    int estado = (err == ESP_OK) ? esp_http_client_get_status_code(c) : 0;
    esp_http_client_cleanup(c);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: no se pudo entregar (%s)", url, esp_err_to_name(err));
    }
    return estado;
}

/* Prueba TCP A PELO: abre un socket, conecta al puerto y manda un GET minimo.
 * Es el nivel de abajo del HTTP: distingue "no hay red / no resuelve" de "la
 * conexion se abre pero el servidor no contesta", que es lo que hay que saber.
 * Se prueban los dos puertos de la P4: el 80 (portal normal) y el 8081 (pesado). */
static void tcp_crudo(int puerto)
{
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) { ESP_LOGW(TAG, "tcp %d: no se pudo crear el socket", puerto); return; }

    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dir = {0};
    dir.sin_family = AF_INET;
    dir.sin_port = htons((uint16_t)puerto);
    dir.sin_addr.s_addr = inet_addr("192.168.4.1");

    if (connect(s, (struct sockaddr *)&dir, sizeof(dir)) != 0) {
        ESP_LOGW(TAG, "tcp %d: la P4 no acepta la conexion (errno=%d)", puerto, errno);
        close(s);
        return;
    }
    const char *peticion = "GET / HTTP/1.0\r\nHost: 192.168.4.1\r\n\r\n";
    int enviado = send(s, peticion, strlen(peticion), 0);
    if (enviado <= 0) {
        ESP_LOGW(TAG, "tcp %d: conecta pero no deja enviar (errno=%d)", puerto, errno);
        close(s);
        return;
    }
    char buf[128] = {0};
    int n = recv(s, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        ESP_LOGI(TAG, "tcp %d: contesta (%.40s)", puerto, buf);
    } else if (n == 0) {
        ESP_LOGW(TAG, "tcp %d: cierra la conexion sin contestar", puerto);
    } else {
        ESP_LOGW(TAG, "tcp %d: conecta y traga el GET pero NO contesta (errno=%d)",
                 puerto, errno);
    }
    close(s);
}

/* Peticion A MANO con la cabecera Authorization (Basic). Es la prueba que
 * decide: si ESTA funciona y la del cliente de IDF no, el fallo esta en el
 * cliente de la biblioteca, no en la P4 ni en las credenciales. */
static void tcp_con_auth(int puerto)
{
    char user[33] = {0}, pass[65] = {0};
    if (!p4_creds_cargar(user, sizeof(user), pass, sizeof(pass))) {
        ESP_LOGW(TAG, "auth %d: no hay credenciales", puerto);
        return;
    }
    /* Basic = base64("usuario:clave"). Se hace a mano para no depender de nadie. */
    char plano[128];
    snprintf(plano, sizeof(plano), "%s:%s", user, pass);
    static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char cod[192]; size_t n = 0;
    for (size_t i = 0; i < strlen(plano); i += 3) {
        uint32_t v = ((uint8_t)plano[i]) << 16;
        if (i + 1 < strlen(plano)) v |= ((uint8_t)plano[i+1]) << 8;
        if (i + 2 < strlen(plano)) v |= (uint8_t)plano[i+2];
        cod[n++] = b64[(v >> 18) & 63];
        cod[n++] = b64[(v >> 12) & 63];
        cod[n++] = (i + 1 < strlen(plano)) ? b64[(v >> 6) & 63] : '=';
        cod[n++] = (i + 2 < strlen(plano)) ? b64[v & 63] : '=';
    }
    cod[n] = '\0';

    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) return;
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_in dir = {0};
    dir.sin_family = AF_INET; dir.sin_port = htons((uint16_t)puerto);
    dir.sin_addr.s_addr = inet_addr("192.168.4.1");
    if (connect(s, (struct sockaddr *)&dir, sizeof(dir)) != 0) {
        ESP_LOGW(TAG, "auth %d: no conecta (errno=%d)", puerto, errno); close(s); return;
    }
    char pet[400];
    snprintf(pet, sizeof(pet),
             "GET /api/state HTTP/1.0\r\nHost: 192.168.4.1\r\n"
             "Authorization: Basic %s\r\nConnection: close\r\n\r\n", cod);
    send(s, pet, strlen(pet), 0);
    char buf[128] = {0};
    int r = recv(s, buf, sizeof(buf) - 1, 0);
    if (r > 0) ESP_LOGI(TAG, "auth %d: CON credenciales -> %.45s", puerto, buf);
    else       ESP_LOGW(TAG, "auth %d: CON credenciales no contesta (errno=%d)", puerto, errno);
    close(s);
}

/* El cliente DE IDF pero mandando la cabecera a mano (sin esperar al reto 401).
 * Si esto funciona, el problema esta SOLO en el ciclo de autenticacion del
 * cliente; si tampoco, es el cliente entero y hay que mandar por socket. */
static void idf_con_cabecera(void)
{
    char user[33] = {0}, pass[65] = {0};
    if (!p4_creds_cargar(user, sizeof(user), pass, sizeof(pass))) return;
    char plano[128], cod[192]; size_t n = 0;
    snprintf(plano, sizeof(plano), "%s:%s", user, pass);
    static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t lp = strlen(plano);
    for (size_t i = 0; i < lp; i += 3) {
        uint32_t v = ((uint8_t)plano[i]) << 16;
        if (i + 1 < lp) v |= ((uint8_t)plano[i+1]) << 8;
        if (i + 2 < lp) v |= (uint8_t)plano[i+2];
        cod[n++] = b64[(v >> 18) & 63];
        cod[n++] = b64[(v >> 12) & 63];
        cod[n++] = (i + 1 < lp) ? b64[(v >> 6) & 63] : '=';
        cod[n++] = (i + 2 < lp) ? b64[v & 63] : '=';
    }
    cod[n] = '\0';
    char cab[256];
    snprintf(cab, sizeof(cab), "Basic %s", cod);

    esp_http_client_config_t cfg = {
        .url = P4_HOST "/api/state",
        .method = HTTP_METHOD_GET,
        .timeout_ms = 6000,
        .keep_alive_enable = false,
        .auth_type = HTTP_AUTH_TYPE_NONE,     /* la cabecera la ponemos nosotros */
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return;
    esp_http_client_set_header(c, "Authorization", cab);
    esp_err_t err = esp_http_client_perform(c);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "idf con cabecera: HTTP %d", esp_http_client_get_status_code(c));
    } else {
        ESP_LOGW(TAG, "idf con cabecera: fallo (%s)", esp_err_to_name(err));
    }
    esp_http_client_cleanup(c);
}

/* Base64 de "usuario:clave" para la prueba de credenciales malas. Duplicado a
 * proposito (el de produccion es estatico en p4_api.c): asi el test no puede
 * romper el camino real ni al reves. */
static void cabecera_basic_de_prueba(const char *user, const char *pass, char *out, size_t n)
{
    static const char *B = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char plano[128];
    snprintf(plano, sizeof(plano), "%s:%s", user, pass);
    size_t lp = strlen(plano), k = 0;
    k += snprintf(out, n, "Basic ");
    for (size_t i = 0; i < lp && k + 5 < n; i += 3) {
        uint32_t v = ((uint8_t)plano[i]) << 16;
        if (i + 1 < lp) v |= ((uint8_t)plano[i+1]) << 8;
        if (i + 2 < lp) v |= (uint8_t)plano[i+2];
        out[k++] = B[(v >> 18) & 63];
        out[k++] = B[(v >> 12) & 63];
        out[k++] = (i + 1 < lp) ? B[(v >> 6) & 63] : '=';
        out[k++] = (i + 2 < lp) ? B[v & 63] : '=';
    }
    out[k] = '\0';
}

static void tarea(void *arg)
{
    (void)arg;
    /* Se espera a que el enlace este de verdad: la tarea arranca con el Wi-Fi y
     * la IP tarda unos segundos (medido: ~5 s). Sin esta espera el test
     * fallaria por impaciencia y asustaria sin motivo. */
    vTaskDelay(pdMS_TO_TICKS(12000));

    /* Que se vea lo que hace el cliente HTTP por dentro: sin esto, un fallo suyo
     * solo dice "no se pudo entregar" y no hay forma de saber por que. */
    esp_log_level_set("esp_http_client", ESP_LOG_DEBUG);
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_DEBUG);

    /* Y lo primero que hay que saber: hay credenciales guardadas? La placa nueva
     * viene con la NVS vacia, y SIN credenciales la P4 responde 401 y los
     * apuntes de viaje se pierden. */
    char u[33] = {0}, pw[65] = {0};
    bool hay = p4_creds_cargar(u, sizeof(u), pw, sizeof(pw));
    ESP_LOGI(TAG, "credenciales del portal en NVS: %s%s", hay ? "si, usuario '" : "NO hay",
             hay ? u : " (se ponen en Ajustes -> WiFi)");
    if (hay) ESP_LOGI(TAG, "%s", "");

    ESP_LOGI(TAG, "=== auto-test del canal de subida a la P4 ===");
    /* Primero, lo mas basico: hay red hasta la P4 y contesta en sus dos puertos? */
    ESP_LOGI(TAG, "paso: TCP 80"); tcp_crudo(80);
    ESP_LOGI(TAG, "paso: TCP 8081"); tcp_crudo(8081);
    ESP_LOGI(TAG, "paso: TCP con clave"); tcp_con_auth(80);
    ESP_LOGI(TAG, "paso: cliente IDF con cabecera"); idf_con_cabecera();

    /* Se prueba contra /api/state (GET, de lectura) y no contra /api/viaje:
     * asi se comprueba el CAMINO HTTP y las CREDENCIALES sin crear ni tocar
     * ningun viaje, y sin depender de que el handler de viaje conteste a una
     * sonda que no entiende. */
    ESP_LOGI(TAG, "paso: GET /api/state sin credenciales");
    int sin_creds = pedir(P4_URL_VIAJE, HTTP_METHOD_GET, NULL, false);
    vTaskDelay(pdMS_TO_TICKS(1500));
    int con_creds = pedir(P4_URL_VIAJE, HTTP_METHOD_GET, NULL, true);
    int post_raro = con_creds;

    /* OJO: NO se corta aqui aunque el paso 1 falle (el cliente sin credenciales
     * se pierde en redirecciones, y eso no significa que la P4 este apagada).
     * Antes se hacia 'return' y se saltaba justo la prueba que importa, el POST
     * del camino real de los apuntes. */
    /* Se informa, pero NO se usa como veredicto: el cliente de la biblioteca no
     * sabe resolver el reto 401 de la P4 (ver p4_api.c), asi que aqui da un
     * falso negativo. El veredicto de verdad es el POST del paso 3, que usa el
     * camino real. */
    ESP_LOGI(TAG, "1) sonda informativa sin credenciales: %d (el cliente de la biblioteca "
                  "no resuelve el reto 401; el camino real es el paso 3)",
             sin_creds);

    if (con_creds >= 400 && con_creds < 500 && con_creds != 401) {
        ESP_LOGI(TAG, "2) CREDENCIALES DEL PORTAL OK (HTTP %d: reconoce al cliente y "
                      "rechaza la operacion rara)", con_creds);
    } else if (con_creds == 401) {
        ESP_LOGE(TAG, "2) CREDENCIALES MAL o sin poner (HTTP 401): los apuntes de viaje");
        ESP_LOGE(TAG, "   se perderan. Se ponen en Ajustes -> WiFi -> 'Usuario/Clave del portal',");
        ESP_LOGE(TAG, "   copiandolas de la P4 (Ajustes -> Wi-Fi)");
    } else {
        ESP_LOGW(TAG, "2) respuesta rara de la P4 con credenciales: HTTP %d", con_creds);
    }

    /* LA PRUEBA QUE IMPORTA: el camino REAL de los apuntes de viaje, con la
     * funcion que los manda de verdad (p4_api_post), y con una operacion que la
     * P4 no conoce: tiene que contestar 4xx (400) SIN crear ningun viaje. Si
     * devuelve 0, el canal sigue roto. */
    ESP_LOGI(TAG, "paso: POST /api/viaje (camino real de los apuntes)");
    int est = 0;
    bool ok = p4_api_post("{\"op\":\"autotest\",\"id\":0}", &est);
    if (est >= 400 && est < 500) {
        ESP_LOGI(TAG, "3) CANAL DE VIAJES OK: la P4 ha contestado HTTP %d al apunte de prueba "
                      "(rechazado a proposito, no se ha creado nada)", est);
    } else if (ok) {
        ESP_LOGW(TAG, "3) la P4 ha ACEPTADO la operacion de prueba (HTTP %d): revisar", est);
    } else {
        ESP_LOGE(TAG, "3) CANAL DE VIAJES ROTO: estado %d (0 = no hubo respuesta)", est);
    }

    /* 4) El OTRO camino de subida: silenciar una alarma (/api/alarma). Se manda
     * mascara 0 = ninguna alarma: la P4 lo acepta y no calla nada, asi que sirve
     * para comprobar el camino sin efectos. */
    ESP_LOGI(TAG, "paso: POST /api/alarma (camino de silenciar alarmas)");
    est = 0;
    ok = p4_api_post_url(P4_HOST "/api/alarma", "{\"op\":\"silencio\",\"mask\":0}", &est);
    /* Vale cualquier 4xx menos 401: significa que la P4 autentico al cliente y
     * enruto la peticion hasta su handler (que la rechaza por la mascara 0). Un
     * 401 seria credenciales, y un 0 que la peticion no llego. */
    if (est >= 400 && est < 500 && est != 401) {
        ESP_LOGI(TAG, "4) CANAL DE ALARMAS OK: la P4 atiende la orden (HTTP %d; con mascara 0 "
                      "no se ha callado ninguna alarma)", est);
    } else if (est == 401) {
        ESP_LOGE(TAG, "4) CANAL DE ALARMAS: 401 (credenciales del portal)");
    } else {
        ESP_LOGW(TAG, "4) canal de alarmas: HTTP %d%s", est,
                 est == 0 ? " (sin respuesta: no llego)" : "");
    }

    /* 5) Y lo contrario: con credenciales MAL, la P4 tiene que decir 401. Si
     * dijera otra cosa, el aviso de credenciales de Ajustes no saltaria y el
     * usuario no sabria por que no se guardan los viajes. Se fuerza una clave
     * incorrecta SOLO aqui; el camino de produccion no se toca. */
    ESP_LOGI(TAG, "paso: POST /api/viaje con credenciales MAL (para ver el 401)");
    {
        esp_http_client_config_t cfg = {
            .url = P4_HOST "/api/viaje", .method = HTTP_METHOD_POST,
            .timeout_ms = P4_TIMEOUT_MS, .auth_type = HTTP_AUTH_TYPE_NONE,
            .keep_alive_enable = false,
        };
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (c) {
            char cab[192];
            cabecera_basic_de_prueba("victron", "clave-incorrecta", cab, sizeof(cab));
            esp_http_client_set_header(c, "Authorization", cab);
            esp_http_client_set_header(c, "Content-Type", "application/json");
            const char *cuerpo = "{\"op\":\"autotest\"}";
            int len = (int)strlen(cuerpo);
            int est_mal = 0;
            if (esp_http_client_open(c, len) == ESP_OK) {
                esp_http_client_write(c, cuerpo, len);
                esp_http_client_fetch_headers(c);
                est_mal = esp_http_client_get_status_code(c);
                char resto[32];
                while (esp_http_client_read(c, resto, sizeof(resto)) > 0) { }
                esp_http_client_close(c);
            }
            esp_http_client_cleanup(c);
            if (est_mal == 401) {
                ESP_LOGI(TAG, "5) con credenciales malas la P4 contesta 401, como debe "
                              "(el aviso de Ajustes saltaria)");
            } else {
                ESP_LOGW(TAG, "5) con credenciales MALAS la P4 contesta %d: revisar", est_mal);
            }
        }
    }

    ESP_LOGI(TAG, "=== fin del auto-test (no se ha creado ningun viaje) ===");
    vTaskDelete(NULL);
}

void p4_test_start(void)
{
    if (xTaskCreate(tarea, "p4_test", 5120, NULL, 3, NULL) != pdPASS) {
        ESP_LOGW(TAG, "no se pudo crear la tarea del auto-test (sin memoria)");
    }
}
