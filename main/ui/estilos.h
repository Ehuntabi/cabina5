/* Un solo sitio para el tamano de las fuentes de la UI.
 *
 * POR QUE EXISTE: la UI se porto del satelite de 3,5" (480x320 logicos) a la
 * placa de 5" (800x480 reales), que tiene 181 ppp en vez de 160, y ademas se
 * mira desde el asiento del conductor y no de cerca. En vez de tocar los ~105
 * sitios donde se elige fuente, se traducen aqui los nombres: asi el dia que
 * haya que reajustar (o cambiar de pantalla) se hace en este fichero y no en la
 * UI.
 *
 * COMO FUNCIONA: cada macro redefine el nombre viejo al tamano nuevo. Las vistas
 * siguen escribiendo lv_font_montserrat_20 y lo que sale es el 26; no hay que
 * tocar ni una linea de las vistas.
 *
 * ── HISTORIA: LA ESCALA ENCADENADA (hasta el 16-oct-2026) ───────────────────
 *
 * Aqui vivia una tabla que subia cada nombre viejo un escalon (14->20, 16->22,
 * 20->26, 22->30, 24->34, 32->40, 40->48, 48->56). NO ERA LO QUE PASABA: los
 * nombres nuevos eran a su vez macros de esa misma tabla, asi que el
 * preprocesador traducia DOS VECES y la UI renderizaba 26, 30 y 40 donde el
 * diseno decia cinco tamanos:
 *
 *      14 -> 26      16 -> 30      20 -> 26
 *      22 -> 30      24 -> 40      32 -> 40
 *
 * De ahi venian dos cosas que han costado tiempo: "subir un escalon" a veces no
 * cambiaba nada (o cambiaba de mas), y al pedir "al siguiente tamaño" hubo que
 * escribir un tamaño LITERAL (lv_font_montserrat_28, que no era macro) en vez de
 * subir el nombre viejo. La medicion de la linea que se ve en cada widget esta
 * en el volcado del arbol (form_volcar imprime el alto real de cada rotulo).
 *
 * ESTA TABLA YA NO MANDA: se quito al escribir LA ESCALA PLANA (el bloque de
 * abajo, mismo dia). Aquel cambio dejo EL MISMO RESULTADO en pantalla -- medido
 * sobre el codigo, no se movio un pixel -- pero con una sola traduccion, para
 * que el nombre diga lo que se ve. Si hay que reajustar un tamaño, se toca el
 * bloque de abajo, no este.
 *
 * (El "limite conocido" de entonces -- LVGL solo trae Montserrat hasta la 48 --
 * tampoco aplica ya: desde el 16-oct-2026 las fuentes son Inter generadas con
 * lv_font_conv para este proyecto, y la mayor es la 48.)
 *
 * NEGRITA: la negrita va con su propio nombre de papel (FUENTE_*_NEGRITA) para
 * que se lea en la vista QUE es, no cuanto mide. Desde el 16-oct-2026 es la
 * Inter-Bold generada con el mismo comando que las demas (los ficheros
 * lv_font_montserrat_bold_*) se borraron: los alias de fonts/fonts_es.h apuntan
 * ya a las Inter).
 *
 * Este fichero lo incluye lvgl.h... no: lo incluye ui/estilos.h, que a su vez
 * incluyen todas las vistas. Si anades una vista nueva, incluye estilos.h.
 */
#pragma once

#include "lvgl.h"
#include "ui/ui_style.h"   /* la paleta, en un solo sitio */
#include "fonts/fonts_es.h"
#include <string.h>

/* ── La escala, PLANA (16-oct-2026) ─────────────────────────────────────────
 *
 * Antes esto tenia DOS traducciones encadenadas: lv_font_montserrat_16 ->
 * FUENTE_PEQUENA -> lv_font_montserrat_22 -> (macro otra vez) FUENTE_MEDIA ->
 * lv_font_montserrat_30. Resultado real: la UI renderizaba 26, 30 y 40 donde el
 * diseno decia cinco tamanos, y "subir un escalon" a veces no cambiaba nada (o
 * cambiaba de mas). Se queda EL MISMO RESULTADO en pantalla -- no se mueve ni un
 * pixel, esta medido sobre el codigo -- pero escrito de una vez, para que el
 * nombre diga lo que se ve.
 *
 * Los nombres viejos siguen valiendo (los alias estan en fonts/fonts_es.h), asi
 * que las ~100 llamadas de la UI no se han tocado. Y la familia pasa a ser la
 * misma que la de la P4 (Inter), que ademas trae acentos y la n. */
#define FUENTE_TITULAR         lv_font_inter_48_es
#define FUENTE_GRANDE          lv_font_inter_40_es
#define FUENTE_MEDIA_GRANDE    lv_font_inter_40_es   /* antes: 32 -> 40 por la cadena */
#define FUENTE_MEDIA           lv_font_inter_30_es
#define FUENTE_NORMAL          lv_font_inter_26_es
#define FUENTE_PEQUENA         lv_font_inter_30_es   /* antes: 22 -> 30 por la cadena */
#define FUENTE_MUY_PEQUENA     lv_font_inter_26_es   /* antes: 20 -> 26 por la cadena */
/* El PIE: la letra pequeña de verdad (18 px). Los papeles de arriba son todos
 * grandes (26 para arriba) porque la pantalla se mira de lejos, pero hay lineas
 * que NO son dato: la fecha del build, por que arranco, cuanto lleva en marcha.
 * Para esas esta este, que ademas evita que la tarjeta de la version crezca
 * hasta recortarse (16-oct-2026: el usuario vio informacion perdida arriba y
 * abajo del numero de version). */
#define FUENTE_PIE             lv_font_inter_18_es

#define FUENTE_NEGRITA         lv_font_inter_bold_26_es
#define FUENTE_NEGRITA_GRANDE  lv_font_inter_bold_34_es
#define FUENTE_NEGRITA_TITULAR lv_font_inter_bold_40_es

/* ── Traduccion de los nombres que ya usaba la UI ───────────────────────────
 *
 * ESTO NO ES OPCIONAL, y se descubrio de la peor manera (16-oct-2026, el
 * usuario: "el texto pequeñisimo"): la UI llama a las fuentes por su nombre
 * viejo en mas de 100 sitios (lv_font_montserrat_16 es la de los botones
 * pequeños, la 20 la de los rotulos...). Si estos alias NO estan, esos nombres
 * no dan error de compilacion: tiran de las Montserrat que trae el SDK, que
 * siguen compiladas, y el texto sale al tamaño LITERAL (16 px) en vez del papel
 * que le tocaba (30). Al reescribir la escala plana se quedaron fuera y media
 * interfaz se puso con la letra pequeña.
 *
 * Cada nombre viejo apunta al papel que YA renderizaba (la 16 y la 22 acababan
 * las dos en 30, la 14 y la 20 en 26, la 24 y la 32 en 40), asi que con esto la
 * placa se ve como se veia antes de tocar nada. */
#define lv_font_montserrat_14   FUENTE_MUY_PEQUENA
#define lv_font_montserrat_16   FUENTE_PEQUENA
#define lv_font_montserrat_20   FUENTE_NORMAL
#define lv_font_montserrat_22   FUENTE_MEDIA
#define lv_font_montserrat_24   FUENTE_MEDIA_GRANDE
#define lv_font_montserrat_32   FUENTE_GRANDE

/* ── La ESCALA de la pantalla ────────────────────────────────────────────────
 *
 * LA CLAVE DE TODO ESTO, y conviene entenderla porque explica los bandazos de
 * hoy: esta pantalla es de 800x480. La UI venia de una de 480x320 (->
 * "apaisada" en los comentarios viejos). 800/480 = 1,667.
 *
 * Mas resolucion NO es "todo mas grande": es MAS SITIO. Si un boton se define
 * con 120 px de ancho, en esta pantalla sigue midiendo 120 px fisicos, que con
 * 181 ppp son menos centimetros que antes -> se ve PEQUENO. Para que se vea
 * igual de grande que en la de 3,5" hay que multiplicar por 1,667. Y si ademas
 * se quiere MAS grande (que es lo que se ha comprado esta pantalla, y lo que
 * pidio el usuario: "he comprado esta pantalla para que todo se vea mas
 * grande"), se multiplica un poco mas: de ahi el 1,8.
 *
 * Como se usa: ESC(20) = 36. Se aplica a las MEDIDAS (anchos, altos, separaciones,
 * margenes) en el sitio donde se escriben. NO se aplica a la letra: los tamanos
 * de fuente ya estan elegidos a mano en la escala de arriba, que sube dos
 * escalones (no 1,8x) para que el texto no se salga de las cajas.
 *
 * Lo que NO se toca: porcentajes (lv_pct), LV_SIZE_CONTENT, colores, indices y
 * los valores que ya se pusieron a escala nueva a mano (el boton de Volver de
 * 192x56, las casillas de 253, los campos de 50...). */
#define UI_ESCALA   1.8
#define ESC(v)      ((lv_coord_t)((v) * UI_ESCALA))


/* ── REGLA DE DISENO: el selector de moneda (primera de las comunes) ─────────
 *
 * Habia 5 desplegables de moneda en view_registro.c, cada uno con un ancho
 * distinto inventado a mano (90, 36%, 150, flex_grow 2, 20%): ninguno medido
 * contra el texto mas largo que de verdad puede salir ahi ("RON lei", la unica
 * opcion de 7 caracteres de MONEDA_OPCIONES). Con letra grande y un ancho
 * estrecho, esa opcion se recorta.
 *
 * LA REGLA: el ancho de CUALQUIER desplegable de moneda se calcula con esta
 * funcion, nunca a mano. Mide "RON lei" de verdad con la fuente que se vaya a
 * usar (lv_text_get_width, la misma herramienta que ya usa rotulo_ajustar) y le
 * suma el relleno interno + la flecha del desplegable (medido en la placa:
 * sin estos ~44 px la flecha se come la ultima letra). Si algun dia cambia la
 * lista de monedas, basta con tocar MONEDA_OPCIONES: el ancho se recalcula
 * solo. */
#define MONEDA_OPCIONES \
    "EUR\n" "GBP\n" "CHF Fr\n" "SEK kr\n" \
    "NOK kr\n" "DKK kr\n" "PLN zl\n" "CZK Kc\n" \
    "HUF Ft\n" "RON lei"
#define MONEDA_OPCION_MAS_LARGA  "RON lei"
#define MONEDA_DD_RELLENO  44

static inline lv_coord_t moneda_dd_ancho(const lv_font_t *fuente)
{
    return lv_text_get_width(MONEDA_OPCION_MAS_LARGA,
                             (uint32_t)strlen(MONEDA_OPCION_MAS_LARGA),
                             fuente, 0)
           + MONEDA_DD_RELLENO;
}


/* ── REGLA DE DISENO: contraste del texto en los campos (segunda comun) ──────
 *
 * Los textarea (Importe, Litros, Kilometros...) son la "pastilla" blanca del
 * tema claro de LVGL por defecto (CONFIG_LV_THEME_DEFAULT_DARK no esta
 * activado): ninguno fijaba el color del valor ni del texto de relleno
 * ("0.00"/"0"), asi que dependian de lo que trajera el tema. Pedido por el
 * usuario: que el texto destaque mas sobre ese fondo blanco.
 *
 * LA REGLA: todo textarea de dato pasa por esta funcion. Valor en negro
 * puro (maximo contraste posible sobre blanco) y el texto de relleno en un
 * gris bien oscuro (no el gris claro de serie, que sobre blanco casi no se
 * ve) para que se note que es una pista y no un dato ya metido. */
#define COL_VALOR_CAMPO       UI_COLOR_SOBRE_CLARO_HEX   /* texto sobre el campo BLANCO (ver ui_style.h) */
#define COL_PLACEHOLDER_CAMPO 0x555555

static inline void campo_texto_contraste(lv_obj_t *ta)
{
    /* El VALOR tecleado usa LV_PART_MAIN (el label interno del textarea). El
     * texto de relleno ("0.00") es una parte DISTINTA de verdad en LVGL 9,
     * LV_PART_TEXTAREA_PLACEHOLDER (ver draw_placeholder() en
     * lv_textarea.c) -- sin saber eso, fijar un color "0" los pisaria entre
     * si y uno de los dos se quedaria con el color que no toca. */
    lv_obj_set_style_text_color(ta, lv_color_hex(COL_VALOR_CAMPO), LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, lv_color_hex(COL_PLACEHOLDER_CAMPO),
                                LV_PART_TEXTAREA_PLACEHOLDER);
}


/* ── REGLA DE DISENO: alto de casilla UNICO, 50 (tercera comun) ───────────────
 *
 * Primero se probo un alto proporcional a la fuente de cada campo (ver
 * historial): arreglaba el hueco de Litros/Kilometros (letra 32, caja a pelo
 * en 72) pero dejaba su caja mas alta que la de Importe (letra 24, caja 50) --
 * visto en la placa: "veo diferentes de tamano vertical entre importe y
 * litros y kilómetros". Pedido explicito del usuario: TODAS las casillas
 * donde se introducen datos miden 50, sea cual sea su fuente. Importe ya
 * estaba en 50 y se confirmo correcto ("importe esta ok"): es la referencia. */
#define CAMPO_DATO_ALTO  50


/* ── REGLA DE DISENO: color alterno por fila en listas (cuarta comun) ────────
 *
 * Listas largas de casilla+importe (Servicios, Aguas): todas las filas en el
 * mismo blanco no dejaban distinguir una de la siguiente de un vistazo.
 * Pedido: que la fila de "Agua potable" (o la que toque) se vea de un color
 * distinto a la de al lado. Dos tonos que alternan por indice -- ni sacan otro
 * significado (no son categorias) ni compiten con el color de la categoria
 * del formulario (la del boton Volver/titulo), que se queda igual. */
#define COL_FILA_PAR    UI_COLOR_TEXT_HEX  /* blanco: filas 0, 2, 4... */
#define COL_FILA_IMPAR  UI_COLOR_TEXT_DIM_HEX  /* gris azulado: filas 1, 3, 5... */

static inline uint32_t fila_color_alterna(uint8_t indice)
{
    return (indice % 2 == 0) ? COL_FILA_PAR : COL_FILA_IMPAR;
}


/* ── Ajuste automatico del texto de un boton ─────────────────────────────────
 *
 * POR QUE: al subir la escala de fuentes, los rotulos largos de los botones
 * empezaron a salirse -- medido con las metricas reales de las fuentes,
 * "Terminar salida" en letra 40 mide 350 px y su boton tiene 260: se salia
 * 102 px. Ir arreglando boton a boton es pelearse con el sintoma; lo que hace
 * falta es que el rotulo se ajuste al hueco que tiene.
 *
 * COMO: se mide el texto con la fuente de verdad (lv_font_get_glyph_width, que
 * es sumar los anchos de los glifos) y se baja de escalon SOLO lo que haga
 * falta. Se aplica al pintar (LV_EVENT_DRAW_MAIN) y no solo al crear, porque
 * muchos de estos botones son cajas flexibles cuyo ancho definitivo no se sabe
 * hasta que LVGL hace el reparto.
 *
 * El tope inferior (FUENTE_MUY_PEQUENA) es a proposito: por debajo de esa letra
 * el boton ya no se lee de lejos y lo que hay que cambiar es el rotulo, no
 * seguir encogiendo.
 *
 * OJO, ESTO YA SE HIZO MAL UNA VEZ (8-oct-2026, y la placa se quedo colgada):
 * la primera version aplicaba la fuente desde el evento DRAW_MAIN, o sea
 * DENTRO del pintado. Cambiar la fuente invalida el objeto, la invalidacion
 * vuelve a pintarlo y el pintado vuelve a entrar aqui: recursion hasta que
 * salta el watchdog (medido: "task_wdt: Task watchdog got triggered", con la
 * traza repitiendo lv_obj_redraw -> rotulo_ajustar_cb -> lv_obj_set_style_text_font).
 * Por eso ahora se hace con LV_EVENT_SIZE_CHANGED / STYLE_CHANGED y con un
 * cerrojo de reentrada: fuera del pintado y una sola vez por cambio. */
/* ── Medir un texto: CARACTERES, no BYTES (corregido el 16-oct-2026) ─────────
 *
 * La version anterior recorria la cadena byte a byte y sumaba
 * lv_font_get_glyph_width() por byte. Con texto ASCII da igual, pero las
 * etiquetas SI llevan acentos (y las Inter los traen), y una letra acentuada son
 * DOS bytes en UTF-8: cada byte de mas contaba como un glifo de relleno. Medido
 * con las metricas reales: "Configuración" mide 200,5 px y la cuenta por bytes
 * estimaba 222,5 (+11 %), asi que el auto-ajuste elegia un escalon MENOS del que
 * cabia y el rotulo salia mas pequeno de lo necesario.
 *
 * lv_text_get_width() ya recorre la cadena como UTF-8 (un caracter por letra) y
 * es la misma herramienta que usan moneda_dd_ancho() aqui al lado y
 * rotulo_ajustar(). */
static inline lv_coord_t texto_ancho(const lv_font_t *f, const char *txt)
{
    if (!f || !txt) return 0;
    return lv_text_get_width(txt, (uint32_t)strlen(txt), f, 0);
}

/* Escalones que se prueban, de mayor a menor. El ultimo es el tope inferior:
 * por debajo, lo que hay que cambiar es el rotulo, no seguir encogiendo. */
/* El ultimo escalon es la RED DE SEGURIDAD: por debajo de el, un rotulo que no
 * quepa significa que el texto es demasiado largo para ese boton y lo que hay
 * que cambiar es el texto. Se llega hasta la 18 (que en esta pantalla se lee,
 * aunque sea pequena) porque hay botones justos con textos legitimos: medido,
 * "Apartarlo (era una prueba)" mide 284 px en letra 20 y su boton tiene ~350. */
#define ROTULO_ESCALONES { &FUENTE_GRANDE, &FUENTE_MEDIA_GRANDE, &FUENTE_MEDIA, \
                           &FUENTE_NORMAL, &FUENTE_PEQUENA, &FUENTE_MUY_PEQUENA, \
                           &lv_font_montserrat_18 }

/* ── El ajuste, y POR QUE NO PUEDE IR DENTRO DE UN EVENTO ────────────────────
 *
 * ESTO YA HA COLGADO LA PLACA DOS VECES (8-oct-2026), asi que el como importa
 * tanto como el que:
 *
 *   1. Primera version: el ajuste corria en LV_EVENT_DRAW_MAIN, o sea DENTRO del
 *      pintado. Cambiar la fuente invalida el objeto -> se repinta -> vuelve a
 *      entrar: recursion hasta agotar la pila (traza del watchdog con
 *      lv_obj_redraw / rotulo_ajustar_cb repitiendose).
 *   2. Segunda version: en LV_EVENT_SIZE_CHANGED / STYLE_CHANGED. Parecia lo
 *      correcto, pero cambiar la fuente PROVOCA un cambio de tamano del objeto,
 *      que manda otro SIZE_CHANGED, que vuelve a ajustar... bucle infinito. El
 *      sintoma es inconfundible: "refrescos LVGL: 0.0/s" con el panel barriendo
 *      a 39 Hz y el heap quieto (no se reserva ni se pierde memoria: solo gira).
 *      Se reprodujo abriendo el formulario de Peaje, que es el que tiene el
 *      campo con letra 48.
 *
 * AHORA: un TEMPORIZADOR de LVGL revisa los botones registrados cinco veces por
 * segundo, FUERA de cualquier evento y fuera del pintado. Ahi no hay
 * reentrada posible: se mira el ancho, se elige el escalon y se aplica, y si
 * algo cambia de tamano el siguiente tic lo vuelve a mirar. El coste es
 * despreciable (medir una cadena son unas decenas de sumas).
 *
 * Se decide SIEMPRE por el ancho del BOTON, que no depende de la fuente del
 * rotulo, asi que no puede oscilar entre dos escalones. */
/* ── API del autoajuste (el CODIGO vive en ui/rotulos.c) ────────────────────
 *
 * POR QUE NO ESTA AQUI, si el diseno se explica en este mismo fichero
 * (auditoria del 16-oct-2026): esto eran una tabla y unas funciones `static`
 * DENTRO DE UNA CABECERA, y una cabecera con `static` no es "un solo sitio":
 * cada .c que la incluye se queda con SU COPIA. La incluyen 7 unidades de
 * compilacion, asi que habia 7 tablas s_rotulos distintas -- y el temporizador
 * lo crea el BSP (esp_bsp.c), que iteraba SU lista vacia mientras los botones
 * se apuntaban a la copia de quien los creaba (view_registro.c,
 * confirm_screen.c...). Resultado: el reajuste periodico NO corria nunca, y
 * confirm_screen.c cuenta con el para reajustar sus botones cuando cambia el
 * TEXTO (su cache solo mira el ancho).
 *
 * Con un .c de verdad hay UNA tabla y UN temporizador. El porque de cada pieza
 * (temporizador y no evento, escalones, cache por ancho) esta en el bloque de
 * arriba, encima de ROTULO_ESCALONES. */
void rotulos_arrancar(void);

/* Deja el rotulo de un boton apuntado para que el temporizador lo ajuste (y le
 * aplica ya el escalon que le toca). */
void rotulo_autoajustable(lv_obj_t *btn, lv_obj_t *lbl);

/* ── Medidas de la pantalla (8-oct-2026) ──────────────────────────────────────
 *
 * POR QUE ESTE BLOQUE: la UI se porto del satelite de 3,5", que dibujaba en
 * 480x320 logicos (apaisado). Esta placa son 800x480 REALES (ver display.h y
 * LCD_H_RES/LCD_V_RES), asi que el alto es el mismo pero SOBRAN 320 px de
 * ancho: por eso la UI salia estrecha y arrimada a la izquierda, y por eso los
 * toques "no encontraban nada" que pulsar (los widgets no estaban donde la
 * pantalla grande los pinta, aunque el tactil si mandaba bien las
 * coordenadas).
 *
 * El eje que manda es el ALTO: 480 px son los mismos que antes, asi que el
 * tamano de letra (que ya se subio un escalon para los 181 ppp de esta
 * pantalla) NO se toca. Lo que se reparte de nuevo es el ancho. */
#define UI_ANCHO   800
#define UI_ALTO    480

/* Margen de la pantalla y hueco entre tarjetas (los mismos que ya usaba la
 * rejilla de la pantalla de datos: 4 y 4). */
#define UI_MARGEN    4
#define UI_HUECO     4

/* Ancho maximo del contenido de UNA columna (formularios y menus de Ajustes).
 *
 * Con 800 px de ancho, un campo de texto a pantalla completa mide 800: el
 * rotulo queda en una punta y el valor en la otra, y la vista se lee fatal. La
 * solucion es la de siempre en una pantalla ancha: el contenido se queda en una
 * columna centrada de ancho COMODO y el resto es margen.
 *
 * 560 px es el ancho al que un campo "Precio/litro" + su moneda siguen
 * leyendose de un vistazo. Si algun dia hay una pantalla mas ancha, se sube
 * este numero y ya esta: no hay que tocar ninguna vista. */
#define UI_ANCHO_COLUMNA  560

/* Margen lateral de la columna de contenido, y su variante ESTRECHA.
 *
 * Los formularios con dos campos por fila (o con un desplegable y un selector
 * al lado del importe, como la pernocta) necesitan mas ancho: con el margen
 * ancho, el contenido se queda en 320 px y las tres cosas de la fila de precio
 * salen apretadas y pegadas a la izquierda. Con UI_MARGEN_ANCHO el contenido
 * respira (680 px) sin llegar a los 800, que es lo que se lee mal. */
#define UI_MARGEN_ANCHO  60
