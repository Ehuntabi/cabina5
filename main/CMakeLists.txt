# Fuentes de la aplicacion. Mismo reparto que el satelite de 3,5" (del que se
# porto): raiz + ui/ + icons/ + fonts/ + net/.
# OJO CON EL GLOB (auditoria del 25-nov-2026, H-40): se evalua al CONFIGURAR, asi
# que un .c nuevo no entra en el binario hasta el siguiente "idf.py reconfigure".
# Se intento CONFIGURE_DEPENDS y el IDF no lo admite (compila este fichero en modo
# script: "CONFIGURE_DEPENDS is invalid for script and find package modes"). La
# red de seguridad es test/fuentes_compiladas.sh, que compara los .c de main/ con
# los .obj del build.
file(GLOB SOURCES "*.c" "ui/*.c" "icons/*.c" "fonts/*.c" "net/*.c")

idf_component_register(
    SRCS       ${SOURCES}
    # ${CMAKE_BINARY_DIR}: ahi deja el configure el version_build.h que genera
    # scripts/gen_version_header.cmake (el sello de version de Ajustes).
    INCLUDE_DIRS "." "ui" "net" "icons" "fonts" "${CMAKE_BINARY_DIR}"
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
        esp_http_client   # <-- portal de la P4 (p4_api.c) y OTA (ota_rx.c)
        mbedtls           # <-- base64 del volcado y sha256 de la OTA
        app_update        # <-- esp_ota_*: actualizacion por Wi-Fi (net/ota_rx.c)
)

# MODELO QUE ESTA PANTALLA LE PIDE A LA P4 EN LA ACTUALIZACION (17-oct-2026).
#
# net/ota_rx.c es EL MISMO fichero en las dos cabinas y este es el unico dato
# que cambia: la P4 tiene un .bin por modelo y hay que pedir el de uno mismo.
#
# TIENE QUE IR EN LA COMPILACION Y NO SER UN VALOR POR DEFECTO: si la 5" pidiera
# "35cabina", se bajaria el firmware de la 3,5" (otra pantalla, otro tamaño y
# otra UI) y la dejaria inservible en el siguiente arranque. Sin este define,
# ota_rx.c no compila a proposito (#error).
target_compile_definitions(${COMPONENT_LIB} PRIVATE OTA_MODELO="cabina5")
