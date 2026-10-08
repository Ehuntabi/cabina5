/* view_info.h - Pantalla de info agrupada (Fase 1).
 *
 * Grid con las 6 categorias que llegan por mini_msg_t desde la P4:
 * Bateria, Bateria motor, DC/DC, Frigo, Aguas, Exterior. A diferencia del
 * carrusel de una sola card del mini (pantalla de 320x172), aqui hay sitio
 * de sobra para verlas todas a la vez.
 */
#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Crea el grid dentro de parent (normalmente lv_scr_act()) y arranca el
 * timer de refresco (2 Hz, sondea data_model_get()). */
void view_info_create(lv_obj_t *parent);

/* Pastilla de "N sin enviar". La llama el repartidor de la cola DESDE SU TAREA,
 * asi que por dentro salta a LVGL con lv_async_call. Con 0 se esconde sola.
 *
 * Va en ESTA pantalla y no en un cartel aparte porque es la que esta puesta
 * cuando vas a quitar el contacto, y esta pantalla no puede saber que vas a
 * hacerlo: se queda sin corriente y ya. Por eso el aviso tiene que estar
 * visible todo el rato mientras quede algo, no saltar "al apagar". */
void view_info_set_pendientes(size_t pendientes);

/* Modo de alto contraste, que va con el brillo: 100% = contraste alto.
 * Lo llaman la propia pantalla de datos (doble toque o toque largo) y el boton
 * de Ajustes. */
void view_info_set_contraste(bool activo);

/* Cuantos apuntes quedan ABIERTOS (declarados y sin cerrar). Comparte pastilla
 * con los "sin enviar" -- ver el comentario de pendientes_aplicar().
 *
 * Va aqui y no solo en la pantalla de registros porque esta es la que esta
 * puesta mientras conduces: si el aviso solo vive en la otra, hay que acordarse
 * de ir a mirarlo, que es justo lo que no se hace. */
void view_info_set_sin_cerrar(size_t sin_cerrar);

/* Aviso "Toca al llegar a <nombre>" para una salida puntual que ya abrio la
 * carpeta en la P4 pero todavia no ha llegado al sitio de la accion (ver el
 * campo 'declarado' de salida_vista_t en salida.h). NULL o "" lo esconde.
 *
 * Va en ESTA pantalla por el mismo motivo que view_info_set_sin_cerrar: es
 * la que esta puesta mientras conduces hacia el sitio, y ahi es donde hace
 * falta el recordatorio, no en un menu al que solo se vuelve si te
 * acuerdas. Al tocarlo, declara la llegada (ver
 * view_registro_puntual_declarar_llegada en view_registro.h). */
void view_info_set_puntual_pendiente(const char *nombre);

/* Deja la alarma de la bateria CON el altavoz tachado (silenciada) para la
 * captura de pantallas, sin mandar nada a la P4.
 *
 * Solo lo llama capture_carousel.c, y solo existe para poder enseñar en el
 * manual y en la conversacion como se ve una alarma silenciada: el estado de
 * silencio se pone tocando el icono, y eso manda una orden de verdad a la P4,
 * que en una captura no esta. Nada de produccion llama a esto. */
void view_info_captura_alarma_silenciada(void);

#ifdef __cplusplus
}
#endif
