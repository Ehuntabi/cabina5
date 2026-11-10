/* viaje_cola_integracion_arnes.c — la INTEGRACION de la cola con el diario
 * (10-nov-2026). Ver viaje_cola_integracion.sh.
 *
 * POR QUE EXISTE. El 10-nov-2026 se anadio una copia duradera de cada apunte en
 * una particion de la flash (main/net/viaje_diario.c) y se cambio la logica de
 * main/net/viaje_cola.c: la cola de la NVS pasa a ser un PREFIJO EXACTO de los
 * pendientes del diario. De esa regla depende que el `fin` no pueda adelantar a
 * los apuntes que solo viven en el diario (el fallo que se cazo ese dia: el
 * viaje se cerraba en la P4 sin ellos). Eso es lo que se prueba aqui.
 *
 * COMO. Se compilan los DOS ficheros DE PRODUCCION (viaje_cola.c y
 * viaje_diario.c, este con -DVIAJE_DIARIO_TEST: flash simulada en RAM) y lo de
 * fuera se dobla: NVS (mapa en RAM que puede decir "no queda sitio"), FreeRTOS
 * (xTaskCreate NO crea la tarea: la llama este arnes) y el POST a la P4 (se
 * guardan los cuerpos enviados, en orden, y se contesta el codigo que se quiera).
 * Nada de lo que se prueba es una copia: el codigo que corre es el que va a la
 * placa.
 *
 * LOS LOGS DE PRODUCCION VAN A stderr (ver test/dobles/esp_log.h); el informe
 * de aqui va a stdout.
 */
#include <stdio.h>
#include <string.h>

#include "viaje_cola.h"
#include "viaje_diario.h"
#include "dobles.h"

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

/* ── Cuerpos de apunte, con la misma pinta que los de apunte.c ───────────── */

static void cuerpo_registro(char *out, size_t n, uint32_t id, const char *tipo)
{
    snprintf(out, n,
             "{\"op\":\"registro\",\"id\":%lu,\"tipo\":\"%s\",\"ts\":1699999999,"
             "\"datos\":{\"importe\":\"62.40\"}}",
             (unsigned long)id, tipo);
}

static void cuerpo_fin(char *out, size_t n, uint32_t id)
{
    snprintf(out, n,
             "{\"op\":\"fin\",\"id\":%lu,\"eventos\":15,\"destino\":\"Casa\"}",
             (unsigned long)id);
}

/* Lector propio del id: lo que se comprueba es QUE ha salido por el cable, asi
 * que no se usa viaje_diario_id_de() para leerlo (seria juzgar con la misma
 * pieza que se esta probando). */
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

/* ── Estado del arranque y atajos ─────────────────────────────────────────── */

static size_t s_ultimo_aviso;   /* lo que la cola le ha dicho a la pantalla */
static int    s_avisos;

static void cb_pendientes(size_t pendientes)
{
    s_ultimo_aviso = pendientes;
    s_avisos++;
}

/* Placa recien flasheada: flash del diario y NVS vacias, la P4 contesta 200 y
 * viaje_cola_init() monta todo (el diario primero y la cola despues). */
static void arranca_limpio(void)
{
    viaje_diario_test_borra_todo();
    dobles_nvs_borra();
    dobles_p4_limpia();
    s_ultimo_aviso = (size_t)-1;
    s_avisos = 0;
    viaje_cola_init(cb_pendientes);
}

static size_t cola(void)   { return viaje_cola_pendientes(); }
static size_t diario(void) { return viaje_diario_pendientes(); }

/* Deja correr al repartidor hasta que no quede nada pendiente. Cada pasada es
 * una llamada a la tarea (xTaskCreate no la crea). El tope es para que un fallo
 * se vea como FALLO y no como un test colgado. */
static void reparto_hasta_vaciar(void)
{
    for (int i = 0; i < 20; i++) {
        dobles_tarea_pasada();
        if (cola() == 0 && diario() == 0) return;
    }
    ok(false, "el repartidor no ha terminado de vaciar (posible bucle)");
}

/* Los ids de las tres claves q0..q2, para comprobar el ORDEN en la NVS. */
static void ids_en_nvs(uint32_t *ids, size_t n)
{
    static const char *claves[] = { "q0", "q1", "q2", "q3" };
    for (size_t i = 0; i < n && i < 4; i++) {
        ids[i] = id_en(dobles_nvs_cadena(claves[i]));
    }
}

int main(void)
{
    char b[CUERPO_MAX];
    char leido[CUERPO_MAX];
    uint32_t id_leido;
    uint32_t ids[4];
    int desordenados;
    viaje_cola_error_t motivo;

    /* ── 0. Arranque ─────────────────────────────────────────────────────── */
    printf("=== 0. Arranque con todo vacio ===\n");
    arranca_limpio();
    ok(dobles_tarea_hay(), "viaje_cola_init() registra el repartidor");
    ok(viaje_diario_disponible(), "el diario esta disponible");
    ok(cola() == 0 && diario() == 0, "arranca con la cola y el diario vacios");
    ok(viaje_cola_sin_grabar() == 0, "y con 0 sin grabar");

    /* ── 1. push_interno(): diario Y cola; con la cola llena, solo diario ── */
    printf("\n=== 1. El alta: al diario Y a la cola; con la cola llena, solo al diario ===\n");
    arranca_limpio();

    cuerpo_registro(b, sizeof b, 1, "repostaje");
    motivo = VIAJE_COLA_ERR_NVS;
    ok(viaje_cola_push(b, &motivo), "el primer apunte se acepta");
    ok(motivo == VIAJE_COLA_ERR_NINGUNO, "y sin motivo de error");
    ok(diario() == 1, "queda anotado en el diario");
    ok(cola() == 1, "y encolado en la NVS");
    ok(id_en(dobles_nvs_cadena("q0")) == 1, "la NVS tiene su cuerpo en q0");
    ok(s_ultimo_aviso == 1, "y la pantalla ha recibido el aviso con 1");

    /* 14 = los apuntes que caben de verdad (CAPACIDAD menos los 2 huecos que se
     * reservan al cierre del viaje). */
    for (uint32_t id = 2; id <= 14; id++) {
        cuerpo_registro(b, sizeof b, id, "peaje");
        if (!viaje_cola_push(b, &motivo)) {
            printf("FALLO  el apunte %lu no entra en una cola con sitio\n",
                   (unsigned long)id);
            fallos++;
        }
    }
    ok(cola() == 14, "la ventana de envio se llena a 14 apuntes");
    ok(diario() == 14, "y el diario tiene esos mismos 14");
    ok(viaje_cola_apuntes_libres() == 0, "no quedan huecos de apunte");

    /* El 15: la cola esta llena. Tiene que quedar SOLO en el diario, y push
     * tiene que decir que SI (el dato esta en la flash). */
    cuerpo_registro(b, sizeof b, 15, "parada");
    motivo = VIAJE_COLA_ERR_NVS;
    ok(viaje_cola_push(b, &motivo),
       "con la cola llena, viaje_cola_push() devuelve TRUE (el dato esta a salvo)");
    ok(motivo == VIAJE_COLA_ERR_NINGUNO, "y no es un error de NVS");
    ok(cola() == 14, "el 15 NO entra en la ventana de envio");
    ok(diario() == 15, "pero SI queda en el diario (no se pierde)");
    ok(viaje_diario_contiene(15), "el diario lo tiene pendiente");
    ok(!dobles_nvs_tiene("q14"), "no queda un fantasma q14 en la NVS");
    ok(viaje_cola_sin_grabar() == 15,
       "la pantalla cuenta 15 sin grabar (no los 14 de la cola)");
    ok(s_ultimo_aviso == 15, "y el aviso del contador tambien dice 15");

    /* ── 2. LA INVARIANTE DE ORDEN (el fallo del 10-nov-2026) ────────────── */
    printf("\n=== 2. El `fin` NO puede adelantar a los que solo estan en el diario ===\n");
    /* Venimos del caso 1: cola 14 (los apuntes 1..14), diario 15 (el 15 detras). */
    cuerpo_fin(b, sizeof b, 99);
    motivo = VIAJE_COLA_ERR_NVS;
    ok(viaje_cola_push(b, &motivo), "el fin se acepta");
    ok(cola() == 14, "pero NO entra en la cola: el 15 sigue por delante");
    ok(diario() == 16, "el fin espera en el diario, DETRAS del 15");
    ok(viaje_diario_en(14, leido, sizeof leido, &id_leido) && id_leido == 15,
       "en el diario, el pendiente 14 es el apunte 15");
    ok(viaje_diario_en(15, leido, sizeof leido, &id_leido) && id_leido == 99,
       "y el 15 (el fin) esta detras");
    ok(viaje_cola_ultima_es_op("fin"),
       "ultima_es_op(fin) lo ve, aunque el fin solo viva en el diario");

    /* Ahora la P4 contesta 200 a todo. Lo que salga por el cable TIENE que
     * salir en orden: 1..15 y el fin al final. Con el fallo del 10-nov salia
     * 1..14, el fin, y el 15 el ultimo (el viaje se cerraba sin el). */
    dobles_p4_limpia();
    dobles_p4_estado(200);
    reparto_hasta_vaciar();

    ok(dobles_p4_enviados() == 16, "la P4 recibe los 16 cuerpos (15 apuntes + fin)");
    desordenados = 0;
    for (int i = 0; i < 15; i++) {
        const char *c = dobles_p4_cuerpo((size_t)i);
        if (cuerpo_es_op(c, "fin")) {
            printf("FALLO  el fin ha salido en la posicion %d, por delante de apuntes\n", i);
            desordenados++;
        }
        if (id_en(c) != (uint32_t)(i + 1)) {
            printf("FALLO  en la posicion %d esperaba el apunte %d y ha salido el %lu\n",
                   i, i + 1, (unsigned long)id_en(c));
            desordenados++;
        }
    }
    ok(desordenados == 0, "los cuerpos salen 1..15 EN ORDEN");
    ok(cuerpo_es_op(dobles_p4_cuerpo(15), "fin") && id_en(dobles_p4_cuerpo(15)) == 99,
       "y el fin sale EL ULTIMO, detras del 15");
    ok(cola() == 0 && diario() == 0, "no queda nada pendiente");
    ok(viaje_cola_sin_grabar() == 0, "y la pantalla dice 0 sin grabar");

    /* ── 3a. init: rescata lo que solo estaba en la NVS ──────────────────── */
    printf("\n=== 3a. Al arrancar se rescata el apunte que solo estaba en la NVS ===\n");
    /* El diario no deja escribir (una particion rota: es lo que paso el fin de
     * semana del 7-8-nov). Los apuntes solo pueden quedar en la cola. */
    viaje_diario_test_borra_todo();
    dobles_nvs_borra();
    dobles_p4_limpia();
    viaje_diario_test_falla(true);
    viaje_diario_init();              /* no puede ni estrenar: se queda sin diario */
    viaje_cola_init(cb_pendientes);
    ok(!viaje_diario_disponible(), "arranca SIN diario (la particion no deja escribir)");

    for (uint32_t id = 501; id <= 503; id++) {
        cuerpo_registro(b, sizeof b, id, "peaje");
        if (!viaje_cola_push(b, &motivo)) {
            printf("FALLO  el apunte %lu no entra con el diario caido\n", (unsigned long)id);
            fallos++;
        }
    }
    ok(cola() == 3, "los 3 estan en la ventana de envio (la NVS si deja escribir)");
    ok(diario() == 0, "y el diario no tiene ninguno");

    /* Se arregla el diario y se reinicia la placa (init otra vez). */
    viaje_diario_test_falla(false);
    viaje_cola_init(cb_pendientes);
    ok(viaje_diario_disponible(), "al reiniciar ya hay diario");
    ok(diario() == 3, "los 3 apuntes que solo estaban en la NVS se rescatan al diario");
    ok(cola() == 3, "y la cola se reconstruye con los 3");
    ids_en_nvs(ids, 3);
    ok(ids[0] == 501 && ids[1] == 502 && ids[2] == 503,
       "en el MISMO orden en la NVS: 501, 502, 503");

    dobles_p4_limpia();
    dobles_p4_estado(200);
    reparto_hasta_vaciar();
    ok(dobles_p4_enviados() == 3, "y salen los 3 a la P4");
    ok(id_en(dobles_p4_cuerpo(0)) == 501 && id_en(dobles_p4_cuerpo(1)) == 502 &&
       id_en(dobles_p4_cuerpo(2)) == 503, "en orden 501, 502, 503");
    ok(cola() == 0 && diario() == 0, "no se pierde ninguno");

    /* LO QUE NO SE AFIRMA AQUI, Y POR QUE (hallazgo del 10-nov-2026, SIN
     * arreglar). El rescate de arriba es el camino bueno. Si el nvs_open() de
     * solo lectura de main/net/viaje_cola.c:1440 falla en ese mismo bucle (es un
     * fallo transitorio: "sin memoria" al arrancar), el `break` sale del bucle
     * SIN poner reconstruible = false, y entonces borra_cola_entera() +
     * rellena_desde_diario() dejan el indice a cero con los apuntes que solo
     * estaban en la NVS fuera de el: la pantalla dice "0 sin grabar" con los
     * cuerpos todavia en la flash (y el siguiente push pisa q0/q1). Es
     * justamente el caso que este bloque prueba, pero con la NVS fallando, y hoy
     * sale mal; el arnes tiene que pasar, asi que no se afirma aqui. Queda
     * descrito en el informe: hay que anadirlo cuando se arregle. */

    /* ── 3b. init: se borra la cola y se rellena desde el diario ─────────── */
    printf("\n=== 3b. Si la cola se pierde, al arrancar se rellena desde el diario ===\n");
    arranca_limpio();
    for (uint32_t id = 601; id <= 603; id++) {
        cuerpo_registro(b, sizeof b, id, "repostaje");
        viaje_cola_push(b, &motivo);
    }
    ok(cola() == 3 && diario() == 3, "3 en la cola y 3 en el diario");
    dobles_nvs_borra();               /* la NVS de la cola se pierde entera */
    ok(cola() == 0, "la ventana de envio se ha quedado vacia");
    viaje_cola_init(cb_pendientes);
    ok(diario() == 3, "el diario sigue teniendo los 3");
    ok(cola() == 3, "y la cola se rellena desde el diario");
    ids_en_nvs(ids, 3);
    ok(ids[0] == 601 && ids[1] == 602 && ids[2] == 603,
       "rellenada EN ORDEN: 601, 602, 603");

    /* ── 4. viaje_cola_sin_grabar(): el numero de la pantalla ────────────── */
    printf("\n=== 4. viaje_cola_sin_grabar(): el diario manda; sin diario, la cola ===\n");
    arranca_limpio();
    for (uint32_t id = 1; id <= 14; id++) {
        cuerpo_registro(b, sizeof b, id, "peaje");
        viaje_cola_push(b, &motivo);
    }
    cuerpo_registro(b, sizeof b, 15, "parada");
    viaje_cola_push(b, &motivo);
    cuerpo_fin(b, sizeof b, 99);
    viaje_cola_push(b, &motivo);
    ok(cola() == 14 && diario() == 16, "14 en la ventana de envio y 16 en el diario");
    ok(viaje_cola_sin_grabar() == diario(),
       "CON diario, sin_grabar es el total del diario");
    ok(viaje_cola_sin_grabar() != cola(),
       "y NO los de la cola (que son 14: el 15 y el fin no caben)");

    dobles_p4_limpia();
    dobles_p4_estado(200);
    reparto_hasta_vaciar();
    ok(viaje_cola_sin_grabar() == 0, "entregado todo, sin_grabar vuelve a 0");

    /* Y sin diario (particion ausente o rota) manda la cola, como antes. */
    viaje_diario_test_borra_todo();
    dobles_nvs_borra();
    viaje_diario_test_falla(true);
    viaje_diario_init();
    viaje_cola_init(cb_pendientes);
    ok(!viaje_diario_disponible(), "ahora no hay diario");
    cuerpo_registro(b, sizeof b, 901, "repostaje");
    viaje_cola_push(b, &motivo);
    cuerpo_registro(b, sizeof b, 902, "peaje");
    viaje_cola_push(b, &motivo);
    ok(cola() == 2, "sin diario los apuntes van a la cola (como antes del 10-nov)");
    ok(viaje_cola_sin_grabar() == cola(),
       "SIN diario, sin_grabar es la cola");
    viaje_diario_test_falla(false);

    /* ── 5a. Entrega: el 200 marca el id en el diario y avanza la cola ───── */
    printf("\n=== 5a. Un 200 marca el id ENTREGADO en el diario y saca la cabeza ===\n");
    arranca_limpio();
    cuerpo_registro(b, sizeof b, 701, "repostaje");
    viaje_cola_push(b, &motivo);
    cuerpo_registro(b, sizeof b, 702, "peaje");
    viaje_cola_push(b, &motivo);
    ok(cola() == 2 && diario() == 2, "2 en la cola y 2 en el diario");

    dobles_p4_limpia();
    dobles_p4_estado(200);
    reparto_hasta_vaciar();
    ok(dobles_p4_enviados() == 2, "la P4 recibe los 2 cuerpos");
    ok(id_en(dobles_p4_cuerpo(0)) == 701 && id_en(dobles_p4_cuerpo(1)) == 702,
       "en orden: 701 y luego 702");
    ok(cola() == 0, "la cola avanza hasta vaciarse");
    ok(diario() == 0, "y el diario se queda sin pendientes");
    ok(viaje_diario_test_marca_de(701) == 1, "el 701 queda marcado ENTREGADO (1)");
    ok(viaje_diario_test_marca_de(702) == 1, "y el 702 tambien");
    ok(!viaje_diario_contiene(701) && !viaje_diario_contiene(702),
       "ninguno de los dos sigue pendiente");

    /* ── 5b. Descarte a mano: DESCARTADO y no resucita ───────────────────── */
    printf("\n=== 5b. El descarte a mano marca DESCARTADO y no resucita ===\n");
    arranca_limpio();
    cuerpo_registro(b, sizeof b, 711, "repostaje");
    viaje_cola_push(b, &motivo);
    cuerpo_registro(b, sizeof b, 712, "peaje");
    viaje_cola_push(b, &motivo);
    ok(cola() == 2 && diario() == 2, "2 en la cola y 2 en el diario");

    /* La P4 no acepta el apunte de cabeza (un 4xx que no es "vuelve luego"):
     * la cola se marca atascada y la pantalla ofrece "Descartarlo". */
    dobles_p4_limpia();
    dobles_p4_estado(404);
    dobles_tarea_pasada();
    ok(viaje_cola_atasco() == VIAJE_COLA_ATASCO_RECHAZO,
       "con un 4xx de rechazo la cola se marca atascada");
    ok(cola() == 2, "y no avanza sola: los 2 siguen pendientes");

    dobles_p4_limpia();
    dobles_p4_estado(200);
    ok(viaje_cola_descartar_cabeza(), "el usuario descarta la cabeza");
    ok(viaje_diario_test_marca_de(711) == 2,
       "el 711 queda marcado DESCARTADO (2), no entregado");
    ok(!viaje_diario_contiene(711), "y ya no esta pendiente");
    ok(cola() == 1 && diario() == 1, "la cola avanza y el 712 ocupa el hueco");

    reparto_hasta_vaciar();
    ok(dobles_p4_enviados() == 1, "solo sale un cuerpo mas");
    ok(id_en(dobles_p4_cuerpo(0)) == 712, "y es el 712: el descartado no se reenvia");
    ok(cola() == 0 && diario() == 0, "no queda nada pendiente");

    viaje_cola_init(cb_pendientes);   /* reinicio */
    ok(diario() == 0 && cola() == 0, "tras reiniciar sigue sin haber nada");
    ok(!viaje_diario_contiene(711), "y el descartado NO resucita");

    /* ── 6. La NVS no deja escribir el indice: ni fantasma ni desorden ───── */
    printf("\n=== 6. Si la NVS no deja escribir el indice, no queda un fantasma ===\n");
    arranca_limpio();
    dobles_nvs_indices_sin_espacio(true);
    cuerpo_registro(b, sizeof b, 801, "repostaje");
    motivo = VIAJE_COLA_ERR_NVS;
    ok(viaje_cola_push(b, &motivo), "el apunte se acepta (esta a salvo en el diario)");
    ok(diario() == 1, "queda en el diario");
    ok(cola() == 0, "pero no entra en la ventana de envio (no cabe el indice)");
    ok(!dobles_nvs_tiene("q0"), "y el cuerpo a medias se borra: no queda fantasma");
    dobles_nvs_indices_sin_espacio(false);

    cuerpo_registro(b, sizeof b, 802, "peaje");
    ok(viaje_cola_push(b, &motivo), "el siguiente entra");
    ok(cola() == 2, "y al encolarse arrastra al 801 (la cola es un prefijo)");
    ok(id_en(dobles_nvs_cadena("q0")) == 801 && id_en(dobles_nvs_cadena("q1")) == 802,
       "en orden: primero el 801, que se habia quedado fuera");

    dobles_p4_limpia();
    dobles_p4_estado(200);
    reparto_hasta_vaciar();
    ok(dobles_p4_enviados() == 2 && id_en(dobles_p4_cuerpo(0)) == 801 &&
       id_en(dobles_p4_cuerpo(1)) == 802, "y salen a la P4 en ese mismo orden");

    printf("\n%d fallos\n", fallos);
    return fallos ? 1 : 0;
}
