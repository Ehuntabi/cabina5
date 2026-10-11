/* capture_carousel.h - Modo documentacion: recorre las 4 pantallas con
 * datos simulados y vuelca cada una por USB (serie) en base64, para sacar
 * capturas sin depender de la P4 real ni de una tarjeta SD.
 *
 * Activar solo para grabar capturas:
 *   1. CAPTURE_CAROUSEL_ENABLE a 1 aqui abajo.
 *   2. idf.py reconfigure && idf.py build flash monitor
 *   3. El log va soltando bloques "===CAPTURE:<pantalla>:WxH===" ... "===END==="
 *      con el framebuffer RGB565 (con el byte-swap de CONFIG_LV_COLOR_16_SWAP)
 *      en base64. Para sacar los PNG:
 *          python3 tools/decodifica_capturas.py <log.txt> -o screenshot/
 *      (el log se guarda con `idf.py monitor | tee log.txt`, o con cualquier
 *      lector de puerto serie).
 *   4. Volver a poner el flag a 0 antes de publicar: mientras este a 1 la
 *      pantalla ensena datos FALSOS y se congela varios segundos por captura.
 *
 * Lo que inventa, ademas de los numeros: la alarma de BATERIA activa y
 * SILENCIADA, para que la captura de la pantalla de Datos enseñe el icono del
 * altavoz tachado (ver inject_sim_data() y view_info_captura_alarma_silenciada).
 */
#pragma once

#define CAPTURE_CAROUSEL_ENABLE 0
/* Solo datos: inyecta los valores de ejemplo y NO lanza el carrusel de capturas.
 * Sirve para mirar la pantalla con datos en el banco (una P4 sin nada
 * conectado manda todo como "sin dato" y se ven --). Se pone a 1, se graba, se
 * mira, y se vuelve a 0. */
#define CAPTURE_CAROUSEL_SOLO_DATOS 0
/* DIAGNOSTICO (8-oct-2026): abre SOLO el formulario de Peaje ocho segundos
 * despues de arrancar, sin tocar la pantalla. Se puso para cazar el "peaje en
 * negro" con el log a mano: pulsar el boton no se puede hacer desde el PC, y el
 * fallo hay que verlo en la traza. Cerrado el 9-oct-2026: el volcado periodico
 * (subpantallas_medir_cb, cada 3 s) provocaba un panic esporadico
 * (LoadProhibited, EXCVADDR 0x2c) al chocar con las actualizaciones normales
 * de la UI -- reinicios en bucle, sin relacion con el propio contenido de las
 * pantallas. Diagnostico ya cumplido (sirvio para todo el repaso de
 * submenus); se vuelve a 1 solo si hace falta cazar algo nuevo a mano. */
#define CAPTURE_PEAJE_DIAG 0
/* PRUEBA DEL BRILLO (9-oct-2026): ver el comentario largo en capture_carousel.c.
 * Pone una pantalla negra con el valor en grande y va bajando el duty (y la
 * frecuencia del PWM) paso a paso para averiguar donde se apaga de verdad la
 * retroiluminacion. 1 = se hace la prueba al arrancar y manda sobre todo lo
 * demas; 0 = no se compila nada de eso. */
#define PRUEBA_BRILLO 0
/* FOTO DE LA PANTALLA (9-oct-2026): vuelca la pantalla activa por el puerto
 * serie (1 de cada 2 pixeles) para poder VERLA en el PC con
 * tools/decodifica_capturas.py. Se pone a 1 para revisar pantallas a ojo y se
 * vuelve a 0: mientras esta encendida la pantalla se congela ~17 s por foto. */
#define CAPTURA_PANTALLA 0

#ifdef __cplusplus
extern "C" {
#endif

void capture_carousel_start(void);

#ifdef __cplusplus
}
#endif
