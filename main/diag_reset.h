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

/* Marca un reinicio PREVISTO Y BUENO, el de una actualizacion OTA ya aplicada.
 *
 * POR QUE EXISTE (17-oct-2026): el reinicio por software cuenta como FALLO a
 * proposito (ver es_fallo() en el .c: hasta ahora nada reiniciaba a proposito
 * salvo un cuelgue de la UI). La actualizacion OTA reinicia A PROPOSITO y esta
 * bien; sin esta funcion, al volver la tarjeta de Ajustes diria "Se reinicio
 * sola" en rojo, que es justo lo contrario de lo que hay que ver. Con esto, el
 * arranque siguiente lo cuenta como normal y el motivo sale en gris
 * ("Arranque: actualizacion de pantalla").
 *
 * Llamar JUSTO ANTES de esp_restart(), y solo cuando el reinicio es lo que se
 * queria (la version nueva ya esta marcada para arrancar). */
void diag_reset_marcar_sw_bueno(const char *por_que);

/* Para la pantalla de Ajustes. */
bool diag_reset_fue_fallo(void);   /* el arranque anterior no lo provoco el contacto */

/* Dos lineas ya montadas para la tarjeta de Ajustes (buffer interno, no
 * liberar). Devuelve "" si el ultimo arranque fue normal: quien la use debe
 * preguntar antes por diag_reset_fue_fallo(). */
const char *diag_reset_resumen(void);

/* POR QUE ARRANCO ESTA VEZ, en castellano llano, pase lo que pase (16-oct-2026,
 * lo pidio el usuario: "si, en configuracion").
 *
 * diag_reset_resumen() solo habla de FALLOS, y hay dos motivos que no lo son y
 * que son justo los que se quieren ver cuando la pantalla parece reiniciarse
 * sola: un BAJON DE TENSION y un RESET POR EL CABLE USB. Los dos se quedaban
 * solo en el log. Esto los saca a la pantalla de Ajustes, siempre.
 * Devuelve "" solo si nadie llamo a diag_reset_anotar_arranque(). */
const char *diag_reset_motivo_arranque(void);

/* SEGUNDOS QUE LLEVA EN MARCHA esta vez (misma cuenta que el latido, ver
 * heartbeat_task), y el mismo dato en texto para la pantalla de Ajustes:
 * "12 min", "2 h 5 min", "3 dias"...
 *
 * Para que sirve (16-oct-2026): la linea de Ajustes dice POR QUE arranco, y con
 * esto se ve ADEMAS si ha vuelto a arrancar desde la ultima vez que se miro --
 * sin PC, sin log y sin abrir el puerto serie. En el banco la pantalla se
 * reiniciaba por el cable USB y hacia falta justo eso. */
uint32_t diag_reset_segundos(void);
const char *diag_reset_hace_texto(void);

