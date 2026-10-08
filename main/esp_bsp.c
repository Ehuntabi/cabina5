/* Arranque de pantalla, tactil y brillo de la placa JC8048W550C (5", 800x480).
 *
 * COMO ES ESTE PANEL (leer antes de tocar nada):
 * - Es un panel RGB paralelo y el controlador (ST7262) NO recibe comandos: lo
 *   que se configura es el TIMING y un framebuffer en PSRAM del que el panel va
 *   leyendo solo, en bucle, a ~40 Hz. Por eso no hay "tabla de comandos".
 * - Como el panel no para de leer el framebuffer, escribir en el mientras lo
 *   lee se ve como una banda que recorre la pantalla (scroll/desgarro). Lo
 *   resuelve `avoid_tearing` de esp_lvgl_port: LVGL dibuja en los DOS
 *   framebuffers del panel y, cuando termina el cuadro, el driver cambia el DMA
 *   al otro buffer y espera a que el anterior acabe de salir.
 * - OJO: ese camino exige cuadro completo (`direct_mode`/`full_refresh`), o
 *   sea que LVGL dibuja 800x480 en PSRAM. MEDIDO en esta placa: eso da 4 fps.
 *   Por eso `evitar_tearing` es una opcion que se enciende desde fuera y no la
 *   unica via: sin ella, LVGL dibuja en RAM interna (mucho mas rapido) y el
 *   driver copia el trozo, a cambio de desgarro.
 * - El tactil es un GT911 por I2C, sondeado (su pin de interrupcion va a GND,
 *   por eso la direccion es 0x5D y no 0x14).
 *
 * La configuracion que funciona en esta placa (pines, timing y el modo QIO del
 * bootloader) esta verificada contra el xlsx oficial de Guition y contra el
 * repositorio de referencia ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "display.h"
#include "esp_bsp.h"

static const char *TAG = "bsp";

/* Contador para el informe de refrescos/s (diagnostico). */
static volatile uint32_t s_refrescos = 0;
static volatile uint32_t s_vsyncs = 0;
static bool s_congelado = false;
/* 1 = prueba de banco: congela el refresco a los 8 s para ver si el movimiento
 * es de LVGL o del panel. 0 = comportamiento normal. */
#define PRUEBA_CONGELAR 1

/* El bus I2C es compartido con lo que se cuelgue despues (acelerometro, etc.).
 * Se crea una sola vez. */
static i2c_master_bus_handle_t s_i2c_bus = NULL;

/* Estado del brillo: el PWM se configura en el arranque y el porcentaje se
 * recuerda aqui para poder devolverlo (bsp_display_brightness_get). El
 * porcentaje de verdad lo lleva quien manda (brillo.c en la app). */
static int s_brillo_pct = 100;

/* ── Retroiluminacion ─────────────────────────────────────────────────────── */
static esp_err_t bsp_display_brightness_init(void)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LCD_BL_LEDC_RES,
        .timer_num       = LCD_BL_LEDC_TIMER,
        .freq_hz         = LCD_BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "ledc timer");

    const ledc_channel_config_t ch_cfg = {
        .gpio_num   = LCD_PIN_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LCD_BL_LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LCD_BL_LEDC_TIMER,
        .duty       = 0,          /* arranca apagada: se enciende al final del arranque */
        .hpoint     = 0,
    };
    return ledc_channel_config(&ch_cfg);
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (brightness_percent < 0) brightness_percent = 0;
    if (brightness_percent > 100) brightness_percent = 100;
    s_brillo_pct = brightness_percent;
    const uint32_t duty = (255 * brightness_percent) / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_BL_LEDC_CHANNEL, duty),
                        TAG, "ledc_set_duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_BL_LEDC_CHANNEL),
                        TAG, "ledc_update_duty");
    ESP_LOGI(TAG, "Backlight %d%%", brightness_percent);
    return ESP_OK;
}

int bsp_display_brightness_get(void)
{
    return s_brillo_pct;
}

esp_err_t bsp_display_backlight_off(void)
{
    return bsp_display_brightness_set(0);
}

esp_err_t bsp_display_backlight_on(void)
{
    return bsp_display_brightness_set(100);
}

/* ── Bus I2C ──────────────────────────────────────────────────────────────── */
esp_err_t bsp_i2c_init(void)
{
    if (s_i2c_bus) return ESP_OK;
    const i2c_master_bus_config_t cfg = {
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .i2c_port          = -1,             /* deja elegir puerto al driver */
        .scl_io_num        = TOUCH_PIN_SCL,
        .sda_io_num        = TOUCH_PIN_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,   /* la placa ya trae pullups, esto no estorba */
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(err));
        s_i2c_bus = NULL;
    }
    return err;
}

i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void)
{
    return s_i2c_bus;
}

esp_err_t bsp_i2c_deinit(void)
{
    if (!s_i2c_bus) return ESP_OK;
    esp_err_t err = i2c_del_master_bus(s_i2c_bus);
    s_i2c_bus = NULL;
    return err;
}

/* ── Panel RGB ────────────────────────────────────────────────────────────── */
static esp_lcd_panel_handle_t bsp_rgb_panel_new(void)
{
    esp_lcd_panel_handle_t panel = NULL;
    const esp_lcd_rgb_panel_config_t cfg = {
        .clk_src    = LCD_CLK_SRC_DEFAULT,
        .data_width = 16,
        .bits_per_pixel = 16,
        /* DOS framebuffers: es el requisito de avoid_tearing (mientras el panel
         * lee uno, LVGL dibuja en el otro). */
        .num_fbs    = 2,
        .psram_trans_align = 64,  /* el DMA del RGB va mas fino alineado */
        /* Interfaz de datos DESHABILITADO: este panel no tiene pin de datos ni
         * comandos (no es un controlador con registros, es una pantalla RGB). */
        .disp_gpio_num = GPIO_NUM_NC,
        .timings = {
            /* TIMING COPIADO de la definicion oficial de la placa
             * (JC8048W550C.json de platformio-espressif32-sunton) y del repo
             * ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ: 820 px por linea y 500 por
             * cuadro a 16 MHz.
             *
             * Y OJO: hsync_idle_low y vsync_idle_low van en FALSE, no en true.
             * Eso decide donde engancha la sincronia y por tanto donde empieza
             * cada linea; con ellos en true la imagen salia corrida hacia un
             * lado (que es el sintoma que perseguiamos creyendo que era scroll
             * del framebuffer). */
            .pclk_hz = 16 * 1000 * 1000,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch  = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch  = 8,
            .vsync_front_porch = 8,
            .flags = {
                .hsync_idle_low  = false,
                .vsync_idle_low  = false,
                .de_idle_high    = false,
                .pclk_active_neg = true,
                .pclk_idle_high  = false,
            },
        },
        .hsync_gpio_num = LCD_PIN_HSYNC,
        .vsync_gpio_num = LCD_PIN_VSYNC,
        .de_gpio_num    = LCD_PIN_DE,
        .pclk_gpio_num  = LCD_PIN_PCLK,
        .data_gpio_nums = {
            LCD_PIN_DATA_B0, LCD_PIN_DATA_B1, LCD_PIN_DATA_B2, LCD_PIN_DATA_B3, LCD_PIN_DATA_B4,
            LCD_PIN_DATA_G0, LCD_PIN_DATA_G1, LCD_PIN_DATA_G2, LCD_PIN_DATA_G3, LCD_PIN_DATA_G4,
            LCD_PIN_DATA_G5,
            LCD_PIN_DATA_R0, LCD_PIN_DATA_R1, LCD_PIN_DATA_R2, LCD_PIN_DATA_R3, LCD_PIN_DATA_R4,
        },
        .flags.fb_in_psram = true,   /* 800x480x2 = 768 KB: no cabe en RAM interna */
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    return panel;
}

/* ── Tactil (GT911) ───────────────────────────────────────────────────────── */
static esp_lcd_touch_handle_t bsp_touch_new(void)
{
    esp_lcd_panel_io_handle_t io = NULL;
    /* El macro del componente ya trae lo que necesita el GT911 de esta placa:
     * direccion 0x5D (su pin de interrupcion va a GND, por eso no vale la 0x14)
     * y la fase de control deshabilitada. */
    const esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = TOUCH_I2C_ADDR,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .lcd_param_bits = 0,
        .scl_speed_hz = TOUCH_I2C_SPEED_HZ,
        .flags.disable_control_phase = 1,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &io));

    esp_lcd_touch_handle_t touch = NULL;
    const esp_lcd_touch_config_t touch_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = TOUCH_PIN_RST,
        .int_gpio_num = TOUCH_PIN_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(io, &touch_cfg, &touch));
    ESP_LOGI(TAG, "Tactil GT911 listo (direccion 0x%02X)", TOUCH_I2C_ADDR);
    return touch;
}

/* ── Arranque completo ────────────────────────────────────────────────────── */
static lv_display_t *s_disp = NULL;
static lv_indev_t *s_indev = NULL;

/* Pixels por buffer de dibujo: 800 x 40 lineas. 64 KB cada uno en RAM interna
 * (en PSRAM no caben dos de pantalla completa y ademas seria lentisimo). */
#define BSP_BUF_PX      (LCD_H_RES * 40)

/* Retrazo del panel. El driver RGB lo avisa desde su ISR. */
static SemaphoreHandle_t s_vsync_sem = NULL;
static esp_lcd_panel_handle_t s_rgb_panel = NULL;

static bool IRAM_ATTR bsp_rgb_vsync_cb(esp_lcd_panel_handle_t panel,
                                       const esp_lcd_rgb_panel_event_data_t *edata,
                                       void *user_ctx)
{
    BaseType_t despertar = pdFALSE;
    s_vsyncs++;        /* DIAGNOSTICO: frecuencia real de barrido del panel */
    if (s_vsync_sem) xSemaphoreGiveFromISR(s_vsync_sem, &despertar);
    return despertar == pdTRUE;
}

/* LVGL avisa aqui de que tiene un trozo listo. Se espera al retrazo y se copia
 * inmediatamente: el panel empieza su barrido por la linea de arriba, asi que el
 * trozo nuevo se ve en ESTE cuadro y la costura se queda en una linea en vez de
 * pasearse por la pantalla. Si el retrazo tarda mas de lo razonable (panel
 * parado, driver raro) se copia igual: mas vale un desgarro que una pantalla
 * congelada. */
static void bsp_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (s_vsync_sem) xSemaphoreTake(s_vsync_sem, pdMS_TO_TICKS(20));

    esp_lcd_panel_draw_bitmap(s_rgb_panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px_map);

    lv_display_flush_ready(disp);
    s_refrescos++;
}

/* DIAGNOSTICO: refrescos por segundo. Sirve para comparar configuraciones (con
 * y sin avoid_tearing, buffers en RAM interna o en PSRAM) sin adivinar. */
static void bsp_display_fps_cb(lv_timer_t *t)
{
    (void)t;
    static uint32_t t0 = 0;
    const uint32_t ahora = (uint32_t)(esp_timer_get_time() / 1000);

    /* PRUEBA DE BANCO (temporal): a los 8 s se PARA el refresco de LVGL. Si la
     * pantalla se sigue moviendo con el framebuffer quieto, el problema es del
     * panel (timing), no de LVGL. Quitar cuando se aclare. */
    if (PRUEBA_CONGELAR && ahora > 8000 && !s_congelado) {
        s_congelado = true;
        lv_timer_t *refr = lv_display_get_refr_timer(s_disp);
        if (refr) lv_timer_pause(refr);
        ESP_LOGW(TAG, "PRUEBA: refresco de LVGL CONGELADO (framebuffer quieto)");
    }
    if (t0 == 0) t0 = ahora;
    if (ahora - t0 >= 5000) {
        ESP_LOGI(TAG, "refrescos LVGL: %.1f/s | barrido del panel: %.1f Hz | heap interno %u KB",
                 s_refrescos * 1000.0 / (ahora - t0),
                 s_vsyncs * 1000.0 / (ahora - t0),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
        s_refrescos = 0;
        s_vsyncs = 0;
        t0 = ahora;
    }
}

lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg)
{
    ESP_ERROR_CHECK(bsp_display_brightness_init());

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    if (cfg) port_cfg = cfg->lvgl_port_cfg;
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    ESP_ERROR_CHECK(bsp_i2c_init());

    esp_lcd_panel_handle_t panel = bsp_rgb_panel_new();
    s_rgb_panel = panel;
    ESP_LOGI(TAG, "Panel RGB %dx%d listo (ST7262, sin comandos: solo timing)",
             LCD_H_RES, LCD_V_RES);

    /* ── Display de LVGL hecho a mano ─────────────────────────────────────────
     *
     * POR QUE NO SE USA lvgl_port_add_disp_rgb() (medido en esta placa):
     *  - Modo normal: LVGL dibuja en RAM interna (rapido, 12-18 fps) y el driver
     *    copia el trozo al framebuffer. Pero el panel esta LEYENDO ese
     *    framebuffer a la vez, asi que al copiar se pisa lo que ya se esta
     *    mostrando: se ve una banda que recorre la pantalla (scroll).
     *  - Modo avoid_tearing (dibujar en los dos framebuffers y esperar al
     *    retrazo): quita el scroll, pero obliga a cuadro COMPLETO en PSRAM y eso
     *    aqui son 1-4 fps (medido; escribir pixel a pixel en PSRAM es un fallo de
     *    cache por linea). Ademas la 2.5.0 del port ni siquiera respeta los
     *    buffers parciales: se queda en ese modo.
     *
     * Lo que si funciona aqui: buffers PARCIALES en RAM interna (rapido) y
     * copiar al framebuffer JUSTO DESPUES del retrazo del panel, que es cuando
     * el panel empieza a leer por arriba. Asi el trozo copiado se ve en el mismo
     * cuadro y la costura queda en una linea, no en una banda que se pasea.
     */
    s_vsync_sem = xSemaphoreCreateBinary();
    if (!s_vsync_sem) {
        ESP_LOGE(TAG, "sin memoria para el semaforo de retrazo");
        return NULL;
    }
    const esp_lcd_rgb_panel_event_callbacks_t rgb_cbs = {
        .on_vsync = bsp_rgb_vsync_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel, &rgb_cbs, NULL));

    static lv_color_t *buf1 = NULL, *buf2 = NULL;
    /* Los buffers van a PSRAM a proposito: 2 x 64 KB en RAM interna dejan a
     * WiFi sin memoria y esp_wifi_init() falla con ESP_ERR_NO_MEM (visto en el
     * log: "esf_buf_setup_static: alloc eb fail" -> ESP_ERR_NO_MEM). En PSRAM el
     * trozo parcial se dibuja algo mas lento, pero es 1/12 de la pantalla, no la
     * pantalla entera: el problema grave era el cuadro COMPLETO en PSRAM. */
    buf1 = heap_caps_malloc(BSP_BUF_PX * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    buf2 = heap_caps_malloc(BSP_BUF_PX * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "sin PSRAM para los buffers de LVGL (%u px x2)", (unsigned)BSP_BUF_PX);
        return NULL;
    }

    s_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    if (!s_disp) {
        ESP_LOGE(TAG, "lv_display_create fallo");
        return NULL;
    }
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, buf1, buf2, BSP_BUF_PX * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, bsp_lvgl_flush_cb);
    ESP_LOGI(TAG, "LVGL: buffers parciales en PSRAM (%u px), refresco sincronizado al retrazo",
             (unsigned)BSP_BUF_PX);

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = s_disp,
        .handle = bsp_touch_new(),
    };
    s_indev = lvgl_port_add_touch(&touch_cfg);


    if (bsp_display_lock(0)) {
        lv_timer_create(bsp_display_fps_cb, 1000, NULL);
        bsp_display_unlock();
    }

    /* Se enciende al final con el nivel que el usuario tenga guardado: asi no
     * se ve el panel en blanco mientras arranca (la app llama a brillo_init()
     * justo despues y reaplica lo mismo, es idempotente). */
    bsp_display_brightness_set(100);
    return s_disp;
}

lv_indev_t *bsp_display_get_input_dev(void)
{
    return s_indev;
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}
