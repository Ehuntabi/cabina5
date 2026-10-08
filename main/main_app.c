/* main.c
 *
 * 35cabina — satelite tactil de la P4 (ver README.md). Bring-up de
 * hardware (pantalla+tactil+LVGL), recepcion UDP del broadcast de la P4,
 * el carrusel de 3 pantallas (nav.c) y el canal de vuelta hacia la P4
 * (viajes y registros sueltos, main/net/p4_api.c + viaje_cola.c).
 */
#include <stdio.h>
#include <inttypes.h>
#include <lvgl.h>
#include "display.h"
#include "esp_bsp.h"
#include "brillo.h"
#include "lv_port_compat.h"
#include "data_model.h"
#include "tilt.h"
#include "salida.h"
#include "diag_reset.h"
#include "ui/nav.h"
#include "net/udp_rx.h"
#include "net/viaje_cola.h"
#include "net/p4_test.h"   /* auto-test del canal de subida a la P4 */
#include "ui/view_info.h"
#include "capture_carousel.h"   /* modo captura de pantallas por USB */
#include "esp_log.h"
#include "esp_flash.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_debug_helpers.h"   /* esp_backtrace_print: traza de la UI colgada */
#include "icons/splash_logo_5.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "35CABINA";
#define logSection(section) ESP_LOGI(TAG, "\n\n***** %s *****\n", section)
#define LVGL_PORT_ROTATION_DEGREE 90

/* Splash: contenedor negro opaco en el top layer (por encima de cualquier
 * pantalla del carrusel) + el nombre, se autodestruye a los SPLASH_MS.
 *
 * OJO: antes centraba el logo de la pantalla de 3,5" (splash_logo_3_5). Ese
 * logo es de 320x480 y en la pantalla de 5" (800x480) saldria pequeno y
 * descentrado, asi que aqui va un texto. Si se quiere un logo, hay que generar
 * uno para 800x480. */
#define SPLASH_MS 2000

static void splash_done_cb(lv_timer_t *t) {
    /* En LVGL 9 lv_timer_t es OPACO: el user_data se lee con su accesorio. */
    lv_obj_t *splash_bg = (lv_obj_t *)lv_timer_get_user_data(t);
    ESP_LOGI("SPLASH", "quito el splash (%s)", splash_bg ? "hay objeto" : "SIN OBJETO");
    lv_obj_del(splash_bg);

    /* DIAGNOSTICO: quien queda en pantalla y si tiene contenido. */
    lv_obj_t *act = lv_screen_active();
    if (act) {
        lv_obj_t *hijo = lv_obj_get_child(act, 0);
        ESP_LOGW("SPLASH", "pantalla activa %p: %u hijos, primera %p (%dx%d) tam %dx%d",
                 (void *)act, (unsigned)lv_obj_get_child_count(act), (void *)hijo,
                 hijo ? (int)lv_obj_get_width(hijo) : 0,
                 hijo ? (int)lv_obj_get_height(hijo) : 0,
                 (int)lv_obj_get_width(act), (int)lv_obj_get_height(act));
    } else {
        ESP_LOGE("SPLASH", "NO hay pantalla activa");
    }
}

static void splash_create(void) {
    lv_disp_t *disp = lv_disp_get_default();
    lv_coord_t hor = lv_disp_get_hor_res(disp);
    lv_coord_t ver = lv_disp_get_ver_res(disp);

    lv_obj_t *splash_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(splash_bg, 0, 0);
    lv_obj_set_size(splash_bg, hor, ver);
    lv_obj_set_style_bg_color(splash_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(splash_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(splash_bg, 0, 0);
    lv_obj_set_style_radius(splash_bg, 0, 0);
    lv_obj_set_style_pad_all(splash_bg, 0, 0);
    lv_obj_clear_flag(splash_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* LA AUTOCARAVANA, no un rotulo: es el mismo splash que llevaba el satelite
     * de 3,5" (alli en 480x320). El 8-oct-2026 se recupero la imagen del
     * volcado de pixeles del proyecto viejo y se rehizo para esta pantalla
     * (ver main/icons/splash_logo_5.c). Aqui no se escala nada: se dibuja a su
     * tamano (665x394) porque se genero ya a esa medida, y escalar en tiempo
     * real costaria un repintado por pixel en cada arranque. */
    lv_obj_t *logo = lv_image_create(splash_bg);
    lv_image_set_src(logo, &splash_logo_5);
    /* SOLO la imagen: ni rotulo ni version (quitados el 8-oct-2026, a peticion
     * del usuario: "el texto del splash es horrible"). La version se mira en
     * Ajustes, que es donde toca. Centrada sin mas. */
    lv_obj_center(logo);

    lv_timer_t *t = lv_timer_create(splash_done_cb, SPLASH_MS, splash_bg);
    lv_timer_set_repeat_count(t, 1);
    ESP_LOGI("SPLASH", "puesto el splash (%d ms), temporizador %s",
             SPLASH_MS, t ? "creado" : "NO CREADO");
}

/* Heartbeat: diagnostico cada 30s (uptime, heap, PSRAM, contador de vida
 * de LVGL). La recuperacion ante cuelgue la hace lvgl_wdog_task. */
static void heartbeat_task(void *arg) {
    (void)arg;
    while (1) {
        ESP_LOGI("HB", "uptime=%llus free_heap=%u min=%u free_psram=%u lvgl=%u",
                 esp_timer_get_time() / 1000000ULL,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)esp_get_minimum_free_heap_size(),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)lvgl_port_get_loop_count());
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/* Supervisor anti-cuelgue respaldado por el Task WDT hardware — ver
 * comentario original en victronsolardisplayesp-multi-device_pantalla_3.5,
 * reutilizado tal cual (bring-up de estabilidad, no logica Victron). */
#define LVGL_WDOG_PERIOD_MS 2000
#define LVGL_WDOG_GRACE_MS  60000
static void lvgl_wdog_task(void *arg) {
    (void)arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    uint32_t prev = lvgl_port_get_loop_count();
    uint32_t stalled_ms = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(LVGL_WDOG_PERIOD_MS));

        uint32_t now = lvgl_port_get_loop_count();
        if (now != prev) {
            prev = now;
            stalled_ms = 0;
        } else {
            stalled_ms += LVGL_WDOG_PERIOD_MS;
        }

        if (stalled_ms < LVGL_WDOG_GRACE_MS) {
            esp_task_wdt_reset();
        } else {
            /* Dejamos el motivo apuntado ANTES de que salte el Task WDT: si no,
             * el arranque siguiente solo sabria decir "watchdog de tarea" y no
             * que fue la UI la que se quedo parada. Una sola vez (la marca se
             * consume al leerla en el arranque) para no escribir la flash en
             * cada vuelta del bucle mientras esta colgada. */
            static bool motivo_marcado = false;
            if (!motivo_marcado) {
                diag_reset_marcar_sw("UI colgada");
                motivo_marcado = true;
            }
            ESP_LOGE("WDOG", "UI sin avanzar %ums; dejando saltar el Task WDT",
                     (unsigned)stalled_ms);
            /* Traza de donde esta atascada la tarea de LVGL. Sin esto, un
             * cuelgue de la UI (bucle infinito, sin panic ni watchdog) no deja
             * rastro: los contadores siguen, el panel barre y no hay ni una
             * linea que decir DONDE se ha quedado. Decodificar con
             * xtensa-esp32s3-elf-addr2line -pfiaC -e build/cabina5.elf <dirs> */
            esp_backtrace_print(40);
        }
    }
}

void setup(void);

void app_main(void) {
    setup();
}

void setup(void) {
    logSection("LVGL init start");

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "This is %s chip, %d cores", CONFIG_IDF_TARGET, chip_info.cores);

    uint32_t flash_size;
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        ESP_LOGE(TAG, "Get flash size failed");
        return;
    }
    ESP_LOGI(TAG, "%" PRIu32 "MB flash, min free heap: %" PRIu32 ", free PSRAM: %u",
             flash_size / (1024 * 1024),
             esp_get_minimum_free_heap_size(),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* La memoria persistente se arranca ANTES que la pantalla porque el brillo
     * guardado se lee de ahi: si se hiciera despues habria que encender la
     * retroiluminacion a un valor cualquiera y corregirlo un instante mas
     * tarde, que de noche se ve como un fogonazo. */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    /* Que se sepa si la pantalla se reinicio SOLA (watchdog, panic, cuelgue):
     * motivo en el log y tarjeta roja en Ajustes cuando no lo provoco el
     * contacto. Va justo despues de NVS y antes de que la UI lea la tarjeta.
     * Ver diag_reset.c. */
    diag_reset_anotar_arranque();

    logSection("Display init");
    /* En esta placa el panel YA es horizontal (800x480), asi que no se rota
     * nada: la 3,5" era vertical y por eso llevaba rotate=90.
     * El tamano de los buffers y del bounce buffer los decide el BSP (ver
     * esp_bsp.c): aqui solo se le pasa la config de la tarea de LVGL. */
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
    };
    if (!bsp_display_start_with_config(&cfg)) {
        ESP_LOGE(TAG, "la pantalla no arranco");
        return;
    }
    lv_port_contador_arrancar();   /* contador de vida para lvgl_wdog_task */
    brillo_init();   /* nivel guardado; la primera vez, el ALTO */

    /* LA RED VA ANTES QUE LA UI, y esto es a proposito (portado de la 5"):
     * esp_wifi_init() se queda con la RAM interna que necesita en bloques
     * GRANDES (buffers estaticos y su tarea) y no sabe apañarse con las sobras.
     * La UI, en cambio, es elastica: LVGL tiene su propio pool y si le falta
     * memoria se queja pero no impide arrancar. Al reves (UI primero) el
     * resultado medido es "create wifi task: failed to create task" y la placa
     * se reinicia en bucle. */
    udp_rx_start();

    if (!lvgl_port_lock(5000)) {
        ESP_LOGE(TAG, "No se pudo tomar lvgl_port_lock al iniciar UI");
        return;
    }

    /* La salida en curso y lo que quedo abierto. Va ANTES de construir la UI:
     * el menu de registros pregunta el estado nada mas crearse, y lee la marca
     * de vida antes de pisarla (ver salida.h). */
    salida_init();

    /* No es fatal si no responde: tilt_is_present() queda en false y la
     * pantalla de inclinacion lo muestra en vez de crashear. */
    tilt_init();

    data_model_init();
    nav_init();
    splash_create();
    lvgl_port_unlock();

    /* El repartidor de apuntes pendientes. Va DESPUES de udp_rx_start() porque
     * necesita la red, y arranca aunque la cola este vacia: lo normal es que
     * quede algo del encendido anterior (esta pantalla se apaga con el
     * contacto, y los apuntes se hacen justo antes). */
    viaje_cola_init(view_info_set_pendientes);

    /* Auto-test del canal de subida (cabina -> P4 por HTTP): deja en el log si
     * la ruta existe, si las credenciales del portal valen y si acepta POST.
     * No crea ningun viaje. Ver net/p4_test.c. */
    p4_test_start();

    /* NO hay reinicio programado, y es a proposito: se quito el de 12 h el
     * 10-sep-2026. Esta pantalla ya se reinicia con cada corte de contacto (es
     * su forma normal de apagarse), asi que el temporizador solo servia para
     * cortar una jornada larga en un momento cualquiera -- y para tapar
     * cualquier fuga en vez de delatarla. Un cuelgue de verdad lo cubren el
     * Task WDT (que lvgl_wdog_task deja saltar a proposito) y el watchdog de
     * tareas; y si algo se reinicia, ahora se ve: diag_reset y la tarjeta
     * "Ultimo reinicio" de Ajustes. */

    if (xTaskCreate(heartbeat_task, "hb", 3072, NULL, 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(heartbeat_task) fallo: sin diagnostico periodico");
    }
    /* lvgl_wdog_task es el respaldo anti-cuelgue de verdad (ver su comentario):
     * si esto no llega a crearse, un cuelgue de LVGL ya no se recupera solo.
     * Se comprueba el motivo CONCRETO (errno de FreeRTOS) porque "fallo" a secas
     * no dice si fue memoria, prioridad o tabla llena: paso en la unidad nueva el
     * 7-oct-2026 y hubo que adivinar. La memoria interna se mira ANTES de crear
     * la tarea, que es el dato que hace falta si el fallo es por RAM. */
    BaseType_t crea;
    crea = xTaskCreate(lvgl_wdog_task, "lvgl_wdog", 3072, NULL, 6, NULL);
    if (crea != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(lvgl_wdog) fallo (err=%d): SIN recuperacion anti-cuelgue de LVGL"
                      " (heap interno libre %u B, PSRAM %u B)",
                 (int)crea,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }

    /* Modo captura de pantallas (capture_carousel.c). Con el interruptor de
     * capture_carousel.h a 0 la funcion es un no-op, asi que esto se puede
     * dejar enchufado siempre: el dia de la captura solo hay que cambiar el
     * flag. Va lo ULTIMO del setup porque necesita la pantalla montada y el
     * modelo de datos vivo. Auditado el 14-sep-2026: antes esto NO estaba
     * llamado, y el carrusel (que si estaba escrito y commiteado) no arrancaba
     * nunca -- por eso la captura no salia. */
    capture_carousel_start();

    logSection("Setup complete");
}
