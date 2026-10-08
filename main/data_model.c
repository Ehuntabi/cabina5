#include "data_model.h"
#include "net/mini_proto.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

static mini_data_t s_data;
static portMUX_TYPE s_data_mux = portMUX_INITIALIZER_UNLOCKED;

void data_model_init(void) {
    portENTER_CRITICAL(&s_data_mux);
    memset(&s_data, 0, sizeof(s_data));
    portEXIT_CRITICAL(&s_data_mux);

    /* Sin datos demo: arrancamos con has_data=false (todo lo puso el memset)
     * -> la UI muestra "--" en todas las cards hasta que llegue el primer
     * mini_msg real por UDP desde la P4. */
}

/* Con el modo captura activo (capture_carousel.c) la pantalla tiene que enseñar
 * los datos simulados, y la P4 los pisa cada segundo con los suyos. Mientras
 * este puesto, se ignora lo que llega por UDP. En produccion nadie lo activa:
 * solo lo llama data_model_set_simulated(). 30-sep-2026. */
static bool s_simulado;

void data_model_update_from_msg(const struct mini_msg *msg)
{
    if (s_simulado) return;   /* modo captura: mandan los datos simulados */
    if (!msg || msg->version != MINI_PROTO_VERSION) return;

    /* Construir el snapshot completo en una copia local y solo entrar en
     * la seccion critica para el swap final: la seccion critica queda
     * minima y no bloquea al lector (LVGL) mas de lo estrictamente
     * necesario. */
    mini_data_t tmp;
    portENTER_CRITICAL(&s_data_mux);
    tmp = s_data;
    portEXIT_CRITICAL(&s_data_mux);

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    /* Shunt, CADA CAMPO POR SEPARADO ("NA por campo", auditoria del
     * 23-sep-2026). La P4 pone el centinela solo en el campo que no tiene: lo
     * normal es que el SoC venga NA mientras el SmartShunt sincroniza, con el
     * voltaje y la corriente ya buenos. Antes se miraba SOLO el SoC y, si
     * venia NA, se tiraban los tres: la pantalla ponia "--" en V y en I aunque
     * los tuviera. El caso "no hay nada" sigue cubierto: si el shunt no esta
     * fresco, la P4 manda los tres centinelas y aqui los tres quedan
     * invalidos (es el else que faltaba y se arreglo el 24-ago-2026: sin el,
     * has_data se quedaba en true para siempre y la pantalla seguia ensenando
     * el ultimo porcentaje con el SmartShunt apagado). */
    tmp.soc_valido = (msg->shunt_soc_deci != MINI_NO_DATA_I16);
    tmp.v_valido   = (msg->shunt_voltage_centi != MINI_NO_DATA_I16);
    tmp.i_valido   = (msg->shunt_current_milli != MINI_NO_DATA_I32);
    tmp.has_data   = tmp.soc_valido || tmp.v_valido || tmp.i_valido;
    tmp.shunt_soc_deci      = msg->shunt_soc_deci;      /* el centinela se guarda tal cual */
    tmp.shunt_voltage_centi = msg->shunt_voltage_centi;
    tmp.shunt_current_milli = msg->shunt_current_milli;
    /* P = V * I -> centi V * milli A / 100000 = W, signo conservado. Solo si
     * hay los dos campos; si no, 0 (nadie la pinta hoy, pero que no sea un
     * numero inventado). */
    tmp.shunt_power_w = (tmp.v_valido && tmp.i_valido)
        ? (int32_t)((int64_t)msg->shunt_voltage_centi *
                    msg->shunt_current_milli / 100000)
        : 0;

    /* Aux del SmartShunt = bateria de arranque/motor. */
    if (msg->aux_input != MINI_NO_DATA_U8) {
        tmp.aux_value_raw = msg->aux_value_raw;
        tmp.aux_input     = msg->aux_input;
        tmp.aux_has_data  = true;
    } else {
        tmp.aux_has_data  = false;
    }

    /* DC/DC — "sin dato" si ambos voltajes vienen NO_DATA. */
    if (msg->dcdc_v_in_centi != MINI_NO_DATA_I16 ||
        msg->dcdc_v_out_centi != MINI_NO_DATA_I16) {
        tmp.dcdc_v_in_centi  = msg->dcdc_v_in_centi;
        tmp.dcdc_v_out_centi = msg->dcdc_v_out_centi;
        tmp.dcdc_state       = msg->dcdc_state;
        tmp.dcdc_has_data    = true;
    } else {
        tmp.dcdc_has_data    = false;
    }

    /* Frigo + ventilador */
    if (msg->frigo_temp_centi != MINI_NO_DATA_I16) {
        tmp.frigo_temp_centi = msg->frigo_temp_centi;
        tmp.frigo_fan_pct    = msg->frigo_fan_pct;
        tmp.frigo_has_data   = true;
    } else {
        tmp.frigo_has_data   = false;
    }

    /* Aguas: cada tanque se evalua por separado -- una trama nativa NE185
     * (sin NE187) trae limpia valida pero grises siempre a "sin dato" (ver
     * ne185.c), y con un unico flag combinado eso apagaba tambien el bloque
     * de limpia. */
    if (msg->water_clean != MINI_NO_DATA_U8) {
        tmp.water_clean          = msg->water_clean;
        tmp.water_clean_has_data = true;
    } else {
        tmp.water_clean_has_data = false;
    }
    if (msg->water_gray != MINI_NO_DATA_U8) {
        tmp.water_gray          = msg->water_gray;
        tmp.water_gray_has_data = true;
    } else {
        tmp.water_gray_has_data = false;
    }

    /* Exterior */
    if (msg->exterior_temp_centi != MINI_NO_DATA_I16) {
        tmp.exterior_temp_centi = msg->exterior_temp_centi;
        tmp.exterior_has_data   = true;
    } else {
        tmp.exterior_has_data   = false;
    }

    /* Reloj de la P4. Se copia tal cual, incluido el 0 = "aun no tiene hora
     * buena": el que lo use ya distingue. */
    tmp.epoch_local = msg->epoch_local;
    tmp.gps_estado  = msg->gps_estado;

    /* Alarmas activas de la P4 (bitmask). Se copia tal cual, incluido el 0 =
     * "ninguna": aqui no hay sentinel de "sin dato" porque el byte siempre
     * viene con un valor valido, y quien lo pinte tiene que mirar ademas que el
     * enlace este fresco (last_update_ms), no este campo. */
    tmp.alarmas = msg->alarmas;

    tmp.last_update_ms = now;

    portENTER_CRITICAL(&s_data_mux);
    s_data = tmp;
    portEXIT_CRITICAL(&s_data_mux);
}

/* Plazo sin recibir NADA de la P4 a partir del cual los datos se dan por
 * caducados. 5 s = el mismo que usa la UI para el punto de enlace y el icono del
 * GPS, para que todo diga lo mismo a la vez. La P4 emite cada 1 s. */
#define DATA_CADUCA_MS 5000

void data_model_get(mini_data_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_data_mux);
    *out = s_data;
    portEXIT_CRITICAL(&s_data_mux);

    /* SI EL ENLACE SE HA CAIDO, LOS DATOS NO VALEN.
     *
     * Antes se quedaban congelados con la ultima lectura y la pantalla los
     * seguia enseñando como si fueran de ahora: con la P4 apagada se veia una
     * tension de bateria de hace media hora, y eso puede hacer tomar una
     * decision mala (lo vio el usuario el 7-oct-2026). El modelo ya guardaba
     * last_update_ms para el punto de conexion y el GPS, pero las cifras no lo
     * miraban.
     *
     * Se invalidan TODOS los bloques aqui, en un solo sitio: la UI ya sabe
     * pintar "sin dato" cuando has_data es false, asi que no hay que tocar cada
     * widget. El aviso de que no hay enlace lo da ademas el punto rojo/gris. */
    if (out->last_update_ms != 0) {
        uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
        if ((ms - out->last_update_ms) >= DATA_CADUCA_MS) {
            out->has_data = false;
            out->soc_valido = out->v_valido = out->i_valido = false;
            out->aux_has_data = false;
            out->dcdc_has_data = false;
            out->frigo_has_data = false;
            out->exterior_has_data = false;
            out->water_clean_has_data = false;
            out->water_gray_has_data = false;
        }
    }
}

void data_model_set_simulated(const mini_data_t *sim)
{
    if (!sim) return;
    s_simulado = true;
    portENTER_CRITICAL(&s_data_mux);
    s_data = *sim;
    portEXIT_CRITICAL(&s_data_mux);
}
