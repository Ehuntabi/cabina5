/* udp_rx.c - Receiver UDP de 35cabina, asociado al SoftAP de la P4 como STA.
 *
 * Portado del satelite viejo C6 (victron_mini/main/net/udp_rx.c, retirado el
 * 17-sep-2026): mismo protocolo, mismo patron de reconexion.
 *
 * SSID/password NO estan hardcodeados en firmware: se guardan en NVS
 * (config_storage.c, load/save_wifi_config) para poder cambiar de P4 (ej.
 * la de repuesto para pruebas) desde la pantalla de Ajustes sin
 * reflashear. wifi_credentials.h (no versionado) solo se usa como valor
 * de fabrica la PRIMERA vez que arranca con NVS vacia -- el mismo
 * problema que ya sufrio victron_mini (password fija en firmware, sin
 * forma de cambiarla sin reflashear) queda resuelto aqui.
 *
 * Flujo:
 *  1. nvs/netif/event_loop init.
 *  2. Carga SSID/pass de NVS (o de wifi_credentials.h si es la primera vez).
 *  3. esp_wifi en modo STA, conecta al AP de la P4.
 *  4. Reconexion automatica si la pierde.
 *  5. Tras obtener IP por DHCP, arranca task que bind UDP en :4242 y
 *     llama data_model_update_from_msg con cada mini_msg_t valido.
 */
#include "udp_rx.h"
#include "mini_proto.h"
#include "wifi_credentials.h"
#include "config_storage.h"
#include "../data_model.h"
#include "../reloj.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_crc.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include <string.h>
#include <errno.h>
#include <sys/time.h>   /* struct timeval: la espera del socket (SO_RCVTIMEO) */

static const char *TAG = "udp_rx";

#define WIFI_BIT_CONNECTED  BIT0
#define WIFI_BIT_GOT_IP     BIT1

static EventGroupHandle_t s_wifi_events;
static uint32_t s_msgs_ok = 0;
static uint32_t s_msgs_bad = 0;

/* Estado del enlace, para la pantalla de informacion (view_info.c): con esto se
 * distingue "no veo la red" de "estoy asociada pero la P4 no manda nada" de
 * "todo bien". Sin esto, en la furgo solo se veia el punto gris y habia que
 * adivinar por que. 30-sep-2026. */
static _Atomic bool s_asociado = false;
static volatile int64_t s_ultimo_rx_us = 0;
/* Senal medida por ESTA tarea, no por la de dibujo: preguntarle al Wi-Fi desde
 * la tarea de LVGL puede bloquearla (la API coge su cerrojo) y con el vigilante
 * de tareas encima eso es un reinicio. Paso el 30-sep-2026: la pantalla de
 * Configuracion reiniciaba la placa por preguntar cada segundo. */
static volatile int     s_rssi_cache = 0;
static volatile int64_t s_rssi_us = 0;
static esp_timer_handle_t s_reconnect_timer;
static esp_timer_handle_t s_dhcp_timer;      /* asociada sin IP: ver dhcp_timer_cb */
#define RECONNECT_DELAY_US (500 * 1000)

/* ── Vigilante de DATOS (2-oct-2026) ────────────────────────────────────────
 *
 * Para que sirve: la cabina puede quedar ASOCIADA y MUDA. Paso el 2-oct-2026 en
 * el banco, con la P4 recien arrancada: la radio aceptaba la asociacion (eso lo
 * hace el C6 por su cuenta) pero NINGUN paquete cruzaba en ningun sentido, asi
 * que ni llegaba la telemetria ni el DHCP contestaba. La pantalla se quedaba en
 * "sin datos" y no habia forma de salir de ahi sin tocar nada.
 *
 * Con esto: si estamos asociados y llevamos SIN_DATOS_US sin recibir un mensaje
 * valido, se fuerza una reconexion (desconectar y volver a conectar). El
 * reconectar rehace la asociacion desde cero y, con ella, el camino de datos.
 * Los primeros intentos van seguidos; si no hay manera, se espacian mucho para
 * no estar tirando el enlace cada poco.
 *
 * LOS NUMEROS ESTAN ELEGIDOS CON CUIDADO (2-oct-2026, corregidos el mismo dia):
 * al principio eran 15 s y el resultado fue PEOR, no mejor. La razon es que hay
 * un fallo que la cabina NO puede arreglar desde su lado: cuando la P4 arranca
 * con el camino de datos del AP muerto (asociacion si, paquetes no), por mucho
 * que la cabina se reconecte sigue muda; lo unico que la cura es que la P4
 * reinicie su AP, y eso su escalera lo hace en unos 4 minutos. Si la cabina se
 * pone a desconectar cada 15 s, lo unico que consigue es que la pantalla parpadee
 * ("sin conexion") y que el DHCP no llegue a cuajar. Por eso: 60 s de silencio
 * para el primer intento (le da tiempo a la P4), y despues uno cada 5 minutos
 * como mucho. La paciencia es aqui la virtud.
 *
 * OJO: la medida la hace la tarea de recepcion (recvfrom con espera de 2 s), no
 * la de dibujo: preguntarle al Wi-Fi desde LVGL bloquea la UI y con el watchdog
 * de tareas encima eso es un reinicio. Ya paso el 30-sep-2026. */
#define SIN_DATOS_US          (60 * 1000 * 1000)      /* 1er intento: 1 min */
#define SIN_DATOS_US_LARGO    (5 * 60 * 1000 * 1000)  /* despues: cada 5 min */
#define SIN_DATOS_TOLERANCIA  2

static int      s_sin_datos_intentos = 0;
static int64_t  s_ultimo_intento_us  = 0;   /* ultima reconexion forzada */
static int64_t  s_escucha_desde_us   = 0;   /* cuando se abrio el socket */
static bool     s_aviso_mudez_dado   = false;
/* Cierto mientras la IP la pusimos nosotros (el DHCP no contesto). Se usa para
 * volver a intentar el DHCP en la siguiente reconexion: si no, la cabina se
 * queda con la IP fija para siempre y la P4 (que mira sus concesiones) cree que
 * el enlace esta roto aunque funcione. */
static _Atomic bool s_ip_fija = false;

static char s_ssid[33];
static char s_pass[65];

static void load_or_init_credentials(void)
{
    size_t ssid_len = sizeof(s_ssid);
    size_t pass_len = sizeof(s_pass);
    esp_err_t err = load_wifi_config(s_ssid, &ssid_len, s_pass, &pass_len);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Sin credenciales en NVS, usando valor de fabrica de wifi_credentials.h");
        strncpy(s_ssid, WIFI_CRED_SSID, sizeof(s_ssid) - 1);
        s_ssid[sizeof(s_ssid) - 1] = '\0';
        strncpy(s_pass, WIFI_CRED_PASS, sizeof(s_pass) - 1);
        s_pass[sizeof(s_pass) - 1] = '\0';
        save_wifi_config(s_ssid, s_pass);
    }
}

/* Lista las redes que ve la radio. Solo para diagnostico: cuando la P4 no
 * aparece, esto dice si el problema es que no esta emitiendo, que se llama de
 * otra forma o que su cifrado no pasa el filtro de wifi_configure_sta(). */
/* Backoff tras fallos seguidos: 60s al principio, hasta 5 min si la red
 * lleva un buen rato sin aparecer. Es solo diagnostico (confirmado con el
 * usuario el 09-sep-2026) -- el listado en si no cambia, pero cada escaneo
 * bloquea esp_wifi_scan_start(..., true) la tarea de EVENTOS DEL SISTEMA
 * (no una tarea propia) ~2s, y en una reconexion larga (fuera de cobertura
 * varias horas) el minuto fijo lo dispara cientos de veces sin aportar nada
 * nuevo al log. */
#define REDES_BACKOFF_MIN_S   60
#define REDES_BACKOFF_MAX_S  300
#define REDES_BACKOFF_TRAS_N_FALLOS  5   /* fallos seguidos antes de espaciar al maximo */
/* Fuera de la funcion (no 'static' local) para que on_wifi_event pueda
 * ponerlo a 0 en WIFI_EVENT_STA_CONNECTED: sin resetearlo, una desconexion
 * NUEVA (dias despues, red distinta) heredaba el backoff largo de una
 * caida anterior ya resuelta, en vez de volver a diagnosticar rapido desde
 * el principio. */
static int s_redes_fallos_seguidos = 0;
static void log_redes_visibles(void)
{
    static int64_t ultimo_us = 0;
    int64_t intervalo_s = (s_redes_fallos_seguidos >= REDES_BACKOFF_TRAS_N_FALLOS)
                        ? REDES_BACKOFF_MAX_S : REDES_BACKOFF_MIN_S;
    int64_t ahora = esp_timer_get_time();
    if (ultimo_us != 0 && (ahora - ultimo_us) < intervalo_s * 1000000LL) return;
    ultimo_us = ahora;
    s_redes_fallos_seguidos++;

    wifi_scan_config_t cfg = { .show_hidden = true };
    if (esp_wifi_scan_start(&cfg, true) != ESP_OK) return;

    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n == 0) {
        ESP_LOGW(TAG, "Escaneo: NO se ve ninguna red. La P4 no esta emitiendo "
                      "o esta fuera de alcance.");
        return;
    }
    if (n > 12) n = 12;

    wifi_ap_record_t *aps = calloc(n, sizeof(*aps));
    if (!aps) return;
    if (esp_wifi_scan_get_ap_records(&n, aps) == ESP_OK) {
        ESP_LOGW(TAG, "Escaneo: %u redes visibles (busco '%s')", n, s_ssid);
        for (uint16_t i = 0; i < n; i++) {
            ESP_LOGW(TAG, "  '%s'  canal=%d  rssi=%d  authmode=%d%s",
                     (const char *)aps[i].ssid, aps[i].primary, aps[i].rssi,
                     aps[i].authmode,
                     strcmp((const char *)aps[i].ssid, s_ssid) == 0 ? "   <-- ES ESTA" : "");
        }
    }
    free(aps);
}

static void wifi_configure_sta(void)
{
    wifi_config_t wc = {0};
    /* Copia a mano y con el cero GARANTIZADO (migracion a IDF 5.5.5,
     * 7-oct-2026). Aqui estaban dos strncpy, que dejan la cadena sin terminar si
     * el origen mide justo lo que el campo (SSID de 32 = el maximo permitido), y
     * el GCC 14 que trae el 5.5 lo detecta y corta el build. Con snprintf tampoco
     * vale: si puede truncar, avisa igual. Y el aviso era correcto -- sin el cero
     * el driver de Wi-Fi leia lo que hubiera detras del campo. */
    memcpy(wc.sta.ssid, s_ssid, sizeof(wc.sta.ssid));
    wc.sta.ssid[sizeof(wc.sta.ssid) - 1] = '\0';
    memcpy(wc.sta.password, s_pass, sizeof(wc.sta.password));
    wc.sta.password[sizeof(wc.sta.password) - 1] = '\0';
    /* Si la red o la clave venian mas largas que el campo, se han recortado: que
     * quede en el log, porque el sintoma si no es un "NO_AP_FOUND" que despista. */
    if (strlen(s_ssid) >= sizeof(wc.sta.ssid) ||
        strlen(s_pass) >= sizeof(wc.sta.password)) {
        ESP_LOGW(TAG, "SSID/clave mas largos que el campo del driver: recortados");
    }
    /* 'threshold' es el cifrado MINIMO que se acepta y va por el valor del enum,
     * asi que poner uno mas alto que el del AP hace que el escaneo lo descarte y
     * el sintoma sea un enganoso "reason=201 (NO_AP_FOUND)": parece que la red
     * no esta, cuando esta delante.
     *
     * Historia, porque costo una noche entera (21-ago-2026): el AP del 7" estuvo
     * ABIERTO y llamandose "ESP_<MAC>" desde julio -- el C6 llevaba el firmware
     * de fabrica de la placa, hablaba otro protocolo, la configuracion del AP le
     * llegaba vacia y levantaba el suyo por defecto. Se arreglo actualizando el
     * firmware del C6 desde el propio 7" (Ajustes -> Wi-Fi -> Actualizar radio),
     * y desde entonces el AP es "VictronConfig" con WPA2 de verdad.
     *
     * Por eso esto vuelve a WPA2_PSK: acepta el AP y rechaza redes abiertas o
     * WEP, que es de lo que protege. Si alguna vez reaparece el AP abierto, el
     * problema esta en el C6, NO aqui: no bajar esto sin mirar antes el log del
     * 7" ("AP en la radio: ssid=... authmode=..."). */
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wc.sta.pmf_cfg.required = false;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    ESP_LOGI(TAG, "AP SSID='%s'", s_ssid);
}

void udp_rx_get_credentials(char *ssid_out, size_t ssid_len, char *pass_out, size_t pass_len)
{
    if (ssid_out && ssid_len > 0) {
        strncpy(ssid_out, s_ssid, ssid_len - 1);
        ssid_out[ssid_len - 1] = '\0';
    }
    if (pass_out && pass_len > 0) {
        strncpy(pass_out, s_pass, pass_len - 1);
        pass_out[pass_len - 1] = '\0';
    }
}

bool udp_rx_set_credentials(const char *ssid, const char *pass)
{
    if (!ssid || !pass) return false;
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    strncpy(s_pass, pass, sizeof(s_pass) - 1);
    s_pass[sizeof(s_pass) - 1] = '\0';

    esp_err_t err = save_wifi_config(s_ssid, s_pass);
    if (err != ESP_OK) {
        /* Se reconecta igual (con lo tecleado, que vale para esta sesion),
         * pero se avisa: sin NVS, al apagar el contacto vuelven las de antes
         * y el usuario debe saberlo. */
        ESP_LOGW(TAG, "No se pudo guardar la configuracion Wi-Fi en NVS (%s): "
                      "el cambio solo durara hasta el proximo apagado",
                 esp_err_to_name(err));
    }
    wifi_configure_sta();
    ESP_LOGI(TAG, "Credenciales actualizadas, reconectando a '%s'", s_ssid);
    esp_wifi_disconnect();
    esp_wifi_connect();
    return err == ESP_OK;
}

/* Corre en la tarea del esp_timer, no en sys_evt: aqui si es seguro que el
 * reintento se retrase sin bloquear el procesado de otros eventos WiFi/IP. */
static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

/* Vigilante del DHCP. La cabina puede asociarse y quedarse SIN IP: pasa cuando
 * la P4 se reinicia justo mientras se asocia, el DHCP no contesta y aqui nadie
 * lo vuelve a pedir. Antes se quedaba asi para siempre -- asociada, sin socket
 * y sin datos -- y solo se arreglaba desenchufandola. Ahora, si en 20 s no ha
 * llegado la IP, se desconecta y el reintento de siempre la vuelve a conectar.
 * Paso el 30-sep-2026, en el banco y con la P4 reiniciandose. */
#define DHCP_TIMEOUT_US (20 * 1000 * 1000)

/* Si el DHCP de la P4 no contesta, la cabina se pone una direccion FIJA y sigue
 * funcionando. Es la pieza que fallaba: el servidor DHCP de la P4 se atasca de
 * vez en cuando (sobre todo tras un reinicio suyo) y la cabina se quedaba sin
 * datos hasta desenchufar la P4. La direccion de la P4 es fija (192.168.4.1,
 * esta en su firmware), asi que la cabina puede darse la .200 sin preguntar a
 * nadie.
 *
 * Por que la .200 y no otra: el DHCP de la P4 no tiene rango configurado, asi
 * que usa el de la IDF, que es desde la .2 hasta la .101 (empieza en la IP del
 * servidor + 1 y lo corta DHCPS_MAX_LEASE = 100 direcciones; ver
 * components/lwip/apps/dhcpserver/dhcpserver.c, dhcps_poll_set). La .200 queda
 * fuera de ese rango, asi que la P4 no se la puede dar a nadie mas y no hay
 * riesgo de que dos cacharros acaben con la misma IP.
 *
 * Antes de esto, al no llegar la IP, se desconectaba y reintentaba. Aquello
 * arreglaba el caso "asociada a medias" pero seguia dependiendo del DHCP; esto
 * lo elimina de la ecuacion. 1-oct-2026, a peticion del usuario: "que funcione
 * a veces si y otras no no me gusta". */
#define IP_FIJA_CABINA   "192.168.4.200"

/* Espera para el DHCP: 20 s la primera vez (la P4 puede estar arrancando), y
 * solo 5 s cuando venimos de la IP fija. El reintento de DHCP de una reconexion
 * BORRA la IP (esp_netif_dhcpc_start hace esp_netif_reset_ip_info), asi que son
 * segundos sin direccion: mejor pocos. Con el DHCP sano contesta en milisegundos
 * (medido: 50 ms), asi que 5 s de sobra. */
#define DHCP_TIMEOUT_US    (20 * 1000 * 1000)
#define DHCP_REINTENTO_US  (5 * 1000 * 1000)
static int64_t s_dhcp_espera_us = DHCP_TIMEOUT_US;

static void dhcp_timer_cb(void *arg)
{
    (void)arg;
    if (!s_asociado) return;                       /* ya se cayo sola */
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return;

    esp_netif_ip_info_t ip = { 0 };
    ip.ip.addr = esp_ip4addr_aton(IP_FIJA_CABINA);
    ip.netmask.addr = esp_ip4addr_aton("255.255.255.0");
    ip.gw.addr = esp_ip4addr_aton("192.168.4.1");

    esp_netif_dhcpc_stop(netif);                   /* no-op si no estaba andando */
    if (esp_netif_set_ip_info(netif, &ip) != ESP_OK) {
        ESP_LOGE(TAG, "no he podido ponerme la IP fija " IP_FIJA_CABINA);
        return;
    }
    ESP_LOGW(TAG, "el DHCP no contesta: me pongo la IP fija " IP_FIJA_CABINA
                  " y sigo (la P4 esta en 192.168.4.1)");
    s_ip_fija = true;
    s_dhcp_espera_us = DHCP_REINTENTO_US;   /* la proxima vez, ventana corta */
    /* La tarea de recepcion espera esta bandera para abrir el socket. */
    xEventGroupSetBits(s_wifi_events, WIFI_BIT_GOT_IP);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA start");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_CONNECTED:
                s_asociado = true;
                ESP_LOGI(TAG, "Asociado a %s", s_ssid);
                /* La senal se mide AQUI, en la tarea de eventos del Wi-Fi, y se
                 * guarda: la pantalla solo lee el numero guardado. Antes se
                 * media en la tarea de recepcion (que necesita un paquete) y por
                 * eso una cabina asociada y muda ensenaba "0 dBm", un valor que
                 * no existe y despista mas que ayuda. */
                {
                    wifi_ap_record_t ap_rec;
                    if (esp_wifi_sta_get_ap_info(&ap_rec) == ESP_OK) {
                        s_rssi_cache = ap_rec.rssi;
                        s_rssi_us = esp_timer_get_time();
                        ESP_LOGI(TAG, "Senal de la P4: %d dBm", s_rssi_cache);
                    }
                }
                /* Si venimos de la IP fija (el DHCP fallo la vez anterior), se
                 * le da otra oportunidad al DHCP aprovechando esta reconexion:
                 * asi el sistema vuelve solo a lo normal y la P4 vuelve a ver su
                 * concesion. Si no contesta en la ventana corta, dhcp_timer_cb
                 * vuelve a poner la IP fija y seguimos funcionando. */
                if (s_ip_fija) {
                    esp_netif_t *n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                    /* La marca se quita SOLO si el cliente DHCP arranca de verdad:
                     * si el arranque falla y la quitamos igual, no se vuelve a
                     * intentar nunca mas y la cabina se queda con la IP fija para
                     * siempre (y la P4, que mira sus concesiones, creyendo que el
                     * enlace esta roto aunque funcione). */
                    if (n && esp_netif_dhcpc_start(n) == ESP_OK) {
                        s_ip_fija = false;
                        ESP_LOGW(TAG, "venia con IP fija: le doy otra oportunidad al DHCP");
                    } else {
                        ESP_LOGW(TAG, "no he podido rearrancar el DHCP: sigo con la IP fija");
                    }
                }
                /* A partir de aqui, la IP tiene que llegar en s_dhcp_espera_us. */
                esp_timer_stop(s_dhcp_timer);      /* no-op si no estaba armado */
                esp_timer_start_once(s_dhcp_timer, s_dhcp_espera_us);
                xEventGroupSetBits(s_wifi_events, WIFI_BIT_CONNECTED);
                s_redes_fallos_seguidos = 0;   /* la proxima caida vuelve a diagnosticar rapido */
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                s_asociado = false;
                wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
                ESP_LOGW(TAG, "Desconectado reason=%d, siguiente intento en 0.5s",
                         d ? d->reason : -1);
                /* Con NO_AP_FOUND (201) el mensaje solo no basta: dice que no
                 * la encuentra, pero no si es que no esta, si esta con otro
                 * nombre o si la esta descartando por el cifrado. Se lista lo
                 * que ve, de vez en cuando para no llenar el log ni pasarse el
                 * dia escaneando (un escaneo bloquea la radio ~2 s). */
                if (d && d->reason == WIFI_REASON_NO_AP_FOUND) log_redes_visibles();
                xEventGroupClearBits(s_wifi_events, WIFI_BIT_CONNECTED | WIFI_BIT_GOT_IP);
                esp_timer_stop(s_dhcp_timer);        /* sin IP que esperar */
                esp_timer_stop(s_reconnect_timer);   /* no-op si no estaba armado */
                esp_timer_start_once(s_reconnect_timer, RECONNECT_DELAY_US);
                break;
            }
            default: break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP: " IPSTR " GW: " IPSTR,
                 IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.gw));
        s_ip_fija = false;                     /* IP del DHCP: lo normal */
        s_dhcp_espera_us = DHCP_TIMEOUT_US;    /* y la ventana, la de siempre */
        esp_timer_stop(s_dhcp_timer);          /* ya tenemos IP: no hay nada que vigilar */
        xEventGroupSetBits(s_wifi_events, WIFI_BIT_GOT_IP);
    }
}

/* Vigilante de mudez: lo llama la tarea de recepcion cada vez que el socket se
 * queda sin paquete (espera de 2 s), NUNCA la tarea de dibujo. Ver el comentario
 * de SIN_DATOS_US arriba. */
static void vigila_datos(void)
{
    if (!s_asociado) {
        /* Sin asociacion ya hay reconexion en marcha: aqui no se toca nada. */
        s_aviso_mudez_dado = false;
        return;
    }
    int64_t ahora = esp_timer_get_time();
    int64_t ref = s_ultimo_rx_us;
    if (s_escucha_desde_us > ref) ref = s_escucha_desde_us;
    if (s_ultimo_intento_us > ref) ref = s_ultimo_intento_us;
    if (ref == 0) return;                      /* todavia no escuchamos */

    int64_t mudo_us = ahora - ref;
    int64_t umbral = (s_sin_datos_intentos < SIN_DATOS_TOLERANCIA) ? SIN_DATOS_US
                                                                  : SIN_DATOS_US_LARGO;
    if (mudo_us < umbral) {
        s_aviso_mudez_dado = false;
        return;
    }
    if (!s_aviso_mudez_dado) {
        ESP_LOGW(TAG, "asociada pero SIN DATOS desde hace %lld s: fuerzo reconexion "
                      "(intento %d)", (long long)(mudo_us / 1000000),
                 s_sin_datos_intentos + 1);
        s_aviso_mudez_dado = true;
    }
    s_sin_datos_intentos++;
    s_ultimo_intento_us = ahora;
    esp_wifi_disconnect();   /* el evento DISCONNECTED rearma el reintento de 0,5 s */
}

/* Manda el latido a la P4 (ver mini_proto.h). Lo llama la tarea de recepcion,
 * que es la unica que sabe cuanto llevamos sin datos, y va limitado a uno cada
 * 2 s. Para que sirve: la P4 no tiene forma de saber si su telemetria llega,
 * asi que se lo decimos nosotros; y el latido en si le demuestra que la subida
 * funciona. Con eso la P4 puede reparar el AP en segundos en vez de a ciegas. */
static void manda_latido(int sock)
{
    static int64_t ultimo_us = 0;
    int64_t ahora = esp_timer_get_time();
    if (ultimo_us != 0 && (ahora - ultimo_us) < 2 * 1000 * 1000) return;
    ultimo_us = ahora;

    mini_latido_t l;
    memset(&l, 0, sizeof(l));
    l.magic   = MINI_LATIDO_MAGIC;
    l.version = MINI_PROTO_VERSION;
    int64_t ref = s_ultimo_rx_us ? s_ultimo_rx_us : s_escucha_desde_us;
    l.seg_sin_datos = (ref == 0) ? -1 : (int16_t)((ahora - ref) / 1000000);
    l.ip_fija = s_ip_fija ? 1 : 0;
    l.crc32 = esp_crc32_le(0, (const uint8_t *)&l, sizeof(l) - sizeof(uint32_t));

    struct sockaddr_in dst = {0};
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons(MINI_LATIDO_UDP_PORT);
    dst.sin_addr.s_addr = inet_addr("192.168.4.1");   /* la P4 */
    int n = sendto(sock, &l, sizeof(l), 0, (struct sockaddr *)&dst, sizeof(dst));
    if (n != (int)sizeof(l)) {
        static uint32_t fallos = 0;
        if ((++fallos % 15) == 1) {
            ESP_LOGW(TAG, "no he podido mandar el latido a la P4 (errno=%d, %lu fallos)",
                     errno, (unsigned long)fallos);
        }
    }
}

static void rx_task(void *arg)
{
    (void)arg;

    xEventGroupWaitBits(s_wifi_events, WIFI_BIT_GOT_IP,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    /* Crear (o recrear) el socket con reintento. Antes un fallo aqui hacia
     * vTaskDelete -> la tarea moria para siempre y la 3.5" se quedaba muda
     * hasta reboot, mientras que udp_tx.c (el emisor, en la P4) ya tenia este
     * mismo fallo arreglado con reintento cada 5s. Detectado auditando el
     * 07-sep-2026. */
    struct sockaddr_in bind_addr = {0};
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_port        = htons(MINI_PROTO_UDP_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int sock = -1;
    while (sock < 0) {
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) {
            ESP_LOGE(TAG, "socket() fallo: errno=%d (reintento en 5s)", errno);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
            ESP_LOGE(TAG, "bind(:%d) fallo: errno=%d (reintento en 5s)",
                     MINI_PROTO_UDP_PORT, errno);
            close(sock);
            sock = -1;
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
    }
    ESP_LOGI(TAG, "Escuchando UDP :%d (sizeof(mini_msg_t)=%u)",
             MINI_PROTO_UDP_PORT, (unsigned)sizeof(mini_msg_t));

    /* Espera maxima de 2 s: asi esta tarea se despierta aunque no llegue nada y
     * puede vigilar si el enlace se ha quedado mudo (ver vigila_datos). Antes se
     * quedaba bloqueada en recvfrom para siempre y una cabina asociada pero muda
     * no se enteraba de nada. */
    struct timeval espera = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &espera, sizeof(espera));
    s_escucha_desde_us = esp_timer_get_time();

    uint8_t buf[256];
    struct sockaddr_in src;
    /* IP fija del AP de la P4 (igual que P4_URL en p4_api.c): es el gateway
     * por defecto de cualquier SoftAP de ESP-IDF, y esta pantalla es una
     * STA asociada a el, no otra cosa. Cualquier otro emisor en la misma
     * red (un vecino, un dispositivo ajeno) que mandara algo con el formato
     * correcto se aceptaba igual -- sin autenticacion, es la unica valla.
     * Detectado auditando el 07-sep-2026. */
    const in_addr_t p4_addr = inet_addr("192.168.4.1");

    for (;;) {
        /* recvfrom() SOBREESCRIBE slen con el tamano real de src; sin
         * resetearlo antes de cada llamada, una iteracion que lo dejara mas
         * corto de lo normal se arrastraria a las siguientes. */
        socklen_t slen = sizeof(src);
        int n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&src, &slen);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Ni un paquete en la espera de 2 s: es lo normal cuando la P4
                 * arranca o se reinicia. Se aprovecha para decirle a la P4 como
                 * la vemos (latido) y para comprobar si llevamos demasiado
                 * tiempo mudos. No se avisa en el log. */
                manda_latido(sock);
                vigila_datos();
                continue;
            }
            ESP_LOGW(TAG, "recvfrom errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (src.sin_addr.s_addr != p4_addr) {
            ESP_LOGW(TAG, "drop: origen %s no es la P4",
                     inet_ntoa(src.sin_addr));
            s_msgs_bad++;
            continue;
        }
        if (n != (int)sizeof(mini_msg_t)) {
            ESP_LOGW(TAG, "drop: tam %d != %u", n, (unsigned)sizeof(mini_msg_t));
            s_msgs_bad++;
            continue;
        }
        const mini_msg_t *msg = (const mini_msg_t *)buf;
        if (msg->version != MINI_PROTO_VERSION) {
            ESP_LOGW(TAG, "drop: version %u", msg->version);
            s_msgs_bad++;
            continue;
        }
        uint32_t expected = esp_crc32_le(0, buf, sizeof(mini_msg_t) - sizeof(uint32_t));
        if (expected != msg->crc32) {
            ESP_LOGW(TAG, "drop: crc 0x%08lx != 0x%08lx",
                     (unsigned long)msg->crc32, (unsigned long)expected);
            s_msgs_bad++;
            continue;
        }
        data_model_update_from_msg(msg);
        reloj_set_desde_p4(msg->epoch_local);
        s_ultimo_rx_us = esp_timer_get_time();   /* enlace vivo: ver udp_rx_enlace() */
        s_sin_datos_intentos = 0;                /* llego un paquete: escalera a cero */
        s_aviso_mudez_dado = false;
        manda_latido(sock);                      /* y se lo contamos a la P4 */
        if (s_asociado && esp_timer_get_time() - s_rssi_us > 2000000) {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                s_rssi_cache = ap.rssi;
                s_rssi_us = esp_timer_get_time();
            }
        }
        s_msgs_ok++;

        /* El reloj de la P4 es lo unico que permite contar lo que dura una
         * parada (esta pantalla no tiene RTC). Se avisa UNA vez cuando llega y
         * otra si se pierde, para que no haya que adivinar por que una parada
         * no se abre: sin fecha valida no se abre, y punto. */
        {
            static bool tenia_fecha = false;
            bool hay = (msg->epoch_local != 0);
            if (hay != tenia_fecha) {
                if (hay) {
                    /* epoch_local ya viene en hora local de la P4, asi que se
                     * desmonta a mano en vez de con localtime(), que aqui
                     * aplicaria un huso que esta pantalla no tiene puesto. */
                    uint32_t t = msg->epoch_local;
                    uint32_t seg = t % 86400u;
                    uint32_t dias = t / 86400u;
                    ESP_LOGI(TAG, "Reloj de la P4 recibido: dia %lu, hora %02lu:%02lu "
                                  "-> las paradas ya se pueden contar",
                             (unsigned long)dias,
                             (unsigned long)(seg / 3600), (unsigned long)((seg / 60) % 60));
                } else {
                    ESP_LOGW(TAG, "La P4 ha dejado de dar la hora: las paradas "
                                  "no se podran abrir ni cerrar");
                }
                tenia_fecha = hay;
            }
        }

        if ((s_msgs_ok % 10) == 1) {
            ESP_LOGI(TAG, "RX OK #%lu (bad=%lu) from %s soc=%d.%d V=%d.%02d I=%ld mA "
                          "agua limpia=%u grises=%u gps=%u",
                     (unsigned long)s_msgs_ok, (unsigned long)s_msgs_bad,
                     inet_ntoa(src.sin_addr),
                     msg->shunt_soc_deci / 10, msg->shunt_soc_deci % 10,
                     msg->shunt_voltage_centi / 100, msg->shunt_voltage_centi % 100,
                     (long)msg->shunt_current_milli,
                     msg->water_clean, msg->water_gray, msg->gps_estado);
            /* Las aguas se dejan en el log a proposito: salio de depurar por que
             * el aviso de grises no se veia, y resulto util para distinguir "no
             * llega el dato" de "llega y no se pinta". Cuesta una linea cada 10
             * paquetes. */
        }
    }
}

void udp_rx_start(void)
{
    load_or_init_credentials();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    s_wifi_events = xEventGroupCreate();

    const esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_cb,
        .name = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_reconnect_timer));

    const esp_timer_create_args_t dhcp_args = {
        .callback = dhcp_timer_cb,
        .name = "wifi_dhcp",
    };
    ESP_ERROR_CHECK(esp_timer_create(&dhcp_args, &s_dhcp_timer));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                 on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                 on_wifi_event, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_configure_sta();
    ESP_ERROR_CHECK(esp_wifi_start());

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "STA listo. MAC=" MACSTR " esperando IP por DHCP...", MAC2STR(mac));

    if (xTaskCreate(rx_task, "udp_rx", 4096, NULL, 4, NULL) != pdPASS) {
        /* Sin esto la pantalla se queda sin telemetria de la P4 para
         * siempre, en silencio -- STA conectada y todo, pero muda. */
        ESP_LOGE(TAG, "xTaskCreate(rx_task) fallo: SIN telemetria de la P4");
    }
}

/* Estado del enlace para la pantalla de informacion. Devuelve el SSID que se
 * esta buscando, si estamos asociados, la senal (solo si lo estamos) y cuantos
 * segundos llevamos sin recibir un mensaje valido de la P4 (-1 = nunca). */
void udp_rx_enlace(char *ssid, size_t ssid_len, bool *asociado,
                   int *rssi_dbm, int *seg_sin_datos)
{
    if (ssid && ssid_len) {
        strncpy(ssid, s_ssid, ssid_len - 1);
        ssid[ssid_len - 1] = '\0';
    }
    if (asociado) *asociado = s_asociado;
    if (rssi_dbm) {
        /* Numero guardado: NO se llama aqui a la API de Wi-Fi. Quien pregunta
         * puede ser la tarea de dibujo, y bloquearla es un reinicio (ver
         * arriba). Si nunca se ha medido, 0. */
        *rssi_dbm = s_asociado ? s_rssi_cache : 0;
    }
    if (seg_sin_datos) {
        int64_t t = s_ultimo_rx_us;
        *seg_sin_datos = (t == 0) ? -1 : (int)((esp_timer_get_time() - t) / 1000000);
    }
}
