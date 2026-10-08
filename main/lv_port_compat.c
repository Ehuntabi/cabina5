/* Implementacion del puente con la app (ver lv_port_compat.h). */
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "src/others/snapshot/lv_snapshot.h"
#include "lv_port_compat.h"

static const char *TAG = "port_compat";

/* ── Contador de vida de LVGL ───────────────────────────────────────────────
 * Un temporizador de LVGL de 100 ms que solo incrementa un contador. Si el
 * contador deja de subir, es que la tarea de LVGL esta atascada (no que este
 * trabajando), que es lo que necesita saber lvgl_wdog_task. */
static volatile uint32_t s_loop_count = 0;
static lv_timer_t *s_contador = NULL;

static void contador_cb(lv_timer_t *t)
{
    (void)t;
    s_loop_count++;
}

void lv_port_contador_arrancar(void)
{
    if (s_contador) return;                 /* idempotente */
    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "sin cerrojo de LVGL: el contador de vida no arranca");
        return;
    }
    s_contador = lv_timer_create(contador_cb, 100, NULL);
    lvgl_port_unlock();
    if (!s_contador) {
        ESP_LOGW(TAG, "no se pudo crear el contador de vida de LVGL");
    }
}

uint32_t lvgl_port_get_loop_count(void)
{
    return s_loop_count;
}

/* ── Captura de la pantalla activa ──────────────────────────────────────────
 * Redibuja el arbol de objetos entero a un buffer propio (lv_snapshot de
 * LVGL 9). Es mejor que copiar el buffer de dibujo de LVGL, que en modo parcial
 * y con doble buffer puede estar a medio pintar o ser el cuadro anterior: aqui
 * sale siempre la pantalla completa y actual. */
bool lv_port_snapshot(uint16_t **buf, uint16_t *w, uint16_t *h)
{
    if (!buf || !w || !h) return false;
    *buf = NULL;
    *w = 0;
    *h = 0;

    if (!lvgl_port_lock(2000)) {
        ESP_LOGW(TAG, "sin cerrojo de LVGL: no se puede capturar");
        return false;
    }

    bool ok = false;
    lv_obj_t *pantalla = lv_screen_active();
    lv_draw_buf_t *foto = lv_snapshot_take(pantalla, LV_COLOR_FORMAT_RGB565);
    if (foto && foto->data && foto->header.w > 0 && foto->header.h > 0) {
        const size_t n = (size_t)foto->header.w * foto->header.h * 2;
        uint16_t *copia = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
        if (copia) {
            memcpy(copia, foto->data, n);
            *buf = copia;
            *w = (uint16_t)foto->header.w;
            *h = (uint16_t)foto->header.h;
            ok = true;
        } else {
            ESP_LOGE(TAG, "sin memoria para la captura (%u bytes)", (unsigned)n);
        }
    } else {
        ESP_LOGE(TAG, "lv_snapshot_take fallo");
    }
    if (foto) lv_draw_buf_destroy(foto);
    lvgl_port_unlock();
    return ok;
}
