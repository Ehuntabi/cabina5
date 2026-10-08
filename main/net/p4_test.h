/* p4_test.h — Auto-test del canal de subida a la P4 (ver p4_test.c).
 *
 * Se llama UNA vez, al arrancar. No crea ningun viaje: solo comprueba que la
 * ruta existe, si las credenciales del portal son validas y si la P4 acepta un
 * POST. Todo queda en el log. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void p4_test_start(void);

#ifdef __cplusplus
}
#endif
