/* Arranque de pantalla, tactil y brillo de la placa JC8048W550C (5", 800x480).
 *
 * COMO ES ESTE PANEL:
 * - El panel es RGB paralelo y el controlador (ST7262) NO recibe comandos: lo
 *   que se configura es el TIMING y un framebuffer en PSRAM del que el panel va
 *   leyendo solo. Por eso no hay "tabla de comandos" en este fichero.
 * - El panel no para de leer de PSRAM. Cualquier cosa que sature la PSRAM
 *   (copias grandes, escrituras gordas) se ve como parpadeo. El framebuffer va
 *   en PSRAM (768 KB: no cabe en RAM interna) y los buffers de dibujo de LVGL en
 *   RAM INTERNA, que es el reparto que recomienda Espressif para paneles RGB.
 * - El tactil es un GT911 por I2C, sondeado (su pin de interrupcion va a GND).
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_interface.h"   /* esp_lcd_panel_io_t: el puente de abajo */
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "display.h"
#include "esp_bsp.h"

static const char *TAG = "bsp";

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

/* ── Como se dibuja en este panel (leer antes de cambiar nada) ───────────────
 *
 * Un panel RGB NO SE "ENVIA": el controlador lee el framebuffer de PSRAM el
 * solo, en bucle, a 49 Hz. Lo unico que se puede hacer es escribir en el
 * framebuffer sin pisar el trozo que esta leyendo en ese instante.
 *
 * Con UN framebuffer no hay forma de hacerlo: el driver copia el buffer de
 * LVGL sobre el unico framebuffer que hay, que es justo el que se esta
 * mostrando -> se ve una banda horizontal desplazandose ("scroll horizontal"),
 * que es lo que pasaba.
 *
 * La forma correcta, y la que usa el driver de IDF para esto, es tener DOS
 * framebuffers y DEJAR QUE LVGL DIBUJE DENTRO DE ELLOS:
 *   - El panel lee el framebuffer A mientras LVGL dibuja en el B.
 *   - Al terminar, el driver engancha el DMA al B y el A queda libre.
 *   - `esp_lcd_panel_draw_bitmap` detecta que el buffer de LVGL YA ES un
 *     framebuffer y no copia nada: solo cambia el indice y avisa.
 * Por eso los buffers de dibujo son de pantalla completa y viven en PSRAM, y
 * por eso NO se usa lvgl_port_add_disp() (su diseno da por hecho que el driver
 * copia por debajo, y con un panel RGB eso es exactamente lo que no vale).
 *
 * El dibujo directo desde PSRAM es ademas lo recomendado por Espressif para
 * paneles RGB: la PSRAM ya se esta leyendo para el panel, y la RAM interna se
 * reserva para lo que de verdad la necesita (WiFi, DMA).
 */
static esp_lcd_panel_handle_t s_rgb_panel = NULL;
static lv_disp_draw_buf_t s_disp_buf;
static lv_disp_drv_t s_disp_drv;

/* LVGL avisa aqui de que tiene un trozo listo. `draw_bitmap` escribe en el
 * framebuffer que el panel NO esta leyendo y cambia el DMA a ese; al volver, el
 * buffer de LVGL ya es el que se ve, asi que se puede reutilizar de inmediato. */
static uint32_t s_flush_n = 0;          /* refrescos desde el ultimo informe */
static uint32_t s_flush_t0 = 0;
static uint32_t s_draw_us = 0;          /* tiempo dentro de draw_bitmap */
static uint32_t s_sep_us = 0;           /* tiempo entre el fin de un refresco y el siguiente */
static uint64_t s_px = 0;               /* pixeles refrescados (para ver si es pantalla completa) */
static int64_t s_ultimo_fin = 0;

static void bsp_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    int64_t t0 = esp_timer_get_time();
    esp_lcd_panel_draw_bitmap(s_rgb_panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, color_p);
    int64_t t1 = esp_timer_get_time();
    lv_disp_flush_ready(drv);

    s_draw_us += (uint32_t)(t1 - t0);
    if (s_ultimo_fin) s_sep_us += (uint32_t)(t0 - s_ultimo_fin);
    s_ultimo_fin = t1;
    s_px += (uint32_t)(area->x2 - area->x1 + 1) * (uint32_t)(area->y2 - area->y1 + 1);

    /* DIAGNOSTICO TEMPORAL: refrescos/s, pixeles por cuadro (384000 = pantalla
     * completa), en que buffer y cuanto se tarda. */
    s_flush_n++;
    uint32_t ahora = (uint32_t)(esp_timer_get_time() / 1000);
    if (s_flush_t0 == 0) s_flush_t0 = ahora;
    if (ahora - s_flush_t0 >= 2000) {
        ESP_LOGI(TAG, "refrescos: %u en %u ms (%.1f/s) | %.0f px/s (pantalla completa = %.1f/s) | buf %p | driver %.1f ms, resto %.1f ms",
                 (unsigned)s_flush_n, (unsigned)(ahora - s_flush_t0),
                 s_flush_n * 1000.0 / (ahora - s_flush_t0),
                 s_px * 1000.0 / (ahora - s_flush_t0),
                 s_px / (double)(LCD_H_RES * LCD_V_RES) * 1000.0 / (ahora - s_flush_t0),
                 color_p,
                 s_draw_us / 1000.0 / s_flush_n, s_sep_us / 1000.0 / s_flush_n);
        s_flush_n = 0;
        s_flush_t0 = ahora;
        s_draw_us = 0;
        s_sep_us = 0;
        s_px = 0;
    }
}

/* ── Panel RGB ────────────────────────────────────────────────────────────── */
static esp_lcd_panel_handle_t bsp_rgb_panel_new(void)
{
    esp_lcd_panel_handle_t panel = NULL;
    const esp_lcd_rgb_panel_config_t cfg = {
        .clk_src    = LCD_CLK_SRC_DEFAULT,
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs    = 1,          /* un framebuffer: LVGL dibuja en RAM interna y el driver copia el trozo */
        .psram_trans_align = 64,  /* el DMA del RGB va mas fino alineado */
        /* Interfaz de datos DESHABILITADO: este panel no tiene pin de datos ni
         * comandos (no es un controlador con registros, es una pantalla RGB). */
        .disp_gpio_num = GPIO_NUM_NC,
        .timings = {
            /* 16 MHz es lo que usa la referencia de esta placa (y 14 MHz sin
             * bounce buffer). A 16 MHz el panel refresca a ~38 Hz; subirlo
             * acorta la ventana en la que el driver copia encima de lo que el
             * panel esta leyendo, o sea que la banda de scroll se estrecha. */
            .pclk_hz = 18 * 1000 * 1000,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_pulse_width = 7,
            .hsync_back_porch  = 40,
            .hsync_front_porch = 40,
            .vsync_pulse_width = 7,
            .vsync_back_porch  = 10,
            .vsync_front_porch = 10,
            .flags.pclk_active_neg = true,
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
    s_rgb_panel = panel;
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
        /* Sin driver_data: la version 1.0.4 del componente no lo usa (el
         * segundo campo de la config solo existe en la 2.x). */
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(io, &touch_cfg, &touch));
    ESP_LOGI(TAG, "Tactil GT911 listo (direccion 0x%02X)", TOUCH_I2C_ADDR);
    return touch;
}

/* ── Arranque completo ────────────────────────────────────────────────────── */
static lv_disp_t *s_disp = NULL;
static lv_indev_t *s_indev = NULL;

lv_disp_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg)
{
    ESP_ERROR_CHECK(bsp_display_brightness_init());

    /* lvgl_port_init() se sigue usando: es quien crea la tarea de LVGL y el
     * tic. Lo que NO se usa es lvgl_port_add_disp(): el display se registra a
     * mano mas abajo (ver el comentario largo sobre los framebuffers). */
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    if (cfg) port_cfg = cfg->lvgl_port_cfg;
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    ESP_ERROR_CHECK(bsp_i2c_init());

    esp_lcd_panel_handle_t panel = bsp_rgb_panel_new();
    ESP_LOGI(TAG, "Panel RGB %dx%d listo (ST7262, sin comandos: solo timing)",
             LCD_H_RES, LCD_V_RES);

    /* Buffers de dibujo de LVGL en RAM INTERNA, en trozos de 40 lineas.
     *
     * MEDIDO en esta placa: dibujar DIRECTAMENTE en los framebuffers de PSRAM
     * da 4 fotogramas por segundo (250 ms por cuadro, 0,8 ms de ellos dentro
     * del driver RGB). El resto es LVGL escribiendo pixel a pixel en PSRAM: al
     * escribir en PSRAM hay un fallo de cache por linea, y 800x480 sale a
     * ~650 ns/pixel. Con los buffers en RAM interna el mismo cuadro baja a
     * milisegundos y el driver copia el trozo al framebuffer de PSRAM de una
     * pasada (memcpy), que es como lo hace el ejemplo oficial de esta placa.
     *
     * 800 x 40 lineas x 2 B = 64 KB por buffer; dos, para que dibujar y copiar
     * no se esperen. En RAM interna no caben buffers de pantalla completa. */
    const uint32_t buf_px = LCD_H_RES * 40;
    static lv_color_t *buf1 = NULL, *buf2 = NULL;
    buf1 = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA);
    buf2 = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "sin RAM interna para los buffers de LVGL (64 KB x2)");
        return NULL;
    }
    lv_disp_draw_buf_init(&s_disp_buf, buf1, buf2, buf_px);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = LCD_H_RES;
    s_disp_drv.ver_res  = LCD_V_RES;
    s_disp_drv.flush_cb = bsp_lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_disp_buf;
    /* full_refresh: LVGL pinta el cuadro COMPLETO en cada refresco, no solo la
     * zona que cambio. Es obligatorio con framebuffers de pantalla completa:
     * al alternar A/B, cada buffer tiene que quedar con la pantalla entera
     * buena, o el panel ensena el trozo viejo del otro buffer (parpadeo).
     * A cambio, no se gana nada pintando solo trozos: el panel lee siempre la
     * pantalla completa de PSRAM. */
    s_disp_drv.full_refresh = 0;
    s_disp = lv_disp_drv_register(&s_disp_drv);
    if (!s_disp) {
        ESP_LOGE(TAG, "lv_disp_drv_register fallo");
        return NULL;
    }
    ESP_LOGI(TAG, "LVGL: buffers de dibujo en RAM interna (%p, %p), %u px cada uno",
             buf1, buf2, (unsigned)buf_px);

    if (cfg && cfg->rotate != LV_DISP_ROT_NONE) {
        /* El panel es horizontal de nacimiento (800x480), asi que lo normal es
         * no rotar. Se deja la puerta abierta por si la caja pide vertical. */
        lv_disp_set_rotation(s_disp, cfg->rotate);
    }

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = s_disp,
        .handle = bsp_touch_new(),
    };
    s_indev = lvgl_port_add_touch(&touch_cfg);

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
