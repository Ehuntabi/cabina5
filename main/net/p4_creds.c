#include "p4_creds.h"

#include <string.h>
#include "esp_log.h"

#include "config_storage.h"      /* load_portal_creds (NVS: lo que ponga Ajustes) */
#include "wifi_credentials.h"    /* PORTAL_CRED_USER/PASS (valores de fabrica) */

static const char *TAG = "p4_creds";

bool p4_creds_cargar(char *user, size_t user_len, char *pass, size_t pass_len)
{
    if (!user || !pass || user_len == 0 || pass_len == 0) return false;

    size_t ul = user_len, pl = pass_len;
    if (load_portal_creds(user, &ul, pass, &pl) == ESP_OK && user[0]) {
        return true;                     /* las de Ajustes: mandan esas */
    }

    /* Nada guardado (placa nueva, o alguien las borro): se usan las de fabrica.
     * Son las mismas con las que la P4 crea su portal cuando arranca con la NVS
     * vacia, asi que de fabrica encajan; si en la P4 se cambiaron, hay que
     * ponerlas en Ajustes -> WiFi. */
    snprintf(user, user_len, "%s", PORTAL_CRED_USER);
    snprintf(pass, pass_len, "%s", PORTAL_CRED_PASS);
    ESP_LOGI(TAG, "sin credenciales en NVS: se usan las de fabrica (usuario '%s')", user);
    return user[0] != '\0';
}
