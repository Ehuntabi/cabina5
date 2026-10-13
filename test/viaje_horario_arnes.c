/* Arnés del test del cambio de horario (ver viaje_horario.sh).
 *
 * El cuerpo de salida_noches() es el REAL de main/salida.c; aqui solo se
 * sustituye reloj_delta_horario(), que es lo que en la placa dice si ha
 * cambiado el horario mientras el tramo estaba abierto. */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

__DELTA__

__FN__

#define DIA  86400u
#define H(h) ((uint32_t)(h) * 3600u)

static int fallos = 0, pruebas = 0;
static void caso(const char *que, int delta, uint32_t ini, uint32_t fin, uint32_t esperado)
{
    g_delta = delta;
    uint32_t got = salida_noches(ini, fin);
    pruebas++;
    if (got != esperado) {
        printf("FALLO  %-42s delta=%d -> %u noches (esperado %u)\n", que, delta, got, esperado);
        fallos++;
    } else {
        printf("ok     %-42s -> %u\n", que, got);
    }
}

int main(void)
{
    /* Una pernocta normal: de las 22:00 a las 08:00 del dia siguiente = 1 noche. */
    caso("noche normal (22:00 -> 08:00)", 0, H(22), DIA + H(8), 1);
    /* La misma noche, cruzando el cambio a horario de invierno: el epoch_local
     * del inicio quedo en la escala vieja, asi que se le suma -3600. */
    caso("la misma, cruzando a invierno", -3600, H(22), DIA + H(8), 1);
    /* Y el caso que de verdad cambia el resultado: entra a las 00:30 y sale a
     * las 23:30 del mismo dia -> 0 noches; si el inicio se movio una hora atras
     * (23:30 del dia anterior) es UNA noche. */
    caso("entra y sale el mismo dia", 0, DIA + H(0) + 1800, DIA + H(23) + 1800, 0);
    caso("el mismo, con el inicio corrido", -3600, DIA + 1800, DIA + H(23) + 1800, 1);
    /* Fin antes que inicio: 0, nunca un numero absurdo. */
    caso("fin antes que inicio", 0, DIA + H(8), H(22), 0);
    caso("varios dias", 0, H(9), 3 * DIA + H(9), 3);
    printf("\n%d pruebas, %d fallos\n", pruebas, fallos);
    return fallos ? 1 : 0;
}
