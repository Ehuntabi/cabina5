/* p4_creds.h — Credenciales del PORTAL de la P4, en un solo sitio.
 *
 * Existe por un fallo real (7-oct-2026): la placa nueva tiene la NVS vacia, el
 * cliente HTTP no mandaba cabecera Authorization, la P4 respondia 401 y el
 * cliente de IDF se perdia siguiendo redirecciones ("ESP_ERR_HTTP_MAX_REDIRECT")
 * — o sea, los apuntes de viaje no salian y el sintoma no decia por que.
 *
 * Orden: lo que haya en NVS (Ajustes -> WiFi) y, si no hay nada, los valores de
 * fabrica de `wifi_credentials.h`, que son los que la P4 crea la primera vez.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Rellena usuario y clave. Devuelve true si hay ALGO que mandar (de NVS o de
 * fabrica). Los buffers tienen que ser de 33 y 65 bytes, como los de NVS. */
bool p4_creds_cargar(char *user, size_t user_len, char *pass, size_t pass_len);

#ifdef __cplusplus
}
#endif
