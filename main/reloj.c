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

int reloj_delta_horario(void) { return s_delta_horario; }

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
    if (utc_off_quince >= UTC_OFF_QUINCE_MIN && utc_off_quince <= UTC_OFF_QUINCE_MAX) {
        if (s_off_quince != INT8_MIN && utc_off_quince != s_off_quince) {
            /* El inicio de un tramo abierto se guardo con el desfase VIEJO: para
             * que la resta salga bien hay que sumarle la diferencia. */
            int salto = (utc_off_quince - s_off_quince) * 900;
            /* CON TOPE, que antes no tenia: esto se ACUMULA en cada cambio y
             * viene de la red (ver DELTA_HORARIO_MAX). */
            int nuevo = s_delta_horario + salto;
            if (nuevo >  DELTA_HORARIO_MAX) nuevo =  DELTA_HORARIO_MAX;
            if (nuevo < -DELTA_HORARIO_MAX) nuevo = -DELTA_HORARIO_MAX;
            s_delta_horario = nuevo;
            ESP_LOGW(TAG, "ha cambiado el horario (%d -> %d cuartos): las "
                          "duraciones de los tramos ya abiertos se corrigen %d s",
                     s_off_quince, utc_off_quince, s_delta_horario);
        }
        s_off_quince = utc_off_quince;
    } else {
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
