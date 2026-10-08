file(GLOB_RECURSE SOURCES "*.c")

idf_component_register(
    SRCS       ${SOURCES}
    INCLUDE_DIRS "."
    REQUIRES   esp_lcd esp_lvgl_port lvgl esp_wifi nvs_flash esp_mm
)
