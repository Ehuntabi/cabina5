/* Arnes del CAMBIO DE HORA (B19, auditoria del 27-oct-2026).
 *
 * POR QUE ESTE TEST. La correccion de un tramo que cruza el cambio de hora se
 * hacia comparando el inicio del tramo con el instante del cambio, y en el
 * cambio de OTONO eso no puede funcionar: la hora local da un salto ATRAS, asi
 * que un tramo abierto justo antes (escala vieja) tiene un epoch MAYOR que el
 * instante del cambio (escala nueva) y la comparacion dice "empezo despues" ->
 * no corrige. Medido: una parada de 20 minutos que cruza el cambio se grababa
 * como 0 minutos.
 *
 * La solucion (reloj_delta_horario_off) es apuntar con que desfase se abrio el
 * tramo y restar: (desfase de ahora - desfase al abrir) * 900. Aqui se prueba
 * esa cuenta en los dos cambios, la caida al calculo viejo cuando el desfase no
 * se conoce, y que el calculo viejo sigue dando lo que daba (para no arreglar
 * una cosa rompiendo otra).
 *
 * Como los demas tests del repo, NO se copia el codigo: se extraen las funciones
 * del fuente de verdad (test/reloj_cambio_hora.sh) y se compilan aqui. Lo que se
 * pone es el estado que en el firmware es 'static' en reloj.c. */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define RELOJ_OFF_DESCONOCIDO  INT8_MIN

/* El estado de reloj.c que estas funciones leen (alli es static). */
static int      s_off_quince    = INT8_MIN;   /* cuartos de hora; INT8_MIN = no se sabe */
static int      s_delta_horario = 0;          /* el salto acumulado desde el ultimo cambio */
static uint32_t s_epoch_cambio  = 0;          /* epoch_local del cambio (0 = ninguno) */

__FN__

static int fallos = 0;

static void comprueba(const char *titulo, int got, int esperado)
{
    if (got == esperado) {
        printf("ok     %-52s -> %+d s\n", titulo, got);
    } else {
        printf("FALLO  %-52s -> %+d s (esperado %+d)\n", titulo, got, esperado);
        fallos = 1;
    }
}

int main(void)
{
    /* Desfases en cuartos de hora: +2 h = 8, +1 h = 4, UTC = 0. */
    const int8_t CEST = 8;    /* verano */
    const int8_t CET  = 4;    /* invierno */

    /* ── 1. El cambio de OCTUBRE (CEST -> CET): el que estaba roto ───────── */
    /* Un tramo abierto a las 02:30 de verano, y ahora (tras el cambio) el
     * desfase de la P4 es de invierno. Lo abierto antes del cambio hay que
     * corregirlo en -3600 s, tenga el epoch que tenga. */
    s_off_quince = CET;
    comprueba("otono: tramo abierto antes del cambio",
              reloj_delta_horario_off(1000000, CEST), -3600);
    /* Y uno abierto DESPUES del cambio (ya con el desfase de invierno) no se
     * toca: su inicio esta en la escala buena. */
    comprueba("otono: tramo abierto despues del cambio",
              reloj_delta_horario_off(1000000, CET), 0);

    /* El epoch no interviene: da igual cual sea. */
    comprueba("otono: con cualquier epoch",
              reloj_delta_horario_off(123456789, CEST), -3600);

    /* ── 2. El cambio de MARZO (CET -> CEST): el sentido contrario ───────── */
    s_off_quince = CEST;
    comprueba("primavera: tramo abierto antes del cambio",
              reloj_delta_horario_off(1000000, CET), 3600);
    comprueba("primavera: tramo abierto despues del cambio",
              reloj_delta_horario_off(1000000, CEST), 0);

    /* ── 3. Un tramo de varios dias que cruza el cambio ─────────────────── */
    s_off_quince = CET;
    comprueba("pernocta de varios dias cruzando octubre",
              reloj_delta_horario_off(1700000000u, CEST), -3600);

    /* ── 4. Sin desfase del tramo: el calculo de antes, tal cual ────────── */
    /* (NVS de una version anterior: no se empeora lo que ya habia.) */
    s_off_quince = CEST;
    s_delta_horario = 3600;          /* como lo dejaria el cambio de marzo */
    s_epoch_cambio  = 2000;          /* el cambio fue en el epoch 2000 */
    comprueba("sin dato: tramo anterior al cambio (calculo viejo)",
              reloj_delta_horario_off(1500, RELOJ_OFF_DESCONOCIDO), 3600);
    comprueba("sin dato: tramo posterior al cambio (calculo viejo)",
              reloj_delta_horario_off(2500, RELOJ_OFF_DESCONOCIDO), 0);
    /* Sin ningun cambio visto (epoch_cambio = 0) y sin desfase del tramo: 0. */
    s_epoch_cambio  = 0;
    s_delta_horario = 0;
    comprueba("sin dato: sin haber visto ningun cambio",
              reloj_delta_horario_off(2500, RELOJ_OFF_DESCONOCIDO), 0);

    /* Y el caso que el calculo viejo NO puede resolver, para que quede escrito:
     * en octubre, un tramo de la escala vieja tiene un epoch mayor que el
     * instante del cambio, asi que el calculo viejo devuelve 0 (el fallo del
     * hallazgo). Con el desfase apuntado, sale bien. */
    s_off_quince    = CET;           /* ya estamos en invierno */
    s_delta_horario = -3600;
    s_epoch_cambio  = 1000;          /* el cambio, en la escala NUEVA */
    comprueba("el fallo original: calculo viejo en octubre",
              reloj_delta_horario_off(1500, RELOJ_OFF_DESCONOCIDO), 0);
    comprueba("B19 arreglado: mismo tramo con el desfase apuntado",
              reloj_delta_horario_off(1500, CEST), -3600);

    printf("\n%s\n", fallos ? "HAY FALLOS" : "TODO OK: el cambio de hora cuadra");
    return fallos;
}
