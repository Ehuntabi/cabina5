# cabina55 — satelite tactil de la P4 en la placa Guition JC8048W550C (5", 800x480)
#
# Proyecto ESP-IDF normal. El target se fija a esp32s3 en sdkconfig.defaults
# (CONFIG_IDF_TARGET="esp32s3"): al ser un proyecto NUEVO no hace falta
# "idf.py set-target", que en un arbol ya configurado borra sdkconfig.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cabina55)
