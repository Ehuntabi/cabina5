/* tilt.h - Acelerometro ADXL345 para el nivel de aparcamiento (Fase 3).
 *
 * Bus I2C PROPIO (I2C_NUM_1, IO17=SDA/IO18=SCL, los de los JST de 4 pines
 * que ademas traen 3V3 y GND: un solo cable de 4 hilos hasta el sensor),
 * NO el compartido con el tactil -- el esquematico oficial del modulo
 * muestra que el bus del tactil (GPIO4/GPIO8) es cableado interno sin pad
 * accesible desde fuera. Funcionalidad totalmente autonoma en esta placa:
 * no depende del P4 ni del GPS.
 *
 * Lectura estatica (autocaravana parada): pitch/roll por arcotangente
 * sobre el vector de gravedad, sin gyro ni fusion de sensores.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Crea el bus I2C dedicado (IO17/IO18) y prueba el sensor (arranca medicion
 * si responde). Devuelve ESP_ERR_NOT_FOUND si no hay ADXL345 cableado --
 * no es fatal, tilt_get() simplemente devolvera false y la UI muestra
 * "sensor no encontrado" en vez de crashear. */
esp_err_t tilt_init(void);

bool tilt_is_present(void);

/* Pitch/roll ya calibrados (offset de NVS restado), en grados.
 * Devuelve false si el sensor no esta presente o la lectura I2C fallo. */
bool tilt_get(float *pitch_deg, float *roll_deg);

/* Rumbo de la brujula en grados (0 = norte, sentido horario). Solo hay si el
 * sensor es un BNO055 (el ADXL345 no tiene magnetometro): devuelve false en
 * ese caso y la UI lo oculta. 'cal' es la calidad del magnetometro (0-3; 3 =
 * calibrado) y sirve para avisar de que hay que girar el sensor. */
bool tilt_get_heading(float *heading_deg, uint8_t *cal);

/* Estado de calibracion del BNO055, sensor por sensor (cada uno 0-3; 3 =
 * calibrado). Del acelerometro sale el NIVEL y del magnetometro la BRUJULA; el
 * giroscopio y el sistema dicen cuanto falta para que la fusion sea de fiar.
 * Devuelve false si el sensor no es un BNO055. */
bool tilt_cal_get(uint8_t *mag, uint8_t *acc, uint8_t *gyr, uint8_t *sys);

/* ¿Hay un perfil de calibracion de la brujula guardado y puesto en el chip?
 * (22-nov-2026). Es la señal buena para saber si el RUMBO es de fiar: con el
 * perfil cargado el rumbo es absoluto aunque el chip deje el contador del
 * magnetometro a 0 (y `sys` a 0 despues de pasar por CONFIG), que es lo normal
 * hasta que el vehiculo se mueve. */
bool tilt_tiene_perfil(void);

/* VUELVE A EMPEZAR LA CALIBRACION DE LA BRUJULA (23-nov-2026).
 *
 * El BNO055 no tiene un comando de "calibrar": se calibra solo mientras lo
 * mueves, y lo unico que la borra de verdad es un RESET del chip (que es
 * justo lo que hace el arranque, y por eso el perfil se restaura despues).
 * Esto hace lo mismo, pero a peticion: borra el perfil guardado en la NVS y
 * reinicia el chip en su propia tarea (el datasheet pide ~650 ms de espera y
 * bloquear la tarea de LVGL ese rato es pedir un aviso de colgada). A partir de
 * ahi hay que GIRAR la pantalla en las tres direcciones; cuando el magnetometro
 * y el giroscopio lleguen a 3/3, tilt_guardar_cal_task guarda el perfil nuevo.
 *
 * Devuelve false si no hay BNO055 o si ya habia un reinicio en curso. Que
 * devuelva true solo significa que ha EMPEZADO: el resultado se mira con
 * tilt_cal_get() y tilt_tiene_perfil(). */
bool tilt_brujula_recalibrar(void);

/* Promedia ~20 lecturas del angulo ACTUAL y las guarda como nuevo "cero"
 * en NVS (config_storage.c, namespace "tilt"). Llamar con la autocaravana
 * parada sobre una referencia de nivel real (nivel de burbuja fisico o
 * superficie conocida). Bloquea ~0.5s (uso desde un boton, no desde LVGL
 * directamente sin feedback). Devuelve false si no hay sensor, si no hubo
 * lecturas validas o si NVS no dejo guardarlas (en ese caso la calibracion
 * vale solo para esta sesion). */
bool tilt_calibrate(void);

#ifdef __cplusplus
}
#endif
