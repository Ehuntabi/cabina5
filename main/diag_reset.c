/* diag_reset.c — reinicios que no provoco el contacto (ver diag_reset.h).
 *
 * Namespace propio en NVS ("diag"), un unico commit por arranque y solo se
 * escribe cuando hay algo que contar: con el ajetreo normal (varios arranques
 * al dia) esto no debe gastar flash ni llenar la pantalla. Creado el
 * 10-sep-2026. */
#include "diag_reset.h"

#include <stdio.h>
#include <string.h>

#include "esp_system.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "diag_reset";

#define NS       "diag"
#define K_VECES  "veces"
#define K_SW_POR "sw_por"

static bool     s_fallo;
static char     s_motivo[48] = "";
static uint32_t s_veces;

/* Motivo en castellano llano, que es lo que se lee en Ajustes. */
static const char *texto(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON:    return "encendido normal (contacto)";
        case ESP_RST_EXT:        return "boton de reset";
        case ESP_RST_SW:         return "reinicio por software";
        case ESP_RST_PANIC:      return "fallo grave (panic)";
        case ESP_RST_INT_WDT:    return "watchdog de interrupcion";
        case ESP_RST_TASK_WDT:   return "watchdog de tarea";
        case ESP_RST_WDT:        return "watchdog";
        case ESP_RST_BROWNOUT:   return "bajon de tension";
        case ESP_RST_DEEPSLEEP:  return "vuelta de sueno profundo";
        case ESP_RST_SDIO:       return "reset por SDIO";
        case ESP_RST_USB:        return "reset por USB (cable)";
        case ESP_RST_JTAG:       return "reset por JTAG";
        case ESP_RST_EFUSE:      return "error de efuse";
        case ESP_RST_PWR_GLITCH: return "glitch de alimentacion";
        case ESP_RST_CPU_LOCKUP: return "bloqueo de CPU";
        default:                 return "desconocido";
    }
}

/* Lo que saca chivato: motivos que solo aparecen si el software va mal. Fuera
 * se quedan a proposito el encendido normal, el boton, el cable de grabar y el
 * BAJON DE TENSION: en una autocaravana el arranque del motor hunde la tension
 * y eso no es un fallo de la pantalla -- si sale, sale en el log y ya esta.
 * Un "reinicio por software" si cuenta: despues de quitar el temporizador de
 * 12 h no queda nada en el programa que reinicie a proposito. */
static bool es_fallo(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_CPU_LOCKUP:
        case ESP_RST_SW:
            return true;
        default:
            return false;
    }
}

void diag_reset_anotar_arranque(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    s_fallo = es_fallo(r);
    snprintf(s_motivo, sizeof(s_motivo), "%s", texto(r));

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "sin NVS: no puedo llevar la cuenta (%s)", s_motivo);
        return;
    }

    /* La marca de "por que" la deja quien pide el reinicio (ver marc_sw). */
    char sw[24] = {0};
    size_t n = sizeof(sw);
    bool hay_marca = (nvs_get_str(h, K_SW_POR, sw, &n) == ESP_OK) && sw[0] != '\0';

    nvs_get_u32(h, K_VECES, &s_veces);

    if (s_fallo) {
        s_veces++;
        if (hay_marca) {
            /* La marca sabe mas que el motivo del chip: "UI colgada" en vez de
             * un generico "watchdog de tarea". */
            snprintf(s_motivo, sizeof(s_motivo), "%s", sw);
        }
        nvs_set_u32(h, K_VECES, s_veces);
    }
    if (hay_marca) {
        nvs_erase_key(h, K_SW_POR);   /* la marca se consume siempre */
    }
    if (s_fallo || hay_marca) {
        nvs_commit(h);                /* sin fallo no se escribe nada */
    }
    nvs_close(h);

    if (s_fallo) {
        ESP_LOGW(TAG, "la pantalla se reinicio sola: %s (%lu veces)",
                 s_motivo, (unsigned long)s_veces);
    } else {
        ESP_LOGI(TAG, "arranque: %s", s_motivo);
    }
}

void diag_reset_marcar_sw(const char *por_que)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    esp_err_t err = nvs_set_str(h, K_SW_POR, por_que ? por_que : "sin motivo");
    if (err == ESP_OK) err = nvs_commit(h);
    if (err != ESP_OK) {
        /* Sin la marca, el arranque siguiente no sabra por que se pidio este
         * reinicio y se quedara en el motivo generico del chip. */
        ESP_LOGW(TAG, "no se pudo anotar la marca del reinicio: %s",
                 esp_err_to_name(err));
    }
    nvs_close(h);
}

bool        diag_reset_fue_fallo(void) { return s_fallo; }

static char s_resumen[96];
const char *diag_reset_resumen(void)
{
    if (!s_fallo) return "";
    snprintf(s_resumen, sizeof(s_resumen),
             "Se reinicio sola: %s\n%lu %s",
             s_motivo, (unsigned long)s_veces,
             (s_veces == 1) ? "vez" : "veces");
    return s_resumen;
}
