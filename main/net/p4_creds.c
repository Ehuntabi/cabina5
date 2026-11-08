#include "p4_creds.h"

#include <string.h>
#include "esp_log.h"

#include "config_storage.h"      /* portal creds + load_wifi_config (NVS) */
#include "udp_rx.h"              /* udp_rx_get_credentials: la pareja APLICADA */
#include "wifi_credentials.h"    /* PORTAL_CRED_USER/PASS (valores de fabrica) */

static const char *TAG = "p4_creds";

/* UNA SOLA CLAVE, Y LA MANDA LA DEL WI-FI (08-nov-2026).
 *
 * QUE PASO: la pantalla guardaba la clave del portal en su propio campo
 * (`wifi/http_pass`, el de Ajustes -> Wi-Fi -> "Portal de la P4"), aparte de la
 * del Wi-Fi (`wifi/password`). Esa segunda copia no la comprobaba nadie: se
 * aceptaba cualquier cosa de 8 a 32 caracteres. En la cabina del usuario quedo
 * `yy9m6n9gaahd` (la clave de una version anterior) mientras la del Wi-Fi era
 * la buena; el Wi-Fi asociaba perfectamente -- asi que en la pantalla todo
 * parecia correcto -- y la P4 contestaba 401 a TODO lo que mandaba la cabina:
 * el "Actualizar pantalla" decia "Credenciales del portal (401)" y el viaje no
 * se podia empezar ("La P4 no acepta la clave"). Costo una tarde encontrarlo.
 *
 * POR QUE ESTA SOLUCION ES DEFINITIVA: en la P4 la clave del punto de acceso y
 * la del portal SON LA MISMA (ver main/ui/settings/settings_wifi.c: la tarjeta
 * del AP avisa de que "esta clave es tambien la del portal abierto", y el
 * formulario del portal no tiene campo propio). Por tanto la cabina no debe
 * guardar dos: la del portal se DERIVA de la del Wi-Fi. Y la del Wi-Fi no puede
 * estar mal sin que se note al instante, porque sin ella no hay asociacion:
 * "P4: sin red" en Ajustes. Dicho al reves: si la pantalla esta viendo datos de
 * la P4, la clave que usa para el portal es, por construccion, la buena.
 *
 * La copia de `http_pass` se sigue escribiendo (autocorregida) para no dejar
 * una NVS mentirosa y para que un firmware anterior que la lea no vuelva a
 * romperse si se grabase marcha atras.
 *
 * Si no hay clave de Wi-Fi (punto de acceso abierto, o NVS vacia antes de
 * configurar), se usa lo guardado y, si no hay nada, los valores de fabrica.
 * Ese es el unico caso en que la clave del portal no se puede derivar. */
bool p4_creds_cargar(char *user, size_t user_len, char *pass, size_t pass_len)
{
    if (!user || !pass || user_len == 0 || pass_len == 0) return false;

    /* La del Wi-Fi, y de la fuente mas fiable: la pareja que udp_rx tiene
     * APLICADA (la que asocio de verdad, en RAM). Mejor que la NVS, que puede
     * no leerse (justo el caso en que antes se caia a la copia vieja del portal
     * y volvia el 401 -- lo canto el arbitro). Si aun no hay nada aplicado
     * (arranque muy temprano), se lee la NVS; y si tampoco, se usa la guardada. */
    char wifi_ssid[33] = {0};
    char wifi_pass[65] = {0};
    udp_rx_get_credentials(wifi_ssid, sizeof wifi_ssid, wifi_pass, sizeof wifi_pass);
    if (wifi_pass[0] == '\0') {
        size_t ssid_len = sizeof(wifi_ssid), wifi_len = sizeof(wifi_pass);
        (void)load_wifi_config(wifi_ssid, &ssid_len, wifi_pass, &wifi_len);
    }
    const bool hay_wifi = (wifi_pass[0] != '\0');

    /* Lo que hubiera guardado como portal (para el usuario y como respaldo). */
    char guardado_user[33] = {0};
    char guardado_pass[65] = {0};
    size_t gu_len = sizeof(guardado_user), gp_len = sizeof(guardado_pass);
    const bool hay_guardado =
        (load_portal_creds(guardado_user, &gu_len, guardado_pass, &gp_len) == ESP_OK &&
         guardado_user[0] != '\0');

    if (hay_wifi) {
        /* LA DEL WI-FI MANDA. El usuario es el de la P4 ("p4"; se respeta el que
         * hubiera guardado por si es una P4 con el usuario viejo "victron", que
         * la P4 sigue aceptando). */
        snprintf(user, user_len, "%s",
                 hay_guardado ? guardado_user : PORTAL_CRED_USER);
        snprintf(pass, pass_len, "%s", wifi_pass);

        /* AUTOCORRECCION: dejar la copia igual que la buena. Es una escritura
         * de NVS y solo ocurre cuando de verdad habia una discrepancia (una
         * vez en la vida de la placa, o cuando se cambia la clave del Wi-Fi). */
        if (!hay_guardado || strcmp(guardado_pass, wifi_pass) != 0) {
            if (save_portal_creds(user, wifi_pass) == ESP_OK) {
                ESP_LOGW(TAG, "la clave del portal guardada no era la del Wi-Fi: "
                              "corregida (son la misma; ver p4_creds.c)");
            } else {
                ESP_LOGW(TAG, "no pude corregir la clave del portal en NVS: uso "
                              "la del Wi-Fi de todas formas");
            }
        }
        return true;
    }

    if (hay_guardado) {
        /* Punto de acceso abierto (o NVS sin Wi-Fi): se usa lo guardado. */
        snprintf(user, user_len, "%s", guardado_user);
        snprintf(pass, pass_len, "%s", guardado_pass);
        return true;
    }

    /* Nada guardado (placa nueva): las de fabrica, que son las mismas con las
     * que la P4 crea su portal al arrancar con la NVS vacia. */
    snprintf(user, user_len, "%s", PORTAL_CRED_USER);
    snprintf(pass, pass_len, "%s", PORTAL_CRED_PASS);
    ESP_LOGI(TAG, "sin credenciales en NVS: se usan las de fabrica (usuario '%s')", user);
    return user[0] != '\0';
}
