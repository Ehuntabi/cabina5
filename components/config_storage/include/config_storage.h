// config_storage.h
#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "nvs.h"

// Brightness settings (NVS namespace: "display")
esp_err_t load_brightness(uint8_t *brightness_out);
esp_err_t save_brightness(uint8_t brightness);

// STA Wi-Fi (AP de la P4 a la que se asocia, NVS namespace: "wifi").
// Permite cambiar de P4 (ej. la de repuesto) sin reflashear -- ver
// main/wifi_credentials.h para el valor de fabrica usado la primera vez
// que se arranca con NVS vacia.
// ssid_out/pass_out deben tener espacio para ssid_len/pass_len bytes;
// al volver, *ssid_len/*pass_len quedan con la longitud real (con \0).
esp_err_t load_wifi_config(char *ssid_out, size_t *ssid_len,
                           char *pass_out, size_t *pass_len);
esp_err_t save_wifi_config(const char *ssid, const char *pass);

// Viaje en curso (NVS namespace: "viaje").
// Lo lleva la propia pantalla: se enciende al confirmar "Iniciar viaje" y se
// apaga al confirmar "Finalizar viaje". Se guarda para que un corte de
// corriente en mitad de un viaje no devuelva el menu a "sin viaje".
// La P4 sigue siendo la duena del viaje de verdad; esto es solo lo que la
// 35cabina cree -- confirmado por HTTP en cada inicio/fin (main/net/p4_api.c).
//
// activo/destino/n_eventos viven en UN SOLO blob (ver el .c), no en claves
// sueltas: iniciar y terminar viaje antes hacian 2-3 nvs_commit seguidos
// (destino, luego contador, luego activo), y un apagon entre medias -- el
// contacto se corta a menudo con la 3.5" en marcha -- dejaba activo=true con
// destino="" o al reves. Detectado auditando el 07-sep-2026.
//
/* Destino del viaje en curso (da nombre a la carpeta en la SD de la P4). */
esp_err_t load_trip_destino(char *out, size_t *len);

/* Arranca el viaje de un tiron: activo=true, destino y n_eventos=1 (el
 * inicio ya cuenta) en un solo commit. Sustituye a llamar por separado
 * save_trip_destino + trip_eventos_reset + save_trip_active. */
esp_err_t save_trip_inicio(const char *destino);

/* Termina el viaje de un tiron: activo=false y destino="" en un solo commit.
 * n_eventos no se toca (lo resetea el proximo save_trip_inicio). Sustituye a
 * llamar por separado save_trip_destino("") + save_trip_active(false). */
esp_err_t save_trip_fin(void);

/* Siguiente numero de apunte. Creciente y sin huecos: la P4 lo usa para
 * descartar duplicados cuando un reintento llega dos veces. Contador propio,
 * fuera del blob de arriba: se llama por cada apunte (mucho mas a menudo que
 * inicio/fin) y ya era atomico por si solo. */
uint32_t next_trip_seq(void);

/* Cuenta de apuntes GENERADOS por el viaje en curso (el inicio incluido). Va en
 * el mensaje de fin para que la P4 sepa si le falta algo. Se cuenta lo generado
 * y no lo entregado: ver el porque en el .c. Vive en el mismo blob que
 * activo/destino, pero se actualiza sola (cada apunte, no solo inicio/fin). */
uint32_t trip_eventos_inc(void);
uint32_t trip_eventos_get(void);   /* sin incrementar: para el mensaje de fin */

/* Usuario y clave del PORTAL de la P4 (no del Wi-Fi). Se ven en la P4, en
 * Ajustes -> Wi-Fi. */
esp_err_t load_portal_creds(char *user_out, size_t *user_len,
                            char *pass_out, size_t *pass_len);
esp_err_t save_portal_creds(const char *user, const char *pass);

// Parada en curso (NVS namespace: "parada").
//
// Una parada en un area, un camping o una pernocta puede durar varios dias, y
// entre medias la pantalla se apaga con el contacto. Se guarda lo que hace
// falta para cerrarla al volver: donde fue, cuando empezo y a que precio.
//
// 'lugar' es el indice de la casilla de sitio en view_registro.c; el nombre
// vive alli, aqui solo viaja el numero.
// 'epoch_inicio' es el reloj de la P4 al llegar (segundos desde 1970 en su hora
// local, ver mini_proto.h). Siempre > 0: sin hora valida no se abre parada,
// porque no habria forma de contar el tiempo.
// 'cobro' distingue como se paga el sitio: 0 = por noche (cambio de dia de
// calendario), 1 = por periodos de 24 h desde que entras, que es como cobran
// algunas areas.
// 'precio' se guarda tal y como se tecleo ("25.00"), vacio si no lo hay.
typedef struct {
    bool     abierta;
    uint8_t  lugar;
    uint32_t epoch_inicio;
    uint8_t  cobro;
    uint8_t  moneda;          // indice en CURRENCY_CODES de view_registro.c
    char     precio[16];
} parada_abierta_t;

// Si no hay nada guardado devuelve .abierta=false (sin error).
esp_err_t load_parada_abierta(parada_abierta_t *out);
esp_err_t save_parada_abierta(const parada_abierta_t *p);
esp_err_t clear_parada_abierta(void);

// Salida en curso y lo que dejo abierto (NVS namespace: "salida").
//
// Sustituye a los dos apartados de arriba ("viaje" y "parada"), que solo sabian
// guardar UNA parada: en una estancia larga hay varias cosas abiertas a la vez
// (pernoctas cinco dias en un camping y por el medio repostas). Ahora se guarda
// TODO el estado en un unico blob, que ademas se escribe de una sola vez: si se
// va la corriente a mitad no queda un estado a medias.
//
// La forma del blob la define main/salida.h, que es quien entiende lo que
// significa cada campo. Aqui solo se guarda y se recupera.
esp_err_t load_salida_blob(void *out, size_t *len);
esp_err_t save_salida_blob(const void *data, size_t len);

// Marca de vida: la hora local de la P4, reescrita cada pocos minutos mientras
// la pantalla esta encendida.
//
// La 3.5" se apaga de GOLPE al quitar el contacto, sin aviso ni tiempo de
// guardar nada, asi que no puede anotar cuando se apago. Lo que hace es dejar
// la hora periodicamente: al arrancar, la ultima marca ES el momento del
// apagon, con el margen del periodo de escritura. Sirve para ofrecer "estuviste
// parado desde las 19:40" cuando se olvido declarar la parada.
//
// Devuelve 0 si nunca se escribio ninguna.
uint32_t  load_salida_vida(void);
esp_err_t save_salida_vida(uint32_t epoch_local);

/* Cuentakilometros del ULTIMO repostaje. Sirve para sacar los litros a los
 * cien: sin el, cada repostaje es un dato suelto y el consumo no se puede
 * calcular hasta tener el historico entero delante. 0 = todavia no hay. */
uint32_t load_ultimo_km(void);
esp_err_t save_ultimo_km(uint32_t km);

// Calibracion de nivel del ADXL345 (NVS namespace: "tilt").
// Offsets en centesimas de grado (deg*100), se restan de cada lectura.
// Si no hay calibracion guardada devuelve 0/0 (sin error).
esp_err_t load_tilt_calibration(int16_t *pitch_offset_centi,
                                 int16_t *roll_offset_centi);
esp_err_t save_tilt_calibration(int16_t pitch_offset_centi,
                                 int16_t roll_offset_centi);
