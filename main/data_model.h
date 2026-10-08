#ifndef DATA_MODEL_H
#define DATA_MODEL_H

#include <stdbool.h>
#include <stdint.h>

/* Modelo compartido entre el receptor UDP (net/udp_rx.c) y la UI
 * (ui/view_info.c). Cada bloque tiene flag has_data para saber si pintar
 * valores o "--".
 *
 * Portado del satelite viejo C6 (victron_mini, retirado el 17-sep-2026), con
 * dos añadidos que aquel no usaba pero mini_msg_t ya trae: DC/DC y ventilador
 * del frigo. A diferencia de aquel, el acceso a g_data va con spinlock
 * (data_model_get(): la escritura ocurre en rx_task, la lectura en el
 * timer de LVGL — dos tareas distintas, sin eso es una carrera de datos). */

typedef struct {
    bool     has_data;
    uint32_t last_update_ms;   /* uptime en ms cuando llegó el dato */

    /* SmartShunt / BMV */
    int16_t  shunt_soc_deci;       /* SOC * 10  (%)   ej: 782 = 78.2 % */
    int16_t  shunt_voltage_centi;  /* V * 100         ej: 1342 = 13.42 V */
    int32_t  shunt_current_milli;  /* A * 1000  signo */
    int32_t  shunt_power_w;
    /* CADA CAMPO PUEDE VENIR SIN DATO POR SEPARADO (contrato en mini_proto.h):
     * la P4 manda el centinela SOLO en el campo que no tiene -- tipicamente el
     * SoC mientras el SmartShunt sincroniza, con el voltaje y la corriente
     * buenos. Antes se miraba unicamente el SoC y, si venia NA, se tiraban los
     * tres: la pantalla ponia "--" en V y en I aunque los tuviera. Auditoria
     * del 23-sep-2026, punto "protocolo v6: NA por campo". */
    bool     soc_valido;
    bool     v_valido;
    bool     i_valido;

    /* Canal auxiliar del SmartShunt = bateria de arranque/motor (NO es la
     * bateria de casa). Crudo, la unidad depende de aux_input. */
    uint16_t aux_value_raw;        /* V*100 (aux_input 0/1) o Kelvin*100 (2) */
    uint8_t  aux_input;            /* 0=voltage2(arranque), 1=mid-point, 2=temp */
    bool     aux_has_data;

    /* DC/DC (Orion / cargador). Solo voltajes, sin corriente cacheada en
     * la P4. device_state: ver VIC_STATE_* en victron_records.h del
     * proyecto P4 (0=Off, 3=Bulk, 4=Absorption, 5=Float, ...). */
    int16_t  dcdc_v_in_centi;
    int16_t  dcdc_v_out_centi;
    uint8_t  dcdc_state;
    bool     dcdc_has_data;

    /* Temperaturas DS18B20 + ventilador del frigo */
    int16_t  frigo_temp_centi;     /* °C * 100 */
    uint8_t  frigo_fan_pct;
    int16_t  exterior_temp_centi;
    bool     frigo_has_data;
    bool     exterior_has_data;

    /* Aguas (NE185 de la P4). Niveles 0..3. Flags INDEPENDIENTES: una trama
     * nativa NE185 (sin NE187) trae limpia valida pero grises siempre a
     * "sin dato" (ver ne185.c) -- con un unico flag combinado, eso apagaba
     * tambien el bloque de limpia aunque su dato fuera bueno. Detectado
     * auditando el 07-sep-2026. */
    uint8_t  water_clean;          /* limpia */
    uint8_t  water_gray;           /* grises */
    bool     water_clean_has_data;
    bool     water_gray_has_data;

    /* Reloj de la P4: segundos desde 1970 ya desplazados a SU hora local, o 0
     * si aun no ha dicho la hora. Es el UNICO reloj que tiene esta pantalla,
     * que no lleva RTC y se apaga con el contacto. Lo usa la parada abierta
     * para contar noches (dividiendo entre 86400 sale el dia) y periodos de
     * 24 h (restando). Ver mini_proto.h. */
    uint32_t epoch_local;

    /* Estado del GPS de la P4: 0=sin datos, 1=buscando, 2=posicion fijada.
     * Esta pantalla NO recibe la posicion, solo si la hay (ver mini_proto.h). */
    uint8_t  gps_estado;

    /* Alarmas ACTIVAS en la P4, bitmask MINI_ALARM_* (ver mini_proto.h).
     *
     * Se recibe en vez de deducirlo de los niveles a proposito: el umbral de la
     * bateria y la subida del congelador los conoce la P4 (son suyos), y aqui
     * no hay forma de saberlos. Antes esta pantalla se quedaba sin saber nada
     * del congelador, que es una de las cuatro alarmas.
     *
     * Dice que la CONDICION se cumple, suene alli o este silenciada: el silencio
     * corta el pitido de la P4, no esta pantalla (que no tiene altavoz). Quien
     * decide si aqui se enseña como "silenciada" es el estado local de esta
     * pantalla (ver view_info.c).
     *
     * CADUCA con el enlace: si la P4 deja de hablar, el ultimo valor se queda
     * congelado, asi que hay que mirarlo solo con last_update_ms fresco. */
    uint8_t  alarmas;
} mini_data_t;

void data_model_init(void);

/* Llamado desde rx_task (net/udp_rx.c) cuando llega un mini_msg_t válido. */
struct mini_msg;  /* forward declare para evitar incluir mini_proto.h aquí */
void data_model_update_from_msg(const struct mini_msg *msg);

/* Copia protegida por lock para que la UI (u otro consumidor) lea un
 * snapshot consistente sin carrera con rx_task. */
void data_model_get(mini_data_t *out);

/* Sustituye el modelo entero por datos de relleno (modo captura de
 * pantallas, ver capture_carousel.h). No la usa nada mas. */
void data_model_set_simulated(const mini_data_t *sim);

#endif
