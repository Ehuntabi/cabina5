/* Arnés del test de parseo del hueco de la SD (ver viaje_cola_libre.sh).
 *
 * El cuerpo de la funcion es el REAL de main/net/viaje_cola.c (lo mete el
 * script); aqui solo van los casos, copiados de lo que manda la P4 de verdad
 * (responde_con_hueco() en victron/main/portal/config_server_viaje.c). */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

__FN__

static const struct { const char *respuesta; int esperado; } casos[] = {
    /* Lo que contesta la P4 de hoy. */
    {"2026-10-14_RIBADEO libre_mb=29652", 29652},
    {"apunte guardado libre_mb=0", 0},
    {"no habia viaje abierto libre_mb=1", 1},
    /* P4 anterior al 14-oct-2026: sin sufijo, no se inventa nada. */
    {"2026-10-14_RIBADEO", -1},
    {"", -1},
    /* Basura: no se pinta. */
    {"libre_mb=", -1},
    {"libre_mb=abc", -1},
    {"libre_mb=-5", -1},
    {"libre_mb=99999999", -1},
    /* El texto del usuario no puede fabricarlo: viene DESPUES de libre_mb=. */
    {"libre_mb=12 y algo mas", 12},
};

int main(void)
{
    int fallos = 0;
    for (unsigned i = 0; i < sizeof(casos)/sizeof(casos[0]); i++) {
        int got = parsea_libre_mb(casos[i].respuesta);
        if (got != casos[i].esperado) {
            printf("FALLO  \"%s\" -> %d (esperado %d)\n", casos[i].respuesta, got,
                   casos[i].esperado);
            fallos++;
        } else {
            printf("ok     \"%-38s\" -> %d\n", casos[i].respuesta, got);
        }
    }
    printf("\n%u casos, %d fallos\n", (unsigned)(sizeof(casos)/sizeof(casos[0])), fallos);
    return fallos ? 1 : 0;
}
