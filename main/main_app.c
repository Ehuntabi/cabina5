/* main.c
 *
 * 35cabina — satelite tactil de la P4 (ver README.md). Bring-up de
 * hardware (pantalla+tactil+LVGL), recepcion UDP del broadcast de la P4,
 * el carrusel de 3 pantallas (nav.c) y el canal de vuelta hacia la P4
 * (viajes y registros sueltos, main/net/p4_api.c + viaje_cola.c).
 */
#include <stdio.h>
#include <inttypes.h>
#include <lvgl.h>
#include "display.h"
#include "esp_bsp.h"
#include "brillo.h"
#include "lv_port_compat.h"
#include "data_model.h"
#include "tilt.h"
#include "salida.h"
#include "reloj.h"   /* reloj_ahora(): la reconciliacion no decide sin hora */
#include "ui/confirm_screen.h"   /* aviso de apuntes perdidos */
#include "config_storage.h"   /* load_trip_activo/destino: reconciliacion M5 */
#include "diag_reset.h"
#include "ui/nav.h"
#include "net/udp_rx.h"
#include "net/viaje_cola.h"
#include "net/p4_test.h"   /* auto-test del canal de subida a la P4 */
#include "ui/view_info.h"
#include "capture_carousel.h"   /* modo captura de pantallas por USB */
#include "esp_log.h"
#include "esp_flash.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_debug_helpers.h"   /* esp_backtrace_print: traza de la UI colgada */
#include "icons/splash_logo_5.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "35CABINA";
#define logSection(section) ESP_LOGI(TAG, "\n\n***** %s *****\n", section)
#define LVGL_PORT_ROTATION_DEGREE 90

/* Splash: contenedor negro opaco en el top layer (por encima de cualquier
 * pantalla del carrusel) + el nombre, se autodestruye a los SPLASH_MS.
 *
 * OJO: antes centraba el logo de la pantalla de 3,5" (splash_logo_3_5). Ese
 * logo es de 320x480 y en la pantalla de 5" (800x480) saldria pequeno y
 * descentrado, asi que aqui va un texto. Si se quiere un logo, hay que generar
 * uno para 800x480. */
#define SPLASH_MS 2000

static void splash_done_cb(lv_timer_t *t) {
    /* En LVGL 9 lv_timer_t es OPACO: el user_data se lee con su accesorio. */
    lv_obj_t *splash_bg = (lv_obj_t *)lv_timer_get_user_data(t);
    ESP_LOGI("SPLASH", "quito el splash (%s)", splash_bg ? "hay objeto" : "SIN OBJETO");
    lv_obj_del(splash_bg);

    /* DIAGNOSTICO: quien queda en pantalla y si tiene contenido. */
    lv_obj_t *act = lv_screen_active();
    if (act) {
        lv_obj_t *hijo = lv_obj_get_child(act, 0);
        ESP_LOGW("SPLASH", "pantalla activa %p: %u hijos, primera %p (%dx%d) tam %dx%d",
                 (void *)act, (unsigned)lv_obj_get_child_count(act), (void *)hijo,
                 hijo ? (int)lv_obj_get_width(hijo) : 0,
                 hijo ? (int)lv_obj_get_height(hijo) : 0,
                 (int)lv_obj_get_width(act), (int)lv_obj_get_height(act));
    } else {
        ESP_LOGE("SPLASH", "NO hay pantalla activa");
    }
}

static void splash_create(void) {
    lv_disp_t *disp = lv_disp_get_default();
    lv_coord_t hor = lv_disp_get_hor_res(disp);
    lv_coord_t ver = lv_disp_get_ver_res(disp);

    lv_obj_t *splash_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(splash_bg, 0, 0);
    lv_obj_set_size(splash_bg, hor, ver);
    lv_obj_set_style_bg_color(splash_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(splash_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(splash_bg, 0, 0);
    lv_obj_set_style_radius(splash_bg, 0, 0);
    lv_obj_set_style_pad_all(splash_bg, 0, 0);
    lv_obj_clear_flag(splash_bg, LV_OBJ_FLAG_SCROLLABLE);

    /* LA AUTOCARAVANA, no un rotulo: es el mismo splash que llevaba el satelite
     * de 3,5" (alli en 480x320). El 8-oct-2026 se recupero la imagen del
     * volcado de pixeles del proyecto viejo y se rehizo para esta pantalla
     * (ver main/icons/splash_logo_5.c). Aqui no se escala nada: se dibuja a su
     * tamano (665x394) porque se genero ya a esa medida, y escalar en tiempo
     * real costaria un repintado por pixel en cada arranque. */
    lv_obj_t *logo = lv_image_create(splash_bg);
    lv_image_set_src(logo, &splash_logo_5);
    /* SOLO la imagen: ni rotulo ni version (quitados el 8-oct-2026, a peticion
     * del usuario: "el texto del splash es horrible"). La version se mira en
     * Ajustes, que es donde toca. Centrada sin mas. */
    lv_obj_center(logo);

    lv_timer_t *t = lv_timer_create(splash_done_cb, SPLASH_MS, splash_bg);
    lv_timer_set_repeat_count(t, 1);
    ESP_LOGI("SPLASH", "puesto el splash (%d ms), temporizador %s",
             SPLASH_MS, t ? "creado" : "NO CREADO");
}

/* Heartbeat: diagnostico cada 30s (uptime, heap, PSRAM, contador de vida
 * de LVGL). La recuperacion ante cuelgue la hace lvgl_wdog_task. */
static void heartbeat_task(void *arg) {
    (void)arg;
    while (1) {
        ESP_LOGI("HB", "uptime=%llus free_heap=%u min=%u free_psram=%u lvgl=%u",
                 esp_timer_get_time() / 1000000ULL,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)esp_get_minimum_free_heap_size(),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)lvgl_port_get_loop_count());
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/* Supervisor anti-cuelgue respaldado por el Task WDT hardware — ver
 * comentario original en victronsolardisplayesp-multi-device_pantalla_3.5,
 * reutilizado tal cual (bring-up de estabilidad, no logica Victron). */
#define LVGL_WDOG_PERIOD_MS 2000
#define LVGL_WDOG_GRACE_MS  60000
static void lvgl_wdog_task(void *arg) {
    (void)arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    uint32_t prev = lvgl_port_get_loop_count();
    uint32_t stalled_ms = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(LVGL_WDOG_PERIOD_MS));

        uint32_t now = lvgl_port_get_loop_count();
        if (now != prev) {
            prev = now;
            stalled_ms = 0;
        } else {
            stalled_ms += LVGL_WDOG_PERIOD_MS;
        }

        if (stalled_ms < LVGL_WDOG_GRACE_MS) {
            esp_task_wdt_reset();
        } else {
            /* Dejamos el motivo apuntado ANTES de que salte el Task WDT: si no,
             * el arranque siguiente solo sabria decir "watchdog de tarea" y no
             * que fue la UI la que se quedo parada. Una sola vez (la marca se
             * consume al leerla en el arranque) para no escribir la flash en
             * cada vuelta del bucle mientras esta colgada. */
            static bool motivo_marcado = false;
            if (!motivo_marcado) {
                diag_reset_marcar_sw("UI colgada");
                motivo_marcado = true;
            }
            ESP_LOGE("WDOG", "UI sin avanzar %ums; dejando saltar el Task WDT",
                     (unsigned)stalled_ms);
            /* Traza de donde esta atascada la tarea de LVGL. Sin esto, un
             * cuelgue de la UI (bucle infinito, sin panic ni watchdog) no deja
             * rastro: los contadores siguen, el panel barre y no hay ni una
             * linea que decir DONDE se ha quedado. Decodificar con
             * xtensa-esp32s3-elf-addr2line -pfiaC -e build/cabina5.elf <dirs> */
            esp_backtrace_print(40);
        }
    }
}

void setup(void);

/* ── RECONCILIACION DEL ESTADO DEL VIAJE AL ARRANCAR (M5, 13-oct-2026) ───────
 *
 * El estado del viaje vive en DOS blobs de NVS que se comiten por separado: el
 * del viaje (activo/destino/n_eventos, en config_storage) y el de la salida (en
 * salida.c). Y al empezar y al terminar se tocan en DOS sitios:
 *
 *   empezar:  save_trip_inicio()        + salida_abrir_viaje()
 *   terminar: viaje_marcar_terminado()  + salida_cerrar()
 *
 * Un corte de corriente entre los dos commits (el contacto se corta a menudo
 * con la pantalla en marcha: es su forma normal de apagarse) deja a la cabina
 * creyendo una cosa y a la P4 otra. Hasta hoy eso se descubria por el 409 al
 * intentar empezar otro viaje, y no habia forma de saber que habia pasado.
 *
 * Lo que DESEMPATA es la cola de pendientes: si el cierre del viaje ya se
 * encolo, es que se llego a pulsar "terminar" -- el fin se entregara solo y la
 * P4 cerrara el viaje; si NO hay fin encolado, el corte fue al empezar.
 *
 * Se hace una sola vez, al arrancar, y no borra nada: o abre la salida que
 * falta (con el destino guardado) o la cierra, segun lo que diga la cola. */
/* true si en ESTE arranque hubo que borrar la NVS (corrupta o de otra version):
 * se perdio lo guardado y hay que decirlo en pantalla, no solo en el log. */
static bool s_nvs_borrada_al_arrancar;

/* ── AVISO DE LO QUE SE PERDIO AL ARRANCAR (B2, 13-oct-2026) ─────────────────
 *
 * Hay dos caminos por los que esta pantalla puede perder apuntes que el usuario
 * VIO guardados, y los dos eran mudos (solo quedaba una linea en el log):
 *   - la NVS estaba corrupta o era de otra version y hubo que borrarla entera
 *     (se lleva la cola de apuntes, el estado de la salida y el del viaje);
 *   - la cola se reinicio al arrancar porque los indices no cuadraban.
 * Ahora se avisa UNA vez, en pantalla, en cuanto la UI esta lista. Que se
 * entere quien lo puede arreglar (volver a apuntar lo que falte) y no un log
 * que nadie lee.
 *
 * Se hace con lv_timer de un solo uso: la UI ya esta construida (nav_init crea
 * las tres pantallas y con ellas el dialogo de confirmacion) y el splash ya se
 * ha ido. */
/* Reintento de la reconciliacion hasta que haya hora (ver su comentario). */
/* NACE A TRUE: si el primer intento no llega ni a correr (sin cerrojo de LVGL,
 * o sin hora todavia), el temporizador tiene que seguir intentandolo. Antes
 * nacia a false y solo se armaba dentro de la reconciliacion, asi que si el
 * primer cerrojo fallaba nadie lo armaba y el temporizador se autoborraba en su
 * primer disparo: M5 no corria NUNCA (lo cazo la tercera revision adversarial
 * del 13-oct-2026). */
static bool s_recon_pendiente = true;
static void reconciliar_viaje_al_arrancar(void);

static void recon_timer_cb(lv_timer_t *t)
{
    if (!s_recon_pendiente) { lv_timer_del(t); return; }
    reconciliar_viaje_al_arrancar();
    if (!s_recon_pendiente) lv_timer_del(t);
}

static void aviso_perdidas_cb(lv_timer_t *t)
{
    lv_timer_del(t);
    int cola = viaje_cola_perdidos_al_arrancar();
    if (!s_nvs_borrada_al_arrancar && cola <= 0) return;   /* no se perdio nada */

    char cuerpo[320];
    if (s_nvs_borrada_al_arrancar) {
        snprintf(cuerpo, sizeof(cuerpo),
                 "La memoria del aparato estaba\n"
                 "estropeada o era de otra version y\n"
                 "ha habido que borrarla.\n\n"
                 "Se han perdido los apuntes sin\n"
                 "enviar%s. Mira en Ajustes y vuelve\n"
                 "a apuntar lo que falte.",
                 cola > 0 ? " (y los de la cola)" : "");
    } else {
        snprintf(cuerpo, sizeof(cuerpo),
                 "Se han perdido %d apunte(s) sin\n"
                 "enviar: la cola de pendientes no\n"
                 "cuadraba al arrancar.\n\n"
                 "Vuelve a apuntar lo que falte.",
                 cola);
    }
    confirm_screen_aviso("SE HAN PERDIDO APUNTES", cuerpo, 0xFF4444, "Entendido");
    ESP_LOGW(TAG, "aviso de perdidas mostrado (nvs_borrada=%d, cola=%d)",
             (int)s_nvs_borrada_al_arrancar, cola);
}

static void reconciliar_viaje_al_arrancar(void)
{
    /* SI EL ESTADO NO SE PUEDE LEER, NO SE TOCA NADA (auditoria del 14-oct-2026):
     * 'load_trip_activo()' devolvia false con el blob ilegible, que es
     * indistinguible de "aqui no hay viaje", y la rama de abajo volvia a marcar
     * el inicio con el contador a 1 -- un viaje de verdad declarado completo.
     * Se reintenta (la NVS puede fallar un momento), con el aviso una sola vez
     * para no llenar el log cada 3 s. */
    int act = load_trip_activo_estado();
    if (act < 0) {
        static bool avisado = false;
        if (!avisado) {
            avisado = true;
            ESP_LOGE(TAG, "no puedo leer el estado del viaje: no toco nada (ni "
                          "abro ni cierro) y lo sigo intentando");
        }
        s_recon_pendiente = true;
        return;
    }
    bool activo     = (act == 1);
    bool hay_salida = salida_hay();

    /* EL CIERRE ENCOLADO MANDA SOBRE TODO LO DEMAS (auditoria del 14-oct-2026).
     *
     * 'viaje_do_finalizar' encola el fin y DESPUES cierra aqui. Un corte de
     * contacto entre las dos cosas (que es como se apaga esto todos los dias)
     * dejaba el viaje AUN marcado activo con la salida abierta: la P4 procesaba
     * el fin y cerraba el viaje, y esta pantalla seguia diciendo "viaje en
     * curso" de un viaje ya cerrado. Todo lo que se anotara a partir de ahi se
     * contestaba 200 y se iba al historial del vehiculo, sin resumen y sin que
     * nadie se enterase.
     *
     * Y al reves: si 'save_trip_fin()' fallo (NVS) y el corte llego antes de
     * 'salida_cerrar()', la rama de abajo REABRIA la salida de un viaje que se
     * estaba cerrando. Por eso esto va ANTES de decidir nada.
     *
     * 'viaje_cola_ultima_es_op' y no 'tiene_op_pendiente': un fin viejo de un
     * viaje anterior (al que se le perdio el 200) tambien esta en la cola, y
     * cerrar por ese cerraria el viaje NUEVO. El fin del viaje que se estaba
     * cerrando es el ultimo que se encolo; si hay algo detras, es de otro. */
    if (activo && viaje_cola_ultima_es_op("fin")) {
        ESP_LOGW(TAG, "cierre ENCOLADO y el viaje sigue abierto aqui: lo cierro "
                      "en local en vez de reabrirlo");
        save_trip_fin();
        salida_cerrar();
        s_recon_pendiente = false;
        return;
    }

    if (activo == hay_salida) { s_recon_pendiente = false; return; }

    /* SIN HORA NO SE DECIDE NADA: salida_abrir_viaje() necesita el reloj (fechan
     * y sellan la salida), asi que sin hora fallaba y el codigo caia en
     * save_trip_fin() -- cerrando el viaje en la cabina mientras la P4 lo
     * mantenia abierto, justo lo contrario de lo que esto viene a arreglar. Se
     * reintenta en la siguiente vuelta (revision del 13-oct-2026). */
    if (!reloj_ahora(NULL)) {
        /* SIN HORA SE REINTENTA: hay que ARMAR el flag, que si no el
         * temporizador lo ve a false, se autoborra en su primer disparo y la
         * reconciliacion no corre NUNCA (lo cazo la revision del 13-oct-2026: el
         * arreglo anterior ponia el flag a false en los dos caminos). */
        s_recon_pendiente = true;
        ESP_LOGW(TAG, "reconciliacion del viaje: aun no hay hora, lo intento luego");
        return;
    }
    s_recon_pendiente = false;

    if (activo && !hay_salida) {
        /* Corte AL EMPEZAR: el viaje existe (la P4 lo tiene abierto y esta NVS
         * lo dice) pero aqui no hay salida con la que anotar. Se abre con el
         * destino guardado, que es recuperar el viaje en vez de perderlo. */
        char destino[24] = {0};
        size_t n = sizeof(destino);
        if (load_trip_destino(destino, &n) != ESP_OK || !destino[0]) {
            ESP_LOGE(TAG, "viaje activo sin salida y sin destino guardado: lo "
                          "cierro aqui (la P4 lo tendra abierto: al empezar otro "
                          "dara 409 y habra que cerrarlo desde ahi)");
            save_trip_fin();
            return;
        }
        ESP_LOGW(TAG, "corte al EMPEZAR el viaje '%s': abro la salida que faltaba",
                 destino);
        if (!salida_abrir_viaje(destino)) {
            /* NO se cierra el viaje por no poder abrir la salida: cerrarlo aqui
             * dejaria a la P4 con el viaje abierto y a esta pantalla creyendo que
             * termino (el mismo fallo que esto viene a arreglar). Se deja
             * pendiente y se reintenta; si de verdad no se puede, el usuario lo
             * vera al intentar anotar algo (do_save se niega sin salida). */
            ESP_LOGE(TAG, "no he podido abrir la salida de '%s': lo dejo pendiente "
                          "y lo reintento", destino);
            s_recon_pendiente = true;
        }
        return;
    }

    /* Salida abierta y el viaje NO consta como activo. Hay dos historias
     * posibles y la cola dice cual:
     *   - hay un "fin" encolado: se pulso terminar y el corte fue antes de
     *     cerrar aqui -> se cierra la salida (el fin hara su trabajo).
     *   - no hay fin: el corte fue al empezar una PUNTUAL, entre abrir la
     *     salida y marcar el contador -> se marca, que es lo que falta. */
    if (viaje_cola_tiene_op_pendiente("fin")) {
        ESP_LOGW(TAG, "salida abierta y el viaje ya no consta activo, con el "
                      "cierre encolado: cierro la salida");
        salida_cerrar();
    } else {
        const salida_vista_t *s = salida_get();
        ESP_LOGW(TAG, "salida '%s' abierta sin el viaje marcado (corte al "
                      "empezar): la marco", s->nombre);
        if (save_trip_inicio(s->nombre) != ESP_OK)
            ESP_LOGE(TAG, "no he podido marcarla: el contador de apuntes puede "
                          "quedar corto");
    }
}


void app_main(void) {
    setup();
}

void setup(void) {
    logSection("LVGL init start");

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "This is %s chip, %d cores", CONFIG_IDF_TARGET, chip_info.cores);

    uint32_t flash_size;
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        ESP_LOGE(TAG, "Get flash size failed");
        return;
    }
    ESP_LOGI(TAG, "%" PRIu32 "MB flash, min free heap: %" PRIu32 ", free PSRAM: %u",
             flash_size / (1024 * 1024),
             esp_get_minimum_free_heap_size(),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* La memoria persistente se arranca ANTES que la pantalla porque el brillo
     * guardado se lee de ahi: si se hiciera despues habria que encender la
     * retroiluminacion a un valor cualquiera y corregirlo un instante mas
     * tarde, que de noche se ve como un fogonazo. */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* ESTO SE LLEVA POR DELANTE TODO lo que hubiera guardado: la cola de
         * apuntes sin enviar (vcola), el estado de la salida y el del viaje. No
         * hay nada que hacer -- la particion esta corrupta o es de otra version
         * --, pero NO puede pasar en silencio: se apunta para avisarlo en
         * pantalla en cuanto la UI este lista (auditoria del 13-oct-2026,
         * hallazgo B2). */
        s_nvs_borrada_al_arrancar = true;
        ESP_LOGE(TAG, "NVS corrupta o de otra version (%s): se borra ENTERA -- se "
                      "pierden los apuntes sin enviar y el estado del viaje",
                 esp_err_to_name(nvs_err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    /* Que se sepa si la pantalla se reinicio SOLA (watchdog, panic, cuelgue):
     * motivo en el log y tarjeta roja en Ajustes cuando no lo provoco el
     * contacto. Va justo despues de NVS y antes de que la UI lea la tarjeta.
     * Ver diag_reset.c. */
    diag_reset_anotar_arranque();

    logSection("Display init");
    /* En esta placa el panel YA es horizontal (800x480), asi que no se rota
     * nada: la 3,5" era vertical y por eso llevaba rotate=90.
     * El tamano de los buffers y del bounce buffer los decide el BSP (ver
     * esp_bsp.c): aqui solo se le pasa la config de la tarea de LVGL. */
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
    };
    if (!bsp_display_start_with_config(&cfg)) {
        ESP_LOGE(TAG, "la pantalla no arranco");
        return;
    }
    lv_port_contador_arrancar();   /* contador de vida para lvgl_wdog_task */
    brillo_init();   /* nivel guardado; la primera vez, el ALTO */

    /* LA RED VA ANTES QUE LA UI, y esto es a proposito (portado de la 5"):
     * esp_wifi_init() se queda con la RAM interna que necesita en bloques
     * GRANDES (buffers estaticos y su tarea) y no sabe apañarse con las sobras.
     * La UI, en cambio, es elastica: LVGL tiene su propio pool y si le falta
     * memoria se queja pero no impide arrancar. Al reves (UI primero) el
     * resultado medido es "create wifi task: failed to create task" y la placa
     * se reinicia en bucle. */
    udp_rx_start();

    if (!lvgl_port_lock(5000)) {
        ESP_LOGE(TAG, "No se pudo tomar lvgl_port_lock al iniciar UI");
        return;
    }

    /* La salida en curso y lo que quedo abierto. Va ANTES de construir la UI:
     * el menu de registros pregunta el estado nada mas crearse, y lee la marca
     * de vida antes de pisarla (ver salida.h). */
    salida_init();

    /* No es fatal si no responde: tilt_is_present() queda en false y la
     * pantalla de inclinacion lo muestra en vez de crashear. */
    tilt_init();

    data_model_init();
    nav_init();
    splash_create();
    lvgl_port_unlock();

    /* El repartidor de apuntes pendientes. Va DESPUES de udp_rx_start() porque
     * necesita la red, y arranca aunque la cola este vacia: lo normal es que
     * quede algo del encendido anterior (esta pantalla se apaga con el
     * contacto, y los apuntes se hacen justo antes). */
    viaje_cola_init(view_info_set_pendientes);

    /* M5: los dos blobs del viaje pueden haber quedado descuadrados por un
     * corte de corriente a mitad de empezar o terminar. Se arregla aqui, y si
     * todavia no hay hora (el primer paquete de la P4 tarda unos segundos) se
     * reintenta cada 3 s hasta que la haya. */
    /* La llamada directa tambien va bajo el cerrojo de LVGL: esta funcion toca
     * el estado de la salida, que la tarea de LVGL lee para pintar (el resto de
     * la reconciliacion corre en el temporizador, que ya va con cerrojo). */
    if (lvgl_port_lock(2000)) {
        reconciliar_viaje_al_arrancar();
        lvgl_port_unlock();
    }
    if (lvgl_port_lock(2000)) {
        lv_timer_t *recon = lv_timer_create(recon_timer_cb, 3000, NULL);
        if (!recon) ESP_LOGE(TAG, "no he podido crear el reintento de la reconciliacion");
        lvgl_port_unlock();
    } else {
        ESP_LOGW(TAG, "sin cerrojo de LVGL: la reconciliacion se queda sin reintento");
    }

    /* Y en cuanto la UI lleve un rato en pie, si se perdio algo se dice. */
    lv_timer_t *aviso = lv_timer_create(aviso_perdidas_cb, 12000, NULL);
    if (aviso) lv_timer_set_repeat_count(aviso, 1);

    /* Auto-test del canal de subida (cabina -> P4 por HTTP): deja en el log si
     * la ruta existe, si las credenciales del portal valen y si acepta POST.
     * No crea ningun viaje. Ver net/p4_test.c. */
    p4_test_start();

    /* NO hay reinicio programado, y es a proposito: se quito el de 12 h el
     * 10-sep-2026. Esta pantalla ya se reinicia con cada corte de contacto (es
     * su forma normal de apagarse), asi que el temporizador solo servia para
     * cortar una jornada larga en un momento cualquiera -- y para tapar
     * cualquier fuga en vez de delatarla. Un cuelgue de verdad lo cubren el
     * Task WDT (que lvgl_wdog_task deja saltar a proposito) y el watchdog de
     * tareas; y si algo se reinicia, ahora se ve: diag_reset y la tarjeta
     * "Ultimo reinicio" de Ajustes. */

    if (xTaskCreate(heartbeat_task, "hb", 3072, NULL, 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(heartbeat_task) fallo: sin diagnostico periodico");
    }
    /* lvgl_wdog_task es el respaldo anti-cuelgue de verdad (ver su comentario):
     * si esto no llega a crearse, un cuelgue de LVGL ya no se recupera solo.
     * Se comprueba el motivo CONCRETO (errno de FreeRTOS) porque "fallo" a secas
     * no dice si fue memoria, prioridad o tabla llena: paso en la unidad nueva el
     * 7-oct-2026 y hubo que adivinar. La memoria interna se mira ANTES de crear
     * la tarea, que es el dato que hace falta si el fallo es por RAM. */
    BaseType_t crea;
    crea = xTaskCreate(lvgl_wdog_task, "lvgl_wdog", 3072, NULL, 6, NULL);
    if (crea != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(lvgl_wdog) fallo (err=%d): SIN recuperacion anti-cuelgue de LVGL"
                      " (heap interno libre %u B, PSRAM %u B)",
                 (int)crea,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }

    /* Modo captura de pantallas (capture_carousel.c). Con el interruptor de
     * capture_carousel.h a 0 la funcion es un no-op, asi que esto se puede
     * dejar enchufado siempre: el dia de la captura solo hay que cambiar el
     * flag. Va lo ULTIMO del setup porque necesita la pantalla montada y el
     * modelo de datos vivo. Auditado el 14-sep-2026: antes esto NO estaba
     * llamado, y el carrusel (que si estaba escrito y commiteado) no arrancaba
     * nunca -- por eso la captura no salia. */
    capture_carousel_start();

    logSection("Setup complete");
}
