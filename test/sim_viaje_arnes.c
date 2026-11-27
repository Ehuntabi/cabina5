/* sim_viaje_arnes.c - la vida de una salida, de punta a punta y sin placa.
 *
 * POR QUE EXISTE. Las piezas del cuaderno de viaje ya tienen sus pruebas por
 * separado: test/viaje_cola_integracion_arnes.c prueba que la cola y el diario
 * no se desordenen, test/viaje_diario_arnes.c prueba el formato de la flash y
 * test/reloj_cambio_hora.sh prueba la correccion del cambio de hora. Lo que no
 * habia era una prueba que juntase el ESTADO de la salida (main/salida.c) con
 * la COLA (main/net/viaje_cola.c) y el DIARIO (main/net/viaje_diario.c) y
 * recorriese lo que de verdad le pasa a una salida: se abre, se declaran cuatro
 * cosas a la vez, se cierra el contacto, se vuelve, se cierran en cualquier
 * orden y al final se cierra la salida. Los fallos que se buscan son los de
 * esa union: ids que se pierden al reiniciar, un quinto evento que se cuela, un
 * apunte contado dos veces al reintentar, o el fin de la salida adelantando a
 * los apuntes que aun no han llegado a la P4.
 *
 * COMO. Se compilan TAL CUAL los ficheros de produccion:
 *     main/salida.c
 *     main/reloj.c
 *     main/net/viaje_cola.c
 *     main/net/viaje_diario.c   (con -DVIAJE_DIARIO_TEST: flash en RAM)
 *
 * Del resto se dobla lo de fuera: la NVS y FreeRTOS (test/dobles/dobles.c), el
 * POST a la P4 (test/dobles/dobles.c), el log y el reloj de la placa
 * (esp_timer_get_time, en dobles.c). main/reloj.c SI se compila de verdad y no
 * se dobla: el escenario 8 depende de su correccion (B19), y arrancarlo con su
 * propio estado es mas fiel que copiar la cuenta en el arnes. El reloj se
 * maneja desde aqui con reloj_set_desde_p4(), que es lo mismo que hace rx_task
 * en la placa. config_storage.c NO se compila (necesita esp_mac.h y media NVS
 * real); las funciones de almacenamiento que usa salida.c se doblan al final
 * de este fichero, con la misma firma que
 * components/config_storage/config_storage.h.
 *
 * LA CAPA DE UI (main/ui/view_registro.c) TAMPOCO SE COMPILA. El arnes
 * reproduce su secuencia al cerrar un evento: coge el id reservado al abrirlo,
 * marca "contado", empuja el cuerpo y borra el evento; y monta los cuerpos a
 * mano con la misma pinta que apunte.c. Lo que se prueba de verdad es salida.c,
 * viaje_cola.c y viaje_diario.c; la UI solo se imita.
 *
 * LOS LOGS DE PRODUCCION VAN A stderr (test/dobles/esp_log.h), salvo los de
 * viaje_diario.c en modo -DVIAJE_DIARIO_TEST, que su rama de test manda a
 * stdout (asi lo hace tambien test/viaje_cola_integracion.sh). El informe de
 * aqui va a stdout.
 *
 * NADA DE ESTE ARNES "ARREGLA" EL FIRMWARE. Si un caso sale en FALLO, el
 * resumen lo cuenta y el test termina con codigo 1: el fallo es del codigo de
 * produccion y se reporta, no se maquilla.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config_storage.h"
#include "dobles.h"
#include "reloj.h"
#include "salida.h"
#include "viaje_cola.h"
#include "viaje_diario.h"

/* Hora de partida de todos los escenarios: 2026-11-26 10:00 en hora local de
 * la P4. Lejos de la medianoche a proposito, para que los segundos que va
 * sumando el reloj de mentira (uno por llamada, ver test/dobles/dobles.c) no
 * cambien de dia y la carpeta esperada no baile. */
#define EPOCH_BASE   1795687200u

/* Desfases en cuartos de hora, como los manda la P4 (ver reloj.h). */
#define CEST 8   /* verano: +02:00 */
#define CET  4   /* invierno: +01:00 */

static int fallos;

static void ok(bool cond, const char *que)
{
    if (cond) {
        printf("ok     %s\n", que);
    } else {
        printf("FALLO  %s\n", que);
        fallos++;
    }
}

/* --- Doble de config_storage ----------------------------------------------
 *
 * salida.c guarda su estado con estas funciones (ver config_storage.h). El
 * blob es opaco para el arnes: se guarda tal cual y se devuelve tal cual. Vive
 * en RAM y SOBREVIVE a salida_init()/viaje_cola_init(), que es exactamente lo
 * que hace falta para el corte de corriente del escenario 6. Se borra con
 * config_dobles_borra(), que simula una NVS de estreno. */
#define BLOB_MAX 512

static uint8_t  s_blob[BLOB_MAX];
static size_t   s_blob_len;
static bool     s_blob_hay;

static int8_t   s_offs[1 + SALIDA_EVENTOS_MAX];
static size_t   s_offs_len;
static bool     s_offs_hay;

static uint32_t s_vida;
static uint32_t s_seq;
static uint32_t s_trip_eventos;
static int      s_trip_incs;

static void config_dobles_borra(void)
{
    memset(s_blob, 0, sizeof s_blob);
    s_blob_len = 0;
    s_blob_hay = false;
    memset(s_offs, 0, sizeof s_offs);
    s_offs_len = 0;
    s_offs_hay = false;
    s_vida = 0;
    s_seq = 0;
    s_trip_eventos = 1;   /* save_trip_inicio() deja el inicio contado */
    s_trip_incs = 0;
}

esp_err_t load_salida_blob(void *out, size_t *len)
{
    if (!out || !len) return ESP_ERR_INVALID_ARG;
    if (!s_blob_hay) return ESP_ERR_NVS_NOT_FOUND;
    if (*len < s_blob_len) {
        /* Como nvs_get_blob de verdad: dice el tamano real y no copia nada. */
        *len = s_blob_len;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out, s_blob, s_blob_len);
    *len = s_blob_len;
    return ESP_OK;
}

esp_err_t save_salida_blob(const void *data, size_t len)
{
    if (!data || len == 0 || len > sizeof s_blob) return ESP_ERR_INVALID_ARG;
    memcpy(s_blob, data, len);
    s_blob_len = len;
    s_blob_hay = true;
    return ESP_OK;
}

esp_err_t load_salida_offs(int8_t *out, size_t n)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_offs_hay || s_offs_len != n) return ESP_ERR_NVS_NOT_FOUND;
    memcpy(out, s_offs, n);
    return ESP_OK;
}

esp_err_t save_salida_offs(const int8_t *offs, size_t n)
{
    if (!offs || n > sizeof s_offs) return ESP_ERR_INVALID_ARG;
    memcpy(s_offs, offs, n);
    s_offs_len = n;
    s_offs_hay = true;
    return ESP_OK;
}

uint32_t load_salida_vida(void)
{
    return s_vida;
}

esp_err_t save_salida_vida(uint32_t epoch_local)
{
    s_vida = epoch_local;
    return ESP_OK;
}

uint32_t next_trip_seq(void)
{
    return ++s_seq;
}

uint32_t trip_eventos_inc(void)
{
    s_trip_incs++;
    return ++s_trip_eventos;
}

bool trip_eventos_get_ok(uint32_t *out)
{
    if (out) *out = s_trip_eventos;
    return true;
}

/* --- Utilidades del arnes ------------------------------------------------- */

static size_t cola(void)   { return viaje_cola_pendientes(); }
static size_t diario(void) { return viaje_diario_pendientes(); }

static void cb_pendientes(size_t pendientes)
{
    (void)pendientes;
}

/* Lector propio del id: lo que se comprueba es QUE ha salido, asi que no se
 * usa viaje_diario_id_de() para leerlo (seria juzgar con la misma pieza que se
 * esta probando). Mismo criterio que viaje_cola_integracion_arnes.c. */
static uint32_t id_en(const char *cuerpo)
{
    if (!cuerpo) return 0;
    const char *p = strstr(cuerpo, "\"id\":");
    if (!p) return 0;
    p += 5;
    uint32_t v = 0;
    bool hay = false;
    while (*p >= '0' && *p <= '9') {
        v = v * 10u + (uint32_t)(*p - '0');
        p++;
        hay = true;
    }
    return hay ? v : 0;
}

static bool cuerpo_es_op(const char *cuerpo, const char *op)
{
    if (!cuerpo || !op) return false;
    char busca[24];
    snprintf(busca, sizeof busca, "\"op\":\"%s\"", op);
    return strstr(cuerpo, busca) != NULL;
}

/* El valor de "minutos" de un cuerpo de apunte, o -1 si no esta. Se usa en el
 * escenario del cambio de hora para mirar lo que de verdad se manda. */
static long minutos_en(const char *cuerpo)
{
    if (!cuerpo) return -1;
    const char *p = strstr(cuerpo, "\"minutos\":");
    if (!p) return -1;
    p += 10;
    bool neg = false;
    if (*p == '-') { neg = true; p++; }
    long v = 0;
    bool hay = false;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
        hay = true;
    }
    if (!hay) return -1;
    return neg ? -v : v;
}

/* Cuerpo de un registro, con la misma pinta que monta apunte.c. "minutos" va
 * siempre para poder comprobar la duracion de los eventos. */
static void cuerpo_registro(char *out, size_t n, uint32_t id, const char *tipo,
                            long minutos)
{
    snprintf(out, n,
             "{\"op\":\"registro\",\"id\":%lu,\"tipo\":\"%s\",\"ts\":%lu,"
             "\"datos\":{\"minutos\":%ld}}",
             (unsigned long)id, tipo, (unsigned long)EPOCH_BASE, minutos);
}

static void cuerpo_fin(char *out, size_t n, uint32_t id, uint32_t eventos)
{
    snprintf(out, n,
             "{\"op\":\"fin\",\"id\":%lu,\"eventos\":%lu,\"destino\":\"Casa\"}",
             (unsigned long)id, (unsigned long)eventos);
}

/* Indice actual del evento con ese id, o -1. Los indices BAJAN al cerrar
 * eventos, asi que el arnes busca por id antes de cada cierre: es lo mismo que
 * hace la pantalla cuando repinta la lista entre un cierre y el siguiente. */
static int indice_de_id(uint32_t id)
{
    for (int i = 0; i < salida_eventos_abiertos(); i++) {
        const salida_evento_t *e = salida_evento_en(i);
        if (e && e->id == id) return i;
    }
    return -1;
}

/* Cuantos minutos lleva abierto el evento, con la MISMA correccion que usa la
 * pantalla (view_registro.c): al inicio se le suma salida_delta_epoch(), que es
 * lo que arregla el salto de hora. Devuelve 0 si el reloj no da hora o si la
 * resta sale al reves (la guarda que evita los "71582778 minutos"). */
static long minutos_desde(uint32_t epoch_ini)
{
    uint32_t ahora = 0;
    if (!reloj_ahora(&ahora)) return 0;
    uint32_t ini_corr = (uint32_t)((int32_t)epoch_ini + salida_delta_epoch(epoch_ini));
    if (ahora <= ini_corr) return 0;
    return (long)((ahora - ini_corr) / 60u);
}

/* Cierra un evento como lo hace la pantalla: coge el id reservado al abrirlo,
 * cuenta el apunte UNA sola vez (contado), lo empuja a la cola/diario y borra
 * el evento. Si el push falla, el evento NO se borra (la pantalla lo deja
 * abierto para reintentar). */
static bool cerrar_evento(uint32_t id, const char *tipo, long *minutos_out)
{
    const int idx = indice_de_id(id);
    if (idx < 0) return false;
    const salida_evento_t *ev = salida_evento_en(idx);
    if (!ev) return false;

    const long minutos = minutos_desde(ev->epoch_ini);
    char b[CUERPO_MAX];
    cuerpo_registro(b, sizeof b, ev->id, tipo, minutos);

    if (!ev->contado) {
        salida_evento_marcar_contado(idx);
        trip_eventos_inc();
    }

    viaje_cola_error_t motivo = VIAJE_COLA_ERR_NINGUNO;
    if (!viaje_cola_push(b, &motivo)) return false;
    salida_evento_borrar(idx);

    if (minutos_out) *minutos_out = minutos;
    return true;
}

/* Deja correr al repartidor hasta que no quede nada pendiente. xTaskCreate no
 * crea la tarea (test/dobles/dobles.c): la llamamos nosotros. El tope es para
 * que un fallo se vea como FALLO y no como un test colgado. */
static void reparto_hasta_vaciar(void)
{
    for (int i = 0; i < 20; i++) {
        dobles_tarea_pasada();
        if (cola() == 0 && diario() == 0) return;
    }
    ok(false, "el repartidor no ha terminado de vaciar (posible bucle)");
}

static void reparto_una_vuelta(void)
{
    dobles_tarea_pasada();
}

/* Arranca el mundo desde cero: flash del diario y NVS de la cola vacias, la
 * P4 contestando 200, el estado de la salida borrado y el reloj en hora. La
 * salida NO se abre aqui: cada escenario decide cual abre. */
static void mundo_nuevo(void)
{
    viaje_diario_test_borra_todo();
    dobles_nvs_borra();
    dobles_p4_limpia();
    config_dobles_borra();
    reloj_set_desde_p4(EPOCH_BASE, CEST);
    salida_init();
    viaje_cola_init(cb_pendientes);
}

/* Reinicio de la placa SIN borrar nada: es el corte de corriente del escenario
 * 6. Se vuelve a montar la salida (lee su blob de la NVS doblada) y la cola
 * (vuelve a montar el diario desde la flash doblada). Mismo orden que
 * main_app.c: salida_init() y DESPUES viaje_cola_init(), para que la tarea que
 * queda registrada en el doble de FreeRTOS sea la del repartidor. */
static void reiniciar_placa(void)
{
    salida_init();
    viaje_cola_init(cb_pendientes);
}

static bool ids_distintos(const uint32_t *ids, size_t n)
{
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++)
            if (ids[i] == ids[j]) return false;
    return true;
}

/* Cuantas entradas del diario llevan ese id. */
static int cuenta_id_en_diario(uint32_t id)
{
    int n = 0;
    char leido[CUERPO_MAX];
    uint32_t id_leido = 0;
    const size_t total = viaje_diario_pendientes();
    for (size_t k = 0; k < total; k++) {
        if (viaje_diario_en(k, leido, sizeof leido, &id_leido) && id_leido == id)
            n++;
    }
    return n;
}

/* Abre los cuatro eventos del escenario: parada, repostaje, pernocta de pago y
 * peaje. Devuelve los ids en 'ids' (0 si alguno no se pudo abrir). */
static void abrir_cuatro_eventos(uint32_t ids[4])
{
    ids[0] = salida_evento_abrir(EV_PARADA, MOTIVO_VISITA, 0);
    ids[1] = salida_evento_abrir(EV_REPOSTAJE, 0, 0);
    ids[2] = salida_evento_abrir(EV_PARADA, MOTIVO_PERNOCTA, SITIO_CAMPING);
    ids[3] = salida_evento_abrir(EV_PEAJE, 0, 0);
}

int main(void)
{
    char b[CUERPO_MAX];
    char leido[CUERPO_MAX];

    /* --- 0. Arranque en frio --------------------------------------------- */
    printf("=== 0. Arranque en frio, sin hora todavia ===\n");
    viaje_diario_test_borra_todo();
    dobles_nvs_borra();
    dobles_p4_limpia();
    config_dobles_borra();
    salida_init();
    viaje_cola_init(cb_pendientes);
    ok(!salida_hay(), "arranca sin salida en curso");
    ok(dobles_tarea_hay(), "la cola ha registrado su tarea de reparto");
    ok(cola() == 0 && diario() == 0, "cola y diario vacios");

    /* --- 1. Abrir la salida ---------------------------------------------- */
    printf("\n=== 1. Abrir una salida con nombre; sin hora no se abre ===\n");
    ok(!salida_abrir_viaje("Ruta del Norte"),
       "SIN hora de la P4 no se abre la salida");
    ok(!salida_hay(), "y no queda salida en curso");
    ok(salida_get()->tipo == SALIDA_NINGUNA, "el tipo sigue siendo NINGUNA");

    reloj_set_desde_p4(EPOCH_BASE, CEST);   /* la P4 ya manda hora */
    ok(salida_abrir_viaje("Ruta del Norte"), "con hora SI se abre la salida");
    {
        const salida_vista_t *v = salida_get();
        ok(v->tipo == SALIDA_VIAJE, "el tipo es SALIDA_VIAJE");
        ok(strcmp(v->nombre, "Ruta del Norte") == 0, "el nombre se guarda tal cual");
        ok(strcmp(v->carpeta, "20261126_Ruta_del_Norte") == 0,
           "la carpeta es AAAAMMDD_nombre (mismo orden que la P4)");
        ok(v->epoch_ini >= EPOCH_BASE && v->epoch_ini <= EPOCH_BASE + 10,
           "la hora de inicio es la de la P4");
        ok(v->n_eventos == 0, "nace sin eventos abiertos");
        ok(parada_sitio_es_de_pago(SITIO_CAMPING),
           "el camping cuenta como sitio de pago");
    }

    /* --- 2. Cuatro eventos a la vez, cada uno distinto -------------------- */
    printf("\n=== 2. Cuatro eventos abiertos a la vez y cada uno se puede cerrar ===\n");
    mundo_nuevo();
    ok(salida_abrir_viaje("Cuatro a la vez"), "se abre la salida");
    {
        uint32_t ids[4];
        abrir_cuatro_eventos(ids);
        ok(ids[0] != 0 && ids[1] != 0 && ids[2] != 0 && ids[3] != 0,
           "los cuatro eventos reciben id");
        ok(ids_distintos(ids, 4), "y los cuatro ids son distintos");
        ok(salida_eventos_abiertos() == 4, "quedan 4 eventos abiertos");

        const salida_evento_t *e;
        e = salida_evento_en(0);
        ok(e && e->tipo == EV_PARADA && e->sub == MOTIVO_VISITA,
           "el primero es una parada por visita");
        e = salida_evento_en(1);
        ok(e && e->tipo == EV_REPOSTAJE, "el segundo es un repostaje");
        e = salida_evento_en(2);
        ok(e && e->tipo == EV_PARADA && e->sub == MOTIVO_PERNOCTA &&
           e->sub2 == SITIO_CAMPING,
           "el tercero es una pernocta en camping (sitio de pago)");
        e = salida_evento_en(3);
        ok(e && e->tipo == EV_PEAJE, "el cuarto es un peaje");

        /* Cada uno se cierra: al cerrar, sale su apunte y el evento desaparece. */
        ok(cerrar_evento(ids[0], "parada", NULL), "se cierra la parada");
        ok(salida_eventos_abiertos() == 3, "y quedan 3 abiertos");
        ok(cerrar_evento(ids[1], "repostaje", NULL), "se cierra el repostaje");
        ok(salida_eventos_abiertos() == 2, "y quedan 2 abiertos");
        ok(cerrar_evento(ids[2], "pernocta", NULL), "se cierra la pernocta");
        ok(salida_eventos_abiertos() == 1, "y queda 1 abierto");
        ok(cerrar_evento(ids[3], "peaje", NULL), "se cierra el peaje");
        ok(salida_eventos_abiertos() == 0, "y no queda ninguno abierto");
        ok(diario() == 4 && cola() == 4, "los 4 apuntes quedan en cola y diario");
        for (int k = 0; k < 4; k++)
            ok(cuenta_id_en_diario(ids[k]) == 1,
               "el id de cada evento esta UNA vez en el diario");
    }

    /* --- 3. Overflow ------------------------------------------------------ */
    printf("\n=== 3. Un quinto evento con los cuatro ocupados se rechaza ===\n");
    mundo_nuevo();
    ok(salida_abrir_viaje("Overflow"), "se abre la salida");
    {
        uint32_t ids[4];
        uint32_t guardados[4];
        uint8_t tipos[4];
        uint32_t quinto;
        abrir_cuatro_eventos(ids);
        ok(salida_eventos_abiertos() == 4, "los cuatro ocupan la cola");

        /* Foto de los cuatro ANTES del intento, para comparar despues. */
        for (int k = 0; k < 4; k++) {
            const salida_evento_t *e = salida_evento_en(k);
            guardados[k] = e ? e->epoch_ini : 0;
            tipos[k] = e ? e->tipo : 0xFF;
        }

        quinto = salida_evento_abrir(EV_AVERIA, 0, 0);
        ok(quinto == 0, "el quinto NO recibe id: se rechaza");
        ok(salida_eventos_abiertos() == 4, "siguen exactamente 4 abiertos");
        ok(salida_evento_en(4) == NULL, "no hay un quinto hueco ocupado");

        /* Los cuatro siguen intactos: mismo id, mismo tipo y misma hora. */
        {
            const salida_evento_t *e0 = salida_evento_en(0);
            const salida_evento_t *e1 = salida_evento_en(1);
            const salida_evento_t *e2 = salida_evento_en(2);
            const salida_evento_t *e3 = salida_evento_en(3);
            ok(e0 && e1 && e2 && e3, "los cuatro huecos siguen ocupados");
            ok(e0 && e1 && e2 && e3 &&
               e0->id == ids[0] && e1->id == ids[1] &&
               e2->id == ids[2] && e3->id == ids[3],
               "los cuatro ids siguen siendo los de antes");
            ok(e0 && e1 && e2 && e3 &&
               e0->epoch_ini == guardados[0] && e1->epoch_ini == guardados[1] &&
               e2->epoch_ini == guardados[2] && e3->epoch_ini == guardados[3],
               "y sus horas de inicio no se han tocado");
            ok(e0 && e1 && e2 && e3 &&
               e0->tipo == tipos[0] && e1->tipo == tipos[1] &&
               e2->tipo == tipos[2] && e3->tipo == tipos[3],
               "y sus tipos tampoco");
        }
    }

    /* --- 4. Cierre en orden distinto ------------------------------------- */
    printf("\n=== 4. Cierre en orden distinto al de apertura: un apunte por cierre ===\n");
    mundo_nuevo();
    ok(salida_abrir_viaje("Desorden"), "se abre la salida");
    {
        uint32_t ids[4];
        const char *tipos[4] = { "pernocta", "parada", "peaje", "repostaje" };
        uint32_t orden_real[4];
        int inc_antes;

        abrir_cuatro_eventos(ids);
        ok(salida_eventos_abiertos() == 4, "los cuatro abiertos");

        /* Se cierran: pernocta (indice 2), parada (0), peaje (3), repostaje (1).
         * Cada cierre busca el indice actual por id, como la pantalla. */
        orden_real[0] = ids[2];
        orden_real[1] = ids[0];
        orden_real[2] = ids[3];
        orden_real[3] = ids[1];

        inc_antes = s_trip_incs;
        for (int k = 0; k < 4; k++) {
            ok(cerrar_evento(orden_real[k], tipos[k], NULL),
               "se cierra un evento en orden distinto");
            ok(diario() == (size_t)(k + 1),
               "y su apunte entra en el diario");
            ok(viaje_diario_contiene(orden_real[k]),
               "el id cerrado esta pendiente en el diario");
            ok(salida_eventos_abiertos() == 3 - k,
               "el evento desaparece de la lista de abiertos");
        }
        ok(s_trip_incs == inc_antes + 4,
           "se han contado 4 eventos generados, ni uno mas");
        ok(cola() == 4 && diario() == 4, "los 4 apuntes esperan en cola y diario");

        for (int k = 0; k < 4; k++)
            ok(cuenta_id_en_diario(orden_real[k]) == 1,
               "cada cierre ha generado su apunte UNA sola vez");

        /* Y salen a la P4 en el orden en que se cerraron. */
        dobles_p4_limpia();
        dobles_p4_estado(200);
        reparto_hasta_vaciar();
        ok(dobles_p4_enviados() == 4, "la P4 recibe los 4 apuntes");
        for (int k = 0; k < 4; k++)
            ok(id_en(dobles_p4_cuerpo((size_t)k)) == orden_real[k],
               "el apunte k sale en el orden del cierre");
        for (int k = 0; k < 4; k++)
            ok(viaje_diario_test_marca_de(orden_real[k]) == 1,
               "y queda marcado ENTREGADO una vez (1)");
    }

    /* --- 5. Reintento del mismo cierre ----------------------------------- */
    printf("\n=== 5. El POST falla y se reintenta: NO se duplica el apunte ===\n");
    mundo_nuevo();
    ok(salida_abrir_viaje("Reintento"), "se abre la salida");
    {
        uint32_t id;
        int inc_antes;
        size_t enviados_fallo;
        size_t total_envios;
        bool mismo_id = true;

        id = salida_evento_abrir(EV_REPOSTAJE, 0, 0);
        ok(id != 0, "se abre el repostaje");
        inc_antes = s_trip_incs;
        ok(cerrar_evento(id, "repostaje", NULL),
           "se cierra: el apunte queda a salvo en diario y cola");
        ok(s_trip_incs == inc_antes + 1,
           "el evento se cuenta UNA vez al cerrarlo");
        ok(cola() == 1 && diario() == 1, "hay un apunte pendiente");

        /* La P4 contesta 500: el POST falla y la cola reintenta la MISMA
         * entrada. El doble deja correr 4 vueltas por pasada. */
        dobles_p4_limpia();
        dobles_p4_estado(500);
        reparto_una_vuelta();
        enviados_fallo = dobles_p4_enviados();
        ok(enviados_fallo >= 1, "la P4 ha recibido el apunte y ha fallado");
        ok(cola() == 1 && diario() == 1,
           "el apunte sigue pendiente: no se pierde al fallar el POST");
        ok(viaje_diario_contiene(id), "y sigue en el diario con su id");
        for (size_t k = 0; k < enviados_fallo; k++) {
            if (id_en(dobles_p4_cuerpo(k)) != id) mismo_id = false;
        }
        ok(mismo_id, "los reintentos llevan el MISMO id (la P4 puede deduplicar)");

        /* La P4 vuelve: el reintento bueno entrega ese mismo apunte. */
        dobles_p4_limpia();
        dobles_p4_estado(200);
        reparto_hasta_vaciar();
        ok(dobles_p4_enviados() >= 1, "la P4 recibe el reintento bueno");
        ok(id_en(dobles_p4_cuerpo(0)) == id, "y es el mismo id, no uno nuevo");
        ok(cola() == 0 && diario() == 0, "entregado: no queda nada pendiente");
        ok(s_trip_incs == inc_antes + 1,
           "el reintento NO ha vuelto a contar el evento");
        ok(viaje_diario_test_marca_de(id) == 1,
           "el diario lo marca ENTREGADO (1), no DESCARTADO ni duplicado");
        ok(cuenta_id_en_diario(id) == 0, "y no queda copia pendiente de ese id");

        /* Entre las dos fases, la P4 ha visto el mismo id mas de una vez (el
         * reintento). Eso es justo lo que la P4 deduplica: lo que NO puede
         * pasar es que haya dos ids distintos para el mismo cierre. */
        total_envios = enviados_fallo;
        for (size_t k = 0; k < dobles_p4_enviados(); k++) {
            total_envios++;
            if (id_en(dobles_p4_cuerpo(k)) != id) mismo_id = false;
        }
        ok(total_envios >= 2 && mismo_id,
           "la P4 ha visto el mismo id mas de una vez, sin duplicar el apunte");
    }

    /* 5b. El mismo cierre reintentado cuando falla el PUSH (no el POST). El
     * evento se queda abierto y la pantalla vuelve a intentarlo con el MISMO
     * id; el guard "contado" de salida.c tiene que evitar contarlo dos veces. */
    printf("\n=== 5b. Si falla el push, el evento sigue abierto y se reintenta ===\n");
    mundo_nuevo();
    ok(salida_abrir_viaje("Reintento del push"), "se abre la salida");
    {
        uint32_t id;
        int inc_antes;

        id = salida_evento_abrir(EV_BOMBONA, 0, 0);
        ok(id != 0, "se abre la bombona");
        inc_antes = s_trip_incs;

        /* Ni la flash del diario ni la NVS de la cola dejan escribir: el push
         * falla y el evento NO se puede dar por cerrado. */
        dobles_nvs_sin_espacio(true);
        viaje_diario_test_falla(true);
        ok(!cerrar_evento(id, "bombona", NULL),
           "con la NVS y el diario llenos, el cierre falla");
        ok(salida_eventos_abiertos() == 1,
           "y el evento SIGUE abierto para reintentarlo");
        ok(cola() == 0 && diario() == 0, "no se ha guardado ningun apunte");
        ok(s_trip_incs == inc_antes + 1,
           "pero el evento ya se ha contado (se cuenta antes de encolar)");

        /* Se hace sitio y se reintenta el MISMO cierre. */
        dobles_nvs_sin_espacio(false);
        viaje_diario_test_falla(false);
        ok(cerrar_evento(id, "bombona", NULL),
           "al segundo intento el cierre entra");
        ok(s_trip_incs == inc_antes + 1,
           "y NO se vuelve a contar: el reintento no suma un evento");
        ok(cola() == 1 && diario() == 1, "queda un unico apunte pendiente");
        ok(cuenta_id_en_diario(id) == 1,
           "con el id reservado al abrir, una sola vez en el diario");
        ok(id_en(dobles_nvs_cadena("q0")) == id,
           "y ese mismo id en la cola de envio");

        dobles_p4_limpia();
        dobles_p4_estado(200);
        reparto_hasta_vaciar();
        ok(dobles_p4_enviados() == 1 && id_en(dobles_p4_cuerpo(0)) == id,
           "sale una vez a la P4, con el id de siempre");
        ok(cola() == 0 && diario() == 0, "y no queda nada pendiente");
    }

    /* --- 6. Corte de corriente con 2 eventos abiertos -------------------- */
    printf("\n=== 6. Corte de corriente: 2 eventos abiertos sobreviven al reinicio ===\n");
    mundo_nuevo();
    ok(salida_abrir_viaje("Apagon"), "se abre la salida");
    {
        uint32_t id_a, id_b;
        int inc_antes;
        id_a = salida_evento_abrir(EV_PARADA, MOTIVO_DESCANSO, 0);
        id_b = salida_evento_abrir(EV_REPOSTAJE, 0, 0);
        ok(id_a != 0 && id_b != 0 && id_a != id_b, "quedan 2 eventos abiertos");

        /* El contacto se va y vuelve: NO se borra la NVS ni la flash. */
        reiniciar_placa();
        {
            const salida_vista_t *v = salida_get();
            ok(salida_hay(), "tras el reinicio sigue habiendo salida");
            ok(v->tipo == SALIDA_VIAJE, "y sigue siendo un viaje");
            ok(strcmp(v->nombre, "Apagon") == 0, "con el mismo nombre");
            ok(strcmp(v->carpeta, "20261126_Apagon") == 0, "y la misma carpeta");
            ok(v->n_eventos == 2, "con los 2 eventos AUN abiertos");
            ok(v->eventos[0].id == id_a && v->eventos[1].id == id_b,
               "y los mismos ids reservados");
            ok(v->eventos[0].tipo == EV_PARADA &&
               v->eventos[0].sub == MOTIVO_DESCANSO,
               "el primero sigue siendo la parada de descanso");
            ok(v->eventos[1].tipo == EV_REPOSTAJE,
               "el segundo sigue siendo el repostaje");
            ok(salida_delta_epoch(v->eventos[0].epoch_ini) == 0 &&
               salida_delta_epoch(v->eventos[1].epoch_ini) == 0,
               "y sus desfases horarios (B19) se han conservado en la NVS");
            ok(salida_evento_primero() &&
               salida_evento_primero()->id == id_a,
               "el orden de la cola se conserva");
        }

        inc_antes = s_trip_incs;
        ok(cerrar_evento(id_a, "parada", NULL),
           "tras el reinicio se puede cerrar el primero");
        ok(cerrar_evento(id_b, "repostaje", NULL),
           "y tambien el segundo");
        ok(salida_eventos_abiertos() == 0, "no queda ninguno abierto");
        ok(cola() == 2 && diario() == 2, "sus 2 apuntes quedan pendientes");
        ok(s_trip_incs == inc_antes + 2, "y se cuentan una vez cada uno");

        dobles_p4_limpia();
        dobles_p4_estado(200);
        reparto_hasta_vaciar();
        ok(dobles_p4_enviados() == 2, "salen los 2 a la P4");
        ok(id_en(dobles_p4_cuerpo(0)) == id_a &&
           id_en(dobles_p4_cuerpo(1)) == id_b,
           "en el orden en que se abrieron");
    }

    /* --- 7. El fin va despues de los apuntes pendientes ------------------- */
    printf("\n=== 7. Cerrar la salida con eventos abiertos: el fin va AL FINAL ===\n");
    mundo_nuevo();
    ok(salida_abrir_viaje("Cierre con abiertos"), "se abre la salida");
    {
        uint32_t ids[4];
        uint32_t id_fin;
        uint32_t n_ev = 0;
        uint32_t id_leido = 0;
        viaje_cola_error_t motivo = VIAJE_COLA_ERR_NINGUNO;

        abrir_cuatro_eventos(ids);
        /* Se cierran dos: esos son los apuntes PENDIENTES que el fin no puede
         * adelantar. Los otros dos siguen abiertos a proposito. */
        ok(cerrar_evento(ids[0], "parada", NULL), "se cierra la parada");
        ok(cerrar_evento(ids[1], "repostaje", NULL), "se cierra el repostaje");
        ok(salida_eventos_abiertos() == 2, "quedan 2 eventos ABIERTOS");
        ok(cola() == 2 && diario() == 2, "y 2 apuntes pendientes");

        ok(trip_eventos_get_ok(&n_ev), "se puede leer la cuenta de apuntes");
        id_fin = next_trip_seq();
        cuerpo_fin(b, sizeof b, id_fin, n_ev + 1);
        ok(viaje_cola_push(b, &motivo),
           "el fin se acepta aunque queden eventos abiertos");
        ok(cola() == 3 && diario() == 3,
           "y entra DETRAS de los 2 apuntes, no delante");

        ok(viaje_diario_en(0, leido, sizeof leido, &id_leido) &&
           id_leido == ids[0], "en el diario, el primero es la parada");
        ok(viaje_diario_en(1, leido, sizeof leido, &id_leido) &&
           id_leido == ids[1], "despues el repostaje");
        ok(viaje_diario_en(2, leido, sizeof leido, &id_leido) &&
           id_leido == id_fin, "y el fin, el ultimo");
        ok(id_en(dobles_nvs_cadena("q0")) == ids[0] &&
           id_en(dobles_nvs_cadena("q1")) == ids[1] &&
           id_en(dobles_nvs_cadena("q2")) == id_fin,
           "en la cola de envio tambien: apuntes y luego el fin");
        ok(viaje_cola_ultima_es_op("fin"),
           "ultima_es_op(fin) lo reconoce");

        /* La salida se cierra con los 2 eventos abiertos: es lo que hace
         * terminar_salida tras avisar de que se pierden (ver salida.h). */
        salida_cerrar();
        ok(!salida_hay(), "la salida queda cerrada");
        ok(salida_eventos_abiertos() == 0,
           "los eventos que quedaban abiertos se olvidan (comportamiento documentado)");

        dobles_p4_limpia();
        dobles_p4_estado(200);
        reparto_hasta_vaciar();
        ok(dobles_p4_enviados() == 3, "a la P4 salen 2 apuntes y el fin");
        ok(id_en(dobles_p4_cuerpo(0)) == ids[0] &&
           id_en(dobles_p4_cuerpo(1)) == ids[1],
           "primero los dos apuntes pendientes");
        ok(cuerpo_es_op(dobles_p4_cuerpo(2), "fin") &&
           id_en(dobles_p4_cuerpo(2)) == id_fin,
           "y el fin, EL ULTIMO: no adelanta a los pendientes");
        {
            bool abiertos_fuera = true;
            for (size_t k = 0; k < 3; k++) {
                const uint32_t x = id_en(dobles_p4_cuerpo(k));
                if (x == ids[2] || x == ids[3]) abiertos_fuera = false;
            }
            ok(abiertos_fuera,
               "los dos eventos que se quedaron abiertos no generan apunte");
        }
    }

    /* --- 8. Cambio de hora entre abrir y cerrar -------------------------- */
    printf("\n=== 8. Cambio de hora (+/-3600 s) entre abrir y cerrar: duracion sana ===\n");
    mundo_nuevo();
    ok(salida_abrir_viaje("Cambio de hora"), "se abre la salida");
    {
        uint32_t id_a, id_b;
        uint32_t ini_a, ini_b;
        long min_a = -1, min_b = -1;
        int delta_a, delta_b;

        /* 8a. OTONO: la hora local salta 3600 s ATRAS (CEST -> CET) despues de
         * 20 minutos reales de parada. Sin la correccion, el "ahora" queda por
         * DEBAJO del inicio guardado y la resta sale negativa (la pantalla la
         * recorta a 0); con la correccion (B19) son 20 minutos. */
        id_a = salida_evento_abrir(EV_PARADA, MOTIVO_DESCANSO, 0);
        ok(id_a != 0, "otono: se abre la parada");
        {
            const int idx_a = indice_de_id(id_a);
            ok(idx_a >= 0, "otono: el evento esta en la lista");
            ini_a = (idx_a >= 0) ? salida_evento_en(idx_a)->epoch_ini : EPOCH_BASE;
        }
        reloj_set_desde_p4((uint32_t)((int32_t)ini_a + 1200 - 3600), CET);
        delta_a = salida_delta_epoch(ini_a);
        ok(delta_a == -3600,
           "otono: el inicio de la parada se corrige -3600 s");
        ok(cerrar_evento(id_a, "parada", &min_a), "otono: se cierra la parada");
        ok(min_a >= 15 && min_a <= 25,
           "otono: la duracion sale alrededor de 20 min, no negativa ni absurda");

        /* 8b. PRIMAVERA: la hora local salta 3600 s ADELANTE (CET -> CEST),
         * tambien tras 20 minutos reales. El inicio queda 1 h por detras y sin
         * corregir la parada saldria de 80 minutos. */
        id_b = salida_evento_abrir(EV_REPOSTAJE, 0, 0);
        ok(id_b != 0, "primavera: se abre el repostaje");
        {
            const int idx_b = indice_de_id(id_b);
            ok(idx_b >= 0, "primavera: el evento esta en la lista");
            ini_b = (idx_b >= 0) ? salida_evento_en(idx_b)->epoch_ini : EPOCH_BASE;
        }
        reloj_set_desde_p4((uint32_t)((int32_t)ini_b + 1200 + 3600), CEST);
        delta_b = salida_delta_epoch(ini_b);
        ok(delta_b == 3600,
           "primavera: el inicio del repostaje se corrige +3600 s");
        ok(cerrar_evento(id_b, "repostaje", &min_b), "primavera: se cierra el repostaje");
        ok(min_b >= 15 && min_b <= 25,
           "primavera: la duracion sale alrededor de 20 min, no 80");

        ok(min_a >= 0 && min_b >= 0 && min_a < 1440 && min_b < 1440,
           "ninguna de las dos duraciones es negativa ni de dias");

        /* Y lo que de verdad ha quedado escrito en los cuerpos: "minutos" de
         * los dos apuntes, leidos del diario antes de entregarlos. */
        ok(diario() == 2, "los dos apuntes estan en el diario");
        {
            uint32_t id_leido = 0;
            char cuerpo_a[CUERPO_MAX], cuerpo_b[CUERPO_MAX];
            bool hay_a = viaje_diario_en(0, cuerpo_a, sizeof cuerpo_a, &id_leido);
            bool hay_b = viaje_diario_en(1, cuerpo_b, sizeof cuerpo_b, &id_leido);
            ok(hay_a && hay_b, "se pueden leer los dos cuerpos del diario");
            if (hay_a) {
                long m = minutos_en(cuerpo_a);
                ok(m >= 15 && m <= 25,
                   "en el cuerpo de otono, minutos son alrededor de 20");
            }
            if (hay_b) {
                long m = minutos_en(cuerpo_b);
                ok(m >= 15 && m <= 25,
                   "en el cuerpo de primavera, minutos son alrededor de 20");
            }
        }
    }

    /* ── 9. H-46 con reinicio: dos tramos con desfases distintos ──────────
     * El fallo del 25-nov: al borrar un evento se mueven los desfases de sitio;
     * si no se graban, tras un apagon el tramo que queda se cierra con el
     * desfase de otro (1 h de mas o de menos). */
    printf("\n=== 9. H-46: cerrar un tramo, reiniciar y cerrar el que queda ===\n");
    mundo_nuevo();
    reloj_set_desde_p4(EPOCH_BASE, CET);
    ok(salida_abrir_viaje("H46"), "se abre la salida en horario de invierno");
    {
        uint32_t a = salida_evento_abrir(EV_PARADA, MOTIVO_COMER, 0);
        ok(a != 0, "se abre el tramo A con CET");
        uint32_t ini_a = 0;
        { const int i = indice_de_id(a); if (i >= 0) ini_a = salida_evento_en(i)->epoch_ini; }
        /* Cambio de hora de primavera: la hora local salta 3600 s adelante. */
        reloj_set_desde_p4((uint32_t)((int32_t)ini_a + 1200 + 3600), CEST);
        uint32_t b = salida_evento_abrir(EV_REPOSTAJE, 0, 0);
        ok(b != 0, "y el tramo B ya con CEST");
        long m = -1;
        ok(cerrar_evento(a, "comida", &m) && m >= 15 && m <= 25,
           "se cierra A con ~20 min");
        reiniciar_placa();                                    /* apagon */
        ok(salida_eventos_abiertos() == 1, "tras reiniciar queda solo B");
        {   /* 20 minutos mas de reloj, ya sin cambio de hora */
            uint32_t ini_b = 0;
            const int i = indice_de_id(b);
            if (i >= 0) ini_b = salida_evento_en(i)->epoch_ini;
            reloj_set_desde_p4(ini_b + 1200, CEST);
        }
        ok(cerrar_evento(b, "repostaje", &m) && m >= 15 && m <= 25,
           "y B se cierra con ~20 min, no con 80");
        reparto_hasta_vaciar();
        ok(true, "se entrega todo");
    }

    /* ── 10. Blob de una version anterior (sin desfases) y sin la copia vieja ─
     * Es el fallo encontrado simulando: con los desfases a 0 (UTC) el tramo se
     * cerraba con 0 minutos. Con el arreglo, el punto de partida es "no se sabe"
     * y se usa el calculo de antes: la duracion NO puede salir 0. */
    printf("\n=== 10. Blob viejo sin desfases y sin copia vieja: no inventar UTC ===\n");
    mundo_nuevo();
    reloj_set_desde_p4(EPOCH_BASE, CEST);
    ok(salida_abrir_viaje("Viejo"), "se abre la salida");
    {
        uint32_t a = salida_evento_abrir(EV_AVERIA, 0, 0);
        ok(a != 0, "se abre un tramo");
        /* Se recorta el blob a lo que guardaba la version anterior (sin el
         * trailer de desfases) y se tira la copia vieja de la clave. */
        salida_test_quitar_desfases_blob();
        s_offs_hay = false;
        reiniciar_placa();
        ok(salida_hay(), "tras reiniciar sigue la salida");
        ok(salida_eventos_abiertos() == 1, "y sigue el tramo abierto");
        reloj_set_desde_p4(EPOCH_BASE + 20 * 60, CET);   /* cambio de hora */
        long m = -1;
        ok(cerrar_evento(a, "averia", &m), "se cierra el tramo");
        ok(m != 0, "la duracion NO sale 0 (no se inventa UTC)");
        ok(m > 0 && m < 90, "y sale en un rango sensato de minutos");
    }

    /* ── 11. Una copia vieja OBSOLETA ya no manda ────────────────────────
     * Antes, si se cortaba entre las dos escrituras, la clave de desfases se
     * quedaba con los valores del tramo anterior y el cierre salia 1 h largo.
     * Ahora los desfases van DENTRO del blob: la copia vieja se ignora. */
    printf("\n=== 11. Copia vieja de desfases obsoleta: no manda ===\n");
    mundo_nuevo();
    reloj_set_desde_p4(EPOCH_BASE, CET);
    ok(salida_abrir_viaje("Obsoleta"), "se abre la salida");
    {
        uint32_t a = salida_evento_abrir(EV_PEAJE, 0, 0);
        ok(a != 0, "se abre el tramo con CET");
        /* Copia vieja con OTRO desfase (8 = CEST), como quedaria si la clave se
         * hubiera quedado del tramo anterior. */
        int8_t vieja[1 + SALIDA_EVENTOS_MAX];
        memset(vieja, CEST, sizeof vieja);
        ok(save_salida_offs(vieja, sizeof vieja) == ESP_OK,
           "se deja una copia vieja con el desfase equivocado");
        uint32_t ini_a = EPOCH_BASE;
        { const int i = indice_de_id(a); if (i >= 0) ini_a = salida_evento_en(i)->epoch_ini; }
        reiniciar_placa();
        reloj_set_desde_p4((uint32_t)((int32_t)ini_a + 1200 + 3600), CEST);
        long m = -1;
        ok(cerrar_evento(a, "peaje", &m), "se cierra el tramo");
        ok(m >= 15 && m <= 25, "sale con ~20 min: manda el blob, no la copia vieja");
    }

    printf("\n%d fallo(s)\n", fallos);
    return fallos ? 1 : 0;
}
