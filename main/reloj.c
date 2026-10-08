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

static const char *TAG = "reloj";

/* Los escribe rx_task y los lee la UI: dos tareas. El portMUX es el mismo
 * patron que data_model.c, y por el mismo motivo -- leer los dos campos por
 * separado mientras el otro los cambia daria una hora imposible. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_epoch;       /* ultimo epoch_local visto, 0 = nunca */
static int64_t  s_epoch_us;    /* esp_timer en ese instante */

void reloj_set_desde_p4(uint32_t epoch_local)
{
    if (epoch_local == 0) return;          /* la P4 aun no sabe la hora */
    /* La P4 ya filtra esto en el envio (udp_tx.c: solo manda epoch_local si
     * ahora > MINI_EPOCH_VALIDO), pero es el otro firmware, por UDP -- no
     * fiarse ciegamente de ese invariante aqui tambien. Un epoch absurdo
     * (1970 y pico, si algun dia cambia el filtro del emisor o hay un bug de
     * protocolo) se trata igual que "sin hora": mejor marcar aproximada la
     * hora de un apunte que sellarla con una fecha imposible. Detectado por
     * el usuario el 09-sep-2026. */
    if (epoch_local < (uint32_t)MINI_EPOCH_VALIDO) {
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
    if (out) *out = e + (uint32_t)((ahora_us - eu) / 1000000);
    return true;
}
