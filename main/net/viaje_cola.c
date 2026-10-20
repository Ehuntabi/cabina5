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
#include "esp_timer.h"   /* esp_timer_get_time: caducidad del hueco de la SD */
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

size_t viaje_cola_apuntes_libres(void)
{
    size_t n = viaje_cola_pendientes();
    size_t tope = (size_t)(CAPACIDAD - VIAJE_COLA_RESERVA_CONTROL);
    return (n >= tope) ? 0 : (tope - n);
}

static void avisar_cambio(void)
{
    if (s_cambio_cb) s_cambio_cb(viaje_cola_pendientes());
}

/* ¿Este cuerpo es una operacion de CONTROL (el cierre del viaje o el descarte)?
 *
 * Se busca el needle completo ("\"op\":\"fin\"") y no la palabra suelta: el
 * apunte de una parada lleva dentro un campo "fin":<hora>, y un strstr(cuerpo,
 * "fin") pelado lo tomaria por el cierre del viaje. Los textos del usuario van
 * con las comillas escapadas, asi que tampoco pueden fabricar el needle. */
static bool es_op_de_control(const char *cuerpo)
{
    if (!cuerpo) return false;
    /* strstr y no un prefijo: esto NO depende de que "op" sea la primera clave
     * del cuerpo (hoy lo es, pero reordenarlas compilaria y pasaria el CI), que
     * es la misma convencion de es_advisory() y viaje_cola_tiene_op_pendiente().
     * El needle lleva "op" a proposito: el apunte de una parada tiene un campo
     * "fin":<hora> dentro, y un strstr(cuerpo,"fin") pelado lo confundiria con
     * el cierre del viaje. Las comillas de los textos del usuario van escapadas
     * (json_str en apunte.c, p4_api.c), asi que no pueden fabricar este needle. */
    return strstr(cuerpo, "\"op\":\"fin\"") != NULL ||
           strstr(cuerpo, "\"op\":\"descartar\"") != NULL;
}

bool viaje_cola_push(const char *cuerpo, viaje_cola_error_t *motivo_out)
{
    if (motivo_out) *motivo_out = VIAJE_COLA_ERR_NINGUNO;

    if (!cuerpo || !cuerpo[0]) return false;
    if (strlen(cuerpo) >= CUERPO_MAX) {
        ESP_LOGE(TAG, "apunte demasiado largo (%u bytes, tope %d), NO se encola",
                 (unsigned)strlen(cuerpo), CUERPO_MAX);
        /* LARGO, no NVS (17-oct-2026): con ERR_NVS la pantalla decia "fallo de
         * memoria del aparato" cuando el problema era que el apunte no cabe. El
         * delito y el diagnostico tienen que coincidir. */
        if (motivo_out) *motivo_out = VIAJE_COLA_ERR_LARGO;
        return false;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t cabeza, cola;
    indices_leer(&cabeza, &cola);

    /* El tope depende de QUE se esta encolando: un apunte normal no puede
     * comerse los huecos reservados al cierre (ver VIAJE_COLA_RESERVA_CONTROL). */
    uint32_t tope = es_op_de_control(cuerpo)
                  ? (uint32_t)CAPACIDAD
                  : (uint32_t)(CAPACIDAD - VIAJE_COLA_RESERVA_CONTROL);
    /* El tope NUNCA puede pasar de CAPACIDAD: migrar_cola() da la cola por
     * corrupta y la BORRA ENTERA si (cola - cabeza) > CAPACIDAD al arrancar
     * (viaje_cola.c, "no_cuadra"), asi que un off-by-one aqui se paga con los
     * apuntes del usuario. */
    if (tope > (uint32_t)CAPACIDAD) tope = (uint32_t)CAPACIDAD;
    if (cola - cabeza >= tope) {
        xSemaphoreGive(s_mutex);
        ESP_LOGE(TAG, "cola LLENA (%lu de %u, tope %lu para este cuerpo): no se "
                      "encola nada mas", (unsigned long)(cola - cabeza),
                 (unsigned)CAPACIDAD, (unsigned long)tope);
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

/* Saca la cabeza: borra su clave de la NVS y avanza el indice.
 *
 * SE LLAMA CON s_mutex COGIDO (por eso no lo coge aqui). Lo partio en dos la
 * tanda 2 de la auditoria del 18-oct-2026: el descarte a mano soltaba el cerrojo
 * y volvia a entrar por descartar_cabeza(), asi que en el hueco el repartidor
 * podia sacar la cabeza vieja y el 200 de ESA entrada llegaba cuando la cabeza
 * ya era la siguiente (se borraba una entrada que no se habia entregado nunca).
 * Ahora hay un solo sitio que borra, y siempre bajo el mismo cerrojo. */
static void borrar_cabeza_con_mutex(void)
{
    uint32_t cabeza, cola;
    indices_leer(&cabeza, &cola);
    if (cabeza >= cola) return;
    char clave[16];
    clave_de(cabeza, clave, sizeof(clave));
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        /* SI EL BORRADO FALLA, la entrada puede REAPARECER como huerfana al
         * arrancar y reenviarse ya entregada. No se puede hacer mas que dejarlo
         * dicho. */
        esp_err_t e_er = nvs_erase_key(h, clave);
        if (e_er != ESP_OK && e_er != ESP_ERR_NVS_NOT_FOUND)
            ESP_LOGW(TAG, "no puedo borrar %s de la NVS (%s): puede reaparecer "
                          "como huerfana", clave, esp_err_to_name(e_er));
        nvs_commit(h);
        nvs_close(h);
    }
    if (!indices_escribir(cabeza + 1, cola)) {
        /* La entrada ya esta borrada pero la cabeza no avanza: la proxima
         * lectura no encontrara la clave. Lo recoge el repartidor, que salta
         * las cabezas ilegibles en vez de atascarse. */
        ESP_LOGE(TAG, "la cabeza de la cola no avanza; se reintentara");
    }
}

/* Saca la cabeza. Solo se llama tras una entrega CONFIRMADA. */
static void descartar_cabeza(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    borrar_cabeza_con_mutex();
    xSemaphoreGive(s_mutex);
    avisar_cambio();
}

/* ¿Este apunte es un AVISO que se puede soltar si la P4 no lo entiende?
 *
 * Hoy solo el `destino` (el cambio de nombre a mitad de viaje): es ADVISORY,
 * porque el nombre nuevo viaja TAMBIEN en el `fin` y la cabina lo tiene en su
 * NVS. En cambio un repostaje o el `fin` NO se pueden soltar nunca.
 *
 * POR QUE HACE FALTA (arbitraje del 13-oct-2026): con una P4 de firmware
 * anterior al 13-oct (la de la autocaravana iba por la v2.33 el 30-sep), la op
 * `destino` da 400 "op?" -- y como la cola no descarta NADA, la cabeza no
 * avanzaba nunca: detras se apilaban los apuntes y el `fin`, a los 16 la cola se
 * llenaba y "Terminar viaje" fallaba. El viaje no se podia cerrar desde la
 * cabina y la P4 lo mantenia abierto. Un aviso que sobra no puede bloquear el
 * cierre de un viaje. */
/* Intentos sobre la MISMA cabeza antes de soltar un aviso (ver es_advisory). */
#define MAX_INTENTOS_ADVISORY 3

/* Cuenta intentos sobre la MISMA entrada de cabeza. Devuelve true cuando ya se
 * ha pasado del presupuesto (y hay que soltarla). */
static bool cabeza_igual(uint32_t *idx, int *intentos)
{
    uint32_t cabeza, cola;
    indices_leer(&cabeza, &cola);
    if (cabeza == *idx) {
        (*intentos)++;
    } else {
        *idx = cabeza;
        *intentos = 1;
    }
    return *intentos >= MAX_INTENTOS_ADVISORY;
}

static bool es_advisory(const char *cuerpo)
{
    return cuerpo && strstr(cuerpo, "\"op\":\"destino\"") != NULL;
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
 * cola, termina de procesarse) es la señal de un atasco que no se arregla solo.
 * Hay DOS casos y se distinguen por el cuerpo de la respuesta (16-oct-2026):
 *
 *   - choque de numeracion: si se borro la NVS de esta cabina a mitad de viaje,
 *     next_trip_seq() vuelve a numerar desde 1 mientras la P4 sigue recordando
 *     un id mucho mas alto del viaje de antes -- y como el inicio de un viaje
 *     nuevo choca con el viaje que la P4 cree que sigue abierto, ningun apunte
 *     de este vuelve a entrar nunca mientras no se intervenga a mano;
 *   - apunte REZAGADO del viaje anterior: la P4 lo dice con todas las letras
 *     ("ese apunte es de un viaje ya cerrado"). Aqui la numeracion esta bien.
 *
 * 20 intentos (~5 min a 15s) no lo dispara nunca en el caso normal, pero si
 * detecta el atasco de verdad. NO se toca el protocolo entre dispositivos: solo
 * se avisa mejor de lo que ya pasaba en silencio. Detectado auditando el
 * 07-sep-2026 (y el segundo caso, el 16-oct-2026). */
#define INTENTOS_409_ATASCO 20
/* POR QUE ESTA ATASCADA LA COLA.
 *
 * Hasta el 13-oct-2026 solo habia dos banderas -- 409 y 401 -- y todo lo demas
 * caia en el mismo saco mudo: un 400 (la P4 rechaza el cuerpo: firmware mas
 * viejo que esta pantalla), un 507 (la P4 no puede escribir en su tarjeta) y
 * "la P4 esta apagada" se veian IGUAL en la pantalla, y el unico texto que
 * salia era "mira si tiene corriente", que es un diagnostico falso para los
 * tres primeros. Ahora hay un estado con motivo, y la pantalla puede decir que
 * pasa y ofrecer la salida.
 *
 * _Atomic y no volatile: lo escribe reparto_task y lo lee la UI (LVGL) --
 * volatile evita que el compilador cachee el valor en un registro, pero NO da
 * ninguna garantia formal de visibilidad entre nucleos (esto es un esp32s3
 * dual-core). Con un entero alineado de un writer y un reader no habia
 * corrupcion real posible en la practica, pero era una carrera de datos en
 * sentido formal. Detectado auditando el 08-sep-2026. */
static _Atomic int s_atasco = VIAJE_COLA_ATASCO_NO;

/* Sube cada vez que la UI descarta una entrada A MANO. El repartidor se queda
 * con el valor ANTES de enviar: si al volver no coincide, la cabeza que creia
 * estar enviando ya no es esa y NO debe sacarla -- sacaria la siguiente, que no
 * ha entregado nadie. Ver viaje_cola_descartar_cabeza(). */
static _Atomic uint32_t s_generacion = 0;

viaje_cola_atasco_t viaje_cola_atasco(void)
{
    return (viaje_cola_atasco_t)s_atasco;
}

bool viaje_cola_bloqueada(void)
{
    viaje_cola_atasco_t a = (viaje_cola_atasco_t)s_atasco;
    return a == VIAJE_COLA_ATASCO_NUMERACION || a == VIAJE_COLA_ATASCO_RECHAZO ||
           a == VIAJE_COLA_ATASCO_REZAGADO;   /* 409 del apunte: tambien se atasca */
}

bool viaje_cola_credenciales_mal(void)
{
    return (viaje_cola_atasco_t)s_atasco == VIAJE_COLA_ATASCO_CLAVE;
}

/* Un 401 sostenido significa credenciales mal puestas en Ajustes -- a
 * diferencia de "la P4 esta apagada" (mismo sintoma en pantalla: nada entra),
 * esto NO se arregla solo con encenderla, hace falta que el usuario corrija
 * usuario/clave. 3 intentos (~45s) basta: una clave mal puesta falla siempre,
 * no hace falta el margen de minutos que si tiene el 409 (que puede ser
 * normal mientras el inicio del viaje termina de procesarse).
 * Detectado auditando el 07-sep-2026. */
/* Intentos seguidos SIN RESPUESTA (estado == 0: no se pudo ni conectar) antes
 * de marcarlo como atasco. La P4 apagada con el vehiculo parado es LO NORMAL y
 * se arregla sola al dar el contacto, asi que no puede marcarse al primer
 * intento; pero con la cola llenandose y sin ninguna marca visible, el usuario
 * no tenia forma de saber por que no cabe otro apunte -- ni salida, porque el
 * descarte solo se ofrece con atasco (auditoria del 14-oct-2026). Tres intentos
 * son ~45 s. */
#define INTENTOS_SIN_RESPUESTA_ATASCO 3

#define INTENTOS_401_ATASCO 3

/* Un 5xx sostenido (507 sobre todo) significa que la P4 esta viva y contesta,
 * pero NO PUEDE ESCRIBIR: su tarjeta esta llena o fallando. Antes esto era
 * indistinguible de "la P4 esta apagada" -- mismo silencio y mismo cartel de
 * "mira si tiene corriente" --, y encenderla mas o vaciar la cola no arregla
 * nada: lo que hay que hacer es hacer sitio en la tarjeta de la P4. 3 intentos
 * (~45 s): un 503 de "tarjeta ocupada" suelto es normal y no debe encender este
 * aviso. */
#define INTENTOS_5XX_ATASCO 3

/* Descarta a mano la entrada de cabeza (el apunte que la P4 no acepta) y deja
 * seguir a los de detras.
 *
 * POR QUE HACE FALTA (auditoria del 13-oct-2026): la cola NO descarta nunca una
 * entrada por un codigo de respuesta -- a proposito, porque un 400 casi siempre
 * es "la P4 es mas vieja" y el apunte entra solo en cuanto se actualice --,
 * pero eso significaba que un apunte que la P4 NUNCA va a aceptar atasca la
 * cola ENTERA para siempre: los siguientes se apilan detras y a los 16
 * (VIAJE_COLA_CAPACIDAD) ya no se puede ni anotar ni cerrar el viaje. La salida
 * que habia era "borralo desde la pantalla" ("sin cerrar" lleva a esa lista),
 * pero esa lista son los eventos de la SALIDA, no la cola: no habia NINGUNA
 * forma de quitar un apunte encolado. Esto la da, con confirmacion en pantalla
 * (lo decide el usuario, no un codigo de respuesta).
 *
 * Devuelve si habia algo que descartar. */
/* ¿Hay en la cola algun apunte pendiente de la operacion 'op'?
 *
 * Existe para RECONCILIAR al arrancar (auditoria de integridad del 13-oct-2026,
 * hallazgo M5): el estado del viaje vive en DOS blobs de NVS -- el del viaje
 * (activo/destino) y el de la salida --, y un corte de corriente entre los dos
 * commits deja a la cabina creyendo una cosa y a la P4 otra. Lo que desempata
 * es la COLA: si el cierre del viaje ya se encolo, es que se llego a pulsar
 * "terminar" (y el fin se entregara solo); si no hay fin encolado, el corte fue
 * al EMPEZAR. Sin esto habia que elegir a ciegas entre cerrar algo que sigue
 * vivo o dejar abierto algo ya cerrado.
 *
 * Solo se usa al arrancar: recorre hasta 16 entradas de NVS una vez. */
bool viaje_cola_tiene_op_pendiente(const char *op)
{
    if (!op || !op[0]) return false;
    char busca[24];
    snprintf(busca, sizeof(busca), "\"op\":\"%s\"", op);

    bool hay = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t cabeza, cola;
    indices_leer(&cabeza, &cola);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        for (uint32_t i = cabeza; i < cola; i++) {
            char clave[16], cuerpo[CUERPO_MAX];
            clave_de(i, clave, sizeof(clave));
            size_t len = sizeof(cuerpo);
            if (nvs_get_str(h, clave, cuerpo, &len) == ESP_OK &&
                strstr(cuerpo, busca)) { hay = true; break; }
        }
        nvs_close(h);
    }
    xSemaphoreGive(s_mutex);
    return hay;
}

/* ¿Cuanto dice la P4 que le queda en la tarjeta? Devuelve -1 si no viene.
 *
 * El sufijo lo añade la P4 al cuerpo de sus respuestas de /api/viaje
 * ("... libre_mb=NNN", ver responde_con_hueco() en config_server_viaje.c). Es
 * TEXTO SIN AUTENTICAR: se usa para AVISAR al usuario, nunca para decidir si un
 * apunte se ha guardado (eso lo dice el codigo HTTP). Sin sufijo -- una P4
 * anterior al 14-oct-2026 -- devuelve -1 y no se enseña nada. */
static int parsea_libre_mb(const char *respuesta)
{
    if (!respuesta) return -1;
    const char *p = strstr(respuesta, "libre_mb=");
    if (!p) return -1;
    p += 9;                                  /* "libre_mb=" */
    if (*p < '0' || *p > '9') return -1;
    long v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        if (v > 1000000) return -1;           /* absurdo: 1 TB, no cuela */
        p++;
    }
    return (int)v;
}

/* ¿Dice la P4 que lo que se le ha llenado es SU MEMORIA (no la tarjeta)? Los
 * 507 de NVS llevan " sin_sitio=nvs": sin eso, el aviso de "no puede escribir"
 * mandaria a hacer sitio en la tarjeta cuando el problema es otro. */
static bool parsea_sin_sitio_nvs(const char *respuesta)
{
    return respuesta && strstr(respuesta, "sin_sitio=nvs") != NULL;
}

/* ¿Dice la P4 que ese apunte es de un viaje YA CERRADO? Es el 409 de un
 * 'registro' rezagado (ver id_rezagado() en su op_registro, que responde "ese
 * apunte es de un viaje ya cerrado: no lo meto en el que hay abierto"). Se mira
 * el cuerpo porque el codigo HTTP es el mismo 409 que un choque de numeracion, y
 * los dos casos piden un cartel DISTINTO: aqui la numeracion esta perfecta y lo
 * que sobra es el apunte. */
static bool parsea_rezagado(const char *respuesta)
{
    return respuesta && strstr(respuesta, "viaje ya cerrado") != NULL;
}

/* Lo ultimo que dijo la P4, y cuando lo dijo. -1 = no se sabe.
 *
 * _Atomic y no volatile: el escritor es la tarea de reparto y el lector la de
 * LVGL, en otra CPU (esto es un S3 de dos nucleos), y el par valor+fecha tiene
 * que leerse coherente -- es la misma regla que sigue s_atasco en este fichero. */
static _Atomic int     s_sd_libre_mb = -1;
static _Atomic int64_t s_sd_libre_us = 0;
static _Atomic bool    s_nvs_sin_sitio = false;

#define LIBRE_MB_VALIDEZ_US  (30LL * 60LL * 1000000LL)   /* 30 min */

bool viaje_cola_p4_nvs_llena(void)
{
    return s_nvs_sin_sitio;
}

int viaje_cola_libre_mb(void)
{
    if (s_sd_libre_mb < 0) return -1;
    /* El dato caduca: si la P4 no ha dicho nada en media hora, mejor no enseñar
     * una cifra que puede ser de hace tres dias (la pantalla se apaga con el
     * contacto, asi que "hace tres dias" es literal). */
    if (esp_timer_get_time() - s_sd_libre_us > LIBRE_MB_VALIDEZ_US) return -1;
    return s_sd_libre_mb;
}

bool viaje_cola_ultima_es_op(const char *op)
{
    if (!op || !op[0]) return false;
    char busca[24];
    snprintf(busca, sizeof(busca), "\"op\":\"%s\"", op);

    bool es = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t cabeza, cola;
    indices_leer(&cabeza, &cola);
    nvs_handle_t h;
    if (cola > cabeza && nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        char clave[16], cuerpo[CUERPO_MAX];
        clave_de(cola - 1, clave, sizeof(clave));
        size_t len = sizeof(cuerpo);
        if (nvs_get_str(h, clave, cuerpo, &len) == ESP_OK &&
            strstr(cuerpo, busca)) es = true;
        nvs_close(h);
    }
    xSemaphoreGive(s_mutex);
    return es;
}

bool viaje_cola_descartar_cabeza(void)
{
    /* SOLO SI SIGUE ATASCADA, y todo dentro del MISMO cerrojo.
     *
     * Dos agujeros que cazo la revision del 13-oct-2026:
     *  (a) antes se soltaba el mutex para mirar si habia algo y se volvia a
     *      coger dentro de descartar_cabeza(): en ese hueco el repartidor podia
     *      sacar la cabeza X (entregada) y este descarte se llevaba por delante
     *      Y, que no habia enviado nadie;
     *  (b) el dialogo se abre porque la cola estaba atascada, pero el apunte
     *      atascado ENTRA SOLO muchas veces (es lo que dice el propio texto: "si
     *      actualizas la P4, entra solo"): al confirmar, se borraba la cabeza
     *      ACTUAL, que ya era un apunte bueno.
     * Ahora se comprueba el atasco y se avanza la cabeza en el mismo bloque, y
     * la generacion sube ANTES de tocar nada. */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t cabeza, cola;
    indices_leer(&cabeza, &cola);
    if (cabeza >= cola) {
        xSemaphoreGive(s_mutex);
        return false;
    }
    /* SOLO SI SIGUE ATASCADA. Se probo a añadir "o si esta llena" y es un
     * proxy peligroso: una cola que drena A MEDIAS (una entrega lenta, o un
     * aviso de destino que se suelta solo) sigue pareciendo llena, y el
     * descarte se llevaria por delante la cabeza NUEVA, que si se iba a
     * entregar. El caso "llena porque la P4 no contesta" tiene su propio
     * motivo de atasco (VIAJE_COLA_ATASCO_SIN_RESPUESTA), que se marca solo y
     * se limpia solo en cuanto la P4 vuelve a contestar. */
    if (s_atasco == VIAJE_COLA_ATASCO_NO) {
        xSemaphoreGive(s_mutex);
        ESP_LOGW(TAG, "el apunte de cabeza ya no esta atascado: no descarto nada");
        return false;
    }
    s_generacion++;                 /* el repartidor no debe sacar la siguiente */
    /* El borrado va DENTRO de este mismo cerrojo (ver borrar_cabeza_con_mutex). */
    borrar_cabeza_con_mutex();
    xSemaphoreGive(s_mutex);
    avisar_cambio();                /* el contador de la pantalla, ya sin cerrojo */
    /* El atasco era de ESA entrada: la marca se va con ella. Si la siguiente
     * vuelve a fallar, se volvera a marcar sola en unos segundos. */
    s_atasco = VIAJE_COLA_ATASCO_NO;
    ESP_LOGW(TAG, "apunte de cabeza DESCARTADO A MANO desde la pantalla");
    return true;
}

static void reparto_task(void *arg)
{
    (void)arg;
    char cuerpo[CUERPO_MAX];
    uint32_t idx_400 = UINT32_MAX;   /* indice de cola del ultimo 400 visto */
    int intentos_400 = 0;
    uint32_t idx_adv = UINT32_MAX;   /* indice del ultimo advisory que no entra */
    int intentos_adv = 0;
    uint32_t idx_409 = UINT32_MAX;   /* indice de cola del ultimo 409 visto */
    int intentos_409 = 0;
    int intentos_401 = 0;   /* no distingue apunte: una clave mal puesta falla con cualquiera */
    int intentos_5xx = 0;   /* idem: si la P4 no puede escribir, falla con todos */
    int intentos_sin_resp = 0;   /* idem con "no contesta" (estado == 0) */

    while (1) {
        /* LA GENERACION SE LEE ANTES DE LEER LA CABEZA, no antes de enviar.
         * Si se leyera despues de leer_cabeza(), este caso se colaba: el
         * repartidor lee la entrada A, el usuario descarta a mano (la cabeza
         * pasa a B), el repartidor captura la generacion YA NUEVA, entrega A
         * -- que efectivamente estaba en vuelo -- y al volver cree que nadie ha
         * tocado nada y saca B sin haberlo entregado. Leyendola antes, ese
         * descarte invalida la comprobacion y no se saca nada. */
        uint32_t gen = s_generacion;
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
                /* CON ESPERA A PROPOSITO (auditoria del 16-oct-2026): si
                 * indices_escribir() falla de forma persistente, la entrada se
                 * borra pero la cabeza NO avanza, asi que esta rama se repite en
                 * bucle cerrado sin dormir -- el repartidor se comia la CPU (y con
                 * ella el resto de tareas) sin avanzar nada. Con la espera, como
                 * mucho se reintenta cada REINTENTO_MS. */
                vTaskDelay(pdMS_TO_TICKS(REINTENTO_MS));
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
        /* 'gen' se capturo al principio de la vuelta (ver arriba). Si la UI ha
         * descartado algo mientras tanto, al volver NO se saca nada: la cabeza
         * ya es otra y sacarla seria perder un apunte que nadie ha entregado. */
        /* El cuerpo de la respuesta trae (si la P4 es de hoy) cuanto le queda
         * en la tarjeta: se guarda para poder avisar ANTES de que se llene. */
        char resp[96];
        bool ok = p4_api_post_resp(cuerpo, &estado, resp, sizeof(resp));
        if (ok || estado == 507) {
            int libre = parsea_libre_mb(resp);
            if (libre >= 0) {
                s_sd_libre_mb = libre;
                s_sd_libre_us = esp_timer_get_time();
                s_nvs_sin_sitio = false;
            } else {
                /* SIN SUFIJO NO SE CONSERVA LO VIEJO (revision adversarial del
                 * 14-oct-2026): si la tarjeta se llena despues de un 'inicio'
                 * (que si trae el dato), quedarse con la cifra anterior hacia
                 * que el aviso dijera "le quedan 29000 MB" con la tarjeta llena
                 * -- una mentira con pinta de dato fresco. Se olvida y punto. */
                s_sd_libre_mb = -1;
                s_nvs_sin_sitio = parsea_sin_sitio_nvs(resp);
            }
        }

        if (ok) {
            idx_400 = UINT32_MAX;
            idx_409 = UINT32_MAX;
            intentos_409 = 0;
            intentos_401 = 0;
            intentos_5xx = 0;
            intentos_sin_resp = 0;
            s_atasco = VIAJE_COLA_ATASCO_NO;
            if (s_generacion == gen) {
                descartar_cabeza();
            } else {
                /* Se entrego, pero la entrada que se envio ya no esta en la
                 * cola: no se toca. Se quedara (o no) y, como mucho, se
                 * reenviara; la P4 la reconoce por el id y contesta
                 * "duplicado". */
                ESP_LOGW(TAG, "entregado, pero la cola cambio mientras tanto: "
                              "no saco nada (la P4 lo vera como duplicado)");
            }
            /* Sin espera: si hay mas, se sigue vaciando de seguido. Que la P4
             * este respondiendo es justo el momento de aprovechar. */
            continue;
        }

        /* 429 CUENTA COMO 401 (auditoria del 14-oct-2026): el freno por IP de la
         * P4 se aplica DESPUES de fallar la comprobacion de la clave
         * (config_server_auth.c: el 429 sale por el mismo camino que el 401), asi
         * que un 429 tambien significa "la clave no vale", solo que la P4 pide
         * esperar. Antes reseteaba este contador y ADEMAS borraba el aviso, de
         * modo que una clave mal intercalada con frenos no llegaba NUNCA a los 3
         * fallos: la pantalla se quedaba sin decir "CLAVE MAL, AJUSTES" y el
         * usuario miraba la corriente de la P4 en vez de Ajustes. */
        if (estado == 401 || estado == 403 || estado == 429) {
            intentos_401++;
            if (intentos_401 >= INTENTOS_401_ATASCO && !viaje_cola_credenciales_mal()) {
                s_atasco = VIAJE_COLA_ATASCO_CLAVE;
                ESP_LOGE(TAG, "%d respuestas %s seguidas: usuario/clave del portal "
                              "mal en Ajustes, no se van a arreglar solos",
                         intentos_401, estado == 429 ? "429 (freno de la P4)" : "401");
                avisar_cambio();   /* mismo motivo que el aviso de 409 atascado */
            }
            vTaskDelay(pdMS_TO_TICKS(REINTENTO_MS));
            continue;
        }
        /* Cualquier OTRA respuesta dice que esta peticion si paso la clave (o que
         * el problema es otro), asi que el contador de credenciales vuelve a
         * cero. Ojo: un 2xx ya lo ha hecho mas arriba. */
        intentos_401 = 0;
        if (viaje_cola_credenciales_mal()) s_atasco = VIAJE_COLA_ATASCO_NO;

        if (estado == 409) {
            uint32_t cabeza, cola;
            indices_leer(&cabeza, &cola);
            if (cabeza == idx_409) {
                intentos_409++;
            } else {
                idx_409 = cabeza;
                intentos_409 = 1;
                s_atasco = VIAJE_COLA_ATASCO_NO;   /* apunte distinto: cuenta desde cero */
            }
            /* DOS CAUSAS DISTINTAS DETRAS DEL MISMO 409 (auditoria del
             * 16-oct-2026). La P4 contesta 409 tanto si el apunte es del viaje
             * ANTERIOR (y lo dice en el cuerpo: "viaje ya cerrado") como si lo
             * que rechaza es el viaje entero (ya tiene otro abierto). Antes se
             * anunciaba "hemos perdido la numeracion" para los dos, y en el caso
             * del apunte rezagado eso es FALSO: la numeracion esta bien.
             *
             * El estado que manda es el de la cabeza, asi que si ya habia uno
             * marcado y la cabeza NO ha cambiado, no se vuelve a avisar (de ahi
             * el "ya esta en ese estado"): pero si el motivo cambio -- p.ej.
             * entro un apunte rezagado detras de un viaje rechazado -- hay que
             * corregirlo, o el cartel seguiria contando la historia vieja. */
            if (intentos_409 >= INTENTOS_409_ATASCO) {
                viaje_cola_atasco_t nuevo = parsea_rezagado(resp)
                                          ? VIAJE_COLA_ATASCO_REZAGADO
                                          : VIAJE_COLA_ATASCO_NUMERACION;
                if (viaje_cola_atasco() != nuevo) {
                    s_atasco = nuevo;
                    ESP_LOGE(TAG, "409 sostenido %d veces sobre el mismo apunte: %s. "
                                  "No se pierde nada, sigue en cola, pero no entrara "
                                  "solo.",
                             intentos_409, nuevo == VIAJE_COLA_ATASCO_REZAGADO
                                 ? "es de un viaje YA CERRADO (lo dice la P4)"
                                 : "probable choque de numeracion con la P4 (se "
                                   "borro su NVS a mitad de viaje?)");
                    /* El numero de pendientes no ha cambiado (sigue siendo el
                     * mismo apunte atascado), asi que sin esto la pantalla no se
                     * enteraria hasta el siguiente cambio real de la cola. */
                    avisar_cambio();
                }
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
             * sigue intentando: en cuanto la P4 se actualice, entra solo. Y
             * ahora ADEMAS se puede descartar a mano desde la pantalla (ver
             * viaje_cola_descartar_cabeza), que es la salida para el apunte que
             * la P4 no va a aceptar nunca. */
            if (es_advisory(cuerpo) && intentos_400 >= MAX_INTENTOS_ADVISORY) {
                ESP_LOGW(TAG, "el aviso de destino no entra (la P4 no conoce esa "
                              "op: firmware viejo?). Lo SUELTO: el destino va "
                              "tambien en el cierre, y un aviso no puede bloquear "
                              "los apuntes ni el fin que vienen detras");
                descartar_cabeza();
                /* La marca de atasco era de ESE apunte (el aviso) y se va con el:
                 * la cabeza nueva es un apunte SANO y no puede nacer con la
                 * pastilla roja puesta, o "Descartarlo" -- que solo exige
                 * atasco != NO -- borraria un apunte bueno. Mismo motivo que en
                 * viaje_cola_descartar_cabeza(). */
                s_atasco = VIAJE_COLA_ATASCO_NO;
                idx_400 = UINT32_MAX;
                intentos_400 = 0;
                continue;
            }
            if (viaje_cola_atasco() != VIAJE_COLA_ATASCO_RECHAZO) {
                s_atasco = VIAJE_COLA_ATASCO_RECHAZO;
                ESP_LOGE(TAG, "la P4 rechaza el apunte con 400 tras %d intentos. "
                              "NO se descarta solo: sigue en cola y se reintenta. "
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
             * reintenta, y se puede descartar a mano desde la pantalla. Si de
             * verdad el apunte esta mal, que lo decida el usuario y no un codigo
             * de respuesta. */
            if (es_advisory(cuerpo)) {
                if (cabeza_igual(&idx_adv, &intentos_adv)) {
                    ESP_LOGW(TAG, "el aviso de destino no entra (%d): lo suelto, "
                                  "que el destino va tambien en el cierre", estado);
                    descartar_cabeza();
                    /* Ver el mismo arreglo en la rama del 400: la marca de
                     * atasco se va con el apunte que la puso (si no, la cabeza
                     * nueva, que es sana, nace con la pastilla roja y se puede
                     * borrar con "Descartarlo"). */
                    s_atasco = VIAJE_COLA_ATASCO_NO;
                    continue;
                }
            }
            if (viaje_cola_atasco() != VIAJE_COLA_ATASCO_RECHAZO) {
                s_atasco = VIAJE_COLA_ATASCO_RECHAZO;
                ESP_LOGE(TAG, "la P4 rechaza el apunte con %d. NO se descarta "
                              "solo: sigue en cola y se reintenta: %s", estado, cuerpo);
                avisar_cambio();
            }
            vTaskDelay(pdMS_TO_TICKS(REINTENTO_MS));
            continue;
        }

        /* SIN RESPUESTA (estado == 0): no se pudo ni conectar. Es lo normal con
         * la P4 apagada y se arregla solo al dar el contacto, asi que no se
         * marca al primer intento -- pero si sigue asi, el usuario tiene que
         * poder verlo y hacer sitio: con la cola llena no cabe otro apunte y
         * hasta ahora no habia ninguna salida desde la pantalla. */
        if (estado == 0) {
            if (++intentos_sin_resp >= INTENTOS_SIN_RESPUESTA_ATASCO &&
                s_atasco == VIAJE_COLA_ATASCO_NO) {
                s_atasco = VIAJE_COLA_ATASCO_SIN_RESPUESTA;
                ESP_LOGE(TAG, "la P4 no contesta (%d intentos seguidos): %lu apunte(s) "
                              "esperando. La cola se llena y no se puede anotar mas "
                              "hasta que vuelva o se aparte el mas antiguo",
                         intentos_sin_resp, (unsigned long)viaje_cola_pendientes());
                avisar_cambio();
            }
        } else {
            intentos_sin_resp = 0;
        }

        /* 5xx: la P4 esta VIVA y contesta, pero no puede escribir (507: tarjeta
         * llena o fallando; 503: ocupada). Antes esto caia en el mismo saco mudo
         * que "la P4 esta apagada" -- el usuario veia "mira si tiene corriente"
         * sobre una P4 que estaba encendida y contestando. Ver el comentario de
         * INTENTOS_5XX_ATASCO. */
        if (estado >= 500) {
            intentos_5xx++;
            if (intentos_5xx >= INTENTOS_5XX_ATASCO &&
                viaje_cola_atasco() != VIAJE_COLA_ATASCO_P4_ESCRITURA) {
                s_atasco = VIAJE_COLA_ATASCO_P4_ESCRITURA;
                ESP_LOGE(TAG, "la P4 contesta %d (%d veces seguidas): esta "
                              "encendida pero NO PUEDE ESCRIBIR. Mira su "
                              "tarjeta (llena?). Los apuntes NO se pierden: "
                              "siguen en cola.", estado, intentos_5xx);
                avisar_cambio();
            }
            vTaskDelay(pdMS_TO_TICKS(REINTENTO_MS));
            continue;
        }
        intentos_5xx = 0;

        /* Ni respuesta (estado 0: ni se pudo hablar con ella). Aqui si que es
         * "la P4 no esta". */
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

/* Cuantos apuntes se han perdido AL ARRANCAR (la cola se reinicio porque los
 * indices no cuadraban o porque cambio la capacidad). Se enseña en pantalla una
 * vez: antes solo quedaba una linea en el log y el usuario no tenia forma de
 * saber que los apuntes que vio guardados ya no estaban (auditoria del
 * 13-oct-2026, hallazgo B2). */
static int s_perdidos_arranque;

int viaje_cola_perdidos_al_arrancar(void) { return s_perdidos_arranque; }

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
        s_perdidos_arranque += (int)(cola - cabeza);
        ESP_LOGE(TAG, "se pierden %d apunte(s) sin entregar al reiniciar la cola",
                 (int)(cola - cabeza));
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
