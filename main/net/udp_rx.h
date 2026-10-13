/* udp_rx.h - Receiver UDP de 35cabina.
 *
 * Configura el ESP32-S3 como STA, se asocia al SoftAP de la P4 y escucha
 * paquetes UDP broadcast en MINI_PROTO_UDP_PORT con el payload mini_msg_t.
 *
 * SSID/password se guardan en NVS (cambiables desde Ajustes sin
 * reflashear); wifi_credentials.h (no versionado, ver
 * wifi_credentials.h.example y .gitignore) solo es el valor de fabrica
 * del primer arranque.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void udp_rx_start(void);

/* Estado del enlace con la P4, para pintarlo en la pantalla de informacion.
 * Ver el comentario en udp_rx.c (30-sep-2026). */
void udp_rx_enlace(char *ssid, size_t ssid_len, bool *asociado,
                   int *rssi_dbm, int *seg_sin_datos);

/* POR QUE SE DESCARTA LO QUE LLEGA (auditoria del 14-oct-2026).
 *
 * Desde la pantalla, "la P4 no esta" y "la P4 habla otro idioma" (firmware
 * mezclado: otro tamano de mensaje o otra version) se veian IGUAL: -- y el
 * punto gris. Y no se arreglan igual -- una se arregla encendiendo la P4, la
 * otra grabando la placa que se quedo atras --, asi que hay que poder
 * distinguirlas sin un portatil. */
typedef enum {
    UDP_RX_DROP_NO = 0,      /* no se ha descartado nada: o no llega, o va bien */
    UDP_RX_DROP_ORIGEN,      /* llega de una IP que no es la P4 */
    UDP_RX_DROP_TAM,         /* tamano distinto: firmware de otro protocolo */
    UDP_RX_DROP_VERSION,     /* version distinta: firmware de otra version */
    UDP_RX_DROP_CRC,         /* la version cuadra pero el CRC no: ruido/corrupcion */
} udp_rx_drop_t;

/* Ultimo descarte y que se vio en el (version y tamano del paquete). Los tres
 * punteros pueden ser NULL. */
void udp_rx_diagnostico(udp_rx_drop_t *motivo, int *version_vista, int *tam_visto);

/* Copia las credenciales actuales (para prefijar la pantalla de Ajustes). */
void udp_rx_get_credentials(char *ssid_out, size_t ssid_len, char *pass_out, size_t pass_len);

/* Guarda nuevas credenciales en NVS y fuerza una reconexion inmediata.
 * Devuelve false si no se pudieron guardar en NVS: entonces el cambio solo
 * dura hasta el proximo apagado (quien lo llame debe avisar de ello). */
bool udp_rx_set_credentials(const char *ssid, const char *pass);

#ifdef __cplusplus
}
#endif
