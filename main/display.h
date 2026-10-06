/* Pines y timing de la placa Guition JC8048W550C (ESP32-S3, 5" 800x480).
 *
 * DE DONDE SALEN: del BSP de su placa hermana Sunton/Guition 8048S050C
 * (github.com/mr-sven/esp32-8048S050C), que es la misma familia de modulo con
 * 5" y 800x480. En estos clones los pines coinciden casi siempre, pero ESTO NO
 * ESTA PROBADO EN NUESTRA PLACA: si el panel sale en blanco, con la imagen
 * desplazada o con los colores cambiados, el sospechoso numero uno es este
 * fichero. Se corrige mirando el esquema que viene en la caja.
 *
 * Todo lo de aqui es suposicion a verificar en el bring-up (marcado con [V]). */
#pragma once

#include "driver/gpio.h"

/* ── Panel RGB (ST7262), 16 bits ──────────────────────────────────────────── */
#define LCD_H_RES               800
#define LCD_V_RES               480

#define LCD_PIN_HSYNC           GPIO_NUM_39   /* [V] */
#define LCD_PIN_VSYNC           GPIO_NUM_41   /* [V] */
#define LCD_PIN_DE              GPIO_NUM_40   /* [V] */
#define LCD_PIN_PCLK            GPIO_NUM_42   /* [V] */

/* Orden B0..B4, G0..G5, R0..R4 (RGB565 en paralelo) */
#define LCD_PIN_DATA_B0         GPIO_NUM_8    /* [V] */
#define LCD_PIN_DATA_B1         GPIO_NUM_3
#define LCD_PIN_DATA_B2         GPIO_NUM_46
#define LCD_PIN_DATA_B3         GPIO_NUM_9
#define LCD_PIN_DATA_B4         GPIO_NUM_1
#define LCD_PIN_DATA_G0         GPIO_NUM_5
#define LCD_PIN_DATA_G1         GPIO_NUM_6
#define LCD_PIN_DATA_G2         GPIO_NUM_7
#define LCD_PIN_DATA_G3         GPIO_NUM_15
#define LCD_PIN_DATA_G4         GPIO_NUM_16
#define LCD_PIN_DATA_G5         GPIO_NUM_4
#define LCD_PIN_DATA_R0         GPIO_NUM_45
#define LCD_PIN_DATA_R1         GPIO_NUM_48
#define LCD_PIN_DATA_R2         GPIO_NUM_47
#define LCD_PIN_DATA_R3         GPIO_NUM_21
#define LCD_PIN_DATA_R4         GPIO_NUM_14

/* ── Retroiluminacion (PWM por LEDC) ──────────────────────────────────────── */
#define LCD_PIN_BACKLIGHT       GPIO_NUM_2    /* [V] */
#define LCD_BL_LEDC_TIMER       LEDC_TIMER_0
#define LCD_BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define LCD_BL_LEDC_FREQ_HZ     4000
#define LCD_BL_LEDC_RES         LEDC_TIMER_8_BIT

/* ── Tactil GT911 por I2C ─────────────────────────────────────────────────── */
#define TOUCH_PIN_SCL           GPIO_NUM_20   /* [V] */
#define TOUCH_PIN_SDA           GPIO_NUM_19   /* [V] */
#define TOUCH_PIN_RST           GPIO_NUM_38   /* [V] */
/* En esta familia el pin de INTERRUPCION del GT911 va ruteado a GND por una
 * resistencia (R17 en el 8048S050C): no se puede usar, y por eso el driver va
 * sondeando por I2C en vez de esperar interrupcion. */
#define TOUCH_PIN_INT           GPIO_NUM_NC   /* [V] */
#define TOUCH_I2C_ADDR          0x5D          /* por lo mismo: la 0x14 no responde */
#define TOUCH_I2C_SPEED_HZ      400000
