/* viaje_cola.c — cola de apuntes pendientes de entregar a la P4.
 *
 * Por que existe: la 3.5" se apaga con el contacto CONSTANTEMENTE, y las
 * paradas y los repostajes se apuntan justo antes de apagar. Sin cola, un
 * apunte hecho con la P4 caida se perderia sin que nadie se entere. Con la P4
 * encendida casi siempre (arranca antes que esta pantalla), la cola pasa de ser
 * el camino habitual a ser la red de seguridad -- pero es justo la red la que
 * tiene que aguantar.
 *
 * En NVS y no en RAM, por lo mismo: quitar el contacto no puede tirar nada.
 *
 * ORDEN GARANTIZADO. Se envia siempre desde la cabeza y no se pasa a la
 * siguiente hasta que la actual se entrega. El "fin" de viaje entra como uno
 * mas, asi que NUNCA adelanta a los registros que se apuntaron antes que el:
 * un viaje no puede cerrarse en la P4 con apuntes suyos todavia por llegar.
 */
#include "viaje_cola.h"
#include "p4_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

static const char *TAG = "viaje_cola";

#define NS          "vcola"
#define K_CABEZA    "cabeza"     /* indice del proximo a enviar */
#define K_COLA      "cola"       /* indice donde se escribira el siguiente */
#define K_CAPACIDAD "capacidad"  /* la CAPACIDAD que estaba en vigor al escribir esto */

/* DIECISEIS, y el numero sale de una cuenta, no de un redondeo. La particion
 * "nvs" paso de 16 a 64 KB el 09-sep-2026 (partitions.csv): con 16 KB, entre
 * la cola (16 entradas de hasta 896 B, ~14 KB) y todo lo demas que vive en el
 * mismo namespace (Wi-Fi, calibracion, estado de la salida, contadores) mas
 * la pagina entera que NVS se reserva siempre para su propio compactado, no
 * cabia de verdad -- el aviso de "casi llena" no se alcanzaba nunca antes de
 * un fallo real de escritura. Con 64 KB si cabe con margen; el numero de
 * entradas se deja en 16 porque ya era el que hacia falta, no el problema.
 *
 * Que se acumulen apuntes NO es lo normal: la P4 esta siempre encendida y la
 * cola se vacia sola en segundos. Si crece es que no le llegan. Al llenarse se
 * AVISA en vez de tirar nada en silencio, que es el fallo que esta cola existe
 * para evitar. */
#define CAPACIDAD   VIAJE_COLA_CAPACIDAD

/* CUERPO_MAX vive en viaje_cola.h (publica): apunte_encolar() en
 * view_registro.c necesita el MISMO numero para su buffer local, y antes lo
 * llevaba duplicado a mano como el literal 896 -- sin CI que lo vigilara,
 * bastaba con tocar uno de los dos sitios para que divergieran en silencio.
 * Detectado auditando el 08-sep-2026. */

/* Cada cuanto se reintenta la cabeza cuando hay algo pendiente. 15 s: lo
 * bastante seguido para que al encender la P4 se vacie enseguida, y lo bastante
 * espaciado para no estar dando la lata a un AP que no esta. */
#define REINTENTO_MS 15000

static SemaphoreHandle_t s_mutex;
/* Despierta a reparto_task al momento cuando se encola algo con la cola
 * vacia, en vez de esperar a que se cumpla el REINTENTO_MS del sueno en
 * curso (hasta 15 s de retraso en enviar, p.ej. el "fin" de una salida
 * puntual justo despues de tocar Terminar salida). SOLO se usa para eso: el
 * reintento tras un fallo (401/409/400) se queda con el sueno fijo de
 * siempre, porque ahi despertar antes no ayuda -- la cabeza sigue siendo la
 * misma entrada problematica. Detectado el 09-sep-2026. */
static SemaphoreHandle_t s_despertar;
static viaje_cola_cambio_cb s_cambio_cb;

/* ── NVS ──────────────────────────────────────────────────────────────────── */

static void indices_leer(uint32_t *cabeza, uint32_t *cola)
{
    *cabeza = 0; *cola = 0;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    nvs_get_u32(h, K_CABEZA, cabeza);
    nvs_get_u32(h, K_COLA, cola);
    nvs_close(h);
}

/* Devuelve si se han guardado DE VERDAD. Antes no se miraba, y ahi habia un
 * agujero por el que se perdian apuntes en silencio: el cuerpo se escribe
 * primero y el indice despues, asi que si el indice no entra -- la NVS son
 * 16 KB y se puede llenar -- el apunte queda escrito pero INVISIBLE, y push()
 * devolvia true igualmente. La pantalla decia "guardado" y no habia nada. */
static bool indices_escribir(uint32_t cabeza, uint32_t cola)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "no puedo abrir la NVS para los indices de la cola");
        return false;
    }
    esp_err_t e = nvs_set_u32(h, K_CABEZA, cabeza);
    if (e == ESP_OK) e = nvs_set_u32(h, K_COLA, cola);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "no puedo guardar los indices de la cola: %s", esp_err_to_name(e));
        return false;
    }
    return true;
}

/* Borra una entrada por su clave. Se usa para deshacer un push a medias. */
static void borrar_entrada(const char *clave)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, clave);
    nvs_commit(h);
    nvs_close(h);
}

/* Los indices solo CRECEN; la clave es el indice modulo la capacidad. Asi no
 * hay que renumerar nada al sacar de la cabeza, que seria reescribir la NVS
 * entera por cada envio. */
static void clave_de(uint32_t idx, char *out, size_t n)
{
    snprintf(out, n, "q%lu", (unsigned long)(idx % CAPACIDAD));
}

/* ── API ──────────────────────────────────────────────────────────────────── */

size_t viaje_cola_pendientes(void)
{
    uint32_t cabeza, cola;
    indices_leer(&cabeza, &cola);
    return (size_t)(cola - cabeza);
}

static void avisar_cambio(void)
{
    if (s_cambio_cb) s_cambio_cb(viaje_cola_pendientes());
}

bool viaje_cola_push(const char *cuerpo, viaje_cola_error_t *motivo_out)
{
    if (motivo_out) *motivo_out = VIAJE_COLA_ERR_NINGUNO;

    if (!cuerpo || !cuerpo[0]) return false;
    if (strlen(cuerpo) >= CUERPO_MAX) {
        ESP_LOGE(TAG, "apunte demasiado largo (%u bytes), NO se encola",
                 (unsigned)strlen(cuerpo));
        if (motivo_out) *motivo_out = VIAJE_COLA_ERR_NVS;
        return false;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t cabeza, cola;
    indices_leer(&cabeza, &cola);

    if (cola - cabeza >= CAPACIDAD) {
        xSemaphoreGive(s_mutex);
        ESP_LOGE(TAG, "cola LLENA (%d): no se encola nada mas", CAPACIDAD);
        if (motivo_out) *motivo_out = VIAJE_COLA_ERR_LLENA;
        return false;
    }

    char clave[16];
    clave_de(cola, clave, sizeof(clave));

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        xSemaphoreGive(s_mutex);
        if (motivo_out) *motivo_out = VIAJE_COLA_ERR_NVS;
        return false;
    }
    esp_err_t e = nvs_set_str(h, clave, cuerpo);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) {
        xSemaphoreGive(s_mutex);
        ESP_LOGE(TAG, "no puedo guardar el apunte: %s", esp_err_to_name(e));
        /* NVS_NOT_ENOUGH_SPACE aqui NO es "la cola logica esta llena" (eso ya
         * se ha descartado arriba): es la particion entera sin sitio, por lo
         * demas que comparte con ella. Un apagon/reset la P4 no lo arregla. */
        if (motivo_out) *motivo_out = VIAJE_COLA_ERR_NVS;
        return false;
    }

    if (!indices_escribir(cabeza, cola + 1)) {
        /* Sin indice, ese apunte no existe para nadie. Se borra y se dice que
         * NO se ha guardado, en vez de dejar un fantasma y contestar que si. */
        borrar_entrada(clave);
        xSemaphoreGive(s_mutex);
        ESP_LOGE(TAG, "el apunte NO se ha encolado (no cabe el indice)");
        if (motivo_out) *motivo_out = VIAJE_COLA_ERR_NVS;
        return false;
    }
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "encolado #%lu (%u pendientes): %s",
             (unsigned long)cola, (unsigned)(cola + 1 - cabeza), cuerpo);
    avisar_cambio();
    /* Si reparto_task esta dormida esperando a que la cola deje de estar
     * vacia, que se entere ya: sin esto, este apunte podia tardar hasta
     * REINTENTO_MS (15 s) en empezar a enviarse. */
    if (s_despertar) xSemaphoreGive(s_despertar);
    return true;
}

/* Saca la cabeza. Solo se llama tras una entrega CONFIRMADA. */
static void descartar_cabeza(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t cabeza, cola;
    indices_leer(&cabeza, &cola);
    if (cabeza < cola) {
        char clave[16];
        clave_de(cabeza, clave, sizeof(clave));
        nvs_handle_t h;
        if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_key(h, clave);
            nvs_commit(h);
            nvs_close(h);
        }
        if (!indices_escribir(cabeza + 1, cola)) {
            /* La entrada ya esta borrada pero la cabeza no avanza: la proxima
             * lectura no encontrara la clave. Lo recoge el repartidor, que
             * salta las cabezas ilegibles en vez de atascarse. */
            ESP_LOGE(TAG, "la cabeza de la cola no avanza; se reintentara");
        }
    }
    xSemaphoreGive(s_mutex);
    avisar_cambio();
}

static bool leer_cabeza(char *out, size_t n)
{
    uint32_t cabeza, cola;
    indices_leer(&cabeza, &cola);
    if (cabeza >= cola) return false;

    char clave[16];
    clave_de(cabeza, clave, sizeof(clave));
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = n;
    esp_err_t e = nvs_get_str(h, clave, out, &len);
    nvs_close(h);
    return e == ESP_OK;
}

/* ── El repartidor ────────────────────────────────────────────────────────── */

/* Cuantas veces se le da el beneficio de la duda a un 400 antes de tirarlo
 * de verdad. No es solo "el apunte esta mal formado": un recorte por timeout
 * de recv() en el portal de la P4 (red lenta, WiFi flojo) puede producir el
 * mismo 400 sobre un apunte que en realidad estaba bien. Descartarlo al
 * primer intento perdia esos casos para siempre. 409/408/429/401/5xx siguen
 * reintentando sin limite (ver el comentario de mas abajo, no cambia).
 * Detectado auditando el 07-sep-2026. */
#define MAX_INTENTOS_400 3

/* Un 409 sostenido sobre el MISMO apunte durante mucho rato (no unos pocos
 * ciclos: eso es normal mientras el inicio, que puede ir justo delante en la
 * cola, termina de procesarse) es la señal de un caso concreto: si se borro
 * la NVS de esta 35cabina a mitad de viaje, next_trip_seq() vuelve a
 * numerar desde 1 mientras la P4 sigue recordando un id mucho mas alto del
 * viaje de antes -- y como el inicio de un viaje nuevo choca con el viaje que
 * la P4 cree que sigue abierto, ningun apunte de este vuelve a entrar nunca
 * mientras no se intervenga a mano. 20 intentos (~5 min a 15s) no lo dispara
 * nunca en el caso normal, pero si detecta el atasco de verdad. NO se toca
 * el protocolo entre dispositivos: solo se avisa mejor de lo que ya pasaba
 * en silencio. Detectado auditando el 07-sep-2026. */
#define INTENTOS_409_ATASCO 20
/* _Atomic y no volatile: lo escribe reparto_task y lo lee la UI (LVGL) --
 * volatile evita que el compilador cachee el valor en un registro, pero NO
 * da ninguna garantia formal de visibilidad entre nucleos (esto es un
 * esp32s3 dual-core). Con un bool de un writer y un reader no habia
 * corrupcion real posible en la practica, pero era una carrera de datos en
 * sentido formal. _Atomic cuesta lo mismo en el binario para un bool
 * alineado (misma carga/almacen de un byte) y cierra el hueco de verdad.
 * Detectado auditando el 08-sep-2026. */
static _Atomic bool s_atascada_409 = false;

bool viaje_cola_bloqueada(void)
{
    return s_atascada_409;
}

/* Un 401 sostenido significa credenciales mal puestas en Ajustes -- a
 * diferencia de "la P4 esta apagada" (mismo sintoma en pantalla: nada entra),
 * esto NO se arregla solo con encenderla, hace falta que el usuario corrija
 * usuario/clave. Antes las dos cosas ensenaban el mismo "N pendientes" sin
 * distincion. 3 intentos (~45s) basta: una clave mal puesta falla siempre,
 * no hace falta el margen de minutos que si tiene el 409 (que puede ser
 * normal mientras el inicio del viaje termina de procesarse).
 * Detectado auditando el 07-sep-2026. */
#define INTENTOS_401_ATASCO 3
static _Atomic bool s_atascada_401 = false;   /* ver el porque de _Atomic arriba */

bool viaje_cola_credenciales_mal(void)
{
    return s_atascada_401;
}

static void reparto_task(void *arg)
{
    (void)arg;
    char cuerpo[CUERPO_MAX];
    uint32_t idx_400 = UINT32_MAX;   /* indice de cola del ultimo 400 visto */
    int intentos_400 = 0;
    uint32_t idx_409 = UINT32_MAX;   /* indice de cola del ultimo 409 visto */
    int intentos_409 = 0;
    int intentos_401 = 0;   /* no distingue apunte: una clave mal puesta falla con cualquiera */

    while (1) {
        if (!leer_cabeza(cuerpo, sizeof(cuerpo))) {
            /* OJO: leer_cabeza dice que no por dos motivos distintos, y hay que
             * separarlos. Si la cola esta vacia, a dormir. Pero si hay
             * pendientes y aun asi no se puede leer, esa entrada esta rota o no
             * esta: sin saltarla, la cola se queda atascada PARA SIEMPRE detras
             * de ella, con el aviso de "N sin enviar" puesto y sin avanzar. */
            if (viaje_cola_pendientes() > 0) {
                ESP_LOGE(TAG, "la cabeza de la cola no se puede leer: la salto "
                              "para no atascar lo que viene detras");
                descartar_cabeza();
                continue;
            }
            /* Cola vacia: dormir hasta REINTENTO_MS, pero SIN esperar a que
             * se cumpla si mientras tanto se encola algo nuevo -- viaje_
             * cola_push() suelta este semaforo, y aqui se despierta al
             * momento en vez de quedarse hasta 15 s sin enviar nada. */
            xSemaphoreTake(s_despertar, pdMS_TO_TICKS(REINTENTO_MS));
            continue;
        }

        int estado = 0;
        bool ok = p4_api_post(cuerpo, &estado);

        if (ok) {
            idx_400 = UINT32_MAX;
            idx_409 = UINT32_MAX;
            intentos_409 = 0;
            intentos_401 = 0;
            s_atascada_409 = false;
            s_atascada_401 = false;
            descartar_cabeza();
            /* Sin espera: si hay mas, se sigue vaciando de seguido. Que la P4
             * este respondiendo es justo el momento de aprovechar. */
            continue;
        }

        if (estado == 401) {
            intentos_401++;
            if (intentos_401 >= INTENTOS_401_ATASCO && !s_atascada_401) {
                s_atascada_401 = true;
                ESP_LOGE(TAG, "401 sostenido %d veces: usuario/clave del portal "
                              "mal en Ajustes, no se van a arreglar solos",
                         intentos_401);
                avisar_cambio();   /* mismo motivo que el aviso de 409 atascado */
            }
            vTaskDelay(pdMS_TO_TICKS(REINTENTO_MS));
            continue;
        }
        intentos_401 = 0;
        s_atascada_401 = false;

        if (estado == 409) {
            uint32_t cabeza, cola;
            indices_leer(&cabeza, &cola);
            if (cabeza == idx_409) {
                intentos_409++;
            } else {
                idx_409 = cabeza;
                intentos_409 = 1;
                s_atascada_409 = false;   /* apunte distinto: cuenta desde cero */
            }
            if (intentos_409 >= INTENTOS_409_ATASCO && !s_atascada_409) {
                s_atascada_409 = true;
                ESP_LOGE(TAG, "409 sostenido %d veces sobre el mismo apunte: "
                              "probable choque de numeracion con la P4 (se "
                              "borro la NVS a mitad de viaje?). No se pierde "
                              "nada, sigue en cola, pero no entrara solo.",
                         intentos_409);
                /* El numero de pendientes no ha cambiado (sigue siendo el
                 * mismo apunte atascado), asi que sin esto la pantalla no se
                 * enteraria hasta el siguiente cambio real de la cola. */
                avisar_cambio();
            }
            vTaskDelay(pdMS_TO_TICKS(REINTENTO_MS));
            continue;
        }

        /* 400 aparte: unos pocos intentos antes de tirarlo (ver el comentario
         * de MAX_INTENTOS_400), no al primero. */
        if (estado == 400) {
            uint32_t cabeza, cola;
            indices_leer(&cabeza, &cola);
            if (cabeza == idx_400) {
                intentos_400++;
            } else {
                idx_400 = cabeza;
                intentos_400 = 1;
            }

            if (intentos_400 < MAX_INTENTOS_400) {
                ESP_LOGW(TAG, "la P4 rechaza con 400 (intento %d/%d, puede ser "
                              "un recorte de red): %s",
                         intentos_400, MAX_INTENTOS_400, cuerpo);
                vTaskDelay(pdMS_TO_TICKS(REINTENTO_MS));
                continue;
            }
            /* NO se descarta. Un 400 repetido sobre el mismo apunte casi nunca
             * es que el apunte este mal: es que la P4 es mas vieja que esta
             * pantalla y no entiende el cuerpo (paso el 30-sep-2026: la P4 de
             * la autocaravana iba por la v2.33). Tirarlo era perder un
             * repostaje o el cierre de un viaje por una version. Se queda en
             * cola, marcado como atascado (el mismo aviso rojo que el 409) y se
             * sigue intentando: en cuanto la P4 se actualice, entra solo. */
            if (!s_atascada_409) {
                s_atascada_409 = true;
                ESP_LOGE(TAG, "la P4 rechaza el apunte con 400 tras %d intentos. "
                              "NO se descarta: sigue en cola y se reintenta. "
                              "Suele ser la P4 con firmware viejo: %s",
                         intentos_400, cuerpo);
                avisar_cambio();
            }
            vTaskDelay(pdMS_TO_TICKS(REINTENTO_MS));
            continue;
        }

        /* 4xx que NO son "vuelve luego": el apunte esta mal formado y
         * reintentarlo eternamente atascaria la cola entera detras de el. Se
         * tira, pero dejando constancia bien visible en el log.
         *
         * Cuatro se EXCLUYEN porque significan "ahora no" y no "esto no vale":
         *   400 tiene su propio tratamiento arriba (unos intentos, no al primero).
         *   401 credenciales mal puestas -> se arreglan en Ajustes y entonces
         *       el mismo apunte entra bien. Tirarlo seria perder un repostaje
         *       por un dedazo.
         *   409 no hay viaje abierto todavia en la P4 -> el inicio puede estar
         *       aun por delante en esta misma cola.
         *   408 / 429 son "vuelve luego" por definicion. */
        if (estado >= 400 && estado < 500 && estado != 400 && estado != 401 &&
            estado != 408 && estado != 409 && estado != 429) {
            /* Tampoco se descarta (ver el comentario del 400): se marca y se
             * reintenta. Si de verdad el apunte esta mal, se borra desde la
             * pantalla -- "sin cerrar" lleva justo a esa lista --, pero que lo
             * decida el usuario y no un codigo de respuesta. */
            if (!s_atascada_409) {
                s_atascada_409 = true;
                ESP_LOGE(TAG, "la P4 rechaza el apunte con %d. NO se descarta: "
                              "sigue en cola y se reintenta: %s", estado, cuerpo);
                avisar_cambio();
            }
            vTaskDelay(pdMS_TO_TICKS(REINTENTO_MS));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(REINTENTO_MS));
    }
}

/* La capacidad bajo de 64 a 16 el 24-ago-2026 (la NVS son 16 KB y con 64 nunca
 * se habria llegado al tope: se llenaba antes la particion). Eso deja dos
 * cabos:
 *
 *  - Entradas HUERFANAS q16..q63 de la version anterior, ocupando para siempre
 *   una memoria que va justa. Se borran una vez.
 *  - Los indices se traducen a clave con "modulo capacidad", asi que si habia
 *   apuntes pendientes al actualizar, la cabeza apuntaria a OTRA entrada. Si
 *   los indices no cuadran con la capacidad de ahora, se reinicia la cola
 *   entera y se dice: mejor perderlos avisando que entregar el apunte
 *   equivocado. */
#define CAPACIDAD_ANTERIOR  64

static void migrar_cola(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;

    bool borrado = false;
    for (uint32_t i = CAPACIDAD; i < CAPACIDAD_ANTERIOR; i++) {
        char clave[16];
        snprintf(clave, sizeof(clave), "q%lu", (unsigned long)i);
        if (nvs_erase_key(h, clave) == ESP_OK) borrado = true;
    }
    if (borrado) {
        nvs_commit(h);
        ESP_LOGW(TAG, "borradas entradas huerfanas de la capacidad anterior");
    }

    /* Detector de verdad: compara la CAPACIDAD que estaba en vigor la
     * ULTIMA vez que se escribio algo aqui contra la de ahora -- no cuantas
     * entradas hay pendientes. El chequeo antiguo (cola-cabeza > CAPACIDAD)
     * se colaba un caso: con pocos apuntes pendientes, el RECUENTO seguia
     * pareciendo valido bajo la capacidad nueva, pero la clave de cada uno
     * sale de "indice modulo capacidad" -- un cambio de capacidad cambia a
     * que clave apunta cada indice aunque el recuento no lo delate, y la
     * cabeza podia acabar leyendo el cuerpo de OTRO apunte sin que nada lo
     * detectase. Detectado el 09-sep-2026 (auditando el cambio de NVS a 64
     * KB). K_CAPACIDAD no existia antes de este mismo arreglo: "primera_vez"
     * cubre ese arranque de transicion sin tirar una cola que ya era valida. */
    uint32_t capacidad_anterior = 0;
    bool primera_vez = (nvs_get_u32(h, K_CAPACIDAD, &capacidad_anterior) != ESP_OK);

    uint32_t cabeza = 0, cola = 0;
    nvs_get_u32(h, K_CABEZA, &cabeza);
    nvs_get_u32(h, K_COLA, &cola);

    bool capacidad_cambio = !primera_vez && capacidad_anterior != CAPACIDAD;
    bool no_cuadra = (uint32_t)(cola - cabeza) > CAPACIDAD;

    if (capacidad_cambio || no_cuadra) {
        if (capacidad_cambio) {
            ESP_LOGE(TAG, "la capacidad de la cola cambio de %lu a %d: las claves "
                          "ya no se corresponden con los indices, la reinicio",
                     (unsigned long)capacidad_anterior, CAPACIDAD);
        } else {
            ESP_LOGE(TAG, "los indices de la cola no cuadran (%lu pendientes para "
                          "una capacidad de %d): la reinicio",
                     (unsigned long)(cola - cabeza), CAPACIDAD);
        }
        for (uint32_t i = 0; i < CAPACIDAD; i++) {
            char clave[16];
            snprintf(clave, sizeof(clave), "q%lu", (unsigned long)i);
            nvs_erase_key(h, clave);
        }
        nvs_set_u32(h, K_CABEZA, 0);
        nvs_set_u32(h, K_COLA, 0);
        nvs_set_u32(h, K_CAPACIDAD, CAPACIDAD);
        nvs_commit(h);
        nvs_close(h);
        return;
    }

    if (primera_vez) {
        nvs_set_u32(h, K_CAPACIDAD, CAPACIDAD);
        nvs_commit(h);
    }

    /* Apunte huerfano: viaje_cola_push() graba el CUERPO y lo comitea, y solo
     * DESPUES avanza 'cola' con un commit aparte (ver el comentario ahi mismo).
     * Un apagon justo entre los dos deja el cuerpo grabado de verdad en la
     * tarjeta pero invisible para siempre: 'cola' nunca llego a incluirlo, asi
     * que ni el repartidor lo ve ni un push nuevo lo pisaria (usa la MISMA
     * clave modulo CAPACIDAD, asi que sin este recorte un push futuro lo
     * habria sobrescrito igualmente, pero mientras tanto ocupa sitio en una
     * particion que va muy justa).
     *
     * La clave que le tocaria a 'cola' es la unica donde esto puede pasar: si
     * hay algo escrito ahi es justo el cuerpo del ultimo push que no llego a
     * contarse. Se recupera incluyendolo (cola++) en vez de dejarlo perdido.
     * Detectado auditando el 07-sep-2026 (ver el hallazgo de robustez #7). */
    char clave_borde[16];
    snprintf(clave_borde, sizeof(clave_borde), "q%lu", (unsigned long)(cola % CAPACIDAD));
    /* Solo se pregunta el TAMAÑO (out_value=NULL), sin leer el cuerpo entero a
     * una pila que no lo necesita: es el modo documentado de NVS para
     * comprobar si una clave existe. */
    size_t len = 0;
    if (nvs_get_str(h, clave_borde, NULL, &len) == ESP_OK &&
        (uint32_t)(cola - cabeza) < CAPACIDAD) {
        ESP_LOGW(TAG, "apunte huerfano recuperado en '%s' (el indice nunca "
                      "llego a incluirlo tras un apagon a medio guardar)",
                 clave_borde);
        nvs_set_u32(h, K_COLA, cola + 1);
        nvs_commit(h);
    }
    nvs_close(h);
}

void viaje_cola_init(viaje_cola_cambio_cb cb)
{
    s_mutex = xSemaphoreCreateMutex();
    s_despertar = xSemaphoreCreateBinary();
    /* Solo puede fallar sin heap, y ahi ya hay problemas mayores que este --
     * pero un xSemaphoreTake/Give sobre NULL es peor: crashea sin decir por
     * que. Dejarlo dicho en el log al menos explica la causa real. */
    if (!s_mutex)     ESP_LOGE(TAG, "xSemaphoreCreateMutex fallo: sin memoria?");
    if (!s_despertar) ESP_LOGE(TAG, "xSemaphoreCreateBinary fallo: sin memoria?");
    s_cambio_cb = cb;

    migrar_cola();

    size_t n = viaje_cola_pendientes();
    if (n) ESP_LOGW(TAG, "arranco con %u apuntes sin entregar del encendido anterior",
                    (unsigned)n);

    /* 6 KB por el cliente HTTP, igual que la tarea de envio directo que
     * sustituye. Prioridad 4: por debajo de LVGL, esto nunca corre prisa. */
    if (xTaskCreate(reparto_task, "viaje_cola", 6144, NULL, 4, NULL) != pdPASS) {
        /* Sin el repartidor, la cola se llena y nunca se vacia: los apuntes
         * pendientes se acumulan sin entregarse nunca a la P4, en silencio. */
        ESP_LOGE(TAG, "xTaskCreate(reparto_task) fallo: la cola no se va a repartir");
    }
    avisar_cambio();
}
