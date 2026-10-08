/* Puente entre la aplicacion (escrita para LVGL 8 + el port propio "lv_port")
 * y el BSP de cabina5 (LVGL 9 + esp_lvgl_port 2.x).
 *
 * PARA QUE HACE FALTA: al portar la app del satelite de 3,5" a la placa de 5"
 * se decidio NO traer lv_port.c (1200 lineas de port propio que esp_lvgl_port
 * 2.x ya hace mejor: tarea de LVGL, tic, cerrojo, tactil). La aplicacion, en
 * cambio, llama a esos nombres en 47 sitios, asi que en vez de tocar 47 sitios
 * se traduce aqui. Es una capa fina a proposito: si algun dia se unifica, este
 * es el unico fichero que hay que borrar.
 *
 * LO QUE **NO** HACE: crear el display ni el tactil. De eso se encarga
 * bsp_display_start_with_config() (ver esp_bsp.c), que ademas es quien monta el
 * bounce buffer sin el cual la imagen se corre sola en esta placa.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* El cerrojo y el tic ya los lleva esp_lvgl_port con los mismos nombres. */
/* lvgl_port_lock() y lvgl_port_unlock() vienen de esp_lvgl_port.h. */

/* Contador de vida de LVGL. En el port viejo lo llevaba el bucle de la tarea;
 * en esp_lvgl_port no existe, asi que se cuenta con un temporizador de LVGL de
 * 100 ms. Sirve para lo mismo que antes: que lvgl_wdog_task distinga "LVGL
 * atascado" de "LVGL trabajando" (si el contador no sube en 60 s, la tarea de
 * LVGL esta colgada y se deja saltar el Task WDT para que la placa se recupere).
 * Arrancarlo es idempotente. */
uint32_t lvgl_port_get_loop_count(void);
void lv_port_contador_arrancar(void);

/* Captura de la pantalla activa (la usa el modo captura por USB,
 * capture_carousel.c). Devuelve un buffer RGB565 de w*h en PSRAM que hay que
 * liberar con free(). En LVGL 9 esto lo hace lv_snapshot_take() redibujando el
 * arbol, que es mejor que copiar el buffer de dibujo: sale un cuadro coherente
 * aunque LVGL este a medias de pintar. */
bool lv_port_snapshot(uint16_t **buf, uint16_t *w, uint16_t *h);

#ifdef __cplusplus
}
#endif
