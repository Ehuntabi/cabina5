/* reloj.c — la hora, con la P4 delante o sin ella.
 *
 * Esta pantalla NO tiene RTC ni pila: la hora se la da la P4 una vez por
 * segundo (epoch_local de mini_proto.h). Mientras la P4 esta ahi, basta con
 * leer el ultimo valor recibido.
 *
 * El problema es cuando NO esta, que es el caso que importa. Escenario real:
 * repostas con la P4 apagada, el apunte se queda en la cola y sale dos dias
 * despues al arrancar el motor. Si la hora la pusiera la P4 al recibirlo, ese
 * repostaje figuraria DOS DIAS TARDE. Y si se usara el ultimo epoch_local a
 * secas, figuraria en el instante en que la P4 se apago, que tampoco es.
 *
 * Asi que se guarda el ultimo epoch CON el esp_timer de ese instante, y para
 * sellar se le suma lo transcurrido desde entonces. No hace falta un reloj
 * propio: basta con recordar cuando se supo la hora por ultima vez.
 *
 * Limite conocido: esp_timer se reinicia con el aparato. Si la 3.5" arranca sin
 * la P4 a la vista, no hay de donde sacar la hora y reloj_ahora() dice que no.
 * Quien lo llame decide que hacer -- para los apuntes, marcarlos como de hora
 * aproximada; para iniciar un viaje, no dejar.
 */
#include "reloj.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "net/mini_proto.h"
#include <stdint.h>

static const char *TAG = "reloj";

/* ── COTAS DE CORDURA (auditoria del 16-oct-2026) ───────────────────────────
 *
 * Todo lo de aqui viene de la RED (la P4, por UDP), asi que ninguna cota es
 * adorno: sin ellas, un byte raro se convierte en fechas imposibles o en
 * duraciones de un millon de horas.
 *
 * EPOCH_MAXIMO: el epoch_local es uint32 y en teoria llega hasta 2106, pero un
 * valor absurdo por arriba es tan malo como el 1970 por abajo: al sumarle el
 * tiempo transcurrido (reloj_ahora) da la vuelta al contador y la hora salta
 * HACIA ATRAS, que es justo lo que dispara las duraciones negativas gigantes.
 * 2100-01-01 deja margen de sobra para la vida de esta autocaravana. */
#define EPOCH_MAXIMO          4102444800u   /* 2100-01-01 00:00, hora local */

/* UTC_OFF_QUINCE: el desfase horario en cuartos de hora. Un huso real esta
 * entre -12:00 y +14:00, o sea entre -48 y +56 cuartos. Lo de fuera es un byte
 * corrupto (o una P4 que habla otra cosa) y se ignora, como si no lo hubiera
 * mandado. */
#define UTC_OFF_QUINCE_MIN    (-48)
#define UTC_OFF_QUINCE_MAX    (56)

/* DELTA_HORARIO_MAX: tope del salto ACUMULADO que se le aplica a los tramos ya
 * abiertos. Un cambio de horario real son 1-2 h; 24 h es margen mas que
 * suficiente y evita que una P4 que fuera alternando valores raros hiciera
 * crecer esto sin fin (las duraciones se iban al garete y el propio int
 * acababa desbordando). */
#define DELTA_HORARIO_MAX     86400

/* Los escribe rx_task y los lee la UI: dos tareas. El portMUX es el mismo
 * patron que data_model.c, y por el mismo motivo -- leer los dos campos por
 * separado mientras el otro los cambia daria una hora imposible. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_epoch;       /* ultimo epoch_local visto, 0 = nunca */
static int64_t  s_epoch_us;    /* esp_timer en ese instante */

/* El desfase horario que manda la P4 (cuartos de hora con signo) y el salto
 * pendiente si ha cambiado desde la ultima vez. */
static int s_off_quince = INT8_MIN;   /* INT8_MIN = todavia no lo se */
static int s_delta_horario = 0;

/* Epoch_local en el que se detecto el ultimo cambio de horario (0 = ninguno).
 * Sirve para NO corregir los tramos que empezo DESPUES del cambio. */
static uint32_t s_epoch_cambio = 0;


int reloj_delta_horario_desde(uint32_t epoch_ini)
{
    /* EL DELTA SOLO VALE PARA TRAMOS ABIERTOS ANTES DEL CAMBIO (17-oct-2026).
     *
     * El delta existe para arreglar el INICIO de un tramo que se guardo con el
     * desfase viejo: si a ese inicio se le suma la diferencia, la resta contra
     * "ahora" sale bien. Pero se aplicaba a TODO, tambien a los tramos abiertos
     * DESPUES del cambio, cuyo inicio ya esta en la escala nueva. Medido en host
     * (arbitraje de protocolo): una parada de 1200 s abierta despues del cambio
     * se mandaba como 4800 s, y una pernocta de las 00:30 se iba al dia anterior
     * (una noche falsa, con su precio).
     *
     * Se compara el inicio del tramo con el momento del cambio: si empezo en o
     * despues, no hay nada que corregir. */
    if (s_epoch_cambio == 0 || epoch_ini >= s_epoch_cambio) return 0;
    return s_delta_horario;
}

/* ── EL DESFASE DE CADA TRAMO (B19, auditoria del 27-oct-2026) ──────────────
 *
 * El delta de arriba adivina si un tramo empezo antes o despues del cambio
 * comparando su epoch con el instante del cambio. Eso funciona en primavera y
 * FALLA en otono: al entrar en invierno la hora local da un salto ATRAS, asi que
 * el instante del cambio (escala nueva) es 3600 s MENOR que el inicio de un tramo
 * abierto justo antes (escala vieja) y la comparacion dice "empezo despues" -> no
 * corrige. Medido: una parada de 20 minutos que cruza el cambio se grababa como
 * 0 minutos, y una de varias horas salia una hora corta.
 *
 * Con la hora local no se puede desempatar, porque durante la hora siguiente al
 * cambio las dos escalas se solapan. Lo que si se sabe es CON QUE DESFASE se
 * abrio el tramo: se apunta al abrirlo (un int8, cuartos de hora) y la correccion
 * es la diferencia con el desfase de ahora. Eso es exacto en los dos sentidos y
 * no depende de comparar horas de dos escalas.
 *
 * Si el desfase del tramo no se conoce (NVS de una version anterior), se cae al
 * calculo de siempre: no empeora nada de lo que habia. */
int8_t reloj_off_quince_actual(void)
{
    return (s_off_quince == INT8_MIN) ? RELOJ_OFF_DESCONOCIDO : (int8_t)s_off_quince;
}

int reloj_delta_horario_off(uint32_t epoch_ini, int8_t off_ini)
{
    if (off_ini == RELOJ_OFF_DESCONOCIDO || s_off_quince == INT8_MIN)
        return reloj_delta_horario_desde(epoch_ini);
    return (s_off_quince - off_ini) * 900;
}

void reloj_set_desde_p4(uint32_t epoch_local, int utc_off_quince)
{
    if (epoch_local == 0) return;          /* la P4 aun no sabe la hora */

    /* ¿HA CAMBIADO EL HORARIO? (14-oct-2026). El desfase lo manda la P4 en un
     * byte que antes era relleno; una P4 vieja manda 0, y 0 es un desfase
     * legitimo (UTC), asi que solo se actua cuando el valor CAMBIA de uno
     * conocido a otro: asi una P4 vieja no dispara nada.
     *
     * SE COMPRUEBA CONTRA LOS HUSOS REALES, no contra el rango de un int8
     * (auditoria del 16-oct-2026): el llamante ya hace (int)(int8_t), asi que
     * aquel "INT8_MIN..INT8_MAX" era una condicion SIEMPRE cierta y por ahi
     * entraba cualquier byte. Lo de fuera de -12:00..+14:00 se ignora. */
    bool off_valido = (utc_off_quince >= UTC_OFF_QUINCE_MIN &&
                       utc_off_quince <= UTC_OFF_QUINCE_MAX);
    if (!off_valido) {
        ESP_LOGW(TAG, "desfase horario fuera de rango (%d cuartos): lo ignoro",
                 utc_off_quince);
    }
    /* La P4 ya filtra esto en el envio (udp_tx.c: solo manda epoch_local si
     * ahora > MINI_EPOCH_VALIDO), pero es el otro firmware, por UDP -- no
     * fiarse ciegamente de ese invariante aqui tambien. Un epoch absurdo
     * (1970 y pico, si algun dia cambia el filtro del emisor o hay un bug de
     * protocolo) se trata igual que "sin hora": mejor marcar aproximada la
     * hora de un apunte que sellarla con una fecha imposible. Detectado por
     * el usuario el 09-sep-2026.
     *
     * Y POR ARRIBA TAMBIEN (16-oct-2026): antes solo habia cota inferior, asi
     * que un epoch de 2106 y pico (byte corrupto, o una P4 con el RTC loco)
     * entraba tal cual y al sumarle el tiempo transcurrido daba la vuelta al
     * uint32: la hora saltaba al pasado y las duraciones se disparaban. */
    if (epoch_local < (uint32_t)MINI_EPOCH_VALIDO || epoch_local > EPOCH_MAXIMO) {
        ESP_LOGW(TAG, "epoch_local de la P4 fuera de rango (%lu), lo trato como sin hora",
                 (unsigned long)epoch_local);
        return;
    }
    /* El desfase y el momento del cambio se apuntan AQUI, ya validado el epoch
     * (17-oct-2026): antes se apuntaba el desfase ARRIBA, asi que un epoch
     * absurdo se descartaba pero el desfase -- y el salto acumulado -- ya se
     * habian quedado apuntados, y el siguiente epoch bueno calculaba el salto
     * contra el desfase equivocado. */
    if (off_valido) {
        /* EL SALTO SE APLICA AQUI, YA VALIDADO EL EPOCH (tanda 1 de la
         * auditoria del 18-oct-2026). Antes se aplicaba arriba, antes del
         * "if (epoch fuera de rango) return": un paquete con epoch invalido y
         * desfase nuevo dejaba el delta apuntado sin apuntar el desfase, y el
         * siguiente paquete bueno lo aplicaba OTRA vez (doble correccion). */
        if (s_off_quince != INT8_MIN && utc_off_quince != s_off_quince) {
            /* El inicio de un tramo abierto se guardo con el desfase VIEJO: para
             * que la resta salga bien hay que sumarle la diferencia. */
            int salto = (utc_off_quince - s_off_quince) * 900;
            /* CON TOPE: esto se ACUMULA en cada cambio y viene de la red. */
            int nuevo = s_delta_horario + salto;
            if (nuevo >  DELTA_HORARIO_MAX) nuevo =  DELTA_HORARIO_MAX;
            if (nuevo < -DELTA_HORARIO_MAX) nuevo = -DELTA_HORARIO_MAX;
            s_delta_horario = nuevo;
            ESP_LOGW(TAG, "ha cambiado el horario (%d -> %d cuartos): las "
                          "duraciones de los tramos ya abiertos se corrigen %d s",
                     s_off_quince, utc_off_quince, s_delta_horario);
            s_epoch_cambio = epoch_local;
        }
        s_off_quince = utc_off_quince;
    }
    int64_t ahora_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_mux);
    s_epoch    = epoch_local;
    s_epoch_us = ahora_us;
    portEXIT_CRITICAL(&s_mux);
}

bool reloj_ahora(uint32_t *out)
{
    int64_t ahora_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_mux);
    uint32_t e  = s_epoch;
    int64_t  eu = s_epoch_us;
    portEXIT_CRITICAL(&s_mux);

    if (e == 0) return false;
    /* CON TOPE (16-oct-2026): si el epoch guardado esta cerca del maximo, sumarle
     * lo transcurrido lo llevaria a un valor PEQUENO -- un tiempo en el PASADO,
     * que es lo que hace salir duraciones negativas gigantes en las paradas. Se
     * satura en EPOCH_MAXIMO en vez de dar la vuelta. */
    uint32_t extra = (uint32_t)((ahora_us - eu) / 1000000);
    if (out) *out = (extra > EPOCH_MAXIMO - e) ? EPOCH_MAXIMO : e + extra;
    return true;
}
