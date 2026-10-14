/* Arnés del test de los contadores del viaje (ver viaje_contadores.sh).
 *
 * Aquí van COPIADAS, sacadas del código real por el script, las cuatro
 * asignaciones que parten los contadores en bytes sueltos (udp_tx.c de la P4) y
 * las dos que los recomponen (data_model.c de la cabina). Lo único que se
 * escribe a mano es el valor de entrada: los km del ordenador de viaje y sus
 * segundos conduciendo. */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "mini_proto.h"

/* El trozo de data_model.h que necesitamos: lo que la cabina guarda. */
typedef struct {
    uint16_t viaje_km_decimas;
    uint16_t viaje_minutos;
} destino_t;

/* El 't' del ordenador de viaje de la P4: solo los dos campos que se mandan. */
typedef struct {
    double  km;
    int64_t seconds_driving;
} viaje_t;

static mini_msg_t src;
static destino_t  dst;

/* ── CODIGO REAL DE LA P4 (udp_tx.c, build_msg) ───────────────────────────── */
static void codifica(viaje_t t)
{
    memset(&src, 0, sizeof(src));
__ENCODE__
}

/* ── CODIGO REAL DE LA CABINA (data_model.c) ──────────────────────────────── */
static void decodifica(void)
{
__DECODE__
}

static int fallos = 0, pruebas = 0;

static void caso(const char *que, double km, int64_t seg,
                 uint16_t km_dec_esperado, uint16_t min_esperado)
{
    viaje_t t = { .km = km, .seconds_driving = seg };
    codifica(t);
    decodifica();
    pruebas++;
    if (dst.viaje_km_decimas != km_dec_esperado || dst.viaje_minutos != min_esperado) {
        printf("FALLO  %-46s km=%.2f seg=%lld -> %u dec / %u min (esperado %u / %u)\n",
               que, km, (long long)seg, dst.viaje_km_decimas, dst.viaje_minutos,
               km_dec_esperado, min_esperado);
        fallos++;
    } else {
        printf("ok     %-46s %u dec, %u min\n", que, dst.viaje_km_decimas, dst.viaje_minutos);
    }
}

/* Lo que de verdad se juega este test: que el byte alto y el bajo no se crucen.
 * Si se cruzan, los dos numeros siguen "pareciendo" numeros (un viaje de 1200 km
 * se lee como 4920) y no salta nada hasta que se mira el cuaderno en casa. */
static void caso_bytes(const char *que, double km, int64_t seg)
{
    viaje_t t = { .km = km, .seconds_driving = seg };
    codifica(t);
    uint16_t km_dec = (uint16_t)((uint16_t)src.viaje_km_hi << 8 | (uint16_t)src.viaje_km_lo);
    decodifica();
    pruebas++;
    if (dst.viaje_km_decimas != km_dec) {
        printf("FALLO  %-46s los bytes del km no cuadran: %u != %u\n",
               que, dst.viaje_km_decimas, km_dec);
        fallos++;
    } else {
        printf("ok     %-46s bytes del km en orden\n", que);
    }
}

int main(void)
{
    /* Km: decimas, con redondeo al alza en el medio. */
    caso("viaje recien empezado",              0.0,    0,     0,     0);
    caso("49 m no llegan a una decima",        0.049,  0,     0,     0);
    caso("50 m ya son una decima",             0.05,   0,     1,     0);
    caso("un viaje normal",                    123.44, 0,  1234,     0);
    caso("cuatro cifras",                     1234.56, 0, 12346,     0);

    /* Minutos CONDUCIENDO: segundos / 60, truncando (un minuto empieza a los 60 s). */
    caso("un minuto justo",                  0.0,     60,     0,     1);
    caso("59 s no son un minuto",            0.0,     59,     0,     0);
    caso("2 h 35 min",                      10.0,   9300,   100,   155);
    caso("un dia seguido conduciendo",       0.0,  86400,     0,  1440);

    /* Los topes: el que importa es que la saturacion NO caiga en el centinela
     * (0xFFFF = "sin dato"), porque un viaje larguisimo se leeria como "no hay
     * viaje" justo cuando mas largo es. */
    caso("mas km de los que caben",       9999.9,      0, 65534,     0);
    caso("mas minutos de los que caben",     0.0, 99999999,  0, 65534);
    caso("km en negativo (no deberia)",     -5.0,      0,     0,     0);
    caso("segundos en negativo",             0.0,     -5,     0,     0);

    /* El centinela sigue siendo centinela... y no lo produce nunca el codificador. */
    pruebas++;
    src.viaje_km_hi = (uint8_t)(MINI_NO_DATA_U16 >> 8);
    src.viaje_km_lo = (uint8_t)(MINI_NO_DATA_U16 & 0xFF);
    src.viaje_min_hi = (uint8_t)(MINI_NO_DATA_U16 >> 8);
    src.viaje_min_lo = (uint8_t)(MINI_NO_DATA_U16 & 0xFF);
    decodifica();
    if (dst.viaje_km_decimas != MINI_NO_DATA_U16 || dst.viaje_minutos != MINI_NO_DATA_U16) {
        printf("FALLO  el centinela 0xFFFF no llega tal cual a la cabina\n");
        fallos++;
    } else {
        printf("ok     el centinela 0xFFFF (sin viaje abierto) llega tal cual\n");
    }

    /* Y el orden de los bytes, con los valores que se cruzan si se cambia. */
    caso_bytes("1200,0 km",     1200.0, 0);
    caso_bytes("1,0 km",           1.0, 0);
    caso_bytes("3000,0 km",     3000.0, 0);
    caso_bytes("400 min",          0.0, 24000);

    printf("\n%d pruebas, %d fallos\n", pruebas, fallos);
    return fallos ? 1 : 0;
}
