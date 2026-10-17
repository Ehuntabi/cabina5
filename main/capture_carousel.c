#include "capture_carousel.h"
#include "ui/ui_style.h"   /* la paleta, en un solo sitio */
/* LOS ALIAS DE FUENTE, y esto es un arreglo de verdad (auditoria del
 * 16-oct-2026): este fichero NO incluye ui/estilos.h -- por donde el resto de
 * las vistas reciben fonts/fonts_es.h --, asi que su &lv_font_montserrat_48 no
 * era el alias de la Inter 48 sino la MONTSERRAT 48 DEL SDK, que sigue
 * compilada. El texto de la PRUEBA-BRILLO salia con otra tipografia y otro
 * tamaño, y nada avisaba: es el mismo fallo del "texto pequeñisimo" del
 * 16-oct, aqui en el modo captura. */
#include "fonts/fonts_es.h"

/* Estos van aqui arriba y no dentro del bloque del carrusel: la inyeccion de
 * datos de ejemplo (modo banco) se compila tambien con el carrusel apagado. */
#include <stdbool.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"   /* heap_caps_free: el frame copiado es nuestro */
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
    /* LOS INDICADORES DE "HAY DATO", que no se rellenaban (17-oct-2026): la
     * UI no pinta el numero si `*_valido` es falso, asi que la captura de la
     * pantalla de Datos de la 5" salia con la bateria en "--" aunque el valor
     * estuviera puesto. En la 3,5" no se noto porque su modelo no tiene esos
     * indicadores. Se marcan los tres del shunt; el resto (frigo, exterior,
     * aguas) ya lleva su `*_has_data` mas abajo. */
    d.soc_valido           = true;
    d.v_valido             = true;
    d.i_valido             = true;
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
    /* Alarma de bateria activa (la palabra "batería baja" es la de la P4). El
     * bit tiene que ser el de mini_proto.h: es el mismo byte que viaja. */
    d.alarmas              = MINI_ALARM_BATERIA;
    d.last_update_ms       = (uint32_t)(esp_timer_get_time() / 1000);
    data_model_set_simulated(&d);
}
#endif

#if CAPTURE_CAROUSEL_ENABLE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>   /* va_list: la guardia de log del volcado */
#include <unistd.h>   /* write(): el volcado va por el descriptor, sin traducir */
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

/* EL LOG SE TIRA MIENTRAS DURA EL VOLCADO (17-oct-2026).
 *
 * Bajar el nivel a ERROR no basta: hay tags con nivel propio (el WiFi, los
 * componentes) que se saltan el comodin "*", y CUALQUIER linea que salga en
 * medio del volcado desplaza los pixeles que vienen detras: la imagen sale
 * cortada por la mitad, con el trozo de abajo corrido. No se detecta mirando
 * el tamano (los bytes son los que son), solo al abrir el PNG.
 *
 * Solucion: se sustituye la salida del log por la de abajo, que no escribe
 * nada mientras `s_volcando` esta puesto. El volcado (printf/fwrite) no pasa
 * por aqui, asi que sale entero. */
static volatile bool s_volcando;

static int log_sin_volcado(const char *fmt, va_list ap)
{
    if (s_volcando) return 0;
    return vprintf(fmt, ap);
}

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
 * El frame sale de lv_port_snapshot() (que REDIBUJA el arbol de objetos en su
 * propio buffer, ver lv_port_compat.c) y no del draw_buf de LVGL (3.M5):
 * draw_buf->buf_act esta sin rotar y no es de fiar en el momento de la captura.
 * DOS COSAS QUE SE DIJERON MAL Y SE CORRIGIERON EL 16-oct-2026:
 *   1. Esto NO es "la copia rotada que el flush manda al panel". lv_snapshot_take()
 *      redibuja en coordenadas de UI y SIN girar, asi que si algun dia se vuelve
 *      a poner PANTALLA_GIRADA_180 a 1, este volcado y el de
 *      bsp_volcar_framebuffer (que si lee el panel) saldran con 180 grados de
 *      diferencia y con la MISMA cabecera, que es lo que despista.
 *   2. El tamano es el del panel logico en RGB565: 2 BYTES por pixel, NO
 *      sizeof(lv_color_t). En LVGL 9 lv_color_t son 3 bytes siempre (RGB888
 *      empaquetado, lv_color.h), asi que declarar w*h*sizeof(lv_color_t) pedia
 *      1.152.000 B para un buffer de 768.000 B: el fwrite se leia 384 KB de
 *      PSRAM que no eran suyos (y salian las rayas que se achacaban al formato). */
static void dump_screen_uart(const char *name)
{
    /* LOS DATOS DE EJEMPLO CADUCAN A LOS 5 s y un volcado a 115200 tarda 66 s
     * por pantalla: sin reinyectarlos antes de cada copia, de la segunda
     * pantalla en adelante todo saldria en gris ("--"). Y se deja repintar
     * (sin el cerrojo) antes de copiar, o la copia sale con la cifra vieja:
     * los objetos ya tienen los numeros nuevos pero el panel aun no. */
    inject_sim_data();
    vTaskDelay(pdMS_TO_TICKS(200));

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
    /* 2 B/px, que es lo que mide de verdad el buffer de lv_port_snapshot(). */
    size_t raw_len = (size_t)hres * vres * 2;
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

    s_volcando = true;
    /* OJO: POR EL PUERTO NO SE ESCRIBE CON printf/fwrite (17-oct-2026). La
     * consola de ESP-IDF traduce los saltos de linea al escribir en stdout
     * (CONFIG_NEWLIB_STDOUT_LINE_ENDING_CRLF): cada byte 0x0A de la imagen
     * salia como 0x0D 0x0A y el volcado se corria dos bytes por cada uno. Se
     * veia como imagen "partida por la mitad" y costaba verlo porque el
     * tamano cuadraba: los bytes que llegan son otros, no faltan.
     * Escribiendo con write() al descriptor 1, los bytes salen tal cual. */
    dprintf(STDOUT_FILENO, "===BIN:%s:%ux%u:%u===\n", name, (unsigned)hres,
            (unsigned)vres, (unsigned)raw_len);

    /* TROCEADO CON INDICE Y SUMA (17-oct-2026). Antes iba todo del tiron y
     * CUALQUIER byte que se colara en medio -- y se colaba: el watchdog, un
     * aviso de otra tarea -- corria los pixeles y la imagen salia partida por
     * la mitad sin que el tamano lo delatara. Con esta cabecera por trozo el
     * receptor sabe donde va cada uno y comprueba que llega entero:
     *
     *   #<indice>:<bytes>:<suma>
<bytes en crudo>
     *
     * Si un trozo no cuadra, se tira ESE trozo y la imagen sale con una banda
     * mal, no partida. Y lo que escriba otro entre trozo y trozo se descarta
     * buscando el siguiente '#'. */
    const size_t TROZO = 1024;
    for (size_t i = 0; i < raw_len; i += TROZO) {
        size_t n = (raw_len - i < TROZO) ? (raw_len - i) : TROZO;
        uint16_t suma = 0;
        for (size_t k = 0; k < n; k++) suma = (uint16_t)(suma + fb[i + k]);
        dprintf(STDOUT_FILENO, "#%u:%u:%04x\n", (unsigned)(i / TROZO),
                (unsigned)n, suma);
        /* write() puede escribir MENOS de lo pedido (la UART es un
         * periferico: si el buffer de salida se llena, devuelve lo que pudo).
         * Antes se ignoraba el valor devuelto y se perdia el resto del trozo
         * sin avisar: la suma no cuadraba en el receptor y la imagen salia con
         * una banda. Hay que repetir hasta que salga todo. */
        size_t puesto = 0;
        while (puesto < n) {
            ssize_t k = write(STDOUT_FILENO, fb + i + puesto, n - puesto);
            if (k <= 0) break;
            puesto += (size_t)k;
        }
        /* ~180 ms por trozo: se cede la CPU, que es lo que deja correr a las
         * tareas de sistema mientras dura el volcado.
         *
         * Y EL WDT SE ALIMENTA SOLO SI ESTA TAREA ESTA SUSCRITA (17-oct-2026):
         * llamarlo sin estarlo NO es inocuo -- esp_task_wdt_reset() escribe
         * "E task_wdt: esp_task_wdt_reset(707): task not found" por CADA
         * llamada, o sea 375 lineas de 62 bytes dentro del volcado, y el
         * receptor, que espera 768.000 bytes exactos, se queda desalineado y
         * tira la imagen entera. Fue lo que tumbo la tanda de capturas del
         * 17-oct: el volcado parecia corrompido y era el propio WDT metiendo
         * texto en medio. */
        if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
        vTaskDelay(1);
    }
    write(STDOUT_FILENO, "\n===FIN===\n", 11);
    s_volcando = false;
    esp_log_level_set("*", ESP_LOG_INFO);

    /* lv_port_snapshot() devuelve una COPIA en PSRAM (768.000 B por pantalla en
     * la 5") y aqui no se liberaba: a la decima captura se acababa la PSRAM,
     * lv_port_snapshot devolvia false y el carrusel se quedaba sin volcar nada
     * ("sin frame flasheado"), sin que se viera el motivo. */
    heap_caps_free(pix);
}

/* Muestra una pantalla/formulario de registro ya creado (via el "mostrar"
 * que se pase) y lo captura. Settle de 900ms sin el lock: con 500ms se vio
 * algun resto visual de la pantalla anterior colandose en el borde (LVGL
 * necesita soltarse para pintar antes de leer el framebuffer, y a veces no
 * le bastaba). Detectado en la primera tanda de capturas, 08-sep-2026.
 *
 * EL VOLCADO VA FUERA DEL CERROJO (17-oct-2026): a 115200 una pantalla de
 * 800x480 tarda 66 s, y tener el cerrojo de LVGL tomado todo ese rato deja a
 * la tarea de LVGL sin poder pintar; el Task WDT (10 s) reinicia la placa a
 * mitad del volcado y lo que se ve en el panel es el splash una y otra vez.
 * Pasado el 17-oct en la 5"; se diagnostico porque la placa se quedaba
 * "en el splash, temblando". */
static void capture_registro_paso(void (*mostrar)(int), int idx, const char *prefijo)
{
    if (lvgl_port_lock(1000)) {
        mostrar(idx);
        lvgl_port_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(900));
    char nombre[48];
    const char *n = (mostrar == view_registro_mostrar_pantalla)
                   ? view_registro_nombre_pantalla(idx)
                   : view_registro_nombre_formulario(idx);
    snprintf(nombre, sizeof(nombre), "%s_%s", prefijo, n);
    /* dump_screen_uart() coge el cerrojo solo lo justo para copiar el frame. */
    dump_screen_uart(nombre);
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
        for (step = (step_t)0; step < STEP_COUNT; step++) {
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

            /* El volcado NO con el cerrojo tomado: tarda ~66 s a 115200 y la
             * tarea de LVGL tiene que seguir pintando (si no, el Task WDT
             * reinicia la placa a mitad). dump_screen_uart() coge el cerrojo
             * solo para copiar el frame. */
            dump_screen_uart(name);
            /* Cerrar el overlay DESPUES de volcarlo: si se cierra antes, lo
             * que se captura es la pantalla de debajo (salio la de Datos en
             * la tanda del 17-oct). */
            if (step == STEP_AJUSTES) {
                if (lvgl_port_lock(1000)) { nav_close_ajustes(); lvgl_port_unlock(); }
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
    esp_log_set_vprintf(log_sin_volcado);

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
    lv_obj_set_style_bg_color(bg, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    s_pb_lbl = lv_label_create(bg);
    lv_obj_set_style_text_color(s_pb_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_pb_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(s_pb_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_pb_lbl, "empieza");
    lv_obj_center(s_pb_lbl);
    lv_timer_create(pb_tick_cb, 2500, NULL);
    ESP_LOGW(TAG, "PRUEBA-BRILLO: arranca (%d pasos de 2,5 s)",
             (int)(sizeof(s_pb) / sizeof(s_pb[0])));
}
#endif

#if CAPTURA_PANTALLA
/* ── FOTO DE LA PANTALLA (9-oct-2026) ────────────────────────────────────────
 *
 * POR QUE EXISTE: el usuario lleva tres avisos seguidos de cosas que se VEN
 * ("el contenido desaprovecha espacio por debajo", "en aguas el 2/4, 3/4 y 4/4
 * se recortan", "hay que reordenarla") y yo no he visto esa pantalla nunca. Con
 * el arbol de widgets se mide la geometria, pero no se ve el resultado; y
 * yendo a ciegas se tarda el doble en cada arreglo.
 *
 * El volcado lo hace bsp_volcar_framebuffer() (ver esp_bsp.c): lee el
 * framebuffer DEL PANEL, que es exactamente lo que se ve, y lo manda en crudo.
 * En el PC se convierte a PNG con tools/decodifica_capturas.py.
 *
 * POR QUE CADA 20 s Y NO EN BUCLE: durante el volcado (~17 s) la tarea de LVGL
 * esta ocupada y la pantalla no responde. Entre volcado y volcado da tiempo a
 * navegar a otra pantalla, asi que una sola grabacion sirve para varias. La
 * primera sale a los 8 s, ya sin splash. */
#include "esp_bsp.h"     /* bsp_volcar_framebuffer() */
#include "esp_log.h"

static void captura_volcar_cb(lv_timer_t *t)
{
    (void)t;
    bsp_volcar_framebuffer("pantalla", 2);
}
#endif

void capture_carousel_start(void)
{
#ifdef DIAG_MEDIR_TODO
    /* (recorrido de medir, apagado) */
#endif
#if CAPTURA_PANTALLA
    /* Fotos de la pantalla: la primera a los 8 s (ya sin splash) y luego cada
     * 20 s, que es lo que tarda el volcado mas un respiro para navegar. */
    lv_timer_t *tcap = lv_timer_create(captura_volcar_cb, 8000, NULL);
    lv_timer_set_repeat_count(tcap, 1);
    lv_timer_create(captura_volcar_cb, 20000, NULL);
    return;
#elif PRUEBA_BRILLO
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
