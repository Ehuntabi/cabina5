/* ota_cartel.h — el cartel que dice que la pantalla se esta actualizando.
 *
 * POR QUE EXISTE (23-nov-2026, el dueño: "podria poner un cartel indicativo del
 * proceso. de momento la cabina no responde ni se ha reiniciado"): la
 * actualizacion ya avisaba en Ajustes, en la linea de estado de la pantalla de
 * Wi-Fi... que solo se ve si estas EN esa pantalla. Desde cualquier otra, lo
 * unico que se nota es que la pantalla se queda clavada mientras se descarga
 * (LVGL se para a proposito para no refrescar durante el borrado de la flash) y
 * que se reinicia sola al final. Eso parece una averia.
 *
 * Este cartel se pinta encima de TODAS las pantallas (lv_layer_top) y sale solo
 * cuando ota_rx.c dice que hay una actualizacion en marcha. Se refresca cada
 * 250 ms desde un temporizador de LVGL: no hay ninguna llamada desde la tarea
 * de la OTA, asi que no puede haber carreras con el arbol de objetos.
 *
 * OJO: durante la descarga LVGL esta PARADO (ver ota_rx.c), asi que el cartel se
 * ve a saltos: el 0 % antes de empezar y luego cada 10 %. Es a proposito: es el
 * rato que se le devuelve a la pantalla sin quitarle ancho de banda a la flash.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Una vez, al montar la UI (se cuelga de lv_layer_top). */
void ota_cartel_create(void);

#ifdef __cplusplus
}
#endif
