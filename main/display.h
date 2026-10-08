/* Pines y timing de la placa Guition JC8048W550C (ESP32-S3, 5" 800x480).
 *
 * ESTADO: VERIFICADO EN LA PLACA (8-oct-2026). La pantalla se ve bien, quieta y
 * con los colores correctos, y el tactil responde en toda la superficie. Lo que
 * esta aqui ya no es una suposicion.
 *
 * DE DONDE SALEN los numeros, porque hay dos fuentes y NO coinciden:
 *   1. El xlsx oficial de Guition (5-IO pin distribution) del paquete de
 *      descarga. Es comodo pero tiene una errata: en el canal G las etiquetas de
 *      bit no van seguidas (DB6, DB8, DB9, DB10, DB11, DB7).
 *   2. La definicion oficial de placa (JC8048W550C.json del proyecto
 *      platformio-espressif32-sunton) y el repo ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ,
 *      que es codigo que funciona en esta placa.
 * Manda la 2, y ademas se ha comprobado en la pantalla pidiendo COLORES PUROS.
 *
 * OJO CON LOS CANALES R Y B: los dos grupos llevan los MISMOS numeros de pin y
 * solo cambia la etiqueta, asi que el xlsx no sirve para decidirlos. La
 * asignacion de abajo es la buena, y se comprobo pidiendo azul puro (0x001F):
 * con los grupos cruzados el azul sale ROJO y el blanco sale AMARILLO. No la
 * "arregles" mirando la hoja de calculo.
 *
 * El timing NO vive aqui: esta en esp_bsp.c, con la explicacion de por que el
 * bounce buffer es imprescindible (si no, la imagen se corre sola).
 */
#pragma once

#include "driver/gpio.h"

/* ── Panel RGB (ST7262), 16 bits ──────────────────────────────────────────── */
#define LCD_H_RES               800
#define LCD_V_RES               480

#define LCD_PIN_HSYNC           GPIO_NUM_39
#define LCD_PIN_VSYNC           GPIO_NUM_41
#define LCD_PIN_DE              GPIO_NUM_40
#define LCD_PIN_PCLK            GPIO_NUM_42

/* Orden B0..B4, G0..G5, R0..R4 (RGB565 en paralelo).
 *
 * ESTA ASIGNACION ES LA BUENA, y se ha comprobado de la unica forma que vale:
 * pidiendo azul puro (0x001F) y viendo azul en la pantalla. Con los grupos B y R
 * cambiados, el azul puro sale ROJO y el blanco sale AMARILLO (porque al canal
 * azul del panel le llegan los bits del rojo), que es justo lo que se vio.
 *
 * En el xlsx de Guition los dos grupos llevan los MISMOS numeros de pin
 * (IO8/IO3/IO46/IO9/IO1 e IO45/IO48/IO47/IO21/IO14) y solo cambia la etiqueta
 * DB1(B)..DB5(B) / DB13(R)..DB17(R), asi que el xlsx no sirve para decidir: la
 * prueba en la pantalla manda. */
#define LCD_PIN_DATA_B0         GPIO_NUM_8
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
#define LCD_PIN_BACKLIGHT       GPIO_NUM_2 
#define LCD_BL_LEDC_TIMER       LEDC_TIMER_0
#define LCD_BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define LCD_BL_LEDC_FREQ_HZ     4000
#define LCD_BL_LEDC_RES         LEDC_TIMER_8_BIT

/* ── Tactil GT911 por I2C ─────────────────────────────────────────────────── */
#define TOUCH_PIN_SCL           GPIO_NUM_20
#define TOUCH_PIN_SDA           GPIO_NUM_19
/* IO38 es la INTERRUPCION del tactil, no su reset. El GT911 de esta placa no
 * lleva reset conectado: se queda en su direccion por defecto (0x5D). */
#define TOUCH_PIN_RST           GPIO_NUM_NC
/* En esta familia el pin de INTERRUPCION del GT911 va ruteado a GND por una
 * resistencia (R17 en el 8048S050C): no se puede usar, y por eso el driver va
 * sondeando por I2C en vez de esperar interrupcion. */
#define TOUCH_PIN_INT           GPIO_NUM_38
#define TOUCH_I2C_ADDR          0x5D          /* por lo mismo: la 0x14 no responde */
#define TOUCH_I2C_SPEED_HZ      400000
