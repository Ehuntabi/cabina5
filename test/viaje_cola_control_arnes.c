/* Arnés del test de clasificación de la cola (ver viaje_cola_control.sh).
 *
 * El cuerpo de la función es el REAL de main/net/viaje_cola.c (lo mete el
 * script); aquí solo van los casos, copiados de como monta los cuerpos la
 * propia placa (net/p4_api.c y net/apunte.c). */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

__FN__

static const struct { const char *cuerpo; bool control; } casos[] = {
    /* Control: los dos UNICOS que pueden usar los huecos reservados. */
    {"{\"op\":\"fin\",\"id\":1699999999,\"eventos\":7,\"dev\":\"aabbccddee01\"}", true},
    {"{\"op\":\"fin\",\"id\":1699999999,\"eventos\":7,\"destino\":\"BILBAO\",\"dev\":\"aabbccddee01\"}", true},
    {"{\"op\":\"fin\",\"id\":1699999999,\"dev\":\"aabbccddee01\",\"forzar\":true}", true},
    {"{\"op\":\"descartar\",\"id\":1699999999,\"dev\":\"aabbccddee01\"}", true},
    /* NO control: el aviso de destino (advisory) y los apuntes. */
    {"{\"op\":\"destino\",\"id\":1,\"destino\":\"BILBAO\"}", false},
    /* La parada lleva dentro un campo "fin":<hora>: un strstr pelado la tomaria
     * por el cierre del viaje y le regalaria un hueco reservado. */
    {"{\"op\":\"registro\",\"id\":7,\"tipo\":\"parada\",\"ts\":1,\"aprox\":false,\"datos\":{\"fin\":1699999999,\"motivo\":\"comer\"}}", false},
    /* Y un texto del usuario que intente fabricar el needle: las comillas van
     * escapadas, asi que no lo consigue. */
    {"{\"op\":\"registro\",\"id\":8,\"tipo\":\"repostaje\",\"datos\":{\"resumen\":\"dijo \\\"op\\\":\\\"fin\\\" y colo\"}}", false},
    {"{\"op\":\"registro\",\"id\":9,\"tipo\":\"pernocta\",\"datos\":{\"nota\":\"el fin del viaje\"}}", false},
};

int main(void)
{
    int fallos = 0;
    for (unsigned i = 0; i < sizeof(casos) / sizeof(casos[0]); i++) {
        bool got = es_op_de_control(casos[i].cuerpo);
        if (got != casos[i].control) {
            printf("FALLO  control=%d esperado=%d  %.60s\n", (int)got,
                   (int)casos[i].control, casos[i].cuerpo);
            fallos++;
        } else {
            printf("ok     control=%d  %.62s\n", (int)got, casos[i].cuerpo);
        }
    }
    printf("\n%u casos, %d fallos\n", (unsigned)(sizeof(casos) / sizeof(casos[0])), fallos);
    return fallos ? 1 : 0;
}
