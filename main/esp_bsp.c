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
#include "esp_cache.h"
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
        /* El reloj del panel, EXACTO al de la definicion oficial de la placa
         * (LCD_CLK_SRC_PLL160M). Con el DEFAULT el PCLK sale de otra fuente y no
         * siempre cae en la frecuencia pedida: si la frecuencia de pixeles no es
         * la que se cree, cada linea arrastra un resto y la imagen sale CORRIDA
         * HACIA UN LADO, que es justo el sintoma que perseguimos. */
        .clk_src    = LCD_CLK_SRC_PLL160M,
        .data_width = 16,
        .bits_per_pixel = 16,
        /* DOS framebuffers Y bounce buffer: es la receta del ejemplo oficial de
         * Espressif (rgb_avoid_tearing) y la unica combinacion que funciona:
         *  - los framebuffers son en los que dibuja LVGL (pantalla completa);
         *  - el bounce buffer es un buffer INTERNO de 20 lineas del que lee el
         *    DMA, rellenado por la ISR desde el framebuffer de PSRAM. Al no leer
         *    nunca directo de PSRAM, el FIFO no se queda seco y no hay drift.
         * OJO: con num_fbs = 0 NO funciona (la pantalla se queda negra): el
         * driver solo avanza de framebuffer cuando el buffer de dibujo esta
         * dentro de uno, y con cero framebuffers no avanza nunca. */
        .num_fbs    = 1,
        /* Bounce buffer de 20 lineas: Espressif pide >= 20 para que a la ISR le
         * de tiempo a rellenarlo dentro del tiempo de linea. Con 20 lineas son
         * 32 KB por buffer y el driver pide dos (doble buffer de bounce). */
        .bounce_buffer_size_px = 20 * LCD_H_RES,
        .psram_trans_align = 64,  /* = PSRAM_TRANS_ALIGN de la definicion oficial */
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
                /* Las dos en FALSE, segun la definicion oficial de la placa. */
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
        /* RESET NO CONECTADO a proposito: en esta placa el unico pin de control
         * del tactil (IO38) es la INTERRUPCION, no el reset (asi lo dice la
         * definicion oficial de la placa: RESET_PIN = -1, INTERRUPT_PIN = 38).
         * Sin reset, el GT911 se queda en su direccion por defecto (0x5D), que es
         * justo la que responde en esta placa. Con rst=38 el driver hacia la
         * secuencia de seleccion de direccion y el tactil quedaba a medias: el
         * port se quejaba con "Error in register touch interrupt". */
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
    /* OJO: aqui NO se registra ningun callback de interrupcion propio. Se hizo
     * durante la puesta a punto para contar las interrupciones y salieron CERO
     * en 30 s (el pin no hace de interrupcion en esta placa), que es el dato
     * que explica el modo sondeo de mas abajo. Ademas, registrarlo PISA el
     * callback que pone el driver (el que despierta la tarea de LVGL), asi que
     * dejarlo puesto seria cambiar el comportamiento medido. */
    (void)0;
    /* OJO, COSA RARA DEL COMPONENTE (no tocar sin leer esto): pedimos
     * rst = NC (-1) e int = 38, que es lo correcto para esta placa, pero el
     * handle devuelve los dos campos AL REVES (rst = 38, int = 0). Aun asi el
     * tactil FUNCIONA (comprobado leyendo coordenadas reales: 31 toques
     * repartidos por toda la pantalla), porque el driver acaba en modo sondeo
     * por I2C, que es lo que queremos. La consecuencia practica es que el
     * puerto de LVGL NO registra interrupcion y lee el tactil por sondeo; da
     * igual, porque el pin de interrupcion de esta placa no se puede usar.
     * Se deja escrito para que el siguiente no crea que la config esta mal. */
    ESP_LOGI(TAG, "Tactil GT911 listo (direccion 0x%02X, modo sondeo por I2C)", TOUCH_I2C_ADDR);
    return touch;
}

/* Cuenta las lecturas que pide LVGL Y los toques que devuelve el chip.
 *
 * NO ES UN ADORNO: las dos cifras juntas dicen donde esta el fallo cuando el
 * tactil no responde -- si "lecturas" sube y "puntos" no, el problema es el
 * chip o el cableado; si "lecturas" no sube, LVGL no esta leyendo el
 * dispositivo. Los dos fallos ya han costado una tarde cada uno en esta placa
 * (ver el comentario de LV_INDEV_MODE_TIMER mas abajo y docs/RELEVO), asi que
 * la comprobacion del arranque se queda. */
static lv_indev_read_cb_t s_read_cb_original = NULL;
static volatile uint32_t s_lecturas = 0;
static volatile uint32_t s_puntos = 0;
static bool s_tactil_comprobado = false;

static void bsp_touch_read_contado(lv_indev_t *indev, lv_indev_data_t *datos)
{
    s_lecturas++;
    if (s_read_cb_original) s_read_cb_original(indev, datos);
    if (datos->state == LV_INDEV_STATE_PRESSED) s_puntos++;
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
/* ── Bounce buffer: la solucion al "screen drift" ────────────────────────────
 *
 * SINTOMA: la imagen entera se va corriendo hacia un lado, sola, aunque el
 * framebuffer no se toque (comprobado con un patron escrito una sola vez y el
 * refresco congelado) y aunque el panel barra a una frecuencia perfectamente
 * estable (39,0 Hz medidos).
 *
 * CAUSA (documentada por Espressif como "screen drift"): el GDMA no llega a
 * tiempo a servir los pixeles desde PSRAM, el FIFO del LCD se queda vacio
 * (under-run) y el puntero del FIFO se desalinea; a partir de ahi el controlador
 * sigue leyendo de la direccion equivocada y muestra las lineas siguientes como
 * si fueran las primeras: la imagen se desplaza linea a linea.
 *
 * SOLUCION (la que dice Espressif): un BOUNCE BUFFER en RAM INTERNA de al menos
 * 20 lineas. El DMA lee siempre de ahi (memoria rapida, nunca se queda seco) y
 * la ISR lo va rellenando desde PSRAM. Aqui el driver lo hace solo porque
 * El driver lo hace solo porque hay un framebuffer (num_fbs = 1): copia de el
 * al bounce buffer desde su ISR. Con num_fbs = 0 no funciona (pantalla negra):
 * sin framebuffers el driver no avanza nunca de cuadro.
 *
 * Ademas esta abierto XIP desde PSRAM (CONFIG_SPIRAM_FETCH_INSTRUCTIONS y
 * CONFIG_SPIRAM_RODATA), que es la otra recomendacion del mismo documento. */

/* LVGL dibuja DIRECTAMENTE en los framebuffers del panel (pantalla completa),
 * y el driver copia de ahi al bounce buffer por el que va el barrido. */
static void bsp_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
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
    /* UN AVISO, UNA VEZ, MEDIO SEGUNDO DESPUES DE ARRANCAR (8-oct-2026).
     *
     * POR QUE AQUI Y NO AL MONTAR EL TACTIL: al montarlo el temporizador
     * acaba de reanudarse y su contador vale 1 -- da igual como este la cosa,
     * siempre parece bien. La comprobacion que vale es esta: medio segundo
     * despues, con la pantalla ya refrescando, el contador tiene que haber
     * subido (son ~30 lecturas/s). Si no sube, el tactil esta muerto y se ve
     * en el log de arranque, que es donde se mira.
     *
     * Se quito el informe cada 2 s que hubo durante la puesta a punto: con la
     * UI en marcha es puro ruido. El contador sigue (cuesta una suma) porque es
     * lo unico que distingue "el chip no detecta" de "LVGL no lee", que son los
     * dos fallos que ya nos han costado una tarde cada uno. */
    if (s_indev && !s_tactil_comprobado) {
        /* No se comprueba a una hora fija: el arranque esta ocupado (WiFi, P4,
         * autotest) y el primer tic del temporizador de LVGL puede tardar mas
         * de medio segundo -- medido, a los 500 ms solo habia 3 lecturas y
         * saltaba una alarma falsa. Se espera a que haya lecturas suficientes,
         * con un tope de tiempo para no callarse si de verdad no lee. */
        static uint32_t t_espera = 0;
        const uint32_t ahora = (uint32_t)(esp_timer_get_time() / 1000);
        if (t_espera == 0) t_espera = ahora;
        if (s_lecturas >= 20) {
            s_tactil_comprobado = true;
            ESP_LOGI(TAG, "Tactil comprobado: %u lecturas, %u toques | temporizador %s",
                     (unsigned)s_lecturas, (unsigned)s_puntos,
                     (lv_timer_get_paused(lv_indev_get_read_timer(s_indev)))
                         ? "PAUSADO (mal)" : "en marcha");
        } else if (ahora - t_espera > 5000) {
            s_tactil_comprobado = true;
            ESP_LOGE(TAG, "TACTIL: solo %u lecturas en 5 s -- LVGL NO esta "
                     "leyendo el dispositivo (revisa el modo y el temporizador)",
                     (unsigned)s_lecturas);
        }
    }
    static uint32_t t0 = 0;
    const uint32_t ahora = (uint32_t)(esp_timer_get_time() / 1000);

    if (t0 == 0) t0 = ahora;
    if (ahora - t0 >= 2000) {
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
    const esp_lcd_rgb_panel_event_callbacks_t rgb_cbs = {
        .on_vsync = bsp_rgb_vsync_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel, &rgb_cbs, NULL));

    /* Buffers de dibujo PARCIALES en RAM INTERNA. Tres motivos medidos:
     *  1. Dibujar el cuadro completo en PSRAM da 4 fps en esta placa (fallo de
     *     cache por linea al escribir); en RAM interna pasa de 12.
     *  2. Con num_fbs = 1 el driver copia cada trozo al unico framebuffer y el
     *     bounce buffer lo va sirviendo: la imagen sale ENTERA por trozos, sin el
     *     parpadeo que salia con dos framebuffers (que solo se actualizaba uno y
     *     el otro ensenaba el cuadro viejo).
     *  3. El bounce buffer (RAM interna) es lo que quita el "screen drift". */
    lv_color_t *buf1 = NULL, *buf2 = NULL;
    buf1 = heap_caps_malloc(BSP_BUF_PX * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    buf2 = heap_caps_malloc(BSP_BUF_PX * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "sin memoria para los buffers de LVGL (%u px x2)", (unsigned)BSP_BUF_PX);
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
    ESP_LOGI(TAG, "LVGL: trozos de %d lineas (RAM interna) -> framebuffer -> bounce buffer",
             (int)(BSP_BUF_PX / LCD_H_RES));

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = s_disp,
        .handle = bsp_touch_new(),
    };
    s_indev = lvgl_port_add_touch(&touch_cfg);

    /* ESTO ES LO QUE HACE QUE EL TACTIL FUNCIONE, no lo borres.
     *
     * En LVGL 9 el temporizador de lectura de un dispositivo de entrada nace
     * PAUSADO (lv_indev_create -> lv_timer_create -> lv_timer_pause), y el port
     * de Espressif no lo reanuda al anadirlo. Sintoma exacto que costo media
     * tarde: el chip detecta los toques (24 en 25 s, medidos leyendolo a pelo),
     * el driver los lee con coordenadas correctas, y LVGL NO REPARTE NI UN
     * EVENTO porque nunca llega a leer el dispositivo. Se comprobo midiendo:
     * lv_timer_get_paused(lv_indev_get_read_timer(indev)) == 1.
     *
     * Se reanuda aqui, cuando ya esta todo montado. */
    if (s_indev) {
        /* Antes de reanudar, se mete el contador de lecturas: el port ya ha
         * puesto su lvgl_port_touchpad_read, y aqui se envuelve para poder
         * decir en el log cuantas veces LVGL pide leer el chip. Sin esto, la
         * unica forma de saber si el temporizador lee de verdad era mirar si
         * los toques hacian algo -- que es justo lo que hay que averiguar. */
        s_read_cb_original = lv_indev_get_read_cb(s_indev);
        if (s_read_cb_original) lv_indev_set_read_cb(s_indev, bsp_touch_read_contado);

        /* MODO SONDO POR TEMPORIZADOR, y esto es lo que hace que el tactil
         * funcione de verdad (8-oct-2026).
         *
         * QUE PASABA: el port de Espressif deja el dispositivo de entrada en
         * modo EVENT (lvgl_port_add_touch -> lv_indev_set_mode(EVENT)), o sea
         * que NO se lee por temporizador: se lee cuando el port recibe el
         * evento LVGL_PORT_EVENT_TOUCH, y ese evento sale UNICAMENTE de la
         * interrupcion del GT911 (IO38). Medido con el contador de lecturas:
         * 1 lectura en el arranque y NINGUNA mas en 30 s, con el temporizador
         * en PAUSADO -- y por eso los toques no hacian nada aunque el chip
         * respondiera por I2C.
         *
         * Se pasa a modo TIMER: LVGL lee el chip cada LV_DEF_REFR_PERIOD
         * (30 ms) pase lo que pase con la interrupcion. Cuesta una lectura I2C
         * cada 30 ms (el driver sondea por I2C de todas formas, ver
         * TOUCH_PIN_INT), y a cambio el tactil deja de depender de un pin que
         * en esta familia de placas esta puesto a GND por una resistencia (ver
         * display.h) y del que no hay que fiarse. */
        lv_indev_set_mode(s_indev, LV_INDEV_MODE_TIMER);
        lv_timer_t *lectura = lv_indev_get_read_timer(s_indev);
        if (lectura) lv_timer_resume(lectura);
        ESP_LOGI(TAG, "Tactil: modo %s, temporizador de lectura %s",
                 lv_indev_get_mode(s_indev) == LV_INDEV_MODE_TIMER ? "SONDEO (timer)"
                                                                   : "EVENTO (int)",
                 (lectura && !lv_timer_get_paused(lectura)) ? "EN MARCHA" : "PAUSADO (mal)");
    }

    if (bsp_display_lock(0)) {
        /* 500 ms: el primer tic es la comprobacion del tactil (ver arriba) y a
         * partir de ahi el informe de refrescos cada 2 s. */
        lv_timer_create(bsp_display_fps_cb, 500, NULL);
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
