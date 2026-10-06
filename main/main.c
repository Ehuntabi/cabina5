/* Arranque en banco de la placa de 5" (JC8048W550C).
 *
 * QUE HACE ESTE main.c: nada de la app del satelite. Es una PANTALLA DE PRUEBA
 * para el bring-up, porque vamos a estrenar placa y pines que no hemos probado
 * nunca. Enseña lo justo para saber si el hardware esta bien:
 *
 *   1. Barra de colores y marco de 1 px en el borde: si el panel esta blanco,
 *      desplazado o con los colores cambiados, se ve AQUI y el sospechoso es
 *      display.h (los pines son de la placa hermana, sin verificar).
 *   2. Tamano logico que cree LVGL (800x480) y su rotacion.
 *   3. Coordenadas del tactil en vivo: para ver si toca donde debe o si hay
 *      que tocar el eje X/Y o invertir algun eje.
 *   4. Estado del Wi-Fi y contador de paquetes UDP de la P4 (puerto 4242): el
 *      satelite vive de esos paquetes, asi que si aqui sube el contador, la
 *      parte de red ya esta probada en la placa nueva.
 *
 * Cuando esto funcione, se monta encima la aplicacion (red UDP con la P4,
 * modelo de datos y UI a 800x480). Ver CLAUDE.md.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "lvgl.h"

#include "display.h"
#include "esp_bsp.h"
#include "wifi_credentials.h"

static const char *TAG = "cabina5";

/* ── Contadores que pinta la pantalla ─────────────────────────────────────── */
static volatile uint32_t s_udp_paquetes = 0;
static volatile uint32_t s_udp_bytes = 0;
static volatile uint16_t s_touch_x = 0, s_touch_y = 0;
static volatile bool     s_touch_pulsado = false;

static lv_obj_t *s_lbl_touch;
static lv_obj_t *s_lbl_udp;
static lv_obj_t *s_lbl_ip;
static lv_obj_t *s_lbl_heap;

/* ── Red: STA al AP de la P4 + recepcion UDP :4242 ────────────────────────── */
#define PUERTO_MINI 4242

static void wifi_evento(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi: desconectado, reintentando");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = (const ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Wi-Fi: IP " IPSTR, IP2STR(&ev->ip_info.ip));
    }
}

static void tarea_udp(void *arg)
{
    (void)arg;
    struct sockaddr_in dir = {
        .sin_family = AF_INET,
        .sin_port = htons(PUERTO_MINI),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "no se pudo crear el socket UDP");
        vTaskDelete(NULL);
        return;
    }
    if (bind(sock, (struct sockaddr *)&dir, sizeof(dir)) < 0) {
        ESP_LOGE(TAG, "no se pudo bindear el puerto %d", PUERTO_MINI);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "UDP escuchando en :%d", PUERTO_MINI);

    static uint8_t buf[512];
    while (1) {
        int n = recv(sock, buf, sizeof(buf), 0);
        if (n > 0) {
            s_udp_paquetes++;
            s_udp_bytes += n;
            /* Solo se enseña el primero por el log: si el protocolo cambia, se
             * ve aqui sin necesidad de tener la app entera montada. */
            if (s_udp_paquetes == 1) {
                ESP_LOGI(TAG, "primer paquete: %d bytes, ver=%u", n, (unsigned)buf[0]);
            }
        }
    }
}

static void red_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evento, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_evento, NULL));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, WIFI_CRED_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, WIFI_CRED_PASS, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(tarea_udp, "udp_rx", 4096, NULL, 5, NULL);
}

/* ── Pantalla de prueba ───────────────────────────────────────────────────── */
static void pantalla_prueba(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *titulo = lv_label_create(scr);
    lv_obj_set_style_text_font(titulo, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(titulo, lv_color_white(), 0);
    lv_label_set_text(titulo, "cabina5 - prueba de placa");
    lv_obj_align(titulo, LV_ALIGN_TOP_MID, 0, 16);

    /* Barra de colores: 8 franjas. Sirve para ver de un golpe si los canales
     * de color estan cambiados (rojo y azul intercambiados = pines R/B
     * cruzados en display.h) o si falta algun bit. */
    lv_obj_t *barra = lv_obj_create(scr);
    lv_obj_remove_style_all(barra);
    lv_obj_set_size(barra, lv_pct(98), 60);
    lv_obj_align(barra, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_set_layout(barra, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(barra, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(barra, 4, 0);
    static const uint32_t colores[8] = {
        0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000,
    };
    for (int i = 0; i < 8; i++) {
        lv_obj_t *c = lv_obj_create(barra);
        lv_obj_remove_style_all(c);
        lv_obj_set_flex_grow(c, 1);
        lv_obj_set_height(c, lv_pct(100));
        lv_obj_set_style_bg_color(c, lv_color_hex(colores[i]), 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    }

    /* Rejilla de fondo cada 100 px: si la imagen sale desplazada o estirada, se
     * ve contra estas lineas. */
    for (int x = 100; x < LCD_H_RES; x += 100) {
        lv_obj_t *l = lv_obj_create(scr);
        lv_obj_remove_style_all(l);
        lv_obj_set_size(l, 1, LCD_V_RES);
        lv_obj_set_pos(l, x, 0);
        lv_obj_set_style_bg_color(l, lv_color_hex(0x304050), 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    }
    for (int y = 100; y < LCD_V_RES; y += 100) {
        lv_obj_t *l = lv_obj_create(scr);
        lv_obj_remove_style_all(l);
        lv_obj_set_size(l, LCD_H_RES, 1);
        lv_obj_set_pos(l, 0, y);
        lv_obj_set_style_bg_color(l, lv_color_hex(0x304050), 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    }

    s_lbl_touch = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lbl_touch, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_lbl_touch, lv_color_hex(0x4FC3F7), 0);
    lv_label_set_text(s_lbl_touch, "tactil: --");
    lv_obj_align(s_lbl_touch, LV_ALIGN_TOP_LEFT, 20, 150);

    s_lbl_ip = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lbl_ip, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_lbl_ip, lv_color_hex(0xB0BEC5), 0);
    lv_label_set_text(s_lbl_ip, "wifi: conectando...");
    lv_obj_align(s_lbl_ip, LV_ALIGN_TOP_LEFT, 20, 200);

    s_lbl_udp = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lbl_udp, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_lbl_udp, lv_color_hex(0x81C784), 0);
    lv_label_set_text(s_lbl_udp, "P4: 0 paquetes");
    lv_obj_align(s_lbl_udp, LV_ALIGN_TOP_LEFT, 20, 250);

    s_lbl_heap = lv_label_create(scr);
    lv_obj_set_style_text_font(s_lbl_heap, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_lbl_heap, lv_color_hex(0x90A4AE), 0);
    lv_label_set_text(s_lbl_heap, "heap --");
    lv_obj_align(s_lbl_heap, LV_ALIGN_TOP_LEFT, 20, 300);

    /* Aviso de que esto NO es la app: que nadie se confunda al verlo. */
    lv_obj_t *pie = lv_label_create(scr);
    lv_obj_set_style_text_font(pie, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(pie, lv_color_hex(0xFFB300), 0);
    lv_label_set_text(pie, "PANTALLA DE PRUEBA - la app del satelite aun no esta portada");
    lv_obj_align(pie, LV_ALIGN_BOTTOM_MID, 0, -12);
}

static void refresco_cb(lv_timer_t *t)
{
    (void)t;
    lv_label_set_text_fmt(s_lbl_touch, "tactil: x=%u y=%u %s",
                          (unsigned)s_touch_x, (unsigned)s_touch_y,
                          s_touch_pulsado ? "PULSADO" : "");
    lv_label_set_text_fmt(s_lbl_udp, "P4: %u paquetes (%u KB)",
                          (unsigned)s_udp_paquetes, (unsigned)(s_udp_bytes / 1024));

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr) {
        lv_label_set_text_fmt(s_lbl_ip, "wifi: " IPSTR, IP2STR(&ip.ip));
    }

    lv_label_set_text_fmt(s_lbl_heap, "heap interno %u KB libres / PSRAM %u KB",
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== cabina5: prueba de placa JC8048W550C (%dx%d) ===", LCD_H_RES, LCD_V_RES);

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = LCD_H_RES * 40,
        .rotate = LV_DISP_ROT_NONE,
    };
    if (!bsp_display_start_with_config(&cfg)) {
        ESP_LOGE(TAG, "la pantalla no arranco");
        return;
    }

    red_init();

    if (bsp_display_lock(3000)) {
        pantalla_prueba();
        lv_timer_create(refresco_cb, 250, NULL);
        bsp_display_unlock();
    }
}
