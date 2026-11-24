/* ota_cartel.c — ver ota_cartel.h. El cartel de la actualizacion de la pantalla.
 *
 * Lo pinta un temporizador de 250 ms que mira ota_rx_estado(), asi que aqui no
 * se llama a nada de la OTA desde otra tarea: solo se LEE un struct con
 * cerrojo. Durante la descarga LVGL esta parado y el temporizador no corre; el
 * cartel se pone al dia en los parones que la propia OTA se toma cada 10 %
 * (ver "Un cuadro cada 10 %" en ota_rx.c).
 */
#include "ota_cartel.h"
#include "ui/ui_style.h"
#include "estilos.h"
#include "../net/ota_rx.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ota_cartel";

static lv_obj_t *s_cartel;
static lv_obj_t *s_titulo;
static lv_obj_t *s_detalle;
static lv_obj_t *s_barra;
static lv_obj_t *s_pie;

/* Que se esta viendo ahora, para no repintar en cada tic. */
static ota_rx_estado_t s_estado_pintado = OTA_RX_QUIETO;
static int             s_pct_pintado    = -1;
static char            s_motivo_pintado[96];

static void cartel_ocultar(void)
{
    if (!s_cartel) return;
    lv_obj_add_flag(s_cartel, LV_OBJ_FLAG_HIDDEN);
    s_estado_pintado = OTA_RX_QUIETO;
    s_pct_pintado = -1;
    s_motivo_pintado[0] = '\0';
}

static void cartel_mostrar(void)
{
    if (!s_cartel) return;
    lv_obj_clear_flag(s_cartel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_cartel);
    /* Y SE REPINTA LA PANTALLA ENTERA (23-nov-2026). Durante la descarga LVGL
     * se para a media pasada: al volver, el panel se queda con el cuadro a
     * medias (el dueño lo vio como "se ha descuadrado la pantalla" al pulsar
     * Configuracion). Invalidar la pantalla activa entera obliga a redibujarla
     * de arriba abajo en la siguiente pasada, asi que no quedan restos. */
    lv_obj_invalidate(lv_scr_act());
}

/* Pinta el estado que haya. Devuelve true si hay algo que enseñar. */
static void pinta(const ota_rx_vista_t *v)
{
    if (!s_cartel) return;

    switch (v->estado) {
        case OTA_RX_COMPROBANDO:
            lv_label_set_text(s_titulo, "Buscando actualización");
            lv_label_set_text(s_detalle, "Preguntando a la P4 qué versión tiene…");
            lv_obj_add_flag(s_barra, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_pie, "");
            cartel_mostrar();
            break;

        case OTA_RX_DESCARGANDO: {
            char t[40], d[64];
            snprintf(t, sizeof t, "Actualizando la pantalla");
            if (v->ofrecida[0])
                snprintf(d, sizeof d, "%s   ·   %d %%", v->ofrecida, v->pct);
            else
                snprintf(d, sizeof d, "%d %%", v->pct);
            lv_label_set_text(s_titulo, t);
            lv_label_set_text(s_detalle, d);
            lv_obj_clear_flag(s_barra, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(s_barra, v->pct, LV_ANIM_OFF);
            lv_label_set_text(s_pie, "No la apagues: se reinicia sola al terminar.");
            cartel_mostrar();
            break;
        }

        case OTA_RX_LISTO:
            lv_label_set_text(s_titulo, "Actualización instalada");
            lv_label_set_text(s_detalle, "Reiniciando la pantalla…");
            lv_obj_add_flag(s_barra, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_pie, "");
            cartel_mostrar();
            break;

        case OTA_RX_ERROR:
            lv_label_set_text(s_titulo, "No se ha podido actualizar");
            lv_label_set_text(s_detalle, v->motivo[0] ? v->motivo : "Error sin detalle");
            lv_obj_set_style_text_color(s_detalle, UI_COLOR_RED, 0);
            lv_obj_add_flag(s_barra, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_pie, "Sigue con la versión que tenía. El detalle, en Ajustes.");
            cartel_mostrar();
            break;

        default:
            /* QUIETO, HAY_NUEVA, AL_DIA: no es una actualizacion EN MARCHA. El
             * "hay nueva" se pregunta en Ajustes, no se avisa por las bravas:
             * un cartel que sale solo cada 60 s seria un estorbo. */
            cartel_ocultar();
            lv_obj_set_style_text_color(s_detalle, UI_COLOR_TEXT, 0);
            break;
    }
}

static void tic_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_cartel) return;

    ota_rx_vista_t v;
    memset(&v, 0, sizeof v);
    ota_rx_estado(&v);

    /* Error: se deja 60 s a la vista y se retira solo.
     *
     * ERAN 8 SEGUNDOS Y NO SE LEIAN (24-nov-2026). El dueño: "sale un mensaje
     * emergente en la cabina que no me da tiempo a leer". Ocho segundos estan
     * bien para un destello, no para enterarse de por que no se ha actualizado:
     * el cartel lleva el motivo entero y hay que poder leerlo con la pantalla a
     * un metro. Un minuto da tiempo sin quedarse pegado en el cuadro. */
    static uint32_t s_error_ms;
    if (v.estado == OTA_RX_ERROR) {
        if (strcmp(v.motivo, s_motivo_pintado) != 0) {
            snprintf(s_motivo_pintado, sizeof s_motivo_pintado, "%s", v.motivo);
            s_error_ms = lv_tick_get();
            if (s_error_ms == 0) s_error_ms = 1;
            pinta(&v);
            s_estado_pintado = OTA_RX_ERROR;
            return;
        }
        if (s_estado_pintado == OTA_RX_ERROR && s_error_ms &&
            lv_tick_elaps(s_error_ms) > 60000) {
            cartel_ocultar();
        }
        return;
    }
    s_error_ms = 0;

    if (v.estado != s_estado_pintado || v.pct != s_pct_pintado) {
        s_estado_pintado = v.estado;
        s_pct_pintado = v.pct;
        pinta(&v);
    }
}

void ota_cartel_create(void)
{
    if (s_cartel) return;

    /* Va en la capa de arriba: se ve con cualquier pantalla cargada y por encima
     * de los dialogos de confirmacion. */
    lv_obj_t *arriba = lv_layer_top();
    if (!arriba) {
        ESP_LOGW(TAG, "sin capa superior: no habra cartel de actualizacion");
        return;
    }

    s_cartel = lv_obj_create(arriba);
    lv_obj_set_size(s_cartel, 600, 210);
    lv_obj_align(s_cartel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_cartel, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(s_cartel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cartel, 3, 0);
    lv_obj_set_style_border_color(s_cartel, UI_COLOR_CYAN, 0);
    lv_obj_set_style_radius(s_cartel, 14, 0);
    lv_obj_set_style_pad_all(s_cartel, 18, 0);
    lv_obj_set_style_pad_row(s_cartel, 10, 0);
    lv_obj_clear_flag(s_cartel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_cartel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cartel, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_titulo = lv_label_create(s_cartel);
    lv_label_set_text(s_titulo, "Actualizando la pantalla");
    lv_obj_set_style_text_color(s_titulo, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_titulo, &FUENTE_MEDIA, 0);

    s_detalle = lv_label_create(s_cartel);
    lv_label_set_text(s_detalle, "");
    lv_obj_set_style_text_color(s_detalle, UI_COLOR_CYAN, 0);
    lv_obj_set_style_text_font(s_detalle, &FUENTE_NORMAL, 0);
    lv_obj_set_style_text_align(s_detalle, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_detalle, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_detalle, 560);

    s_barra = lv_bar_create(s_cartel);
    lv_obj_set_size(s_barra, 520, 18);
    lv_bar_set_range(s_barra, 0, 100);
    lv_bar_set_value(s_barra, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_barra, UI_COLOR_CARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_barra, UI_COLOR_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_barra, 9, LV_PART_MAIN);
    lv_obj_set_style_radius(s_barra, 9, LV_PART_INDICATOR);

    s_pie = lv_label_create(s_cartel);
    lv_label_set_text(s_pie, "");
    lv_obj_set_style_text_color(s_pie, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_pie, &FUENTE_PIE, 0);
    lv_obj_set_style_text_align(s_pie, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_add_flag(s_cartel, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(tic_cb, 250, NULL);
    ESP_LOGI(TAG, "cartel de actualizacion montado");
}
