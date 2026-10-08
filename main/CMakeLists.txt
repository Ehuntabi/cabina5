# Fuentes de la aplicacion. Mismo reparto que el satelite de 3,5" (del que se
# porto): raiz + ui/ + icons/ + fonts/ + net/.
file(GLOB SOURCES "*.c" "ui/*.c" "icons/*.c" "fonts/*.c" "net/*.c")

idf_component_register(
    SRCS       ${SOURCES}
    INCLUDE_DIRS "." "ui" "net" "icons" "fonts"
    PRIV_REQUIRES
        config_storage    # <-- almacen NVS de la app (brillo, ajustes, viajes)
        lvgl
        esp_lcd
        esp_lvgl_port
        esp_mm            # <-- esp_cache_msync (bounce buffer del panel RGB)
        esp_timer
        spi_flash
        nvs_flash
        esp_app_format    # <-- version del firmware (esp_app_get_description)
        esp_netif         # <-- interfaz TCP/IP para el STA hacia la P4
        esp_event         # <-- eventos Wi-Fi/netif
        esp_wifi          # <-- asociarse al AP de la P4
        esp_http_client   # <-- mandar los apuntes al portal de la P4 (p4_api.c)
        mbedtls           # <-- base64 del volcado de pantalla (capture_carousel.c)
)
