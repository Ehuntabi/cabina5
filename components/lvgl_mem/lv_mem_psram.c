/* Asignador de memoria de LVGL sobre PSRAM.
 *
 * POR QUE EXISTE ESTE FICHERO (medido al portar la app a la placa de 5"):
 *
 * La UI de este proyecto (4 pantallas con muchos widgets) pide ~145 KB al
 * construirse. En la placa de 3,5" eso salia del monton general y daba igual,
 * porque el BSP de alli no necesitaba RAM interna para nada mas. En la de 5" el
 * BSP SI necesita RAM interna: el bounce buffer del panel RGB (sin el, la imagen
 * se corre sola, ver esp_bsp.c) y los buffers de dibujo. El resultado medido,
 * con LVGL cogiendo del monton general, era:
 *
 *     RAM interna tras la pantalla: 171 KB libres
 *     RAM interna tras nav_init():   22 KB libres   <- la UI se lleva 145 KB
 *     antes de esp_wifi_init:        12 KB libres
 *     E wifi: create wifi task: failed to create task   -> reinicio en bucle
 *
 * Un pool ESTATICO de LVGL tampoco vale: el enlazador desborda dram0_0_seg (con
 * 256 KB se pasa 62 KB, o sea que el maximo que cabe es ~193 KB y la UI necesita
 * 145: no queda sitio para WiFi).
 *
 * Solucion: que LVGL coja su memoria de la PSRAM, donde sobran 7,4 MB. LVGL 9 lo
 * permite con LV_USE_CUSTOM_MALLOC: implementando aqui sus cuatro funciones
 * "core". El coste es que los objetos de la UI viven en PSRAM en vez de en RAM
 * interna; se acepta a cambio de que WiFi y el panel tengan la RAM que de verdad
 * no pueden sacar de otro sitio. Si algun dia se quiere afinar, este fichero es
 * el unico sitio que hay que tocar: por ejemplo, dejar en RAM interna las
 * reservas pequenas y mandar a PSRAM las grandes.
 */
#include "sdkconfig.h"

#if CONFIG_LV_USE_CUSTOM_MALLOC

#include <string.h>
#include "esp_heap_caps.h"
#include "lvgl.h"

/* Capacidades que se le piden al monton para la memoria de LVGL. */
#define LVGL_MEM_CAPS   (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

/* El monton de ESP-IDF ya existe cuando LVGL arranca, asi que no hay nada que
 * montar ni que desmontar: estas dos existen porque las llama lv_init() y
 * lv_deinit(). */
void lv_mem_init(void) { }
void lv_mem_deinit(void) { }

void *lv_malloc_core(size_t size)
{
    /* Umbral deliberadamente bajo: casi todo a PSRAM. MEDIDO en esta placa: la
     * UI necesita ~100 KB y la RAM interna se la disputan el bounce buffer del
     * panel (64 KB, obligatorio), WiFi y las pilas de las tareas; con el umbral
     * en 64 B quedaban 14 KB libres y ni el heartbeat ni el watchdog de LVGL
     * podian arrancar. Solo lo muy pequeno (nodos de listas, cadenas cortas) se
     * queda en interna, donde es mas rapido. */
    if (size <= 32) {
        void *p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (p) return p;
    }
    return heap_caps_malloc(size, LVGL_MEM_CAPS);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    return heap_caps_realloc(p, new_size, LVGL_MEM_CAPS);
}

void lv_free_core(void *p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
    if (!mon_p) return;
    const size_t libre = heap_caps_get_free_size(LVGL_MEM_CAPS);
    memset(mon_p, 0, sizeof(*mon_p));
    mon_p->total_size = libre;
    mon_p->free_size = libre;
    mon_p->free_biggest_size = heap_caps_get_largest_free_block(LVGL_MEM_CAPS);
}

lv_result_t lv_mem_test_core(void)
{
    /* No hay estructura que comprobar: la memoria la lleva el monton de ESP-IDF,
     * que ya se prueba solo. Se responde OK para no romper lv_mem_test(). */
    return LV_RESULT_OK;
}

#endif /* CONFIG_LV_USE_CUSTOM_MALLOC */
