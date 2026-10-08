/* entry_screen.h - Editor de un campo a PANTALLA COMPLETA.
 *
 * Por que existe: en 480x320 el teclado de LVGL sale por defecto a media
 * pantalla (160 px de alto) y ademas tapa el campo que estas escribiendo. El
 * numero se quedaba en una casilla de 30 px con letra del 14 -- ilegible de un
 * vistazo y con la autocaravana en marcha. Aqui la pantalla entera se dedica a
 * un solo campo: el valor en letra 40 arriba y el teclado en 240 px abajo
 * (teclas ~58 px de alto en vez de ~38).
 *
 * Es un overlay dentro de la pantalla que lo abre, no una pantalla de LVGL
 * aparte: asi no interfiere con el carrusel ni con sus animaciones.
 *
 * Uso:
 *     entry_screen_init(parent);                       // una vez, al construir
 *     entry_screen_open(ta, "Importe", true);          // al tocar el campo
 *
 * Al aceptar copia el texto al textarea original y le lanza un
 * LV_EVENT_VALUE_CHANGED, de modo que los calculos colgados de ese evento
 * (p.ej. el precio/litro del repostaje) siguen funcionando sin tocarlos.
 * Al cancelar no toca nada.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Crea el overlay (oculto) como hijo de 'parent'. Llamar una sola vez. */
void entry_screen_init(lv_obj_t *parent);

/* Abre el editor sobre 'target'. 'label' es el rotulo que se ve arriba
 * (cadena estatica). 'numeric' elige teclado numerico y limita los caracteres
 * aceptados a digitos y punto. */
void entry_screen_open(lv_obj_t *target, const char *label, bool numeric);

/* Cierra el editor SIN volcar lo tecleado. Para cuando algo de fuera se lleva
 * al usuario a otra pantalla (p.ej. un gesto del carrusel) y el overlay no
 * debe quedarse abierto por detras. No hace nada si no estaba abierto. */
void entry_screen_close(void);

#ifdef __cplusplus
}
#endif
