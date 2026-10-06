/* Arranque de pantalla, tactil y brillo de la placa JC8048W550C.
 *
 * DIFERENCIA CLAVE con el satelite de 3,5" (para quien venga de alli):
 * - Alli el panel es QSPI y se le mandan ~200 comandos de inicializacion
 *   (controlador AXS15231B). Aqui NO: el panel es RGB paralelo, el controlador
 *   ST7262 no recibe comandos, y lo que se configura es el TIMING y un
 *   framebuffer en PSRAM del que el panel va leyendo solo. Por eso no hay
 *   "tabla de comandos" en este fichero.
 * - Aqui el panel no para de leer de PSRAM. Cualquier cosa que sature la PSRAM
 *   (copias grandes, escrituras gordas) se ve como parpadeo. El framebuffer va
 *   en PSRAM (no cabe en RAM interna) y los buffers de dibujo de LVGL en RAM
 *   INTERNA, que es el reparto que recomienda Espressif para paneles RGB.
 * - El tactil es un GT911 por I2C (en el 3,5" era otro).
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_check.h"
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

/* El bus I2C es compartido con lo que se cuelgue despues (acelerometro, etc.),
 * igual que en el 3,5". Se crea una sola vez. */
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
        .num_fbs    = 1,          /* un framebuffer; LVGL dibuja en RAM interna y copia por trozos */
        .psram_trans_align = 64,  /* el DMA del RGB va mas fino alineado */
        /* Interfaz de datos DESHABILITADO: este panel no tiene pin de datos ni
         * comandos (no es un controlador con registros, es una pantalla RGB). */
        .disp_gpio_num = GPIO_NUM_NC,
        .timings = {
            /* 16 MHz da margen de sobra para 800x480 a 60 Hz y es el valor que
             * usa la referencia de esta placa (14 MHz sin bounce buffer). */
            .pclk_hz = 16 * 1000 * 1000,
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

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    if (cfg) port_cfg = cfg->lvgl_port_cfg;
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    ESP_ERROR_CHECK(bsp_i2c_init());

    esp_lcd_panel_handle_t panel = bsp_rgb_panel_new();
    ESP_LOGI(TAG, "Panel RGB %dx%d listo (ST7262, sin comandos: solo timing)",
             LCD_H_RES, LCD_V_RES);

    /* Buffers de dibujo de LVGL en RAM INTERNA (el framebuffer del panel ya
     * ocupa PSRAM y el panel la lee sin parar). 800 x 40 lineas x 2 B = 64 KB
     * por buffer, y dos buffers para que dibujar y enviar no se esperen. */
    const uint32_t buf_px = LCD_H_RES * 40;
    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel,
        .buffer_size  = buf_px,
        .hres         = LCD_H_RES,
        .vres         = LCD_V_RES,
        .monochrome   = false,
        .flags.buff_dma = true,
        .flags.buff_spiram = 0,
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);
    if (!s_disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp fallo");
        return NULL;
    }

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
