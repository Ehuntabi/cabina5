# cabina5 — satelite tactil de la P4 en la placa Guition JC8048W550C (5", 800x480)
#
# Proyecto ESP-IDF normal. El target se fija a esp32s3 en sdkconfig.defaults
# (CONFIG_IDF_TARGET="esp32s3"): al ser un proyecto NUEVO no hace falta
# "idf.py set-target", que en un arbol ya configurado borra sdkconfig.
cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Version del firmware (Ajustes -> Configuracion), con la MISMA regla que la P4
# y que la 3,5" (decision del usuario del 13-sep-2026; aqui se porto el
# 16-oct-2026, cuando el usuario vio en la pantalla "v1.1-53-g7bb2100-dirty"):
# la version NUNCA lleva sufijos (ni "-dirty" ni "-N-g<hash>"). Lo que hay por
# encima del ultimo tag ES la version siguiente, +0.1: el arbol justo en el tag
# muestra esa version, y con trabajo encima muestra la siguiente, sin que
# aparezca "-dirty" por ningun lado. El +0.1 es solo el valor del trabajo en
# curso: la version de verdad la fija el tag que crea release.sh.
#
# El patron es "v*.*" y NO "v*.*.*": desde el 24-ago-2026 las versiones llevan
# DOS numeros; el comodin de git abarca tambien el punto, asi que encuentra
# igual las nuevas que las viejas de tres, y las sdkconfig-known-good-* quedan
# fuera por no empezar por v.
#
# Se calcula al CONFIGURAR, asi que se congelaria: por eso el
# CMAKE_CONFIGURE_DEPENDS de abajo (al cambiar el git, se recalcula).
execute_process(
    COMMAND git describe --always --tags --dirty --match "v*.*"
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE VER_DESCRITA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(VER_DESCRITA MATCHES "^v([0-9]+)\.([0-9]+)(\.[0-9]+)?$")
    set(PROJECT_VER "${VER_DESCRITA}")
elseif(VER_DESCRITA MATCHES "^v([0-9]+)\.([0-9]+)-")
    math(EXPR VER_MINOR_SIGUIENTE "1 + ${CMAKE_MATCH_2}")
    set(PROJECT_VER "v${CMAKE_MATCH_1}.${VER_MINOR_SIGUIENTE}")
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/version.txt")
    # Arbol sin .git (una copia de trabajo): la version la deja ahi el script.
    file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" VER_FICHERO LIMIT_COUNT 1)
    string(STRIP "${VER_FICHERO}" VER_FICHERO)
    if(VER_FICHERO MATCHES "^v[0-9]+\.[0-9]+")
        set(PROJECT_VER "${VER_FICHERO}")
    else()
        set(PROJECT_VER "v?")
    endif()
else()
    set(PROJECT_VER "v?")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_LIST_DIR}/.git/HEAD"
             "${CMAKE_CURRENT_LIST_DIR}/.git/index")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cabina5)
