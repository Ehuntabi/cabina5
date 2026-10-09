#include "capture_carousel.h"

/* Estos van aqui arriba y no dentro del bloque del carrusel: la inyeccion de
 * datos de ejemplo (modo banco) se compila tambien con el carrusel apagado. */
#include <stdbool.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "mini_proto.h"
#include "data_model.h"
#include "ui/view_info.h"

static const char *TAG = "capture_carousel";

/* Datos de ejemplo para mirar la pantalla en el banco: esta placa no tiene
 * nada conectado, asi que la P4 manda todo como "sin dato" y solo se ven --.
 * Lo usan los dos modos: el carrusel de capturas y el "solo datos". */
#if CAPTURE_CAROUSEL_ENABLE || CAPTURE_CAROUSEL_SOLO_DATOS
static void inject_sim_data(void)
{
    mini_data_t d = {0};
    d.has_data            = true;
    d.shunt_soc_deci       = 782;    /* 78.2 % */
    d.shunt_voltage_centi  = 1342;   /* 13.42 V */
    d.shunt_current_milli  = -3500;  /* -3.5 A */
    d.shunt_power_w        = -47;
    d.aux_value_raw        = 1265;   /* 12.65 V */
    d.aux_input            = 0;
    d.aux_has_data         = true;
    d.dcdc_v_in_centi      = 1420;
    d.dcdc_v_out_centi     = 1385;
    d.dcdc_state           = 4;      /* Absorption */
    d.dcdc_has_data        = true;
    d.frigo_temp_centi     = 420;    /* 4.2 C */
    d.frigo_fan_pct        = 60;
    d.exterior_temp_centi  = 2350;   /* 23.5 C */
    d.frigo_has_data       = true;
    d.exterior_has_data    = true;
    d.water_clean          = 3;
    d.water_gray           = 1;
    d.water_clean_has_data = true;
    d.water_gray_has_data  = true;
    d.epoch_local          = 1788800000;
    d.gps_estado           = 2;      /* posicion fijada */
    /* Alarma de bateria activa (la palabra "bateria baja" es la de la P4). El
     * bit tiene que ser el de mini_proto.h: es el mismo byte que viaja. */
    d.alarmas              = MINI_ALARM_BATERIA;
    d.last_update_ms       = (uint32_t)(esp_timer_get_time() / 1000);
    data_model_set_simulated(&d);
}
#endif

#if CAPTURE_CAROUSEL_ENABLE

#include <stdio.h>
#include <stdlib.h>
#include "lvgl.h"
#include "esp_bsp.h"   /* bsp_mirar_framebuffer: diagnostico */
#include "lv_port_compat.h"
#include "ui/nav.h"
#include "ui/view_registro.h"
#include "ui/view_info.h"      /* view_info_captura_alarma_silenciada() */
#include "data_model.h"
#include "net/mini_proto.h"    /* MINI_ALARM_BATERIA: el bit que manda la P4 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

/* La segunda definicion de TAG que habia aqui ("capture") sobraba y rompia la
 * compilacion ("redefinition of TAG") en cuanto se encendia
 * CAPTURE_CAROUSEL_ENABLE. Quitada el 8-oct-2026. */

typedef enum {
    STEP_INCLINACION,
    STEP_INFO,
    STEP_REGISTRO,
    STEP_AJUSTES,
    STEP_COUNT
} step_t;

/* Valores de relleno solo para que las 4 pantallas se vean pobladas en la
 * captura, no telemetria real. Uno de cada bloque, todos con has_data=true.
 *
 * Alarma: se pone la de BATERIA activa (bit MINI_ALARM_BATERIA, el mismo byte
 * que manda la P4 en la telemetria) para que se vea el icono del altavoz en la
 * esquina de su tarjeta, y ademas silenciada, que es el estado que hay que
 * poder enseñar. Ver view_info_captura_alarma_silenciada().
 *
 * OJO: last_update_ms se sella con el reloj REAL en cada llamada. Antes esto se
 * llamaba una sola vez al arrancar y el 0xFF... del modelo dejaba el enlace
 * "caducado" a los 5 s, o sea que a partir de ahi la captura salia con los
 * datos en gris y sin el icono de alarma (los dos caducan con el enlace). */

/* Vuelca la pantalla activa por UART en base64, con delimitadores para que
 * un script en el PC lo pueda extraer del log de idf.py monitor. Se llama
 * con el lock de LVGL ya tomado: nadie mas toca el framebuffer mientras
 * se lee.
 *
 * El frame sale de lv_port_snapshot() y no del draw_buf de LVGL (3.M5):
 * lo que hay que enseñar es lo que el panel esta viendo, o sea la copia
 * rotada que el flush manda al panel; draw_buf->buf_act esta sin rotar y
 * no es de fiar en el momento de la captura. El tamano sigue siendo el del
 * panel logico (480x320), asi que el formato del volcado y el decodificador
 * no cambian. */
static void dump_screen_uart(const char *name)
{
    uint16_t w = 0, h = 0;
    uint16_t *pix = NULL;
    if (!lv_port_snapshot(&pix, &w, &h)) {
        /* El port aun no ha flasheado ningun frame completo: sin imagen que
         * enseñar, esta captura se omite con aviso. */
        ESP_LOGW(TAG, "sin frame flasheado todavia: captura '%s' omitida", name);
        return;
    }

    uint32_t hres = w;
    uint32_t vres = h;
    size_t raw_len = (size_t)hres * vres * sizeof(lv_color_t);
    const uint8_t *fb = (const uint8_t *)pix;

    /* ── EL VOLCADO: BINARIO CON LONGITUD DECLARADA, no base64 ─────────────
     *
     * POR QUE CAMBIO (8-oct-2026): con base64, cualquier byte que se cuele en
     * medio (una traza del watchdog, un aviso de otra tarea) rompe el bloque
     * ENTERO y no hay forma de saber cuanto falta: la primera tanda salio con
     * 500 KB de mas por pantalla y no se pudo decodificar ni una. Con el
     * formato de abajo:
     *
     *   ===BIN:<nombre>:<ancho>x<alto>:<bytes>===\n
     *   <bytes en crudo, RGB565 little endian>
     *   \n===FIN===\n
     *
     * el receptor sabe EXACTAMENTE cuantos bytes esperar, asi que un corte o un
     * reintento se detecta en vez de corromper la imagen. Y quita el 33% de
     * sobrecarga del base64: ~67 s por pantalla en vez de ~90.
     *
     * El log se baja a ERROR mientras dura (no a NONE): un aviso grave todavia
     * sale, y como la longitud manda, lo que salga se puede descartar. */
    ESP_LOGI(TAG, "volcando '%s' (%ux%u, %u bytes, ~%u s a 115200)", name,
             (unsigned)hres, (unsigned)vres, (unsigned)raw_len,
             (unsigned)(raw_len / 11520));
    esp_log_level_set("*", ESP_LOG_ERROR);

    printf("===BIN:%s:%ux%u:%u===\n", name, (unsigned)hres, (unsigned)vres,
           (unsigned)raw_len);
    fflush(stdout);

    const size_t TROZO = 2048;
    for (size_t i = 0; i < raw_len; i += TROZO) {
        size_t n = (raw_len - i < TROZO) ? (raw_len - i) : TROZO;
        fwrite(fb + i, 1, n, stdout);
        fflush(stdout);
        /* ~180 ms por trozo: se cede la CPU (que es lo que deja correr a IDLE y
         * al watchdog) y se alimenta el WDT por si esta tarea estuviera
         * suscrita. Sin esto el Task WDT saltaba a mitad del volcado y sus
         * trazas se colaban en el flujo. */
        esp_task_wdt_reset();
        vTaskDelay(1);
    }
    printf("\n===FIN===\n");
    fflush(stdout);
    esp_log_level_set("*", ESP_LOG_INFO);
}

/* Muestra una pantalla/formulario de registro ya creado (via el "mostrar"
 * que se pase) y lo captura. Settle de 900ms sin el lock: con 500ms se vio
 * algun resto visual de la pantalla anterior colandose en el borde (LVGL
 * necesita soltarse para pintar antes de leer el framebuffer, y a veces no
 * le bastaba). Detectado en la primera tanda de capturas, 08-sep-2026. */
static void capture_registro_paso(void (*mostrar)(int), int idx, const char *prefijo)
{
    if (lvgl_port_lock(1000)) {
        mostrar(idx);
        lvgl_port_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(900));
    if (lvgl_port_lock(1000)) {
        char nombre[48];
        const char *n = (mostrar == view_registro_mostrar_pantalla)
                       ? view_registro_nombre_pantalla(idx)
                       : view_registro_nombre_formulario(idx);
        snprintf(nombre, sizeof(nombre), "%s_%s", prefijo, n);
        dump_screen_uart(nombre);
        lvgl_port_unlock();
    }
}

/* Todos los menus y formularios del carrusel de registro. mostrar_menu()/
 * show_form() en view_registro.c son interruptores puros de visibilidad
 * sobre contenedores ya creados -- no dependen de tener una salida de
 * verdad abierta, asi que se pueden recorrer todos sin simular ninguna.
 * view_registro_reset() al final los deja tal y como estaban antes de
 * entrar (mismo criterio que usa nav.c al salir de esta pagina). */
static void capture_registro_todo(void)
{
    for (int i = 0; i < view_registro_num_pantallas(); i++) {
        capture_registro_paso(view_registro_mostrar_pantalla, i, "registro_menu");
    }
    for (int i = 0; i < view_registro_num_formularios(); i++) {
        capture_registro_paso(view_registro_mostrar_formulario, i, "registro_form");
    }
    if (lvgl_port_lock(1000)) {
        view_registro_reset();
        lvgl_port_unlock();
    }
}

static void carousel_task(void *arg)
{
    (void)arg;
    /* Cada vuelta se reinyectan los datos: sellan last_update_ms con el reloj
     * real, y sin eso el enlace "caduca" a los 5 s y todas las capturas salen
     * con los datos apagados (ver inject_sim_data). */
    inject_sim_data();
    /* El altavoz de la alarma de bateria, tachado: el estado que hay que poder
     * enseñar. Se pone una vez; al_refresh_iconos() lo mantiene mientras la
     * alarma siga activa. */
    view_info_captura_alarma_silenciada();

    for (;;) {
        step_t step;
        const char *name;

        inject_sim_data();
        for (step = STEP_INCLINACION; step < STEP_COUNT; step++) {
            if (!lvgl_port_lock(1000)) continue;
            switch (step) {
            case STEP_INCLINACION: nav_ir_a_inclinacion(); name = "inclinacion"; break;
            case STEP_INFO:        nav_ir_a_info();        name = "info";        break;
            case STEP_REGISTRO:    nav_ir_a_registros();   name = "registro";    break;
            case STEP_AJUSTES:     nav_open_ajustes();     name = "ajustes";     break;
            default: name = "?"; break;
            }
            lvgl_port_unlock();

            /* Sin el lock tomado para que la tarea de LVGL pueda terminar
             * la animacion y el render antes de leer el framebuffer. */
            vTaskDelay(pdMS_TO_TICKS(500));

            if (lvgl_port_lock(1000)) {
                dump_screen_uart(name);
                if (step == STEP_AJUSTES) nav_close_ajustes();
                lvgl_port_unlock();
            }

            if (step == STEP_REGISTRO) capture_registro_todo();
        }
    }
}

void capture_carousel_start(void)
{
#if CAPTURE_PEAJE_DIAG
    lv_timer_t *t = lv_timer_create(pernocta_diag_cb, 6000, NULL);
    lv_timer_set_repeat_count(t, 1);
    return;
#endif
    /* 6 KB y no 8: con 8192 el xTaskCreate DEVOLVIA FALLO (visto el 14-sep-2026
     * con una traza) y el carrusel no llegaba a existir nunca -- no salia
     * ninguna captura y NO habia ni un error en el log, que es la peor forma de
     * fallar. La tarea hace un memcpy del framebuffer a base64 y unos fprintfs;
     * no necesita 8 KB. Se comprueba el resultado por si acaso. */
    if (xTaskCreate(carousel_task, "capture_carousel", 6144, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "no puedo crear la tarea del carrusel: la captura NO va a salir");
    }
}

#else


#if CAPTURE_PEAJE_DIAG
/* DIAGNOSTICO: recorre TODOS los formularios, uno cada 3 s, y vuelca el arbol de
 * cada uno al log. Con esto se mide la geometria de todas las pantallas de una
 * pasada, sin tocar la pantalla y sin adivinar.
 *
 * El volcado va DENTRO del mismo tic del temporizador, 800 ms DESPUES de mostrar
 * el formulario: antes de eso LVGL no ha calculado el layout y las medidas
 * salen falsas (dio varias tandas de numeros enganosos). */
#include "lvgl.h"
#include "ui/nav.h"
#include "ui/view_registro.h"
static int s_diag_form = 0;

static void pernocta_diag_cb(lv_timer_t *t)
{
    (void)t;
    /* Primero se MIDE el formulario que quedo abierto en el tic anterior (su
     * layout ya esta calculado), y despues se abre el siguiente. */
    if (s_diag_form > 0 && s_diag_form <= view_registro_num_formularios()) {
        view_registro_diag_arbol(s_diag_form - 1);
    }
    if (s_diag_form == 0) nav_ir_a_registros();
    if (s_diag_form < view_registro_num_formularios()) {
        view_registro_mostrar_formulario(s_diag_form);
        s_diag_form++;
    }
}
#endif

void capture_carousel_start(void)
{
#if CAPTURE_PEAJE_DIAG
    /* Cada 3 s: tiempo de sobra para que el layout este hecho y para leer el
     * volcado por el puerto sin mezclarlo con el siguiente. */
    lv_timer_create(pernocta_diag_cb, 3000, NULL);
    return;
#elif CAPTURE_CAROUSEL_SOLO_DATOS
    inject_sim_data();
    view_info_captura_alarma_silenciada();
    ESP_LOGW(TAG, "datos de ejemplo inyectados (modo banco, sin carrusel)");
#endif
}

#endif
