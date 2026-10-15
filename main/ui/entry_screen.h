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
 * Al aceptar copia el texto al textarea original y le lanza UN
 * LV_EVENT_VALUE_CHANGED, de modo que los calculos colgados de ese evento
 * (p.ej. el precio/litro del repostaje) siguen funcionando sin tocarlos.
 * Al cancelar no toca nada.
 *
 * OJO CON ESCRIBIR EN ESOS CAMPOS POR CODIGO (auditoria del 16-oct-2026):
 * lv_textarea_set_text() manda VALUE_CHANGED por CADA caracter que añade y otra
 * vez al final (LVGL 8.4: lv_textarea.c:158 y :312; LVGL 9.2: :266 y :419). Un
 * campo escondido que escuche ahi se cree aceptado por el usuario cada vez que
 * el codigo lo limpia o lo rellena. Para eso estan entry_screen_mudo() y
 * entry_screen_mudo_ahora(): ver el comentario largo en entry_screen.c.
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

/* Igual, pero para ENCADENAR: se llama desde el callback VALUE_CHANGED del campo
 * anterior (destino -> km del cuadro, al empezar un viaje) y aplaza la apertura
 * un ciclo de LVGL, porque el "Aceptar" cierra este panel DESPUES de avisar al
 * callback y, si no, el paso nuevo nace escondido. Ver el comentario largo en
 * entry_screen.c. */
void entry_screen_open_siguiente(lv_obj_t *target, const char *label, bool numeric);

/* Cierra el editor SIN volcar lo tecleado. Para cuando algo de fuera se lleva
 * al usuario a otra pantalla (p.ej. un gesto del carrusel) y el overlay no
 * debe quedarse abierto por detras. No hace nada si no estaba abierto. */
void entry_screen_close(void);

/* ── SILENCIO MIENTRAS SE ESCRIBE POR CODIGO ────────────────────────────────
 *
 * lv_textarea_set_text() dispara VALUE_CHANGED (una vez por caracter y otra al
 * final), asi que cualquier campo escondido que escuche ahi recibe un aviso
 * como si el usuario hubiera pulsado "Aceptar" cada vez que el codigo lo limpia
 * o lo rellena. Con un destino de 9 letras, el flujo del viaje abria el paso siguiente
 * ONCE VECES con un destino de 9 letras y armaba un cartel de confirmacion con el destino del
 * viaje ANTERIOR (el caso que se cazo el 16-oct-2026: "Empezar el viaje?" con
 * el nombre viejo debajo del editor, listo para arrancar un viaje mal nombrado).
 *
 * Uso, alrededor de la escritura por codigo:
 *     entry_screen_mudo(true);
 *     lv_textarea_set_text(ta, "...");
 *     entry_screen_mudo(false);
 * y al principio del callback que escucha:
 *     if (entry_screen_mudo_ahora()) return;
 *
 * Lleva cuenta de anidamiento: se puede llamar desde dentro de otra zona muda
 * sin que la de dentro reactive los avisos al salir. */
void entry_screen_mudo(bool mudo);

/* true si ahora mismo hay que ignorar los VALUE_CHANGED que provoca el codigo
 * (false si habla el usuario). */
bool entry_screen_mudo_ahora(void);

#ifdef __cplusplus
}
#endif
