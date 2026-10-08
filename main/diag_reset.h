/* diag_reset.h — enterarse de que la pantalla se ha reiniciado SOLA.
 *
 * Esta pantalla se apaga y se enciende con el contacto, varias veces al dia:
 * contar arranques no dice nada, porque casi todos son "encendido normal". Lo
 * que hay que saber es si algun reinicio NO lo provoco el contacto (un watchdog,
 * un panic, un cuelgue) -- y eso solo se puede anotar en el momento: al
 * siguiente corte de contacto el motivo se pierde y el arranque pasa a ser un
 * "encendido normal" mas. Creado el 10-sep-2026. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Lee el motivo del arranque y lo deja en el log (INFO si fue normal, WARN si
 * no) y cuenta los fallos. Llamar UNA vez en setup(), despues de
 * nvs_flash_init(). */
void diag_reset_anotar_arranque(void);

/* Marca el motivo antes de un reinicio pedido por el propio software (p.ej.
 * "UI colgada"): lo consume el arranque siguiente. */
void diag_reset_marcar_sw(const char *por_que);

/* Para la pantalla de Ajustes. */
bool diag_reset_fue_fallo(void);   /* el arranque anterior no lo provoco el contacto */

/* Dos lineas ya montadas para la tarjeta de Ajustes (buffer interno, no
 * liberar). Devuelve "" si el ultimo arranque fue normal: quien la use debe
 * preguntar antes por diag_reset_fue_fallo(). */
const char *diag_reset_resumen(void);
