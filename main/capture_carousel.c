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
static int s_diag_paso = 0;
#define DIAG_PANTALLAS 9    /* PAN_COUNT */
#define DIAG_FORMULARIOS 9  /* CAT_COUNT */

/* Los volcados de las dos pantallas principales del carrusel (info e
 * inclinacion), declarados donde se usan. */
void view_info_diag_arbol(void);
void view_inclinacion_diag_arbol(void);

/* Arranca el carrusel de subpantallas desde la tarea de LVGL (ver arriba). */
static void subpantallas_arranque_cb(lv_timer_t *t)
{
    (void)t;
    nav_subpantallas_arrancar();
}

/* Y ademas, cada 3 s, vuelca la geometria de la subpantalla que se este viendo:
 * asi cada foto del usuario se puede contrastar con los numeros. */
static void subpantallas_medir_cb(lv_timer_t *t)
{
    (void)t;
    const int cat = nav_subpantallas_categoria();
    if (cat >= 0) view_registro_diag_arbol(cat);
}

static void pernocta_diag_cb(lv_timer_t *t)
{
    (void)t;
    /* Se MIDE lo que quedo abierto en el tic anterior (su layout ya esta
     * calculado) y despues se abre lo siguiente. Primero las pantallas de menu
     * y luego los formularios, para tener la geometria de todo de una pasada. */
    /* Primero las tres pantallas principales del carrusel, que son las que mas
     * se usan y las que no se habian medido nunca. */
    if (s_diag_paso == 1) { view_info_diag_arbol(); }
    if (s_diag_paso == 2) { view_inclinacion_diag_arbol(); }

    if (s_diag_paso > 0 && s_diag_paso <= DIAG_PANTALLAS) {
        view_registro_diag_pantalla(s_diag_paso - 1);
    } else if (s_diag_paso > DIAG_PANTALLAS &&
               s_diag_paso <= DIAG_PANTALLAS + DIAG_FORMULARIOS) {
        view_registro_diag_arbol(s_diag_paso - DIAG_PANTALLAS - 1);
    }

    if (s_diag_paso == 0) nav_ir_a_info();
    if (s_diag_paso == 1) nav_ir_a_inclinacion();
    if (s_diag_paso == 2) nav_ir_a_registros();
    if (s_diag_paso < DIAG_PANTALLAS) {
        view_registro_mostrar_pantalla(s_diag_paso);
    } else if (s_diag_paso < DIAG_PANTALLAS + DIAG_FORMULARIOS) {
        view_registro_mostrar_formulario(s_diag_paso - DIAG_PANTALLAS);
    }
    s_diag_paso++;
}
#endif

#if PRUEBA_BRILLO
/* ── PRUEBA DE LA RETROILUMINACION (9-oct-2026) ──────────────────────────────
 *
 * POR QUE EXISTE: el usuario dice que el cambio de brillo "es o encendido o
 * apagado" -- al 50 % la pantalla se queda NEGRA, no tenue. Y no es la primera
 * vez: el 8-oct, al 30 %, "la pantalla esta negra". O sea que en esta placa el
 * PWM de la retroiluminacion no parece graduar la luz, solo encender y apagar.
 *
 * Hasta ahora eso se habia ido resolviendo a ojo, subiendo el nivel bajo (30 ->
 * 60 -> 50) y esperando que sonara la flauta. Esta prueba lo MIDE: pone una
 * pantalla negra con el valor en grande, y va bajando el duty paso a paso. El
 * usuario solo tiene que mirar y decir en cual se apaga.
 *
 * Se prueban ademas TRES FRECUENCIAS, porque es la sospecha principal: si el
 * driver de la retroiluminacion es un elevador con arranque lento, a 4 kHz no le
 * da tiempo a arrancar en cada ciclo y solo se queda encendido con el duty al
 * 100 %; a 200 Hz cada ciclo dura 5 ms y si que le daria tiempo. Si a 200 Hz el
 * 60 % se ve, la solucion es bajar la frecuencia (display.h), no subir el duty.
 *
 * Se lanza desde un temporizador porque esta funcion se llama con el cerrojo de
 * LVGL tomado (ver el resto del fichero). */
#include "esp_bsp.h"      /* bsp_display_brightness_set() */
#include "display.h"      /* LCD_BL_LEDC_TIMER / _FREQ_HZ */
#include "brillo.h"       /* BRILLO_ALTO */
#include "driver/ledc.h"  /* ledc_set_freq(): la prueba de frecuencias */

typedef struct { uint32_t hz; int pct; } pb_paso_t;

static const pb_paso_t s_pb[] = {
    {4000, 100}, {4000, 95}, {4000, 90}, {4000, 85}, {4000, 80},
    {4000,  70}, {4000, 60}, {4000, 50},
    {1000, 100}, {1000, 80}, {1000, 60}, {1000, 40},
    { 200, 100}, { 200, 80}, { 200, 60}, { 200, 40},
};
static int       s_pb_i;
static lv_obj_t *s_pb_lbl;

static void pb_tick_cb(lv_timer_t *t)
{
    const int n = (int)(sizeof(s_pb) / sizeof(s_pb[0]));
    if (s_pb_i >= n) {
        /* Se acaba: se deja todo como estaba. */
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LCD_BL_LEDC_TIMER, LCD_BL_LEDC_FREQ_HZ);
        bsp_display_brightness_set(BRILLO_ALTO);
        if (s_pb_lbl) { lv_obj_del(lv_obj_get_parent(s_pb_lbl)); s_pb_lbl = NULL; }
        lv_timer_del(t);
        ESP_LOGW(TAG, "PRUEBA-BRILLO: fin, de vuelta al %d%% a %d Hz",
                 BRILLO_ALTO, LCD_BL_LEDC_FREQ_HZ);
        return;
    }
    const pb_paso_t *p = &s_pb[s_pb_i++];
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LCD_BL_LEDC_TIMER, p->hz);
    bsp_display_brightness_set(p->pct);
    if (s_pb_lbl) {
        lv_label_set_text_fmt(s_pb_lbl, "%u Hz\n%d %%", (unsigned)p->hz, p->pct);
    }
    ESP_LOGW(TAG, "PRUEBA-BRILLO %d/%d: %u Hz  %d%%", s_pb_i, n,
             (unsigned)p->hz, p->pct);
}

static void pb_arranque_cb(lv_timer_t *t)
{
    (void)t;
    lv_obj_t *bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    s_pb_lbl = lv_label_create(bg);
    lv_obj_set_style_text_color(s_pb_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_pb_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(s_pb_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_pb_lbl, "empieza");
    lv_obj_center(s_pb_lbl);
    lv_timer_create(pb_tick_cb, 2500, NULL);
    ESP_LOGW(TAG, "PRUEBA-BRILLO: arranca (%d pasos de 2,5 s)",
             (int)(sizeof(s_pb) / sizeof(s_pb[0])));
}
#endif

void capture_carousel_start(void)
{
#ifdef DIAG_MEDIR_TODO
    /* (recorrido de medir, apagado) */
#endif
#if PRUEBA_BRILLO
    /* La prueba del brillo manda sobre todo lo demas: si esta encendida, es lo
     * unico que se hace. */
    lv_timer_t *tp = lv_timer_create(pb_arranque_cb, 2500, NULL);
    lv_timer_set_repeat_count(tp, 1);
    return;
#elif CAPTURE_PEAJE_DIAG
    /* PASEO DE MEDIDA (9-oct-2026): recorre los nueve menus y los nueve
     * formularios uno a uno y vuelca el arbol de cada uno al log. Es lo que
     * permite comprobar la geometria de TODAS las pantallas en una sola pasada,
     * sin tocar el tactil.
     *
     * SE LANZA DESDE UN TEMPORIZADOR, NO AQUI DIRECTAMENTE, y es importante:
     * esta funcion se llama con el CERROJO DE LVGL TOMADO (ver main_app.c), y
     * los cambios de pantalla lo vuelven a pedir -> deadlock, la tarea main se
     * queda girando y salta el watchdog de tareas cada 5 s (visto en el
     * arranque). Con el temporizador, todo lo hace la tarea de LVGL, que es
     * quien tiene que hacerlo.
     *
     * 1200 ms por paso y 19 pasos (9 menus + 1 de arranque + 9 formularios). */
    lv_timer_t *t = lv_timer_create(pernocta_diag_cb, 1200, NULL);
    lv_timer_set_repeat_count(t, DIAG_PANTALLAS + DIAG_FORMULARIOS + 1);
    return;
#elif CAPTURE_CAROUSEL_SOLO_DATOS
    inject_sim_data();
    view_info_captura_alarma_silenciada();
    ESP_LOGW(TAG, "datos de ejemplo inyectados (modo banco, sin carrusel)");
#endif
}

#endif
