/* view_registro.c - Menu de registros con iconos (Fase 2, ampliado).
 *
 * Rejilla 3+3 a pantalla completa (iconos del set built-in de LVGL, sin assets
 * de imagen), cada celda con su color: cinco categorias de registro, que abren
 * su formulario con boton de volver, y una sexta de **Wi-Fi** que salta a la
 * pantalla de ajustes -- antes era el engranaje de la esquina de la vista
 * principal, que se quito para no tener dos puertas a lo mismo.
 *
 * Tocar un campo NO saca el teclado aqui: abre el editor a pantalla completa
 * de entry_screen.c (valor en letra 40 + teclado de 240 px).
 *
 * VIAJE es contextual: sin viaje solo ofrece "Iniciar viaje" (finalizar lo que
 * no ha empezado no significa nada); con viaje en marcha ofrece "Anotar
 * parada" en grande y "Finalizar viaje" pequeno abajo. De ahi cuelgan dos
 * pantallas mas que no tienen casilla propia en el menu: PARADA (donde has
 * parado y que has hecho) y, dentro de ella, SERVICIOS del area.
 *
 * El envio a la P4 (Fase 4) ya esta hecho: el inicio del viaje va DIRECTO
 * (net/p4_api.c, porque la carpeta no existe hasta que la P4 lo confirma) y
 * el resto de apuntes se encola y se entrega cuando la P4 responde
 * (net/viaje_cola.c) -- ver viaje_do_iniciar() y viaje_do_finalizar().
 */
#include "view_registro.h"
#include "nav.h"
#include "entry_screen.h"
#include "confirm_screen.h"
#include "view_info.h"
#include "../icons/iconos.h"
#include "estilos.h"   /* escala de fuentes y medidas de la pantalla */
#include "config_storage.h"
#include "p4_api.h"
#include "viaje_cola.h"
#include "apunte.h"
#include "tilt.h"
#include "reloj.h"
#include "salida.h"
#include "../data_model.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "view_registro";

/* Las cinco primeras son las casillas del menu. Las dos ultimas NO tienen
 * casilla: son pantallas que cuelgan de Viaje (parada) y de parada
 * (servicios). Van en la misma lista porque comparten todo el andamiaje --
 * contenedor oculto, cabecera con Volver, limpieza al salir. */
typedef enum {
    CAT_REPOSTAJE = 0,
    CAT_PEAJE,
    CAT_BOMBONA,
    CAT_MANTENIMIENTO,
    CAT_SERVICIOS,
    CAT_VALORACION,
    /* Los dos formularios de CIERRE que faltaban del rediseno del 23-ago-2026.
     * Van AL FINAL a proposito: el orden de este enum es el de CAT_CLAVE, y
     * ese es el de las columnas del CSV de la P4. */
    CAT_AGUAS,
    CAT_ITV,
    /* El cierre de una PERNOCTA: precio, servicios y valoracion. Es lo que se
     * pregunta al marcharte, que es cuando de verdad lo sabes. */
    CAT_PERNOCTA,
    CAT_COUNT
} categoria_t;

/* Destino del boton Volver de una cabecera: al menu de iconos o a otra
 * pantalla (parada vuelve a viaje, servicios vuelve a parada). */
#define BACK_TO_GRID  (-1)

/* La pantalla de SERVICIOS se abre desde dos sitios (el cierre de una pernocta
 * y el formulario viejo de parada), asi que su Volver no puede estar clavado a
 * uno: vuelve a s_serv_desde, que pone quien la abre. */
#define BACK_TO_ORIGEN  (-2)


static lv_obj_t *s_forms[CAT_COUNT];

/* Que evento se esta CERRANDO ahora mismo, o -1. Un formulario abierto desde
 * el menu crea un apunte nuevo; abierto para cerrar un evento, tiene que
 * llevar el id que se reservo AL DECLARARLO (de eso vive la deduplicacion de
 * la P4) y sacarlo de la cola al guardarlo. */
static int      s_cerrando = -1;
static uint32_t s_cerrando_id;
/* Puesto a true al final de view_registro_create(). view_registro_reset()
 * puede llegar antes de que la vista exista (el carrusel la crea perezosa). */
static bool      s_ui_lista;

/* --- Reparto del menu, a pantalla completa (800x480, ver estilos.h) --------
 *
 * Los tamanos van en pixeles y no en porcentaje a proposito: en LVGL el hueco
 * entre celdas (pad_gap) NO se descuenta del porcentaje, asi que tres celdas
 * al 33% + dos huecos se salen del ancho y la tercera baja de fila.
 *
 * Rehecho el 8-oct-2026 al pasar de los 480x320 logicos a los 800x480 reales:
 * tres columnas en vez de dos, que es lo que pide una pantalla apaisada de 800
 * (dos casillas de 380 px de ancho para un icono y una palabra quedaban como
 * dos franjas). Con tres:
 *   ancho = (800 - 2*PAD - 2*GAP) / 3 = 253      alto = (480 - BAR_H - 2*PAD - GAP) / 2
 * El alto depende de la franja de arriba (BAR_H), asi que se deja que la fila
 * sea elastica y la casilla ocupe todo el alto disponible. */
#define MENU_PAD     10
#define MENU_GAP     10
/* Margen del cuerpo de una pantalla y hueco entre filas. Viven aqui arriba
 * (y no junto a pantalla_crear(), que es de donde salieron) porque
 * make_form_container() tambien los usa y esta antes en el fichero. */
#define PAN_PAD      12
#define PAN_GAP      10
#define CEL3_W      253
#define CEL2_W      (CEL3_W * 3 / 2 + MENU_GAP / 2)   /* dos casillas + su hueco */

/* Un color por categoria: fondos CLAROS y vivos con el contenido en NEGRO.
 *
 * La primera version usaba la familia Material 800 (oscura) con texto blanco y
 * se veia apagada en la placa: el morado 0x6A1B9A era casi ilegible y el
 * conjunto daba 3,8-6,4:1 de contraste. Invertido, todas pasan de 8:1 -- mas
 * del doble -- que es lo que hace falta con sol de lado dentro de la
 * autocaravana. Contraste de cada una con el negro, calculado sobre la
 * luminancia relativa (WCAG):
 *
 *   azul     0x4FC3F7 -> 10,5:1      naranja  0xFFA726 -> 10,7:1
 *   verde    0x66BB6A ->  8,8:1      turquesa 0x4DB6AC ->  8,6:1
 *   morado   0xCE93D8 ->  8,8:1
 */
#define COL_VIAJE         0x4FC3F7   /* azul     */
#define COL_REPOSTAJE     0x66BB6A   /* verde    */
#define COL_PEAJE         0xCE93D8   /* morado   */
#define COL_BOMBONA       0xFFA726   /* naranja  */
#define COL_MANTENIMIENTO 0x4DB6AC   /* turquesa */
/* Ajustes Wi-Fi: gris azulado a proposito, para que NO parezca una categoria
 * de registro mas -- no apunta nada del viaje, configura el aparato.
 * 0xB0BEC5 -> 11,4:1 con el negro. */
#define COL_AJUSTES       0xB0BEC5   /* gris azulado */

/* Contenido de las tarjetas: negro puro, que es lo que da el maximo contraste
 * sobre los fondos claros de arriba. */
#define COL_TILE_FG       0x000000

/* Rotulos de campo ("Importe", "Litros"...) sobre el fondo negro del
 * formulario. Estaban en 0x888888, un gris medio que daba 5,9:1 y en la placa
 * apenas se leia. Subido a casi blanco: 14:1. Se queda un punto por debajo del
 * blanco puro del VALOR para que siga notandose cual es el rotulo y cual el
 * dato, jerarquia que refuerza el tamano (16 el rotulo, 24 el valor). */
#define COL_LABEL         0xDDDDDD

/* Botones de accion, mismo criterio: claros con texto negro.
 *   verde  0x66BB6A -> 8,8:1     rojo 0xE57373 -> 7,0:1
 * El rojo se queda algo por debajo del resto a proposito: bajarlo mas lo
 * volveria rosa palido y "Finalizar viaje" tiene que LEERSE como rojo. */
#define COL_ACCION_OK     0x66BB6A
#define COL_ACCION_STOP   0xE57373

static lv_obj_t *s_peaje_importe_ta;
static lv_obj_t *s_peaje_currency_dd;

/* Copia en memoria de si hay parada abierta, para no leer la NVS cada vez que
 * se entra en la pantalla de Viaje. La NVS manda; esto solo la sigue. */
/* Campo oculto donde el editor a pantalla completa deja el destino tecleado. No
 * se ve nunca: el editor necesita un textarea al que volcar, y en la pantalla de
 * Viaje no hay formulario donde ponerlo. */
static lv_obj_t *s_viaje_destino_ta;
static char      s_viaje_destino[24];

/* --- Parada: donde has parado y que has hecho ------------------------------
 * Varias a la vez: en un area sueles vaciar Y llenar en la misma parada. */
/* Como cobra el sitio de una pernocta. Lo que queda del modelo viejo de parada:
 * el resto -- sus casillas, su formulario y su parada en NVS -- se fue con la
 * limpieza del 24-ago-2026.
 *
  * Un camping cobra por NOCHES; un area, segun cual: las hay por noche y las hay
 * por periodos de 24 h desde que entras, y eso hay que decirlo al llegar, que es
 * cuando tienes el cartel delante. */
#define PARADA_COBRO_NOCHE  0
#define PARADA_COBRO_24H    1

/* Servicios que ofrece el area, en su propia pantalla: las cinco casillas de
 * parada + el precio + estas seis no caben juntas en 320 px de alto. */
/* La ultima no es un servicio: es la puerta a la pantalla de valoracion. */
/* SEIS, y la valoracion ya no es uno de ellos: era una casilla mas de la lista
 * que en realidad abria otra pantalla, y no se entendia. Ahora la nota son TRES
 * BOTONES de color en la ultima linea de esta misma pantalla (usuario,
 * 24-ago-2026, con la placa delante). */
#define SERV_COUNT           6
static const char *const SERV_OPCIONES[SERV_COUNT] = {
    "Agua potable", "Vaciado grises", "Vaciado WC",
    "Electricidad", "Duchas/WC", "Basura"
};
static lv_obj_t *s_serv_chk[SERV_COUNT];
/* Lo que cuesta cada servicio. En un area el agua puede costar 1 euro y la luz
 * 2, y hasta el 24-ago-2026 solo se podia decir que LOS HABIA. La valoracion no
 * lleva importe: no es un servicio, es la puerta a su pantalla. */
static lv_obj_t *s_serv_precio_ta[SERV_COUNT];

/* --- Valoracion del sitio, en pantalla propia -----------------------------
 *
 * Antes era una tira de tres botones que se desplegaba dentro de servicios y
 * habia que apretar las casillas para hacerle sitio. En su propia pantalla hay
 * espacio para botones de un dedo y con color, que es lo que hace falta para
 * acertar sin mirar, y ademas caben debajo las dos pegas del sitio.
 *
 * Verde / ambar / rojo, los mismos tonos claros con texto negro del resto de
 * la interfaz (ver el bloque de colores de arriba). */
#define VALORACION_COUNT 3
static const char *const VALORACION[VALORACION_COUNT] = {
    "Buena", "Aceptable", "Mala"
};
static const uint32_t VALORACION_COL[VALORACION_COUNT] = {
    0x66BB6A, 0xFFA726, 0xE57373
};
static uint8_t   s_val_nota;             /* indice en VALORACION */
/* Mientras no se toque ningun boton NO hay nota, y eso no es lo mismo que
 * "Buena": antes la casilla de servicios hacia de interruptor y sin marcarla no
 * habia valoracion. Sin casilla, hace falta decirlo aparte. */
static bool      s_val_puesta;
static lv_obj_t *s_val_btn[VALORACION_COUNT];
static lv_obj_t *s_val_btn_lbl[VALORACION_COUNT];

/* Las pegas del sitio: no son notas sino cosas que pueden pasar con cualquier
 * nota (un sitio recomendable puede no tener sombra). Por eso son casillas
 * sueltas y no opciones de la nota. */
/* CUATRO desde el 24-ago-2026 (antes ruidoso y sin sombra). Salen al pulsar
 * CUALQUIERA de las tres notas, no solo la mala: un sitio bueno puede ser
 * ruidoso, y eso es lo que uno quiere recordar antes de volver. */
#define VAL_EXTRA_COUNT 4
static const char *const VAL_EXTRAS[VAL_EXTRA_COUNT] = {
    "Poco seguro", "Ruidosa", "Inclinada", "Sin sombra"
};
static lv_obj_t *s_val_extra_chk[VAL_EXTRA_COUNT];

/* Mantenimiento: varias casillas a la vez, no una opcion. Con el mismo
 * kilometraje puedes haber hecho el aceite Y su filtro.
 *
 * Los filtros (antes tres casillas sueltas) van agrupados bajo una sola
 * "Filtros": al marcarla se despliegan las cuatro opciones (aceite, gasoil,
 * aire, habitaculo) en un check-grid propio, igual que "Ruedas" despliega su
 * contador. Aqui SI se puede marcar mas de uno a la vez -- en la misma
 * revision cambian el de aceite y el de aire juntos. */
#define MANT_COUNT 6
#define MANT_IDX_FILTROS 1
#define MANT_IDX_RUEDAS  3
#define MANT_IDX_OTROS   5          /* la ultima: abre pantalla para el motivo */
static const char *const MANT_OPCIONES[MANT_COUNT] = {
    "Aceite", "Filtros", "Correa", "Ruedas", "Lavado", "Otros"
};
static lv_obj_t *s_mant_chk[MANT_COUNT];
static lv_obj_t *s_mant_otros_ta;    /* oculto, 0x0: el motivo lo teclea
                                       * entry_screen sobre este textarea */

#define MANT_FILTRO_COUNT 4
static const char *const MANT_FILTRO_OPCIONES[MANT_FILTRO_COUNT] = {
    "Filtro aceite", "Filtro gasoil", "Filtro aire", "Filtro habitaculo"
};
static lv_obj_t *s_mant_filtro_chk[MANT_FILTRO_COUNT];
static lv_obj_t *s_filtros_screen;   /* pantalla propia, tapa el formulario */

static lv_obj_t *s_mant_ruedas_bm;   /* cuantas ruedas; oculto si no se marcan */
static lv_obj_t *s_mant_km_ta;
static lv_obj_t *s_mant_coste_ta;

static lv_obj_t *s_bombona_cuantas_bm;
static lv_obj_t *s_bombona_precio_ta;
static lv_obj_t *s_bombona_currency_dd;

static lv_obj_t *s_repo_importe_ta;
static lv_obj_t *s_repo_litros_ta;
static lv_obj_t *s_repo_km_ta;
static lv_obj_t *s_repo_currency_dd;
static lv_obj_t *s_repo_preciolitro_lbl;

/* --- Aguas: que se ha hecho y lo que ha costado cada cosa ----------------
 *
 * Se declara de un toque al llegar -- con la manguera en la mano no se rellena
 * un formulario -- y se pregunta al volver a dar el contacto, que es cuando ya
 * se sabe lo que ha costado.
 *
 * UN PRECIO POR CADA COSA y no uno total (decision del usuario, 24-ago-2026):
 * en muchas areas el vaciado es gratis y el agua se paga, y con un importe
 * unico no habria manera de saber cual de las dos era. */
typedef enum {
    AGUA_GRISES = 0,
    AGUA_WC,
    AGUA_LLENADO,
    AGUA_COUNT
} agua_idx_t;

static const char *const AGUA_OPCIONES[AGUA_COUNT] = {
    "Vaciar grises", "Vaciar wc", "Llenar agua"
};
static lv_obj_t *s_agua_chk[AGUA_COUNT];
static lv_obj_t *s_agua_precio_ta[AGUA_COUNT];
static lv_obj_t *s_agua_currency_dd;

/* --- ITV -----------------------------------------------------------------
 *
 * Los tres resultados son los NOMBRES OFICIALES de la inspeccion y no un
 * "pasada / no pasada": con desfavorable se puede seguir circulando y hay que
 * volver, con negativa el vehiculo no se puede mover. La diferencia importa
 * demasiado para perderla al anotarla. */
typedef enum {
    ITV_FAVORABLE = 0,
    ITV_DESFAVORABLE,
    ITV_NEGATIVA,
    ITV_RESULTADO_COUNT
} itv_resultado_t;

static const char *const ITV_RESULTADO[ITV_RESULTADO_COUNT] = {
    "Favorable", "Desfavorable", "Negativa"
};
static lv_obj_t *s_itv_resultado_bm;
static lv_obj_t *s_itv_km_ta;
static lv_obj_t *s_itv_precio_ta;
static lv_obj_t *s_itv_currency_dd;

/* --- Cierre de una pernocta ---------------------------------------------
 *
 * Al declararla solo se toco DONDE se duerme (parking/area/camping, gratis o de
 * pago). Lo demas -- lo que costo, que habia y que tal -- se pregunta al
 * marcharte, que es cuando se sabe: si el agua iba, si la luz funcionaba y si
 * se durmio bien.
 *
 * Los datos del evento se copian aqui al abrir el formulario y no se releen al
 * guardar: la hora de fin es la del momento en que dijiste "Terminarla", no la
 * de cuando acabaste de rellenarlo. */
static lv_obj_t *s_pern_info_lbl;      /* "Area de pago  -  2 noches" */
static lv_obj_t *s_pern_precio_row;    /* oculta si el sitio es gratis */
static lv_obj_t *s_pern_precio_lbl;
static lv_obj_t *s_pern_precio_ta;
static lv_obj_t *s_pern_currency_dd;
static lv_obj_t *s_pern_cobro_bm;      /* noche / 24 h; oculto en camping */
static uint8_t   s_pern_sitio;
static uint32_t  s_pern_ini, s_pern_fin;

/* Desde que formulario se abrio la pantalla de servicios (a donde vuelve). */
static int s_serv_desde;

/* Donde se duerme. Viven aqui arriba, y no con el resto de tablas de la parada,
 * porque el resumen y el apunte de la pernocta van antes en el fichero.
 *
 * Nombres de pantalla y CLAVES del CSV separados a proposito, como en todo el
 * fichero: la redaccion de un rotulo puede cambiar, y si las columnas fueran los
 * rotulos ese cambio partiria el historico en dos. */
static const char *const SITIO_NOMBRE[SITIO_COUNT] = {
    "Parking gratis", "Parking de pago", "Area gratis", "Area de pago", "Camping"
};
static const char *const SITIO_CLAVE[SITIO_COUNT] = {
    "parking_gratis", "parking_pago", "area_gratis", "area_pago", "camping"
};

/* Monedas de la Europa continental que puede pisar la autocaravana.
 * Por defecto EUR (indice 0). Mismo orden en las dos listas.
 *
 * SOLO ASCII, y no es un descuido: las fuentes Montserrat que trae LVGL se
 * compilan con el rango "0x20-0x7F,0xB0,0x2022" (ver la cabecera de
 * lv_font_montserrat_24.c), o sea ASCII + grado + bullet. El simbolo del euro
 * (U+20AC), la libra, la "z" polaca con barra y la "c" checa con hacek NO
 * existen en la fuente y salian como un cuadrado vacio. Por eso "zl" y "Kc" van
 * sin diacriticos y EUR/GBP se quedan solo con el codigo.
 *
 * Vale igual para el resto de la interfaz: nada de acentos ni "n" con virgulilla
 * en los textos que se pintan, o apareceran cuadrados. */
#define CURRENCY_OPTIONS \
    "EUR\n" "GBP\n" "CHF Fr\n" "SEK kr\n" \
    "NOK kr\n" "DKK kr\n" "PLN zl\n" "CZK Kc\n" \
    "HUF Ft\n" "RON lei"
static const char *const CURRENCY_CODES[] = {
    "EUR", "GBP", "CHF", "SEK", "NOK", "DKK", "PLN", "CZK", "HUF", "RON"
};

/* El ajuste del rotulo al ancho del boton vive en estilos.h (rotulo_autoajustable),
 * compartido con el dialogo de confirmacion: se hacia en DOS sitios y la primera
 * version, que corria dentro del pintado, colgaba la placa por recursion (ver el
 * comentario de estilos.h). */


/* === Navegacion grid <-> formulario ===================================== */

/* Definidos abajo, junto a los widgets que tocan. */
static void clear_forms(void);
static void filtros_sincroniza(void);
static void filtros_screen_close(void);
static void precio_marca_cb(lv_event_t *e);
/* Definidas mas abajo, con el formulario de la pernocta y con el cierre de la
 * parada; el resumen y el apunte, que van antes en el fichero, las necesitan. */
static uint8_t pern_cobro_actual(void);
static void hora_corta(uint32_t epoch, char *buf, size_t n);
static void parada_terminar(void *ud);

/* --- La parada que NO se declaro -----------------------------------------
 *
 * Paras a comer, no tocas nada y quitas el contacto. Al volver, la pantalla ve
 * un hueco en su marca de vida y lo ofrece: "estuviste parado desde las 14:10".
 *
 * Si dices que si, te lleva a la pantalla de motivos de siempre y el motivo que
 * elijas se anota CON LA HORA DEL APAGON, no con la de ahora. Y como esa parada
 * ya termino -- estas aqui, con el contacto puesto -- se cierra en el acto en
 * vez de quedarse abierta. */
static uint32_t s_olvido_ini;        /* cuando se fue la luz */
static bool     s_olvido_anotando;   /* el proximo motivo es de ESA parada */
static void valoracion_reset(void);

/* Las pantallas de menu. La lista vive aqui arriba y no con su codigo porque
 * cosas de mas arriba necesitan nombrarlas -- el 409 de la P4, sin ir mas
 * lejos, manda a PAN_VIAJE_P4. */
typedef enum {
    PAN_PASEO = 0,       /* indice de TODAS las pantallas (modo paseo) */
    PAN_PRINCIPAL,       /* Nueva salida / Configuracion */
    PAN_TIPO,            /* Viaje / Puntual */
    PAN_SALIDA,          /* Anadir parada / Terminar salida / Configuracion */
    PAN_TIPOS,           /* las seis cosas que se anotan en un viaje */
    PAN_PUNTUAL,         /* las cuatro de una salida puntual */
    PAN_MOTIVO,          /* por que paras */
    PAN_SITIO,           /* donde pasas la noche */
    PAN_ABIERTOS,        /* lo que queda sin cerrar, para poder borrarlo */
    PAN_VIAJE_P4,        /* la P4 tiene un viaje abierto y esta pantalla no */
    PAN_COUNT
} pantalla_t;

static const char *PAN_NOMBRE[PAN_COUNT] = {
    "paseo", "principal", "tipo", "salida", "tipos", "puntual",
    "motivo", "sitio", "abiertos", "viaje_p4",
};

/* Dos destinos de la flecha que NO son pantallas. Van detras de PAN_COUNT para
 * no ocupar sitio en los arrays. */
#define PAN_CANCELA_PUNTUAL  (PAN_COUNT)       /* la flecha cancela, no navega */
#define PAN_VOLVER           (PAN_COUNT + 1)   /* al menu que toque por estado */

/* Definidas abajo, con los menus de la salida. */
static void ocultar_menus(void);
/* Modo paseo: ver la API en view_registro.h. */
static bool s_paseo = false;

static void volver_al_menu(void);
static void mostrar_menu(pantalla_t p);
static void puntual_refresh(void);
static bool avisa_de_lo_abierto(const char *titulo, confirm_cb_t si);

/* Volver al menu deja los formularios EN BLANCO: se vacian sus campos, casillas
 * y selectores. Como es el unico camino de vuelta (boton Volver, guardado
 * confirmado y salida por gesto pasan todos por aqui), basta con hacerlo en un
 * sitio.
 *
 * A que menu se vuelve depende de si hay salida en marcha y de que tipo: lo
 * decide volver_al_menu(). */
static void show_grid(void)
{
    /* Salir del formulario sin guardar deja el evento como estaba: abierto y
     * en la lista. Lo unico que se descarta es la intencion de cerrarlo. */
    s_cerrando = -1;
    /* En paseo NO se vacian los formularios: se va y se vuelve sin perder lo
     * tecleado, que es lo que hace falta para revisarlos uno a uno. */
    if (!s_paseo) {
        clear_forms();
        for (int i = 0; i < CAT_COUNT; i++) {
            lv_obj_add_flag(s_forms[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    volver_al_menu();
}


static void show_form(int idx)
{
    ocultar_menus();
    for (int i = 0; i < CAT_COUNT; i++) {
        if (i == idx) {
            lv_obj_clear_flag(s_forms[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_forms[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void icon_click_cb(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    show_form(idx);
}

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

/* La tarjeta de Wi-Fi no abre un formulario: salta a la pantalla de ajustes,
 * que vive fuera del carrusel. Antes era el engranaje pequeno de la esquina de
 * la vista principal. */
static void ajustes_click_cb(lv_event_t *e)
{
    (void)e;
    nav_open_ajustes();
}

/* El destino viaja +1 en el user_data para que el 0 (CAT_VIAJE) no se
 * confunda con "sin dato". BACK_TO_GRID llega aqui como 0. */
static void back_click_cb(lv_event_t *e)
{
    int dest = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    if (dest == BACK_TO_ORIGEN) show_form(s_serv_desde);
    else if (dest < 0)          show_grid();
    else                        show_form(dest);
}

void view_registro_abrir_sin_cerrar(void)
{
    if (!s_ui_lista || salida_eventos_abiertos() == 0) return;
    entry_screen_close();
    confirm_screen_close();
    filtros_screen_close();
    mostrar_menu(PAN_ABIERTOS);
}

void view_registro_reset(void)
{
    if (!s_ui_lista) return;
    /* En paseo no se toca NADA: ni el formulario ni la pantalla en la que
     * estabas. Es justo lo que se pide al pasear (ver view_registro.h). */
    if (s_paseo) return;
    entry_screen_close();
    confirm_screen_close();
    filtros_screen_close();
    show_grid();
}

/* === Edicion de campos (editor a pantalla completa) ===================== */

/* Tocar un campo abre el editor a PANTALLA COMPLETA (entry_screen.c) en vez de
 * sacar el teclado pequeno tapando el formulario.
 *
 * Se engancha a CLICKED y no a FOCUSED: al volver del editor el campo conserva
 * el foco, asi que con FOCUSED el segundo toque sobre el mismo campo no
 * volveria a abrirlo.
 *
 * El rotulo viaja en el user_data del textarea (siempre un literal, vive toda la
 * ejecucion); el user_data del evento lleva si el campo es numerico. */
static void ta_click_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    bool numeric = (bool)(uintptr_t)lv_event_get_user_data(e);
    entry_screen_open(ta, (const char *)lv_obj_get_user_data(ta), numeric);
}

/* Fila de campo que SE ESTIRA para repartirse el hueco sobrante del formulario.
 *
 * Antes cada fila media 52 px fijos y el resto de la pantalla quedaba negro: el
 * formulario de peaje (un solo campo) desperdiciaba 170 de los 320 px. Con
 * flex_grow las filas se reparten lo que sobre, asi que un formulario corto sale
 * con filas grandes y uno largo (bombona, 4 campos) las deja en su tamano
 * normal. La altura minima evita que se aplasten si algun dia se anaden campos.
 *
 * Dentro va en columna centrada: rotulo arriba, valor debajo. Asi el contenido
 * queda en el medio de la fila por alta que sea, en vez de pegado al borde. */
/* ALTURAS DE LOS CAMPOS. Subidas el 8-oct-2026 al subir toda la letra un
 * escalon (ver estilos.h): el campo de texto pasa de llevar letra 24/32 a
 * 26/40, y con el alto viejo el texto se salia por arriba y por abajo (el
 * usuario lo vio en el formulario de Bombona). Regla: el alto del campo es
 * "linea de la letra mas grande + 12 de aire".
 *
 *   letra 32 -> 38 de linea  ->  50 de campo
 *   letra 40 -> 47 de linea  ->  50 de campo (el mismo sirve) */
#define FIELD_TA_H   50

/* La cabecera crece de 34 a 48 para que quepa el boton de Volver en pastilla.
 * Los 14 px extra los ceden las filas de campo, que son elasticas. */
#define HEADER_H     64

/* Ancho maximo del titulo para no pisar el boton de Volver. Ver add_header(). */
/* El boton de Volver mide 192 de ancho, asi que el titulo tiene que caber en
 * 2*(400 - 192 - 12 de margen) = 392 como mucho; se deja en 300 para que quede
 * aire entre los dos y no se toquen cuando el titulo es largo. */
#define HEADER_TITLE_MAX_W  300

static lv_obj_t *make_field_row(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_width(cont, lv_pct(100));
    lv_obj_set_height(cont, LV_SIZE_CONTENT);
    /* SIN min_height A PROPOSITO (quitado el 8-oct-2026): esta fila tiene dos
     * formatos -- etiqueta encima del campo (columna) y etiqueta al lado
     * (fila, el caso de "Litros + Kilometros") -- y el min_height que se puso
     * para el primero RECORTABA al segundo. Medido: en columna la fila mide
     * 26+50+aire = 84 y el minimo no estorba, pero en fila cada campo mide 50 y
     * con la etiqueta encima son 84+84 = 168: la fila metia 84 y los dos campos
     * se solapaban encima del siguiente. Ahora la fila mide lo que mide su
     * contenido y crece sola. */
    lv_obj_set_flex_grow(cont, 1);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 2, 0);
    lv_obj_set_style_pad_row(cont, 2, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);
    return cont;
}

/* Campo de importe: numero + selector de moneda (por defecto EUR, pero
 * contempla otras monedas de la Europa continental para cuando se viaje
 * fuera de la zona euro). Devuelve la textarea; *dd_out (si no es NULL)
 * se rellena con el dropdown de moneda por si hace falta leerlo luego. */
/* Variante APILADA del campo de importe: la moneda arriba y el numero debajo,
 * los dos a lo ancho y con letra mayor. Solo la usa peaje, que tiene un unico
 * campo y por tanto una fila que se estira hasta ~195 px: sitio de sobra. En
 * repostaje no cabria, porque comparte el alto con litros y precio/litro. */
/* ALTURAS DEL CAMPO GRANDE DE IMPORTE (el de Peaje y el de "Importe" de otros
 * formularios): lleva la letra mas grande de la UI, la 48, cuya linea mide 52
 * px. El campo tiene ademas relleno y borde propios, asi que el hueco interior
 * es el alto menos ~14: con 64 se quedaba en 50 y el texto NO CABIA (52 de
 * linea), y el campo se pasaba el rato recolocandose. Medido y corregido el
 * 8-oct-2026, a raiz del "peaje en negro". */
#define MONEY_BIG_DD_H   56
#define MONEY_BIG_TA_H   72

static lv_obj_t *make_money_field_stacked(lv_obj_t *parent, const char *label_text,
                                           lv_obj_t **dd_out)
{
    lv_obj_t *cont = make_field_row(parent);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_LABEL), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);

    lv_obj_t *ta = lv_textarea_create(cont);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, "0.00");
    lv_obj_set_size(ta, lv_pct(100), MONEY_BIG_TA_H);
    lv_obj_set_style_pad_top(ta, 0, 0);
    lv_obj_set_style_pad_bottom(ta, 0, 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, 0);
    lv_textarea_set_accepted_chars(ta, "0123456789.");
    /* Sin esto no habia tope: un importe/precio de 30+ digitos (tecleado o
     * pegado) desbordaba el snprintf encadenado de repo_recalc_cb (buf[40]) --
     * ver el comentario alli. 10 cifras cubre cualquier importe real de sobra.
     * Detectado auditando el 07-sep-2026. */
    lv_textarea_set_max_length(ta, 10);
    lv_obj_set_user_data(ta, (void *)label_text);
    lv_obj_add_event_cb(ta, ta_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)true);

    /* La moneda ya no reparte fila propia arriba: mas estrecha y clavada
     * abajo a la derecha del campo. FLOATING la saca del flujo flex para
     * poder alinearla a mano sin que empuje al resto de hijos. */
    lv_obj_t *dd = lv_dropdown_create(cont);
    lv_dropdown_set_options(dd, CURRENCY_OPTIONS);
    lv_obj_set_size(dd, ESC(90), MONEY_BIG_DD_H);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_20, 0);
    lv_obj_add_flag(dd, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(dd, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    if (dd_out) *dd_out = dd;

    return ta;
}

/* Media fila: rotulo pequeno arriba y numero grande debajo. Con 'horizontal'
 * (mantenimiento, para hacer sitio a las casillas nuevas) el rotulo pasa a ir
 * a la IZQUIERDA del numero en vez de encima, en una sola linea mas baja. */
static lv_obj_t *make_half_number(lv_obj_t *row, const char *label_text,
                                  bool horizontal)
{
    lv_obj_t *col = lv_obj_create(row);
    lv_obj_set_size(col, lv_pct(48), horizontal ? FIELD_TA_H : lv_pct(100));
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_set_style_pad_column(col, 8, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(col, horizontal ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(col);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_LABEL), 0);
    /* 20 y no 16 como el resto de rotulos: en esta fila el rotulo es lo unico
     * que distingue un numero del otro, asi que tiene que leerse de golpe. */
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);

    lv_obj_t *ta = lv_textarea_create(col);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, "0.00");
    lv_obj_set_size(ta, horizontal ? lv_pct(55) : lv_pct(100),
                    horizontal ? FIELD_TA_H : MONEY_BIG_TA_H);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, 0);
    /* Sin esto el campo no queda centrado en vertical dentro de su alto:
     * mismo caso que Bombona, ver make_money_field. */
    lv_obj_set_style_pad_top(ta, 0, 0);
    lv_obj_set_style_pad_bottom(ta, 0, 0);
    lv_textarea_set_accepted_chars(ta, "0123456789.");
    /* Sin esto no habia tope: un importe/precio de 30+ digitos (tecleado o
     * pegado) desbordaba el snprintf encadenado de repo_recalc_cb (buf[40]) --
     * ver el comentario alli. 10 cifras cubre cualquier importe real de sobra.
     * Detectado auditando el 07-sep-2026. */
    lv_textarea_set_max_length(ta, 10);
    lv_obj_set_user_data(ta, (void *)label_text);
    lv_obj_add_event_cb(ta, ta_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)true);
    return ta;
}

/* Dos numeros en la MISMA linea, cada uno con su rotulo. Para repostaje, que
 * pide importe y litros: puestos uno al lado del otro caben los dos grandes y
 * queda sitio para el precio/litro calculado debajo.
 *
 * 'horizontal' (solo mantenimiento): ademas de compartir fila entre si, cada
 * rotulo pasa a la izquierda de su numero (en vez de encima) y la fila deja
 * de crecer (flex_grow a 0): asi ocupa solo su alto natural, mas bajo, y le
 * deja sitio a las casillas nuevas de Limpieza/Otros. */
static void make_dual_number_row(lv_obj_t *parent,
                                  const char *l1, lv_obj_t **ta1_out,
                                  const char *l2, lv_obj_t **ta2_out,
                                  bool horizontal)
{
    lv_obj_t *cont = make_field_row(parent);
    if (horizontal) lv_obj_set_flex_grow(cont, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (ta1_out) *ta1_out = make_half_number(cont, l1, horizontal);
    if (ta2_out) *ta2_out = make_half_number(cont, l2, horizontal);
}

static lv_obj_t *make_money_field(lv_obj_t *parent, const char *label_text,
                                   lv_obj_t **dd_out, bool moneda_izda)
{
    lv_obj_t *cont = make_field_row(parent);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_LABEL), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);

    /* Sub-fila para poner numero y moneda uno al lado del otro dentro de la
     * columna centrada de make_field_row(). Las dos variantes usan el MISMO
     * alto: FIELD_TA_H ya esta calculado para la letra mas grande que se usa
     * aqui (ver su comentario), asi que ya no hace falta el +10 que llevaba la
     * de Bombona cuando el campo se quedo corto con la letra 32. */
    lv_coord_t ta_h = FIELD_TA_H;
    lv_obj_t *row = lv_obj_create(cont);
    lv_obj_set_size(row, lv_pct(100), ta_h);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ta = lv_textarea_create(row);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, "0.00");
    lv_obj_set_size(ta, lv_pct(62), ta_h);
    lv_obj_set_style_pad_top(ta, 0, 0);
    lv_obj_set_style_pad_bottom(ta, 0, 0);
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ta, moneda_izda ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, 0, 0);
    /* moneda_izda (Bombona, por ahora): un escalon mas grande que el resto
     * de sitios que usan este mismo campo (Repostaje, ITV), que se quedan
     * como estaban. */
    lv_obj_set_style_text_font(ta, moneda_izda ? &lv_font_montserrat_32
                                                : &lv_font_montserrat_24, 0);
    lv_textarea_set_accepted_chars(ta, "0123456789.");
    /* Sin esto no habia tope: un importe/precio de 30+ digitos (tecleado o
     * pegado) desbordaba el snprintf encadenado de repo_recalc_cb (buf[40]) --
     * ver el comentario alli. 10 cifras cubre cualquier importe real de sobra.
     * Detectado auditando el 07-sep-2026. */
    lv_textarea_set_max_length(ta, 10);
    lv_obj_set_user_data(ta, (void *)label_text);
    lv_obj_add_event_cb(ta, ta_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)true);

    lv_obj_t *dd = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, CURRENCY_OPTIONS);
    lv_obj_set_size(dd, lv_pct(36), ta_h);
    lv_obj_align(dd, moneda_izda ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, 0, 0);
    if (moneda_izda) lv_obj_set_style_text_font(dd, &lv_font_montserrat_20, 0);
    if (dd_out) *dd_out = dd;

    return ta;
}

/* Selector de pocas opciones excluyentes (p.ej. cuantas bombonas: 1 o 2).
 * Botonera en vez de desplegable: con dos opciones, un desplegable son dos
 * toques y una lista que tapa la pantalla; asi es un toque directo y con
 * botones grandes, que es lo que hace falta con la autocaravana en marcha.
 * 'map' termina en "" (lo exige lv_btnmatrix). Deja marcada la primera. */
static lv_obj_t *make_choice_row(lv_obj_t *parent, const char *label_text,
                                  const char *map[])
{
    lv_obj_t *cont = make_field_row(parent);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_LABEL), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);

    lv_obj_t *bm = lv_btnmatrix_create(cont);
    lv_btnmatrix_set_map(bm, map);
    lv_obj_set_size(bm, lv_pct(100), FIELD_TA_H + 8);
    lv_obj_set_style_text_font(bm, &lv_font_montserrat_24, 0);
    lv_obj_set_style_bg_opa(bm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bm, 0, 0);
    lv_obj_set_style_pad_all(bm, 0, 0);
    lv_btnmatrix_set_btn_ctrl_all(bm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(bm, true);
    lv_btnmatrix_set_btn_ctrl(bm, 0, LV_BTNMATRIX_CTRL_CHECKED);
    return bm;
}

/* Casillas de verificacion en 2x2, MULTIPLE (no excluyentes).
 *
 * Sustituyen al desplegable del mantenimiento: con el mismo kilometraje puedes
 * haber hecho varias cosas a la vez -- el aceite Y su filtro es el caso
 * tipico -- y un desplegable solo dejaba elegir una.
 *
 * Se reparten en dos columnas para que la casilla y su texto sigan siendo
 * grandes; en una sola columna las cuatro no caben sin encoger. */
#define CHK_H      44   /* alto de cada casilla con su texto (letra 22 + aire) */
#define CHK_GAP     6
/* La pantalla de servicios usa DOS huecos y los cambia sobre la marcha, porque
 * el sitio da para uno o para el otro segun este el selector de valoracion:
 *
 *   selector recogido -> 48 cabecera + 20 rotulo + rejilla + 12 de huecos, con
 *     224 px libres para la rejilla: cuatro filas a 32 con 26 de separacion son
 *     214, entran holgadas y se ve despejado.
 *   selector desplegado -> se le van 56 px, quedan 168 para la rejilla: a 26 de
 *     separacion se saldria (214), asi que se aprieta a 6 y baja a 154.
 *
 * Apretar SOLO mientras eliges la nota es lo que permite tener las dos cosas:
 * casillas separadas el resto del tiempo, que es cuando se tocan. */
#define SERV_CHK_GAP  26

/* 'gap' es el hueco vertical entre filas de casillas. Mantenimiento y parada
 * van con CHK_GAP (6): ahi cada pixel esta comprometido. La pantalla de
 * servicios, en cambio, solo lleva cabecera y casillas, asi que puede
 * permitirse separarlas y ganar puntería. */
static lv_obj_t *make_check_grid(lv_obj_t *parent, const char *const *options,
                                  uint8_t n, lv_obj_t **out, uint32_t color,
                                  lv_coord_t gap)
{
    lv_obj_t *cont = make_field_row(parent);
    /* Sin rotulo: las cuatro opciones se explican solas y el texto de arriba
     * robaba la altura que necesitaban las casillas, que salian cortadas.
     *
     * La altura minima se reserva a mano para las filas que vayan a hacer
     * falta: con LV_SIZE_CONTENT dentro de una fila elastica, si la fila se
     * queda corta el contenido se recorta en silencio (no hay scroll). */
    uint8_t filas = (uint8_t)((n + 1) / 2);
    lv_obj_set_style_min_height(cont, filas * CHK_H + (filas - 1) * gap + 8, 0);

    lv_obj_t *grid = lv_obj_create(cont);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, gap, 0);
    lv_obj_set_style_pad_column(grid, CHK_GAP, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);

    for (uint8_t i = 0; i < n; i++) {
        lv_obj_t *cb = lv_checkbox_create(grid);
        lv_checkbox_set_text(cb, options[i]);
        /* Alto explicito: con LV_SIZE_CONTENT cada casilla salia de una altura
         * distinta segun el texto y la fila de abajo se recortaba. */
        lv_obj_set_size(cb, lv_pct(48), CHK_H);
        lv_obj_set_style_text_color(cb, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(cb, &lv_font_montserrat_20, 0);
        /* Casilla grande: la de serie es diminuta para un dedo en marcha. */
        lv_obj_set_style_width(cb, 28, LV_PART_INDICATOR);
        lv_obj_set_style_height(cb, 28, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(cb, lv_color_hex(color),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_border_color(cb, lv_color_hex(COL_LABEL),
                                      LV_PART_INDICATOR);
        if (out) out[i] = cb;
    }
    return grid;
}

/* Fila COMPACTA de "casilla + importe": a la izquierda lo que se ha hecho, a
 * la derecha lo que ha costado, en el mismo renglon.
 *
 * No usa make_field_row() y no es por capricho: aquella reserva 56 px de minimo
 * y ademas crece. Con tres de estas, el selector de moneda y el boton de
 * guardar, el formulario de aguas se pasaria de los 304 px utiles y Guardar
 * quedaria fuera de la pantalla -- y aqui no hay scroll que valga. Con la
 * altura clavada salen 298. */
#define CHKMONEY_ROW_H  60   /* el de aguas, que tiene sitio de sobra */
#define CHKMONEY_TA_W  150

/* 'alto' porque la misma fila se usa en dos sitios con sitio muy distinto: en
 * aguas son tres y caben holgadas; en servicios son seis mas la valoracion y
 * hay que apretarlas para no tener que deslizar. La letra del importe la elige
 * el alto: por debajo de 40 px la 24 no cabe.
 *
 * 'ta_out' a NULL deja la fila SIN importe: es lo que necesita la casilla de
 * valoracion, que no es un servicio sino la puerta a su pantalla. */
static void make_check_money_row(lv_obj_t *parent, const char *label_text,
                                  lv_obj_t **chk_out, lv_obj_t **ta_out,
                                  lv_coord_t alto)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), alto);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cb = lv_checkbox_create(row);
    lv_checkbox_set_text(cb, label_text);
    lv_obj_align(cb, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_color(cb, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(cb, &lv_font_montserrat_20, 0);
    /* Casilla grande, igual que en make_check_grid(): la de serie es diminuta
     * para un dedo en la cabina. */
    lv_coord_t ind = alto >= 40 ? 28 : 26;
    lv_obj_set_style_width(cb, ind, LV_PART_INDICATOR);
    lv_obj_set_style_height(cb, ind, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(cb, lv_color_hex(COL_VIAJE),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(cb, lv_color_hex(COL_LABEL), LV_PART_INDICATOR);
    if (chk_out) *chk_out = cb;

    if (!ta_out) return;

    lv_obj_t *ta = lv_textarea_create(row);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, "0.00");
    lv_obj_set_size(ta, CHKMONEY_TA_W, alto - 6);
    lv_obj_align(ta, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_font(ta, alto >= 40 ? &lv_font_montserrat_24
                                              : &lv_font_montserrat_20, 0);
    /* Sin el relleno vertical del tema. El numero salia CORTADO por arriba en
     * las filas apretadas de servicios (visto en la placa, 24-ago): la letra 20
     * ocupa 23 px y el tema anade ~5 arriba y ~5 abajo, o sea 33 dentro de una
     * caja de 28. Al no caber, la caja recorta -- y recorta por arriba, que es
     * justo donde esta el numero. */
    lv_obj_set_style_pad_ver(ta, 1, 0);
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, 0);
    lv_textarea_set_accepted_chars(ta, "0123456789.");
    /* Sin esto no habia tope: un importe/precio de 30+ digitos (tecleado o
     * pegado) desbordaba el snprintf encadenado de repo_recalc_cb (buf[40]) --
     * ver el comentario alli. 10 cifras cubre cualquier importe real de sobra.
     * Detectado auditando el 07-sep-2026. */
    lv_textarea_set_max_length(ta, 10);
    lv_obj_set_user_data(ta, (void *)label_text);
    lv_obj_add_event_cb(ta, ta_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)true);
    *ta_out = ta;
}

/* Moneda en UNA linea: rotulo a la izquierda y desplegable a la derecha.
 * make_currency_row() se lleva 68 px con el rotulo encima; en aguas esos 68 px
 * son justo los que necesitan los tres importes. */
#define MONEDA_ROW_H  56
#define MONEDA_DD_W  150

static lv_obj_t *make_currency_inline_row(lv_obj_t *parent, const char *label_text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), MONEDA_ROW_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_LABEL), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);

    lv_obj_t *dd = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, CURRENCY_OPTIONS);
    lv_obj_set_size(dd, MONEDA_DD_W, MONEDA_ROW_H - 2);
    lv_obj_align(dd, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_20, 0);
    return dd;
}

/* Un numero SUELTO a lo ancho, con su rotulo encima. make_dual_number_row()
 * pide dos y la ITV solo tiene uno (los kilometros): el precio va aparte,
 * porque lleva su moneda pegada. */
static lv_obj_t *make_number_field(lv_obj_t *parent, const char *label_text)
{
    lv_obj_t *cont = make_field_row(parent);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_LABEL), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);

    lv_obj_t *ta = lv_textarea_create(cont);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, "0");
    lv_obj_set_size(ta, lv_pct(62), FIELD_TA_H);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, 0);
    /* Sin punto: un cuentakilometros no tiene decimales. */
    lv_textarea_set_accepted_chars(ta, "0123456789");
    /* Tope por la misma razon que en make_money_field* (ver alli): sin el, un
     * cuentakilometros con demasiadas cifras alimentaria el mismo calculo de
     * repo_recalc_cb. 9 cifras cubre cualquier odometro real de sobra. */
    lv_textarea_set_max_length(ta, 9);
    lv_obj_set_user_data(ta, (void *)label_text);
    lv_obj_add_event_cb(ta, ta_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)true);
    return ta;
}

/* Cual de los botones de una botonera esta marcado. lv_btnmatrix_get_selected_btn()
 * NO vale: devuelve el ultimo pulsado, no el que quedo marcado. */
static uint16_t btnmatrix_checked(lv_obj_t *bm, uint16_t count)
{
    for (uint16_t i = 0; i < count; i++) {
        if (lv_buttonmatrix_has_button_ctrl(bm, i, LV_BTNMATRIX_CTRL_CHECKED)) return i;
    }
    return 0;
}

/* Cuantas ruedas hay elegidas ahora mismo (2 o 4, solo pares). */
static unsigned ruedas_elegidas(void)
{
    return (unsigned)((btnmatrix_checked(s_mant_ruedas_bm, 2) + 1) * 2);
}

/* La casilla de Ruedas lleva el numero elegido pegado ("Ruedas: 4"), para
 * verlo sin bajar la vista al selector. Al desmarcar vuelve a su texto seco. */
static void ruedas_actualiza_texto(bool marcado)
{
    lv_obj_t *cb = s_mant_chk[MANT_IDX_RUEDAS];
    if (!marcado) {
        lv_checkbox_set_text(cb, MANT_OPCIONES[MANT_IDX_RUEDAS]);
        return;
    }
    char buf[20];
    snprintf(buf, sizeof(buf), "%s: %u", MANT_OPCIONES[MANT_IDX_RUEDAS],
             ruedas_elegidas());
    lv_checkbox_set_text(cb, buf);   /* set_text copia la cadena */
}

/* Deja marcado el primer boton de una botonera de opcion unica.
 * lv_btnmatrix_set_btn_ctrl() por si solo NO desmarca los demas: el reparto lo
 * hace el manejador interno del widget al pulsar (make_one_button_checked en
 * lv_btnmatrix.c), no la API. Por eso se limpian todos primero. */
static void btnmatrix_reset(lv_obj_t *bm)
{
    lv_btnmatrix_clear_btn_ctrl_all(bm, LV_BTNMATRIX_CTRL_CHECKED);
    lv_btnmatrix_set_btn_ctrl(bm, 0, LV_BTNMATRIX_CTRL_CHECKED);
}

/* Vacia los servicios y la nota del sitio. Se llama al volver al menu: lo
 * marcado era del sitio anterior, y arrastrarlo es como acabo el peaje
 * guardandose el importe del apunte de antes. */
static void servicios_clear(void)
{
    for (uint8_t i = 0; i < SERV_COUNT; i++) {
        lv_obj_clear_state(s_serv_chk[i], LV_STATE_CHECKED);
    }
    for (uint8_t i = 0; i < SERV_COUNT; i++) {
        lv_textarea_set_text(s_serv_precio_ta[i], "");
    }
    valoracion_reset();
}

/* Vacia TODOS los formularios. Lo llama show_grid(), o sea cada vez que se
 * vuelve al menu de iconos, venga de donde venga.
 *
 * Antes solo se ocultaba el formulario y los datos seguian dentro: al reabrir
 * la categoria te encontrabas el importe del peaje anterior. El riesgo no era
 * teclear de mas, era GUARDAR sin mirar el dato de la vez pasada.
 *
 * La moneda vuelve tambien a EUR (indice 0), decision del usuario del
 * 21-ago-2026: fuera de la zona euro obliga a elegirla en cada apunte, pero
 * evita anotar euros con la moneda del pais anterior aun puesta. */
static void clear_forms(void)
{
    lv_textarea_set_text(s_peaje_importe_ta, "");
    lv_dropdown_set_selected(s_peaje_currency_dd, 0);

    lv_textarea_set_text(s_repo_importe_ta, "");
    lv_textarea_set_text(s_repo_litros_ta, "");
    lv_dropdown_set_selected(s_repo_currency_dd, 0);
    /* Despues de vaciar los dos numeros: set_text avisa a repo_recalc_cb, que
     * ya lo deja en "--", pero no depender de ese orden sale mas barato que
     * razonarlo cada vez que se toque el formulario. */
    lv_label_set_text(s_repo_preciolitro_lbl, "--");

    lv_textarea_set_text(s_bombona_precio_ta, "");
    lv_dropdown_set_selected(s_bombona_currency_dd, 0);
    btnmatrix_reset(s_bombona_cuantas_bm);

    lv_textarea_set_text(s_mant_km_ta, "");
    lv_textarea_set_text(s_mant_coste_ta, "");
    lv_textarea_set_text(s_mant_otros_ta, "");
    for (uint8_t i = 0; i < MANT_COUNT; i++) {
        lv_obj_clear_state(s_mant_chk[i], LV_STATE_CHECKED);
    }
    btnmatrix_reset(s_mant_ruedas_bm);
    /* Quitar el estado a mano NO dispara VALUE_CHANGED, asi que el texto de la
     * casilla ("Ruedas: 4") y el ocultado de la fila del contador hay que
     * rehacerlos aqui; si no, ruedas_toggle_cb no se entera. */
    ruedas_actualiza_texto(false);
    lv_obj_add_flag(lv_obj_get_parent(s_mant_ruedas_bm), LV_OBJ_FLAG_HIDDEN);

    for (uint8_t i = 0; i < MANT_FILTRO_COUNT; i++) {
        lv_obj_clear_state(s_mant_filtro_chk[i], LV_STATE_CHECKED);
    }
    filtros_sincroniza();
    lv_obj_add_flag(s_filtros_screen, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t i = 0; i < AGUA_COUNT; i++) {
        lv_obj_clear_state(s_agua_chk[i], LV_STATE_CHECKED);
        lv_textarea_set_text(s_agua_precio_ta[i], "");
    }
    lv_dropdown_set_selected(s_agua_currency_dd, 0);

    lv_textarea_set_text(s_pern_precio_ta, "");
    lv_dropdown_set_selected(s_pern_currency_dd, 0);
    btnmatrix_reset(s_pern_cobro_bm);

    btnmatrix_reset(s_itv_resultado_bm);
    lv_textarea_set_text(s_itv_km_ta, "");
    lv_textarea_set_text(s_itv_precio_ta, "");
    lv_dropdown_set_selected(s_itv_currency_dd, 0);

    servicios_clear();
}

/* Al marcar/desmarcar Ruedas aparece o se esconde el contador de cuantas.
 * Se oculta la FILA entera (el padre), no solo la botonera: si no, el rotulo
 * "Cuantas ruedas" se quedaria colgado y la fila seguiria ocupando alto. */
static void ruedas_toggle_cb(lv_event_t *e)
{
    bool marcado = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    lv_obj_t *fila = lv_obj_get_parent(s_mant_ruedas_bm);
    if (marcado) lv_obj_clear_flag(fila, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(fila, LV_OBJ_FLAG_HIDDEN);
    ruedas_actualiza_texto(marcado);
}

/* OJO con lv_btnmatrix: manda LV_EVENT_VALUE_CHANGED YA AL PRESIONAR
 * (lv_btnmatrix.c:462), y la marca CHECKED no se aplica hasta el SOLTAR
 * (lv_btnmatrix.c:503-514), que manda un segundo VALUE_CHANGED.
 *
 * La primera version escondia el selector desde VALUE_CHANGED: se ocultaba al
 * tocar, el dedo se levantaba sobre un objeto ya oculto, el RELEASED nunca
 * llegaba a la botonera y la eleccion NO se aplicaba nunca. Parecia que el
 * selector "no funcionaba".
 *
 * Por eso van separados: el texto se refresca en VALUE_CHANGED (al presionar
 * saldra el valor viejo, pero el segundo aviso del soltar lo corrige) y el
 * cierre se hace en RELEASED. Se puede confiar en ese orden: en
 * lv_event.c:452 el manejador propio del widget corre ANTES que los callbacks
 * anadidos con lv_obj_add_event_cb, asi que al llegar aqui la marca ya esta
 * puesta. */
static void ruedas_num_cb(lv_event_t *e)
{
    (void)e;
    ruedas_actualiza_texto(true);
}

/* Al soltar, el selector ha cumplido: se esconde y el dato se queda a la vista
 * en la propia casilla ("Ruedas: 4"). Asi el formulario recupera la altura y
 * no hay que deslizar para llegar a Guardar.
 *
 * Para cambiarlo se desmarca Ruedas y se vuelve a marcar. Es un caso raro y
 * evita un boton de "editar" ocupando sitio de forma permanente. */
static void ruedas_release_cb(lv_event_t *e)
{
    (void)e;
    ruedas_actualiza_texto(true);
    lv_obj_add_flag(lv_obj_get_parent(s_mant_ruedas_bm), LV_OBJ_FLAG_HIDDEN);
}

/* Anade a 'out' (que ya tiene 'used' bytes escritos) el listado de filtros
 * marcados separados por comas. Lo usan filtros_sincroniza() (el texto de la
 * propia casilla) y el resumen de confirmacion mas abajo -- misma lista, dos
 * sitios; antes se repetia el bucle en cada uno. Devuelve el nuevo 'used'
 * (igual al de entrada si no habia ninguno marcado). */
static size_t filtros_lista_append(char *out, size_t n, size_t used)
{
    bool alguno = false;
    for (uint8_t i = 0; i < MANT_FILTRO_COUNT; i++) {
        if (!lv_obj_has_state(s_mant_filtro_chk[i], LV_STATE_CHECKED)) continue;
        int w = snprintf(out + used, n - used, "%s%s",
                         alguno ? ", " : "", MANT_FILTRO_OPCIONES[i]);
        if (w < 0 || (size_t)w >= n - used) break;
        used += (size_t)w;
        alguno = true;
    }
    return used;
}

/* Filtros: a diferencia de Ruedas, aqui se puede marcar mas de uno (aceite Y
 * aire en la misma revision), asi que en vez de un desplegable en la propia
 * fila se abre una PANTALLA aparte (build_filtros_screen) con las cuatro
 * opciones y un "Aceptar". Esta funcion repasa lo marcado ahi y deja la
 * casilla "Filtros" (marca + texto con la lista) de acuerdo -- se llama al
 * aceptar esa pantalla, no en cada toque: a medias no hay nada que mostrar. */
static void filtros_sincroniza(void)
{
    lv_obj_t *cb = s_mant_chk[MANT_IDX_FILTROS];
    char buf[80];
    size_t prefijo = (size_t)snprintf(buf, sizeof(buf), "%s: ",
                                      MANT_OPCIONES[MANT_IDX_FILTROS]);
    size_t used = filtros_lista_append(buf, sizeof(buf), prefijo);
    bool alguno = used > prefijo;
    lv_checkbox_set_text(cb, alguno ? buf : MANT_OPCIONES[MANT_IDX_FILTROS]);
    if (alguno) lv_obj_add_state(cb, LV_STATE_CHECKED);
    else        lv_obj_clear_state(cb, LV_STATE_CHECKED);
}

/* Tocar la casilla "Filtros" abre su pantalla (el toque tambien la marca/
 * desmarca sola, cosa del checkbox de serie; da igual, filtros_sincroniza()
 * deja el estado real al aceptar). */
static void filtros_abrir_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_clear_flag(s_filtros_screen, LV_OBJ_FLAG_HIDDEN);
}

static void filtros_aceptar_cb(lv_event_t *e)
{
    (void)e;
    filtros_sincroniza();
    lv_obj_add_flag(s_filtros_screen, LV_OBJ_FLAG_HIDDEN);
}

/* Pantalla de filtros: tapa TODO (lv_layer_top(), mismo patron que
 * splash_create() en main.c) y no un hijo del formulario -- si el formulario
 * de mantenimiento hiciera scroll, un hijo suyo se iria con el scroll y
 * dejaria de tapar la cabecera. Sin boton de volver -- la unica salida es
 * "Aceptar", a proposito: no hay nada que cancelar, solo casillas que se
 * pueden dejar todas sin marcar. */
static void build_filtros_screen(void)
{
    s_filtros_screen = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_filtros_screen);
    lv_obj_set_size(s_filtros_screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_filtros_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_filtros_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_filtros_screen, 12, 0);
    lv_obj_set_style_pad_row(s_filtros_screen, 12, 0);
    lv_obj_clear_flag(s_filtros_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_filtros_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_filtros_screen, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *t = lv_label_create(s_filtros_screen);
    lv_label_set_text(t, "FILTROS");
    lv_obj_set_style_text_color(t, lv_color_hex(COL_MANTENIMIENTO), 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_letter_space(t, 1, 0);

    make_check_grid(s_filtros_screen, MANT_FILTRO_OPCIONES, MANT_FILTRO_COUNT,
                    s_mant_filtro_chk, COL_MANTENIMIENTO, CHK_GAP);

    lv_obj_t *ok = lv_btn_create(s_filtros_screen);
    lv_obj_set_size(ok, lv_pct(100), 52);
    lv_obj_set_style_bg_color(ok, lv_color_hex(COL_MANTENIMIENTO), 0);
    lv_obj_set_style_radius(ok, 10, 0);
    lv_obj_add_event_cb(ok, filtros_aceptar_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ok_lbl = lv_label_create(ok);
    lv_label_set_text(ok_lbl, "Aceptar");
    lv_obj_set_style_text_font(ok_lbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(ok_lbl, lv_color_hex(0x000000), 0);
    lv_obj_center(ok_lbl);
}

/* Cierre de seguridad, mismo motivo que entry_screen_close()/
 * confirm_screen_close(): esta pantalla vive en lv_layer_top() (no dentro
 * del carrusel) y su unica salida normal es "Aceptar". Si el usuario se va
 * de Mantenimiento con ella abierta -- un gesto, o un evento externo que
 * fuerza la navegacion, como salida_eventos_abiertos() -- se quedaria
 * flotando encima de cualquier pantalla para siempre, tapando toda la app. */
static void filtros_screen_close(void)
{
    if (s_filtros_screen) lv_obj_add_flag(s_filtros_screen, LV_OBJ_FLAG_HIDDEN);
}

/* "Otros": no es un si/no, lleva motivo escrito. Igual que "destino del
 * viaje" (view_info.c mas abajo), el texto vive en un textarea invisible de
 * tamano 0 y se edita con el editor a pantalla completa (entry_screen.c); la
 * casilla en si no se toca al tocarla, solo abre el editor. */
static void otros_click_cb(lv_event_t *e)
{
    (void)e;
    entry_screen_open(s_mant_otros_ta, "Motivo", false);
}

/* === Valoracion del sitio ================================================= */

static const char *valoracion_elegida(void)
{
    return s_val_puesta ? VALORACION[s_val_nota] : "";
}



/* La elegida va a todo color y con la marca de visto; las otras dos, apagadas.
 * Con solo el borde no se distinguia de lejos y con solo el color tampoco: dos
 * senales a la vez es lo que hace que se lea de una ojeada y con sol de lado. */
static void valoracion_pinta(void)
{
    for (uint8_t i = 0; i < VALORACION_COUNT; i++) {
        bool sel = s_val_puesta && (i == s_val_nota);
        lv_obj_set_style_bg_opa(s_val_btn[i], sel ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_set_style_border_width(s_val_btn[i], sel ? 4 : 0, 0);
        lv_label_set_text_fmt(s_val_btn_lbl[i], "%s%s",
                              sel ? LV_SYMBOL_OK "  " : "", VALORACION[i]);
    }
}

static void valoracion_reset(void)
{
    s_val_nota = 0;
    s_val_puesta = false;
    for (uint8_t i = 0; i < VAL_EXTRA_COUNT; i++) {
        lv_obj_clear_state(s_val_extra_chk[i], LV_STATE_CHECKED);
    }
    valoracion_pinta();
}

/* Pulsar una nota la deja puesta Y abre las pegas. Al pulsar CUALQUIERA de las
 * tres, no solo la mala (usuario, 24-ago): un sitio bueno puede ser ruidoso o
 * estar inclinado, y esas son justo las cosas que hay que saber antes de
 * volver. Se sale con Volver, sin marcar nada si no hay nada que marcar. */
static void valoracion_nota_cb(lv_event_t *e)
{
    s_val_nota = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    s_val_puesta = true;
    valoracion_pinta();
    show_form(CAT_VALORACION);
}

/* Marcar la casilla de servicios abre la pantalla; desmarcarla borra lo que
 * hubiera puesto, que sin valoracion no significa nada.
 *
 * El estado de un checkbox de LVGL cambia al SOLTAR, no al presionar, asi que
 * abrir otra pantalla desde aqui es seguro: cuando llega este aviso el dedo ya
 * se ha levantado y no queda ningun toque pendiente. */
static lv_obj_t *make_readonly_row(lv_obj_t *parent, const char *label_text)
{
    lv_obj_t *cont = lv_obj_create(parent);
    /* 56 y no 42: el valor va con letra 34 (40 px de linea) y con 42 mas 2 de
     * relleno el texto quedaba recortado por arriba y por abajo -- es la fila
     * del precio por litro calculado, que se lee al repostar. */
    lv_obj_set_size(cont, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 2, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_LABEL), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *val = lv_label_create(cont);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_color(val, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_24, 0);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);

    return val;
}

static lv_obj_t *make_form_container(lv_obj_t *parent)
{
    lv_obj_t *form = lv_obj_create(parent);
    lv_obj_set_size(form, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(form, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(form, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(form, 0, 0);
    /* El formulario en si NO lleva relleno: el contenido va dentro de una
     * COLUMNA CENTRADA de ancho comodo (ver abajo), y asi el negro de fuera
     * sigue tapando la pantalla entera. */
    lv_obj_set_style_pad_all(form, 0, 0);
    /* COLUMNA y no fila, con dos hijos: la cabecera (fija) y el contenido (que
     * se desliza). Ver el porque en el comentario de la columna. */
    lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(form, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(form, LV_OBJ_FLAG_HIDDEN);
    /* FUERA DEL LAYOUT DE LA PANTALLA. La pantalla es una COLUMNA FLEX (franja +
     * cuerpo): si los formularios participan, se reparten el alto con el cuerpo
     * y COLAPSAN. Medido: el contenedor salia de 800x6 px, y con el encogia
     * todo lo de dentro (la columna pedia 560 y quedaba en 320, la cabecera se
     * echaba encima de los campos, el "Volver" se salia de su caja y no
     * respondia, y el negro no llegaba a los bordes). Con IGNORE_LAYOUT manda su
     * tamano (100%x100%) y es lo que tiene que ser: una capa del tamano de la
     * pantalla. Mismo apaño que los overlays. */
    lv_obj_add_flag(form, LV_OBJ_FLAG_IGNORE_LAYOUT);

    /* COLUMNA CENTRADA (8-oct-2026). En los 800 px de esta pantalla, un campo a
     * todo lo ancho deja el rotulo en una punta y el valor en la otra, y una
     * fila de dos campos parte el formulario en dos mitades separadas 400 px:
     * se lee fatal. El contenido se queda en UI_ANCHO_COLUMNA (560) centrado,
     * como en la pantalla de Ajustes (mismo criterio, ver estilos.h).
     *
     * Los menus NO pasan por aqui: sus casillas se reparten los 776 px utiles a
     * proposito (ver CEL3_W). */
    /* CABECERA FIJA: el titulo y el boton de Volver NO se deslizan.
     *
     * POR QUE: los formularios largos (Mantenimiento, Servicios) no caben de
     * alto con la letra nueva -- medido, Mantenimiento pasa de 600 px -- y la
     * solucion barata era dejar que se deslice todo. Pero entonces el "Volver"
     * se va fuera de la pantalla y hay que bajar para poder salir, que es
     * justo lo que no se puede pedir. La cabecera se queda arriba (fuera del
     * area que se desliza) y lo unico que se mueve es el contenido.
     *
     * No cuesta nada: el ancho de la cabecera lo pone el contenedor, asi que
     * "Volver" y el titulo siguen cayendo en las esquinas de la pantalla como
     * en los menus. */
    lv_obj_t *cab = lv_obj_create(form);
    /* ANCHO DE PANTALLA (no el de la columna): el titulo se centra DENTRO de su
     * caja, asi que si la cabecera mide lo que la columna el rotulo sale pegado
     * al boton de Volver. Y ALTO FIJO: con LV_SIZE_CONTENT la cabecera se
     * quedaba en 6 px (el contenedor se crea oculto y ese calculo no se
     * resuelve), con lo que el titulo y el boton se salian de su caja. */
    lv_obj_set_width(cab, UI_ANCHO);
    lv_obj_set_height(cab, HEADER_H);
    lv_obj_align(cab, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(cab, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cab, 0, 0);
    lv_obj_set_style_pad_hor(cab, PAN_PAD, 0);
    lv_obj_set_style_pad_top(cab, 4, 0);
    lv_obj_set_style_pad_bottom(cab, 2, 0);
    lv_obj_clear_flag(cab, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *col = lv_obj_create(form);
    /* A LO ANCHO DE LA PANTALLA (800) con margen interior de 60 por lado: 680
     * px utiles. Antes media 560 y encima llevaba margenes, o sea 440 utiles
     * (o 320) sobre una pantalla de 800: todo lo de dentro se calculaba sobre
     * ese ancho y salia apretado por mucho que se afinaran los porcentajes. */
    lv_obj_set_width(col, UI_ANCHO);
    /* El alto que QUEDA de verdad. La columna se coloca en y = HEADER_H + 11
     * (la cabecera tiene 4+2 de relleno y 4 de separacion del flex), asi que su
     * alto no puede ser UI_ALTO - HEADER_H: medido, con 416 se salia 11 px por
     * debajo de la pantalla y el contenido acababa en y=296 dejando ~115 px
     * vacios mas abajo. */
    lv_obj_set_height(col, UI_ALTO - HEADER_H - 11);
    /* Sin flex_grow: con grow se repartia el alto con la cabecera y la dejaba
     * en 6 px. Se coloca a mano debajo de ella. */
    lv_obj_align(col, LV_ALIGN_TOP_MID, 0, HEADER_H + 11);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, UI_MARGEN_ANCHO, 0);
    lv_obj_set_style_pad_bottom(col, 12, 0);
    lv_obj_set_style_pad_row(col, 10, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    /* Solo se desliza en vertical; en horizontal el gesto es del carrusel. */
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_AUTO);



    /* DEVUELVE EL CONTENEDOR, NO LA COLUMNA (esto ya se hizo mal una vez, el
     * 8-oct-2026, y costo un rato largo: "los formularios salen en negro").
     *
     * Quien llama guarda lo que se devuelve en s_forms[], y s_forms[] es lo que
     * se OCULTA al crear el formulario y se ENSENA al abrirlo (show_form /
     * show_grid). Si se devuelve la columna, el que se oculta y se ensena es la
     * columna... y el contenedor de fuera, que nace oculto y no lo enseña
     * nadie, se queda oculto PARA SIEMPRE: el formulario existe, tiene sus
     * campos y sus botones, y no se ve nada de nada (framebuffer 100% negro,
     * medido). Los hijos de un objeto oculto tampoco se pintan, asi que da
     * igual lo bien que este lo de dentro. */
    return form;
}

/* La COLUMNA de contenido de un formulario, que es donde van los campos.
 *
 * El contenedor tiene dos hijos, siempre en este orden: 0 = la cabecera FIJA
 * (titulo y Volver, que no se desliza) y 1 = la columna que se desliza. Los
 * constructores de formulario meten sus widgets en la columna, nunca en el
 * contenedor: si se metieran en el contenedor acabarian fuera de la columna
 * centrada y a 800 px de ancho. */
/* Vuelca al log el arbol de un objeto con posicion y tamano REALES de cada
 * hijo. Es la unica forma de afinar geometria sin ver la pantalla: las fotos
 * dicen QUE se solapa, esto dice CUANTO y DONDE. DIAGNOSTICO. */
static void form_volcar(lv_obj_t *o, int nivel)
{
    for (int i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *c = lv_obj_get_child(o, i);
        const char *tipo = "obj";
        if (lv_obj_check_type(c, &lv_label_class))          tipo = "label";
        else if (lv_obj_check_type(c, &lv_textarea_class))  tipo = "textarea";
        else if (lv_obj_check_type(c, &lv_button_class))    tipo = "boton";
        else if (lv_obj_check_type(c, &lv_dropdown_class))  tipo = "despleg";
        else if (lv_obj_check_type(c, &lv_buttonmatrix_class)) tipo = "matriz";
        ESP_LOGW(TAG, "%*s[%d] %-8s x=%d y=%d %dx%d%s%s%s", nivel * 2, "", i, tipo,
                 (int)lv_obj_get_x(c), (int)lv_obj_get_y(c),
                 (int)lv_obj_get_width(c), (int)lv_obj_get_height(c),
                 lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN) ? " OCULTO" : "",
                 lv_obj_has_flag(c, LV_OBJ_FLAG_FLOATING) ? " FLOTANTE" : "",
                 lv_obj_check_type(c, &lv_label_class) ? lv_label_get_text(c) : "");
        if (nivel < 4) form_volcar(c, nivel + 1);
    }
}

static lv_obj_t *form_col(lv_obj_t *form)
{
    return form ? lv_obj_get_child(form, 1) : NULL;
}

/* Lo mismo, pero con el margen ESTRECHO: el contenido pasa de 320 a 680 px.
 * Para los formularios que van en fila (el precio de la pernocta, los campos
 * por parejas), que con el margen ancho salen apretados. */
static lv_obj_t *form_col_ancha(lv_obj_t *form)
{
    lv_obj_t *col = form_col(form);
    if (col) {
        lv_obj_set_style_pad_left(col, UI_MARGEN_ANCHO, 0);
        lv_obj_set_style_pad_right(col, UI_MARGEN_ANCHO, 0);
    }
    return col;
}

/* Devuelve el rotulo del titulo: la pantalla de viaje lo reescribe segun haya
 * viaje en marcha o no. 'back_to' es a donde lleva el Volver (BACK_TO_GRID al
 * menu de iconos, o el indice de otra pantalla). */
static lv_obj_t *add_header(lv_obj_t *form, const char *title, lv_color_t color,
                             int back_to)
{
    lv_obj_t *row = lv_obj_create(form);
    lv_obj_set_size(row, lv_pct(100), HEADER_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* Volver en forma de pastilla, con el color de la categoria en el borde y
     * en el texto. Antes eran 80x32 en gris sobre negro: se perdia. Al pulsar
     * se invierte (fondo del color, texto blanco) para que se note el toque.
     *
     * 192x56 Y LETRA 26 (8-oct-2026, a peticion del usuario: "el boton de atras
     * tiene que ser mas grande en todas las pantallas"): con 128x42 y letra 20
     * era el boton mas pequeno de la interfaz, y es el que mas se usa -- es la
     * salida de todo. Un boton de 192x56 son 10x14 mm de cristal, que se
     * acierta con el dedo sin mirar. */
    lv_obj_t *back = lv_btn_create(row);
    lv_obj_set_size(back, 192, 56);
    lv_obj_set_style_radius(back, 28, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(back, 2, 0);
    lv_obj_set_style_border_color(back, color, 0);
    lv_obj_set_style_bg_color(back, color, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(back, color, LV_STATE_PRESSED);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(back, back_click_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)(back_to + 1));
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  Volver");
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(back_lbl, color, 0);
    /* Al pulsar el fondo se vuelve del color (claro), asi que el texto pasa a
     * NEGRO, no a blanco: sobre estos tonos vivos el blanco se pierde. */
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(COL_TILE_FG), LV_STATE_PRESSED);
    lv_obj_center(back_lbl);

    /* Centrado en la fila, no respecto al hueco que deja el boton: asi el
     * titulo cae en el eje de la pantalla.
     *
     * Con ANCHO TOPE, y no es un adorno: el boton ocupa de 0 a 128, asi que un
     * titulo centrado solo puede medir 2*(232-128) = 208 px antes de metersele
     * encima. "SERVICIOS DEL AREA" media ~219 y lo pisaba. Fijando el ancho, un
     * titulo demasiado largo sale con puntos suspensivos -- feo, pero visible y
     * sin tapar el boton, en vez de solaparse en silencio.
     * En letra 22 caben ~14 caracteres: "VIAJE EN CURSO" (~175 px) y
     * "MANTENIMIENTO" (~163 px) entran holgados. */
    lv_obj_t *t = lv_label_create(row);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, color, 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    /* ANCHO: el del texto, con tope. Y EL MODO DE RECORTE IMPORTA, que esto ya
     * ha costado dos vueltas: con LV_LABEL_LONG_DOT y ancho de contenido, LVGL
     * mide el ancho a CERO ("no cabe, habria que recortar") y el titulo sale
     * como tres puntitos. Medido en la placa: "..." de 18x36 px en la cabecera
     * de la pernocta. Con CLIP no hay ese calculo: la etiqueta mide su texto y
     * lo que no quepa se corta, sin puntos. */
    lv_obj_set_width(t, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(t, HEADER_TITLE_MAX_W, 0);
    lv_label_set_long_mode(t, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);
    return t;
}

/* === Callbacks de guardado (solo log, ver Fase 4) ======================= */

/* === Confirmacion antes de guardar ====================================== */

static const char *CAT_NOMBRE[CAT_COUNT] = {
    "repostaje", "peaje", "bombona", "mantenimiento",
    "servicios", "valoracion",
    /* "agua" en SINGULAR y no "aguas": la P4 nombra el fichero "<tipo>s.csv"
     * (csv_por_tipo() en config_server_viaje.c), asi que con "aguas" saldria un
     * "aguass.csv". Con "agua" sale "aguas.csv", que es lo que se espera. */
    "agua", "itv",
    /* Hoja propia ("pernoctas.csv" en la P4) y no metida entre las paradas:
     * lleva sitio, precio, servicios y nota, o sea muchas mas columnas, y
     * mezclarlas dejaria paradas.csv con dos cabeceras distintas. */
    "pernocta"
};

/* CLAVES para el JSON y las columnas del CSV de la P4. Separadas de los rotulos
 * de pantalla A PROPOSITO: los rotulos pueden cambiar de redaccion cuando el
 * usuario pida otra cosa, y si las columnas fueran los rotulos, ese cambio
 * partiria en dos el historico de todos los viajes anteriores.
 *
 * Sin espacios ni acentos: acaban siendo cabeceras de una hoja de calculo.
 * EL ORDEN NO SE TOCA -- es el de las columnas. Añadir al final. */
#define CAT_CLAVE CAT_NOMBRE

static const char *const MANT_CLAVE[MANT_COUNT] = {
    "aceite", "filtros", "correa", "ruedas", "lavado", "otros"
};
/* Columnas propias para el detalle de cada filtro, igual que "ruedas_n" va
 * aparte de "ruedas": permite sumar/filtrar cada uno en la hoja de calculo. */
static const char *const MANT_FILTRO_CLAVE[MANT_FILTRO_COUNT] = {
    "filtro_aceite", "filtro_gasoil", "filtro_aire", "filtro_habitaculo"
};
/* Solo los seis servicios; el septimo de SERV_OPCIONES es la puerta a la
 * pantalla de valoracion, no un servicio. */
static const char *const SERV_CLAVE[SERV_COUNT] = {
    "serv_agua", "serv_vaciado_grises", "serv_vaciado_wc",
    "serv_electricidad", "serv_duchas", "serv_basura"
};
/* Y lo que costo cada uno, en columna aparte del 0/1. El DESGLOSE se guarda
 * siempre, aunque en el resumen del viaje los importes se sumen todos juntos a
 * Alojamiento: para volver o no volver a un area, lo que importa es si lo caro
 * era el agua o la luz. */
static const char *const SERV_PRECIO_CLAVE[SERV_COUNT] = {
    "precio_agua", "precio_grises", "precio_wc",
    "precio_luz", "precio_duchas", "precio_basura"
};
static const char *const VAL_EXTRA_CLAVE[VAL_EXTRA_COUNT] = {
    "poco_seguro", "ruidoso", "inclinada", "sin_sombra"
};
/* Aguas: una columna 0/1 por cosa hecha y otra al lado con su importe. En dos
 * columnas y no en una ("gratis" mezclado con numeros) para poder sumar la
 * columna del precio en la hoja de calculo sin limpiarla antes. */
static const char *const AGUA_CLAVE[AGUA_COUNT] = {
    "vaciado_grises", "vaciado_wc", "llenado_agua"
};
static const char *const AGUA_PRECIO_CLAVE[AGUA_COUNT] = {
    "precio_grises", "precio_wc", "precio_agua"
};

static uint32_t cat_color(categoria_t c)
{
    switch (c) {

        case CAT_REPOSTAJE:     return COL_REPOSTAJE;
        case CAT_PEAJE:         return COL_PEAJE;
        case CAT_BOMBONA:       return COL_BOMBONA;
        /* Servicios y valoracion cuelgan de la pernocta: van con su azul para
         * que se vea que son la misma rama. */
        case CAT_SERVICIOS:
        case CAT_VALORACION:    return COL_VIAJE;
        /* Aguas e ITV con el azul de sus casillas del menu, para que se vea que
         * el formulario es el de la casilla que se toco. */
        case CAT_AGUAS:
        case CAT_ITV:
        case CAT_PERNOCTA:      return COL_VIAJE;
        default:                return COL_MANTENIMIENTO;
    }
}

/* Codigo de moneda de un dropdown, en corto ("EUR"). */
static const char *currency_of(lv_obj_t *dd)
{
    uint16_t i = lv_dropdown_get_selected(dd);
    return i < (sizeof(CURRENCY_CODES) / sizeof(CURRENCY_CODES[0]))
           ? CURRENCY_CODES[i] : "EUR";
}

/* Un campo vacio se muestra como "--" para que se vea que falta, en vez de
 * dejar la linea a medias. */
static const char *val_or_dash(lv_obj_t *ta)
{
    const char *t = lv_textarea_get_text(ta);
    return (t && t[0]) ? t : "--";
}

/* Buffer del resumen. Estatico porque el dialogo lo sigue leyendo despues de
 * que save_generic_cb() haya terminado. */
/* 320 y no 256: la pernocta con todo puesto -- sitio, noches, precio de la
 * noche, los seis servicios con su importe, la nota y las cuatro pegas -- ronda
 * los 225 caracteres, y 256 dejaba un margen que no daba para nada. Lo que va al
 * apunte se recorta aparte a 96 (ver apunte_encolar); el desglose de verdad va
 * en columnas, no en esta linea. */
/* 512 y no 320 (7-oct-2026, migracion a IDF 5.5.5): el GCC 14 que trae esa IDF
 * avisa (y con -Werror corta el build) de que el caso de la pernocta junta el
 * sitio + las noches + el precio + un `serv` de 224 + un `extras` de 16, y con
 * 320 no cabe: "output may be truncated writing up to 15 bytes into a region of
 * size between 8 and 295". El aviso era correcto -- con un sitio de nombre largo
 * y seis servicios marcados, la linea se recortaba de verdad. Esta linea es la
 * que se ve en pantalla; el apunte se recorta aparte a 96 (apunte_encolar). */
static char s_resumen[512];

/* Los servicios marcados, en una linea y con su nombre entero: el dialogo baja
 * la letra si hace falta (confirm_screen.c) y "Vaciado grises" se entiende sin
 * pensar, cosa que "Grises" no. En el sitio de "Valoracion" va la NOTA elegida,
 * que es el dato; y detras las pegas, que no caben en su casilla y aqui es
 * donde se repasan antes de guardar.
 *
 * (El formulario viejo de parada lleva su propia copia de esto metida en el
 * switch; se va con el cuando se limpie el modelo viejo.) */
static void serv_lista_txt(char *buf, size_t n)
{
    size_t used = 0;
    uint8_t marcados = 0;
    buf[0] = '\0';
    for (uint8_t i = 0; i < SERV_COUNT; i++) {
        if (!lv_obj_has_state(s_serv_chk[i], LV_STATE_CHECKED)) continue;
        marcados++;
        /* Cada uno con LO QUE COSTO pegado a su nombre, no todo sumado al final
         * (usuario, 24-ago-2026): un total no dice si lo caro fue el agua o la
         * luz, que es justo lo que se repasa antes de aceptar. El que no lleva
         * importe es que fue gratis, y va con su nombre a secas: poner "gratis"
         * seis veces llenaria la linea de nada. */
        const char *p = lv_textarea_get_text(s_serv_precio_ta[i]);
        int w;
        if (p && p[0]) {
            w = snprintf(buf + used, n - used, "%s%s %s", used ? ", " : "",
                         SERV_OPCIONES[i], p);
        } else {
            w = snprintf(buf + used, n - used, "%s%s", used ? ", " : "",
                         SERV_OPCIONES[i]);
        }
        if (w < 0 || (size_t)w >= n - used) return;
        used += (size_t)w;
    }
    /* La nota y sus pegas van detras, con los servicios: es lo mismo que se
     * repasa antes de guardar y no cabe en dos lineas separadas. */
    if (s_val_puesta) {
        int w = snprintf(buf + used, n - used, "%s%s", used ? ", " : "",
                         valoracion_elegida());
        if (w < 0 || (size_t)w >= n - used) return;
        used += (size_t)w;
        marcados++;
        for (uint8_t i = 0; i < VAL_EXTRA_COUNT; i++) {
            if (!lv_obj_has_state(s_val_extra_chk[i], LV_STATE_CHECKED)) continue;
            marcados++;
            int w = snprintf(buf + used, n - used, "%s%s", used ? ", " : "",
                             VAL_EXTRAS[i]);
            if (w < 0 || (size_t)w >= n - used) return;
            used += (size_t)w;
        }
    }
    if (!marcados) snprintf(buf, n, "--");
}

/* Lo que se ha pagado por los servicios, todo junto. Solo para ensenarlo: en el
 * apunte va el desglose, uno por columna. */
static float serv_total(void)
{
    float t = 0;
    for (uint8_t i = 0; i < SERV_COUNT; i++) {
        t += atof(lv_textarea_get_text(s_serv_precio_ta[i]));
    }
    return t;
}

static void build_resumen(categoria_t cat)
{
    /* 220: lo de arriba (96) mas ", Lavado, Otros: " y hasta 40 caracteres de
     * motivo (el tope de s_mant_otros_ta) -- unos 155. Margen si se anaden
     * opciones. */
    char tipo[220];
    switch (cat) {
        case CAT_REPOSTAJE:
            snprintf(s_resumen, sizeof(s_resumen),
                     "Importe:  %s %s\nLitros:  %s\nKm:  %s\n%s",
                     val_or_dash(s_repo_importe_ta), currency_of(s_repo_currency_dd),
                     val_or_dash(s_repo_litros_ta), val_or_dash(s_repo_km_ta),
                     lv_label_get_text(s_repo_preciolitro_lbl));
            break;
        case CAT_PEAJE:
            snprintf(s_resumen, sizeof(s_resumen), "Importe:  %s %s",
                     val_or_dash(s_peaje_importe_ta),
                     currency_of(s_peaje_currency_dd));
            break;
        case CAT_BOMBONA:
            snprintf(s_resumen, sizeof(s_resumen),
                     "Bombonas:  %u\nPrecio:  %s %s",
                     (unsigned)(btnmatrix_checked(s_bombona_cuantas_bm, 2) + 1),
                     val_or_dash(s_bombona_precio_ta),
                     currency_of(s_bombona_currency_dd));
            break;
        case CAT_MANTENIMIENTO: {
            /* Lista con lo marcado. Si no hay nada, "--": guardar un
             * mantenimiento sin decir que se hizo no tiene sentido, y asi se
             * ve en la propia confirmacion antes de aceptar. */
            tipo[0] = '\0';
            size_t used = 0;
            for (uint8_t i = 0; i < MANT_COUNT; i++) {
                if (!lv_obj_has_state(s_mant_chk[i], LV_STATE_CHECKED)) continue;
                /* Las ruedas se anotan con su cantidad: "Ruedas x2". Los
                 * filtros, con cuales: "Filtros: Filtro aceite, Filtro aire". */
                int w;
                if (i == MANT_IDX_RUEDAS) {
                    w = snprintf(tipo + used, sizeof(tipo) - used, "%sRuedas x%u",
                                 used ? ", " : "",
                                 ruedas_elegidas());
                    if (w < 0 || (size_t)w >= sizeof(tipo) - used) break;
                    used += (size_t)w;
                } else if (i == MANT_IDX_FILTROS) {
                    w = snprintf(tipo + used, sizeof(tipo) - used, "%s%s: ",
                                 used ? ", " : "", MANT_OPCIONES[MANT_IDX_FILTROS]);
                    if (w < 0 || (size_t)w >= sizeof(tipo) - used) break;
                    used += (size_t)w;
                    bool alguno = false;
                    for (uint8_t j = 0; j < MANT_FILTRO_COUNT; j++) {
                        if (!lv_obj_has_state(s_mant_filtro_chk[j], LV_STATE_CHECKED)) continue;
                        w = snprintf(tipo + used, sizeof(tipo) - used, "%s%s",
                                     alguno ? ", " : "", MANT_FILTRO_OPCIONES[j]);
                        if (w < 0 || (size_t)w >= sizeof(tipo) - used) break;
                        used += (size_t)w;
                        alguno = true;
                    }
                } else if (i == MANT_IDX_OTROS) {
                    w = snprintf(tipo + used, sizeof(tipo) - used, "%sOtros: %s",
                                 used ? ", " : "", val_or_dash(s_mant_otros_ta));
                    if (w < 0 || (size_t)w >= sizeof(tipo) - used) break;
                    used += (size_t)w;
                } else {
                    w = snprintf(tipo + used, sizeof(tipo) - used, "%s%s",
                                 used ? ", " : "", MANT_OPCIONES[i]);
                    if (w < 0 || (size_t)w >= sizeof(tipo) - used) break;
                    used += (size_t)w;
                }
            }
            snprintf(s_resumen, sizeof(s_resumen), "%s\nKm:  %s\nCoste:  %s",
                     used ? tipo : "--", val_or_dash(s_mant_km_ta),
                     val_or_dash(s_mant_coste_ta));
            break;
        }
        case CAT_AGUAS: {
            /* Salen las TRES lineas siempre, tenga importe o no (usuario,
             * 24-ago-2026, en la placa). Lo que se repasa aqui es el DINERO: la
             * que no lleva importe pone "gratis", que es lo normal en las
             * aguas, y verlas las tres juntas dice de un vistazo lo que ha
             * costado la parada.
             *
             * Ojo: la marca sigue siendo la que dice QUE se hizo, y va aparte
             * en el CSV. Una linea sin marcar sale aqui como "gratis" pero en
             * la hoja de calculo va como no hecha.
             *
             * Si no hay ni marcas ni importes se resume en una linea, "todo
             * gratis", en vez de tres que dicen lo mismo. */
            const char *moneda = currency_of(s_agua_currency_dd);
            bool algo = false;
            for (uint8_t i = 0; i < AGUA_COUNT; i++) {
                const char *p = lv_textarea_get_text(s_agua_precio_ta[i]);
                if (lv_obj_has_state(s_agua_chk[i], LV_STATE_CHECKED) ||
                    (p && p[0])) {
                    algo = true;
                    break;
                }
            }
            if (!algo) {
                snprintf(s_resumen, sizeof(s_resumen), "todo gratis");
                break;
            }
            size_t used = 0;
            s_resumen[0] = '\0';
            for (uint8_t i = 0; i < AGUA_COUNT; i++) {
                const char *p = lv_textarea_get_text(s_agua_precio_ta[i]);
                int w;
                if (p && p[0]) {
                    w = snprintf(s_resumen + used, sizeof(s_resumen) - used,
                                 "%s%s:  %s %s", used ? "\n" : "",
                                 AGUA_OPCIONES[i], p, moneda);
                } else {
                    w = snprintf(s_resumen + used, sizeof(s_resumen) - used,
                                 "%s%s:  gratis", used ? "\n" : "",
                                 AGUA_OPCIONES[i]);
                }
                if (w < 0 || (size_t)w >= sizeof(s_resumen) - used) break;
                used += (size_t)w;
            }
            break;
        }
        case CAT_PERNOCTA: {
            /* Tres lineas: donde y cuantas noches, lo que costo y que habia.
             * En un sitio gratis la del precio no sale -- no hay nada que
             * repasar -- pero la de servicios si: un parking gratis con agua es
             * justo lo que interesa recordar. */
            uint32_t noches = salida_noches(s_pern_ini, s_pern_fin);
            /* 224: con los seis servicios marcados y su importe, mas la nota y
             * las cuatro pegas, la lista se va a ~185 caracteres. Con 160 se
             * cortaba justo por las pegas. */
            char serv[224];
            serv_lista_txt(serv, sizeof(serv));

            char precio[64];
            precio[0] = '\0';
            if (parada_sitio_es_de_pago(s_pern_sitio)) {
                snprintf(precio, sizeof(precio), "Precio/%s:  %s %s\n",
                         pern_cobro_actual() == PARADA_COBRO_24H ? "24h" : "noche",
                         val_or_dash(s_pern_precio_ta),
                         currency_of(s_pern_currency_dd));
            }
            /* La moneda, UNA vez al final. Los importes van pegados a su
             * servicio (ver serv_lista_txt), asi que aqui solo falta decir en
             * que se paga -- y repetirlo seis veces no cabria. En un sitio de
             * pago ya sale arriba con el precio de la noche, pero en uno gratis
             * esta es la unica linea que lo dice. */
            char extras[16];
            extras[0] = '\0';
            if (serv_total() > 0.005f) {
                snprintf(extras, sizeof(extras), "  (%s)",
                         currency_of(s_pern_currency_dd));
            }
            snprintf(s_resumen, sizeof(s_resumen), "%s  -  %u noche%s\n%sServicios:  %s%s",
                     SITIO_NOMBRE[s_pern_sitio], (unsigned)noches,
                     noches == 1 ? "" : "s", precio, serv, extras);
            break;
        }
        case CAT_ITV:
            snprintf(s_resumen, sizeof(s_resumen),
                     "Resultado:  %s\nKm:  %s\nPrecio:  %s %s",
                     ITV_RESULTADO[btnmatrix_checked(s_itv_resultado_bm,
                                                     ITV_RESULTADO_COUNT)],
                     val_or_dash(s_itv_km_ta), val_or_dash(s_itv_precio_ta),
                     currency_of(s_itv_currency_dd));
            break;
        default:
            s_resumen[0] = '\0';
            break;
    }
}

/* === Parada abierta: la que dura varios dias ==============================
 *
 * Una parada en un area, un camping o una pernocta no termina cuando la
 * guardas: termina cuando te vas, que puede ser dias despues y con la pantalla
 * apagada por medio (se va con el contacto). Asi que al guardarla queda
 * ABIERTA, y al volver a encender se pregunta si ha terminado.
 *
 * El unico reloj que hay es el dia de calendario que manda la P4 (mini_proto.h):
 * esta pantalla no tiene RTC ni pila. Sin ese dato no se abre parada -- no
 * habria forma de contar las noches y quedaria colgada preguntando algo
 * incontestable. */
static uint32_t reloj_p4(void)
{
    /* OJO CON LO QUE HABIA AQUI (arreglado el 24-ago-2026): devolvia
     * d.epoch_local TAL CUAL, o sea el ultimo valor recibido, CONGELADO. Con la
     * P4 callada un rato -- se apaga, se sale del alcance, se reinicia -- esta
     * pantalla creia que seguian siendo las 19:40 de cuando dejo de hablar.
     * Cerrar una parada entonces la fechaba en ESE instante: hora de fin
     * equivocada, duracion equivocada y noches mal contadas, sin que nada
     * chirriara.
     *
     * reloj.c existe justo para esto: guarda la hora CON el esp_timer de cuando
     * la supo y le suma lo transcurrido. Los nueve sitios que llaman aqui
     * preguntan lo mismo -- "¿se que hora es?" -- y esa pregunta se contesta
     * bien aunque la P4 no este delante. */
    uint32_t ahora = 0;
    return reloj_ahora(&ahora) ? ahora : 0;
}

/* Distinto de lo de arriba: aqui se pregunta si la P4 esta AHORA a la escucha.
 * Solo importa para iniciar un viaje, que va directo por HTTP y sin ella no hay
 * carpeta donde meter nada -- y no tiene sentido hacer teclear el destino para
 * luego no poder empezar. */
static bool p4_a_la_escucha(void)
{
    mini_data_t d;
    data_model_get(&d);
    uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
    return d.last_update_ms != 0 && (ms - d.last_update_ms) < 5000;
}

/* Texto del aviso segun por que ha fallado viaje_cola_push(), no siempre el
 * mismo cartel: antes "cola llena, la P4 no la vacia" salia igual para un
 * fallo de NVS que nada tiene que ver con que la P4 este apagada -- daba un
 * diagnostico enganoso justo cuando mas hacia falta acertar. Detectado
 * auditando el 07-sep-2026. */
static const char *cola_fallo_texto(viaje_cola_error_t motivo)
{
    switch (motivo) {
        case VIAJE_COLA_ERR_LLENA:
            return "La cola de pendientes esta\nllena y la P4 no la vacia.\nMira si tiene corriente.";
        case VIAJE_COLA_ERR_NVS:
            return "No he podido guardarlo en la\nmemoria del aparato (puede estar\nllena o dando fallos). Avisa de esto.";
        default:
            return "No se ha podido guardar, sin\nmas detalle. Avisa de esto.";
    }
}

/* Monta el apunte de la categoria y lo mete en la cola.
 *
 * El resumen que va al diario del viaje se reaprovecha de s_resumen, el mismo
 * que acabas de ver en la confirmacion: si lo que se guarda no coincidiera con
 * lo que te enseño la pantalla, seria un fallo dificil de pillar. Se le quitan
 * los saltos de linea, que ahi eran para leerlo y en un CSV sobran. */
/* Guard id-estable para "sueltos" (Peaje y similares SIN evento que cerrar),
 * mismo papel que "contado" + s_cerrando_id tienen para un evento: si el
 * primer intento falla, un reintento reutiliza el MISMO id y NO vuelve a
 * contar -- si simplemente sumaramos otra vez, cada intento fallido dejaria
 * un conteo fantasma que nunca llega a la cola, inflando "esperados" para
 * el resto del viaje. Se limpia (pendiente=false) solo al tener EXITO; si el
 * usuario abandona el intento sin reintentar, el conteo ya hecho se queda
 * puesto -- eso SI es la perdida real que "esperados" tiene que reflejar
 * (mismo espiritu que trip_eventos_get(), ver config_storage.c). Requiere
 * que do_save() NO llame a show_grid()/clear_forms() en el fallo de un
 * suelto -- si no, no habria forma de distinguir "reintento de lo mismo" de
 * "entrada nueva" (el formulario vacio no deja ninguna pista). Detectado
 * por el usuario el 09-sep-2026. */
static struct {
    bool     pendiente;
    uint32_t id;
} s_suelto_pendiente[CAT_COUNT];

/* Devuelve si el apunte ha llegado a la cola. El aviso al usuario ya se
 * muestra aqui dentro en los dos casos de fallo; quien llama solo tiene que
 * decidir si puede seguir adelante (por ejemplo, si puede dar por cerrado el
 * evento que se estaba editando) o no. */
static bool apunte_encolar(categoria_t cat)
{
    /* CUERPO_MAX (viaje_cola.h): la PERNOCTA es el apunte mas largo -- lo de
     * una parada (motivo, horas, minutos, noches) mas precio, SEIS servicios
     * con su importe cada uno, valoracion, dos pegas y la inclinacion. Peor
     * caso MEDIDO: 693 bytes. Lo que sobra es para los dos campos de posicion
     * que traera el GPS. Antes era el literal 896 duplicado a mano; ahora es
     * el mismo #define que usa viaje_cola.c para rechazar (con aviso) lo que
     * no le cabe -- no pueden divergir. */
    char b[CUERPO_MAX];
    /* El id: el reservado al declarar el evento si se esta cerrando uno; si
     * el apunte nace aqui (peaje, o los formularios del menu), el de un
     * intento anterior sin exito de ESTA MISMA categoria (ver
     * s_suelto_pendiente arriba), o uno nuevo si no habia ninguno pendiente. */
    uint32_t id_suelto = 0;
    if (s_cerrando < 0) {
        id_suelto = s_suelto_pendiente[cat].pendiente
                  ? s_suelto_pendiente[cat].id
                  : next_trip_seq();
    }
    size_t u = apunte_cabecera(b, sizeof(b),
                               s_cerrando >= 0 ? s_cerrando_id : id_suelto,
                               CAT_CLAVE[cat]);

    switch (cat) {
        case CAT_REPOSTAJE:
            u = apunte_campo_txt(b, sizeof(b), u, "moneda", currency_of(s_repo_currency_dd));
            u = apunte_campo_txt(b, sizeof(b), u, "importe", lv_textarea_get_text(s_repo_importe_ta));
            u = apunte_campo_txt(b, sizeof(b), u, "litros",  lv_textarea_get_text(s_repo_litros_ta));
            u = apunte_campo_txt(b, sizeof(b), u, "km",      lv_textarea_get_text(s_repo_km_ta));
            u = apunte_campo_txt(b, sizeof(b), u, "calculado", lv_label_get_text(s_repo_preciolitro_lbl));
            break;
        case CAT_PEAJE:
            u = apunte_campo_txt(b, sizeof(b), u, "moneda", currency_of(s_peaje_currency_dd));
            u = apunte_campo_txt(b, sizeof(b), u, "importe", lv_textarea_get_text(s_peaje_importe_ta));
            break;
        case CAT_BOMBONA:
            u = apunte_campo_num(b, sizeof(b), u, "cuantas",
                                 btnmatrix_checked(s_bombona_cuantas_bm, 2) + 1);
            u = apunte_campo_txt(b, sizeof(b), u, "moneda", currency_of(s_bombona_currency_dd));
            u = apunte_campo_txt(b, sizeof(b), u, "precio", lv_textarea_get_text(s_bombona_precio_ta));
            break;
        case CAT_MANTENIMIENTO:
            /* Una columna por casilla, con 0/1. Asi se pueden sumar y filtrar
             * en la hoja de calculo; una lista de texto no se puede. */
            for (uint8_t i = 0; i < MANT_COUNT; i++) {
                u = apunte_campo_num(b, sizeof(b), u, MANT_CLAVE[i],
                                     lv_obj_has_state(s_mant_chk[i], LV_STATE_CHECKED) ? 1 : 0);
            }
            u = apunte_campo_num(b, sizeof(b), u, "ruedas_n",
                                 lv_obj_has_state(s_mant_chk[MANT_IDX_RUEDAS], LV_STATE_CHECKED)
                                 ? (long)ruedas_elegidas() : 0);
            /* Mismo patron: columna propia por filtro, para poder sumar y
             * filtrar cada uno por separado en la hoja de calculo. */
            for (uint8_t i = 0; i < MANT_FILTRO_COUNT; i++) {
                u = apunte_campo_num(b, sizeof(b), u, MANT_FILTRO_CLAVE[i],
                                     lv_obj_has_state(s_mant_filtro_chk[i], LV_STATE_CHECKED) ? 1 : 0);
            }
            u = apunte_campo_txt(b, sizeof(b), u, "km",    lv_textarea_get_text(s_mant_km_ta));
            u = apunte_campo_txt(b, sizeof(b), u, "coste", lv_textarea_get_text(s_mant_coste_ta));
            u = apunte_campo_txt(b, sizeof(b), u, "otros_motivo",
                                 lv_textarea_get_text(s_mant_otros_ta));
            break;
        case CAT_AGUAS:
            /* Dos columnas por cosa: el 0/1 de si se hizo y su importe. Lo que
             * no se ha hecho deja el precio VACIO, que no es lo mismo que un
             * cero: cero es "lo hice y fue gratis". */
            u = apunte_campo_txt(b, sizeof(b), u, "moneda",
                                 currency_of(s_agua_currency_dd));
            for (uint8_t i = 0; i < AGUA_COUNT; i++) {
                bool hecho = lv_obj_has_state(s_agua_chk[i], LV_STATE_CHECKED);
                u = apunte_campo_num(b, sizeof(b), u, AGUA_CLAVE[i], hecho ? 1 : 0);
                u = apunte_campo_txt(b, sizeof(b), u, AGUA_PRECIO_CLAVE[i],
                                     hecho ? lv_textarea_get_text(s_agua_precio_ta[i])
                                           : "");
            }
            break;
        case CAT_PERNOCTA: {
            /* Lleva TODO lo de una parada (motivo, horas, minutos) mas lo suyo,
             * porque va a su propia hoja y tiene que valerse sola.
             *
             * Las horas son las del EVENTO, no las de ahora: la de entrada es
             * de cuando lo declaraste y la de salida, de cuando dijiste
             * "Terminarla" -- no de cuando acabaste de rellenar esto. */
            char hi[8], hf[8];
            hora_corta(s_pern_ini, hi, sizeof(hi));
            hora_corta(s_pern_fin, hf, sizeof(hf));
            u = apunte_campo_txt(b, sizeof(b), u, "sitio", SITIO_CLAVE[s_pern_sitio]);
            u = apunte_campo_txt(b, sizeof(b), u, "inicio", hi);
            u = apunte_campo_txt(b, sizeof(b), u, "fin", hf);
            u = apunte_campo_num(b, sizeof(b), u, "minutos",
                                 (long)((s_pern_fin - s_pern_ini) / 60));
            u = apunte_campo_num(b, sizeof(b), u, "noches",
                                 (long)salida_noches(s_pern_ini, s_pern_fin));
            u = apunte_campo_txt(b, sizeof(b), u, "cobro",
                                 pern_cobro_actual() == PARADA_COBRO_24H ? "24h" : "noche");
            u = apunte_campo_txt(b, sizeof(b), u, "moneda",
                                 currency_of(s_pern_currency_dd));
            u = apunte_campo_txt(b, sizeof(b), u, "precio",
                                 lv_textarea_get_text(s_pern_precio_ta));
            /* Dos columnas por servicio: si lo habia y lo que costo. Vacio no
             * es cero: cero es "lo habia y era gratis", que es la mitad de la
             * gracia de anotarlo. */
            for (uint8_t i = 0; i < SERV_COUNT; i++) {
                bool hay = lv_obj_has_state(s_serv_chk[i], LV_STATE_CHECKED);
                u = apunte_campo_num(b, sizeof(b), u, SERV_CLAVE[i], hay ? 1 : 0);
                u = apunte_campo_txt(b, sizeof(b), u, SERV_PRECIO_CLAVE[i],
                                     hay ? lv_textarea_get_text(s_serv_precio_ta[i]) : "");
            }
            /* valoracion_elegida() y no VALORACION[s_val_nota]: sin haber
             * tocado ningun boton NO hay nota, y guardar "Buena" por defecto
             * seria inventarse el dato. Vacio significa "sin valorar". */
            u = apunte_campo_txt(b, sizeof(b), u, "valoracion", valoracion_elegida());
            for (uint8_t i = 0; i < VAL_EXTRA_COUNT; i++) {
                u = apunte_campo_num(b, sizeof(b), u, VAL_EXTRA_CLAVE[i],
                                     lv_obj_has_state(s_val_extra_chk[i], LV_STATE_CHECKED) ? 1 : 0);
            }
            /* Como quedo aparcada. Es un dato DEL SITIO, no del momento: si un
             * area tiene mucha pendiente, conviene saberlo antes de volver. */
            float pitch = 0, roll = 0;
            if (tilt_get(&pitch, &roll)) {
                u = apunte_campo_num(b, sizeof(b), u, "cabeceo_centi", (long)(pitch * 100));
                u = apunte_campo_num(b, sizeof(b), u, "balanceo_centi", (long)(roll * 100));
            }
            break;
        }
        case CAT_ITV:
            u = apunte_campo_txt(b, sizeof(b), u, "resultado",
                                 ITV_RESULTADO[btnmatrix_checked(s_itv_resultado_bm,
                                                                 ITV_RESULTADO_COUNT)]);
            u = apunte_campo_txt(b, sizeof(b), u, "km",
                                 lv_textarea_get_text(s_itv_km_ta));
            u = apunte_campo_txt(b, sizeof(b), u, "moneda",
                                 currency_of(s_itv_currency_dd));
            u = apunte_campo_txt(b, sizeof(b), u, "precio",
                                 lv_textarea_get_text(s_itv_precio_ta));
            break;
        default:
            break;
    }

    /* El resumen, en una linea. */
    char resumen[96];
    size_t j = 0;
    for (size_t i = 0; s_resumen[i] && j + 1 < sizeof(resumen); i++) {
        resumen[j++] = (s_resumen[i] == '\n') ? ' ' : s_resumen[i];
    }
    resumen[j] = 0;
    u = apunte_cerrar(b, sizeof(b), u, resumen);

    /* Se cuenta ANTES de encolar y pase lo que pase: si el apunte se pierde,
     * la P4 tiene que enterarse de que le falta uno. Guard para no recontar
     * un reintento de lo MISMO: en un evento, "contado" sobre su propio
     * slot; en un suelto, s_suelto_pendiente[cat] (ver comentario mas
     * arriba, junto a su declaracion). */
    const salida_evento_t *ev_cerrando = (s_cerrando >= 0) ? salida_evento_en(s_cerrando) : NULL;
    bool ya_contado = (s_cerrando >= 0) ? (ev_cerrando && ev_cerrando->contado)
                                        : s_suelto_pendiente[cat].pendiente;
    if (!ya_contado) {
        trip_eventos_inc();
        if (s_cerrando >= 0) {
            salida_evento_marcar_contado(s_cerrando);
        } else {
            s_suelto_pendiente[cat].pendiente = true;
            s_suelto_pendiente[cat].id = id_suelto;
        }
    }

    if (u == 0) {
        /* No cabe. No deberia pasar nunca -- el buffer se dimensiono para el
         * apunte mas grande con margen -- pero si alguien añade campos y se
         * pasa, mas vale un cartel que un dato perdido en silencio. */
        ESP_LOGE(TAG, "el apunte '%s' NO CABE en el buffer, no se manda", CAT_NOMBRE[cat]);
        confirm_screen_aviso("No he podido apuntarlo",
                             "Este apunte es demasiado largo\ny no se ha guardado. Avisa de\nesto, es un fallo del programa.",
                             COL_ACCION_STOP, "Entendido");
        return false;
    }

    viaje_cola_error_t motivo;
    if (!viaje_cola_push(b, &motivo)) {
        confirm_screen_aviso("No he podido apuntarlo", cola_fallo_texto(motivo),
                             COL_ACCION_STOP, "Entendido");
        return false;
    }
    /* Exito: si era un suelto pendiente, deja de estarlo -- la PROXIMA vez
     * que se guarde esta categoria (aunque sea la puntual/viaje siguiente)
     * es una entrada nueva de verdad, con su propio id. */
    if (s_cerrando < 0) {
        s_suelto_pendiente[cat].pendiente = false;
    }
    return true;
}

/* La accion de verdad, ya confirmada. */
static void do_save(void *user_data)
{
    categoria_t cat = (categoria_t)(uintptr_t)user_data;

    /* Basta con que haya SALIDA, no viaje-de-toda-la-vida: una salida
     * puntual tiene su propia carpeta en la P4 igual que un viaje (ver
     * puntual_declarar_cb), asi que esto vale para las dos. Antes se exigia
     * viaje y un repostaje camino del taller no se podia guardar. */
    if (!salida_hay()) {
        ESP_LOGW(TAG, "'%s' NO se manda: no hay ninguna salida en marcha", CAT_NOMBRE[cat]);
        confirm_screen_aviso("Guardado solo aqui",
                             "No hay ninguna salida en\nmarcha, asi que esto no se\nguarda. Empieza una antes.",
                             COL_ACCION_STOP, "Entendido");
        show_grid();
        return;
    }

    if (!apunte_encolar(cat)) {
        /* El aviso ya lo ha mostrado apunte_encolar(). Si se estaba cerrando
         * un evento, se deja ABIERTO -- igual que hace parada_terminar() --
         * para no perderlo: antes se borraba igual y no habia forma de
         * reintentarlo.
         *
         * Si era un SUELTO (Peaje...), NO se vuelve al menu: show_grid()
         * llama a clear_forms() y vaciaria el dato tecleado, con lo que un
         * reintento seria indistinguible de una entrada nueva y
         * s_suelto_pendiente (ver apunte_encolar) no podria evitar recontar.
         * Se deja el formulario tal cual -- "Guardar" vuelve a intentarlo
         * con el MISMO id; salir a proposito (flecha atras u otra
         * categoria) SI pasa por show_grid()/clear_forms() como siempre, y
         * ahi se da el intento por abandonado (el conteo ya hecho se
         * queda). */
        if (s_cerrando >= 0) {
            show_grid();
        }
        return;
    }

    /* El cuentakilometros del repostaje se guarda para el SIGUIENTE: es lo que
     * permite sacar los litros a los cien sin tener el historico delante. */
    if (cat == CAT_REPOSTAJE) {
        long km = atol(lv_textarea_get_text(s_repo_km_ta));
        if (km > 0) save_ultimo_km((uint32_t)km);
    }

    /* Guardado y entregado a la cola: el evento deja de estar abierto.
     *
     * Esto NO termina la salida puntual aunque solo lleve una parada:
     * rellenar el repostaje/ITV/etc es la parada, pero la salida sigue en
     * marcha hasta volver al garaje (puede que a horas o dias de aqui). El
     * cierre de verdad es un boton aparte, "Terminar salida" en la pantalla
     * de Puntual (ver puntual_terminar_cb) -- mismo criterio que un viaje,
     * que tampoco se cierra al anadir una parada. */
    if (s_cerrando >= 0) {
        salida_evento_borrar(s_cerrando);
        ESP_LOGI(TAG, "evento cerrado con el formulario de %s", CAT_NOMBRE[cat]);
    }
    show_grid();
}

static void save_generic_cb(lv_event_t *e)
{
    categoria_t cat = (categoria_t)(uintptr_t)lv_event_get_user_data(e);
    build_resumen(cat);
    confirm_screen_open("Es correcto?", s_resumen, cat_color(cat), "Si, guardar",
                        NULL, do_save, (void *)(uintptr_t)cat);
}

/* activo/destino/n_eventos se guardan JUNTOS en un solo commit (ver
 * save_trip_inicio/save_trip_fin en config_storage) -- antes eran 2-3
 * llamadas sueltas y un apagon entre medias podia dejar activo=true con
 * destino="" o al reves. Detectado auditando el 07-sep-2026. */
static void viaje_marcar_iniciado(void)
{
    esp_err_t err = save_trip_inicio(s_viaje_destino);
    if (err != ESP_OK) {
        /* Se sigue adelante: el viaje vale para esta sesion, solo se pierde si
         * se va la luz. Peor seria no dejar iniciarlo por un fallo de NVS. */
        ESP_LOGW(TAG, "No se pudo guardar el estado del viaje: %s", esp_err_to_name(err));
    }
    /* s_suelto_pendiente es estado de sesion, no de viaje: sin este reset, un
     * suelto abandonado sin exito en un viaje ANTERIOR dejaria su categoria
     * "pendiente" para siempre, y la primera vez que se intentase esa misma
     * categoria en ESTE viaje reutilizaria el id viejo y no contaria -- justo
     * cuando trip_eventos_get() ya ha vuelto a 1 para este viaje nuevo. */
    memset(s_suelto_pendiente, 0, sizeof(s_suelto_pendiente));
}

static void viaje_marcar_terminado(void)
{
    esp_err_t err = save_trip_fin();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo guardar el estado del viaje: %s", esp_err_to_name(err));
    }
}

/* --- Envio a la P4 (Fase 4, fase 1) ---------------------------------------
 *
 * El viaje NO se da por iniciado hasta que la P4 confirma. Es lo contrario de
 * lo que hacia antes (marcarlo aqui y ya): si se marcara sin confirmacion, la
 * 3.5" diria "viaje en curso" mientras en la tarjeta de la P4 no hay carpeta
 * ninguna, y todo lo que se anotara despues iria a un viaje que no existe. */

static void aviso_envio_fallo(int estado, const char *que)
{
    char cuerpo[160];
    if (estado == 401) {
        snprintf(cuerpo, sizeof(cuerpo),
                 "La P4 no acepta la clave.\nRevisa usuario y clave en\nAjustes.");
    } else if (estado == 409) {
        /* No deberia llegar aqui: el inicio lo desvia a PAN_VIAJE_P4. */
        snprintf(cuerpo, sizeof(cuerpo),
                 "La P4 dice que ya hay un\nviaje abierto.");
    } else if (estado == 0) {
        snprintf(cuerpo, sizeof(cuerpo),
                 "No he podido hablar con la P4.\nComprueba que este encendida.");
    } else {
        snprintf(cuerpo, sizeof(cuerpo), "La P4 ha respondido %d\ny no se ha guardado.", estado);
    }
    char titulo[40];
    snprintf(titulo, sizeof(titulo), "%s sin guardar", que);
    confirm_screen_aviso(titulo, cuerpo, COL_ACCION_STOP, "Entendido");
}

static void inicio_resultado_cb(bool ok, int estado, void *user_data)
{
    (void)user_data;
    /* 409 = la P4 ya tiene un viaje abierto que esta pantalla no conocia (se
     * empezo antes de perder el estado, o desde otro sitio). Decir "terminalo
     * antes" y quedarse ahi era un callejon sin salida: el unico aparato que
     * puede cerrarlo es ESTE, y la P4 vive en la parte de atras. */
    if (!ok && estado == 409) { mostrar_menu(PAN_VIAJE_P4); return; }
    if (!ok) { aviso_envio_fallo(estado, "Viaje"); return; }
    viaje_marcar_iniciado();
    /* La P4 ya ha creado su carpeta; aqui se abre la salida que sostiene los
     * menus. Si fallase (sin hora no puede ser: la acabamos de usar) el viaje
     * quedaria en la P4 y no en la pantalla, y se veria al momento. */
    salida_abrir_viaje(s_viaje_destino);
    ESP_LOGI(TAG, "viaje iniciado en la P4, destino '%s'", s_viaje_destino);
    show_grid();
}

/* Comprueba el GPS antes de abrir la carpeta de un viaje o de una salida
 * puntual: sin fix, la P4 no anota nada en ruta.csv hasta que lo consiga
 * (ver fila_ruta() en config_server_viaje.c, del lado de la P4), y el
 * trayecto de ida se perderia sin que nadie se entere hasta revisarlo en
 * casa. No se obliga a esperar -- a veces no corre prisa (arrancas ya con
 * el motor en marcha, el GPS tarda un par de minutos en frio) y otras si,
 * asi que se deja elegir. Con fix, 'seguir' se llama directo sin preguntar
 * nada. */
static void con_gps_o_seguir(confirm_cb_t seguir)
{
    mini_data_t d;
    data_model_get(&d);
    if (d.gps_estado == 2) { seguir(NULL); return; }
    confirm_screen_open("Sin ubicacion GPS todavia",
                        "Sin ella no se grabara el\ntrayecto de ida hasta que\nla encuentre.",
                        COL_BOMBONA, "Continuar igual", "Cancelar",
                        seguir, NULL);
}

static void viaje_iniciar_real(void *ud)
{
    (void)ud;
    uint32_t ahora = reloj_p4();
    if (!p4_api_viaje_inicio(next_trip_seq(), s_viaje_destino,
                             ahora / 86400u, inicio_resultado_cb)) {
        aviso_envio_fallo(0, "Viaje");
    }
}

/* Viaje no lleva resumen: no hay nada tecleado que repasar. Solo el segundo
 * toque, que aqui importa mas que en ningun sitio -- finalizar un viaje por un
 * roce cierra el registro en curso de la P4. */
static void viaje_do_iniciar(void *ud)
{
    (void)ud;
    uint32_t ahora = reloj_p4();
    if (ahora == 0) {   /* comprobado tambien antes de teclear; puede caerse en medio */
        confirm_screen_aviso("Enciende la P4 primero",
                             "Sin ella no se que dia es,\ny la carpeta del viaje lleva\nla fecha en el nombre.",
                             COL_ACCION_STOP, "Entendido");
        return;
    }
    con_gps_o_seguir(viaje_iniciar_real);
}

/* El FIN va por la COLA, no directo como el inicio, y la diferencia es a
 * proposito:
 *
 *  - El inicio EXIGE la P4 delante (la carpeta lleva la fecha en el nombre), y
 *    hasta que no confirma no hay viaje. Encolarlo no tendria sentido: no se
 *    puede anotar nada de un viaje cuya carpeta aun no existe.
 *  - El fin se apunta cuando llegas, que es justo cuando puedes haber apagado
 *    ya la P4. Y ademas entra en la cola DETRAS de los registros pendientes,
 *    asi que nunca los adelanta: la P4 no cerrara el viaje con apuntes suyos
 *    todavia por llegar.
 *
 * Por eso aqui el viaje se da por terminado en el acto aunque no se haya
 * entregado: la cola sobrevive al apagon y lo entregara. Lo que no puede pasar
 * es que te quedes con "viaje en curso" en pantalla porque la P4 estaba
 * apagada. */
static void viaje_do_finalizar(void *ud)
{
    (void)ud;
    /* trip_eventos_GET y no _inc: el fin es el ultimo evento, no hay nada
     * despues que contar, y con _inc cada reintento fallido subia el contador.
     * Tres intentos con la cola llena y la P4 daba el viaje por incompleto sin
     * faltarle nada. El +1 es este mismo mensaje. */
    char cuerpo[80];
    p4_api_cuerpo_fin(cuerpo, sizeof(cuerpo), next_trip_seq(), trip_eventos_get() + 1);

    viaje_cola_error_t motivo;
    if (!viaje_cola_push(cuerpo, &motivo)) {
        confirm_screen_aviso("No he podido apuntarlo", cola_fallo_texto(motivo),
                             COL_ACCION_STOP, "Entendido");
        return;
    }
    viaje_marcar_terminado();
    salida_cerrar();
    show_grid();
}

/* Se llama al aceptar el editor del destino. */
static void viaje_destino_listo_cb(lv_event_t *e)
{
    (void)e;
    const char *txt = lv_textarea_get_text(s_viaje_destino_ta);
    if (!txt || !txt[0]) return;      /* lo dejo en blanco: no se hace nada */
    snprintf(s_viaje_destino, sizeof(s_viaje_destino), "%s", txt);

    char cuerpo[120];
    snprintf(cuerpo, sizeof(cuerpo), "Destino: %s\n\nLa carpeta se llamara asi\ny NO se podra cambiar.",
             s_viaje_destino);
    confirm_screen_open("Empezar el viaje?", cuerpo, COL_ACCION_OK,
                        "Si, empezar", "Cancelar", viaje_do_iniciar, NULL);
}

static void viaje_iniciar_cb(lv_event_t *e)
{
    (void)e;
    /* Se exige la P4 ANTES de teclear nada: la carpeta del viaje lleva la fecha
     * en el nombre y este aparato no tiene reloj propio. Preguntar el destino
     * para luego no poder empezar seria hacer teclear en balde. */
    if (!p4_a_la_escucha()) {
        confirm_screen_aviso("Enciende la P4 primero",
                             "Sin ella no se que dia es,\ny la carpeta del viaje lleva\nla fecha en el nombre.",
                             COL_ACCION_STOP, "Entendido");
        return;
    }
    lv_textarea_set_text(s_viaje_destino_ta, "");
    entry_screen_open(s_viaje_destino_ta, "Destino", false);
}

static void viaje_finalizar_cb(lv_event_t *e)
{
    (void)e;
    confirm_screen_open("Finalizar el viaje?", NULL, COL_ACCION_STOP,
                        "Si, finalizar", "Cancelar", viaje_do_finalizar, NULL);
    confirm_screen_ok_destructivo();
}

static lv_obj_t *make_save_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_btn_create(parent);
    /* 72 de alto y letra 26 (la de la escala nueva): con 50 y letra 20 el boton
     * mas importante de cada formulario era el mas pequeno de la pantalla, y en
     * una fila de 76 quedaba descolgado. */
    lv_obj_set_size(btn, lv_pct(100), 72);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_ACCION_OK), 0);
    lv_obj_set_style_bg_color(btn, lv_color_darken(lv_color_hex(COL_ACCION_OK), LV_OPA_30),
                              LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TILE_FG), 0);
    lv_obj_center(lbl);
    rotulo_autoajustable(btn, lbl);
    return btn;
}

/* === Repostaje: precio/litro calculado en vivo =========================== */

static void repo_recalc_cb(lv_event_t *e)
{
    (void)e;
    float importe = atof(lv_textarea_get_text(s_repo_importe_ta));
    float litros = atof(lv_textarea_get_text(s_repo_litros_ta));
    uint16_t cur_idx = lv_dropdown_get_selected(s_repo_currency_dd);
    const char *cur = cur_idx < (sizeof(CURRENCY_CODES) / sizeof(CURRENCY_CODES[0]))
                       ? CURRENCY_CODES[cur_idx] : "EUR";
    char buf[40];
    int u = 0;
    if (litros > 0.0f) {
        u = snprintf(buf, sizeof(buf), "%.3f %s/L", importe / litros, cur);
    } else {
        u = snprintf(buf, sizeof(buf), "--");
    }
    /* snprintf devuelve lo que HABRIA escrito sin el limite del buffer, no lo
     * que cupo de verdad: importe no tiene max_length en su textarea, y con un
     * importe/litros enorme (p.ej. importe con 39+ digitos, litros pequeno)
     * "%.3f" puede pasarse de los 40 bytes de buf. Sin este guard, u > sizeof
     * buf y "sizeof(buf) - (size_t)u" (size_t, sin signo) desborda a un numero
     * gigante -- el segundo snprintf de abajo escribiria fuera de buf[40], en
     * pila, con ese tamano. Bug real, detectado auditando el 07-sep-2026. */
    if (u < 0 || (size_t)u >= sizeof(buf)) u = (int)sizeof(buf) - 1;

    /* Los litros a los cien salen solos comparando con el cuentakilometros del
     * repostaje ANTERIOR. Solo si el numero tiene sentido: sin km previo, con
     * el cuentakilometros hacia atras (se tecleo mal) o con un salto absurdo,
     * es mejor no decir nada que decir una cifra inventada. */
    long km  = atol(lv_textarea_get_text(s_repo_km_ta));
    long ant = (long)load_ultimo_km();
    if (litros > 0.0f && ant > 0 && km > ant && (km - ant) < 5000) {
        snprintf(buf + u, sizeof(buf) - (size_t)u, "  -  %.1f L/100",
                 litros * 100.0f / (float)(km - ant));
    }
    lv_label_set_text(s_repo_preciolitro_lbl, buf);
}

/* === Construccion de cada formulario ===================================== */

/* Boton grande de la pantalla de viaje: se estira para llenar el formulario.
 * Al fijar bg_color propio se pierde el realce de pulsado de LVGL, asi que se
 * oscurece a mano (mismo criterio que los iconos del menu). */
/* 'grande' reparte el hueco sobrante del formulario; sin el, el boton se queda
 * en una pastilla baja al final. "Finalizar viaje" va asi a peticion del
 * usuario: pequeno y abajo, para que la accion habitual (anotar parada) se
 * lleve la pantalla y la destructiva no se toque de un roce. */
#define VIAJE_BTN_PEQ_H  46

static void build_repostaje(lv_obj_t *form)
{
    add_header(form, "REPOSTAJE", lv_color_hex(COL_REPOSTAJE), BACK_TO_GRID);

    /* Sin coordenada GPS ni hora a peticion del usuario (20-ago-2026): tecleadas
     * a mano no aportan nada y estorban en el surtidor. Cuando se abra la Fase 4
     * las pone la P4 al recibir el evento, que ya sabe donde y cuando esta. */
    /* El importe lleva su moneda DENTRO de la fila, en vez de una fila propia
     * para la moneda: hacen falta tres numeros (importe, litros y km) y con la
     * moneda aparte no caben los seis renglones en 320 px.
     *
     * Los KILOMETROS son nuevos del rediseno del 23-ago: con ellos salen solos
     * los litros a los cien y el coste por kilometro. Si no se piden desde el
     * primer dia, los repostajes viejos no los tendran nunca. */
    s_repo_importe_ta = make_money_field(form, "Importe", &s_repo_currency_dd, false);
    make_dual_number_row(form, "Litros",     &s_repo_litros_ta,
                               "Kilometros", &s_repo_km_ta, false);
    lv_obj_add_event_cb(s_repo_importe_ta, repo_recalc_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_repo_litros_ta, repo_recalc_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_repo_km_ta, repo_recalc_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_repo_currency_dd, repo_recalc_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_repo_preciolitro_lbl = make_readonly_row(form, "Calculado");

    make_save_button(form, "Guardar repostaje", save_generic_cb, (void *)(uintptr_t)CAT_REPOSTAJE);
}

static void build_peaje(lv_obj_t *form)
{
    add_header(form, "PEAJE", lv_color_hex(COL_PEAJE), BACK_TO_GRID);

    /* Sin coordenada GPS ni hora, igual que repostaje: ver comentario alli.
     * Al ser el unico campo, va en la variante apilada y grande. */
    s_peaje_importe_ta = make_money_field_stacked(form, "Importe",
                                                  &s_peaje_currency_dd);

    make_save_button(form, "Guardar peaje", save_generic_cb, (void *)(uintptr_t)CAT_PEAJE);
}

static void build_bombona(lv_obj_t *form)
{
    add_header(form, "BOMBONA", lv_color_hex(COL_BOMBONA), BACK_TO_GRID);

    /* Solo lo que el aparato no puede saber: cuantas se han comprado y lo que
     * han costado. Coordenada, dia, hora y lugar los rellena la P4 al recibir
     * el evento (Fase 4) -- ella tiene el reloj bueno y no tiene sentido
     * teclearlos a mano en la gasolinera. */
    /* No lleva el segundo 'const' porque lv_btnmatrix_set_map() pide
     * "const char *map[]" y guarda el puntero al array tal cual. */
    static const char *bombona_map[] = { "1", "2", "" };
    s_bombona_cuantas_bm = make_choice_row(form, "Cuantas has comprado",
                                           bombona_map);
    s_bombona_precio_ta  = make_money_field(form, "Precio total",
                                            &s_bombona_currency_dd, true);

    make_save_button(form, "Guardar bombona", save_generic_cb, (void *)(uintptr_t)CAT_BOMBONA);
}

static void build_mantenimiento(lv_obj_t *form)
{
    add_header(form, "MANTENIMIENTO", lv_color_hex(COL_MANTENIMIENTO), BACK_TO_GRID);

    /* Sin coordenada GPS, igual que repostaje, peaje y bombona: la posicion y
     * la fecha las pone la P4 al recibir el evento (Fase 4). Aqui solo va lo
     * que el aparato no puede deducir. */
    make_check_grid(form, MANT_OPCIONES, MANT_COUNT, s_mant_chk, COL_MANTENIMIENTO,
                    CHK_GAP);

    /* Filtros: al tocar la casilla se abre una pantalla propia con las
     * cuatro opciones (aqui SI se puede marcar mas de una: aceite Y aire en
     * la misma revision) y un "Aceptar" que vuelve aqui. */
    lv_obj_add_event_cb(s_mant_chk[MANT_IDX_FILTROS], filtros_abrir_cb,
                        LV_EVENT_CLICKED, NULL);
    build_filtros_screen();

    /* Otros: casilla con motivo escrito, no un si/no. El textarea que guarda
     * el texto es invisible (0x0, fuera del layout): el editor a pantalla
     * completa (entry_screen) es la unica forma de tocarlo. */
    s_mant_otros_ta = lv_textarea_create(form);
    lv_obj_add_flag(s_mant_otros_ta, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_mant_otros_ta, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(s_mant_otros_ta, 0, 0);
    lv_textarea_set_one_line(s_mant_otros_ta, true);
    lv_textarea_set_max_length(s_mant_otros_ta, 40);
    lv_obj_add_event_cb(s_mant_otros_ta, precio_marca_cb,
                        LV_EVENT_VALUE_CHANGED, s_mant_chk[MANT_IDX_OTROS]);
    lv_obj_add_event_cb(s_mant_chk[MANT_IDX_OTROS], otros_click_cb,
                        LV_EVENT_CLICKED, NULL);

    /* Cuantas ruedas. Oculto salvo que se marque Ruedas: la mayoria de los
     * mantenimientos no las tocan y no tiene sentido ocupar sitio siempre. */
    /* Solo pares: las ruedas se cambian por eje, no sueltas. */
    static const char *ruedas_map[] = { "2", "4", "" };
    s_mant_ruedas_bm = make_choice_row(form, "Cuantas ruedas", ruedas_map);
    lv_obj_add_flag(lv_obj_get_parent(s_mant_ruedas_bm), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_mant_chk[MANT_IDX_RUEDAS], ruedas_toggle_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_mant_ruedas_bm, ruedas_num_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_mant_ruedas_bm, ruedas_release_cb,
                        LV_EVENT_RELEASED, NULL);

    /* Km y coste comparten linea: los dos son numeros cortos y asi caben los
     * dos grandes sin robarle altura a las seis casillas. */
    make_dual_number_row(form, "Km", &s_mant_km_ta, "Coste", &s_mant_coste_ta, true);

    make_save_button(form, "Guardar mantenimiento", save_generic_cb, (void *)(uintptr_t)CAT_MANTENIMIENTO);
}

/* Poner un precio MARCA su casilla. Lo usan las aguas y los servicios de una
 * pernocta. Sin esto, un importe tecleado en una linea sin marcar no contaba: no
 * se guardaba y el resumen decia lo contrario de lo que acababas de escribir. La
 * casilla sigue mandando (es la que dice si lo hubo), pero escribir lo que costo
 * ya implica que lo hubo.
 *
 * Solo MARCA, nunca desmarca: un servicio gratis es la casilla marcada y el
 * importe vacio, y ahi borrar el precio no puede deshacer la marca. */
static void precio_marca_cb(lv_event_t *e)
{
    const char *t = lv_textarea_get_text(lv_event_get_target(e));
    if (t && t[0]) lv_obj_add_state(lv_event_get_user_data(e), LV_STATE_CHECKED);
}

static void build_aguas(lv_obj_t *form)
{
    add_header(form, "AGUAS", lv_color_hex(COL_VIAJE), BACK_TO_GRID);

    /* Una fila por cosa, con su casilla y su importe al lado. La casilla dice
     * que se hizo y el importe lo que costo: hacen falta las dos, porque
     * "vaciado gratis" y "no vaciado" son cosas distintas y un hueco vacio no
     * las distingue.
     *
     * Sin coordenada GPS ni hora, igual que los demas formularios: las pone la
     * P4 al recibir el apunte, que tiene el reloj bueno. */
    for (uint8_t i = 0; i < AGUA_COUNT; i++) {
        make_check_money_row(form, AGUA_OPCIONES[i], &s_agua_chk[i],
                             &s_agua_precio_ta[i], CHKMONEY_ROW_H);
        lv_obj_add_event_cb(s_agua_precio_ta[i], precio_marca_cb,
                            LV_EVENT_VALUE_CHANGED, s_agua_chk[i]);
    }

    /* Una moneda para los tres importes: es la misma parada y el mismo pais. */
    s_agua_currency_dd = make_currency_inline_row(form, "Moneda");

    make_save_button(form, "Guardar aguas", save_generic_cb,
                     (void *)(uintptr_t)CAT_AGUAS);
}

static void build_itv(lv_obj_t *form)
{
    add_header(form, "ITV", lv_color_hex(COL_VIAJE), BACK_TO_GRID);

    /* No lleva el segundo 'const' porque lv_btnmatrix_set_map() pide
     * "const char *map[]" y se queda con el puntero al array. */
    static const char *itv_map[] = { "Favorable", "Desfavorable", "Negativa", "" };
    s_itv_resultado_bm = make_choice_row(form, "Resultado", itv_map);
    /* Letra 20 y no la 24 que pone make_choice_row: cada boton se queda con un
     * tercio de los 464 px (154), y "Desfavorable" con la 24 no cabe y sale
     * cortado. */
    lv_obj_set_style_text_font(s_itv_resultado_bm, &lv_font_montserrat_20, 0);

    /* Los kilometros, como en el repostaje: si no se piden desde el primer dia,
     * las ITV viejas no los tendran nunca y no habra manera de saber a que
     * kilometraje toco cada una. */
    s_itv_km_ta = make_number_field(form, "Kilometros");
    s_itv_precio_ta = make_money_field(form, "Precio", &s_itv_currency_dd, false);

    make_save_button(form, "Guardar ITV", save_generic_cb,
                     (void *)(uintptr_t)CAT_ITV);
}

/* Fila de precio de la parada: importe, moneda y tipo de cobro TODOS en la
 * misma linea, y grandes -- son los tres el mismo dato ("cuanto cuesta cada
 * noche / cada 24 h") y leerlos de un vistazo importa mas que su tamano por
 * separado.
 *
 * Reparto elastico y no en pixeles: cuando el sitio es un camping el selector
 * se esconde (alli siempre se cobra por noches) y los otros dos se reparten su
 * hueco solos, en vez de dejar un agujero.
 *
 * No usa make_money_field porque aquella pone el importe y la moneda en letra
 * 24 sin sitio para nada mas. Alturas: rotulo 16 + fila 52 + huecos = 74, que
 * con cabecera 48 + casillas 116 + acciones 50 + 12 de huecos suman 300 de los
 * 304 utiles. */
#define PRECIO_ROW_H   52
#define PRECIO_GROW_TA  4
#define PRECIO_GROW_DD  2
#define PRECIO_GROW_BM  3

/* Los cuatro cacharros de la fila, para que la pueda montar cualquiera de los
 * dos formularios que la usan (el cierre de la pernocta y la parada vieja). */
typedef struct {
    lv_obj_t *lbl;
    lv_obj_t *ta;
    lv_obj_t *dd;
    lv_obj_t *bm;
} precio_row_t;

static lv_obj_t *make_precio_row(lv_obj_t *parent, precio_row_t *o,
                                  lv_event_cb_t cobro_cb)
{
    lv_obj_t *cont = make_field_row(parent);

    o->lbl = lv_label_create(cont);
    lv_label_set_text(o->lbl, "Precio");
    lv_obj_set_style_text_color(o->lbl, lv_color_hex(COL_LABEL), 0);
    lv_obj_set_style_text_font(o->lbl, &lv_font_montserrat_16, 0);

    lv_obj_t *row = lv_obj_create(cont);
    lv_obj_set_size(row, lv_pct(100), PRECIO_ROW_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    o->ta = lv_textarea_create(row);
    lv_textarea_set_one_line(o->ta, true);
    lv_textarea_set_placeholder_text(o->ta, "0.00");
    lv_obj_set_height(o->ta, lv_pct(100));
    lv_obj_set_flex_grow(o->ta, PRECIO_GROW_TA);
    lv_obj_set_style_text_font(o->ta, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_align(o->ta, LV_TEXT_ALIGN_CENTER, 0);
    lv_textarea_set_accepted_chars(o->ta, "0123456789.");
    lv_textarea_set_max_length(o->ta, 10);   /* mismo motivo que make_money_field* */
    lv_obj_set_user_data(o->ta, (void *)"Precio por noche");
    lv_obj_add_event_cb(o->ta, ta_click_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)true);

    o->dd = lv_dropdown_create(row);
    lv_dropdown_set_options(o->dd, CURRENCY_OPTIONS);
    lv_obj_set_height(o->dd, lv_pct(100));
    lv_obj_set_flex_grow(o->dd, PRECIO_GROW_DD);
    lv_obj_set_style_text_font(o->dd, &lv_font_montserrat_20, 0);

    /* Excluyente y con "Noche" de partida: es lo normal, y el area de 24 h se
     * marca cuando toca. */
    static const char *cobro_map[] = { "Noche", "24 h", "" };
    o->bm = lv_btnmatrix_create(row);
    lv_btnmatrix_set_map(o->bm, cobro_map);
    lv_obj_set_height(o->bm, lv_pct(100));
    lv_obj_set_flex_grow(o->bm, PRECIO_GROW_BM);
    lv_obj_set_style_text_font(o->bm, &lv_font_montserrat_20, 0);
    lv_obj_set_style_bg_opa(o->bm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o->bm, 0, 0);
    lv_obj_set_style_pad_all(o->bm, 0, 0);
    lv_btnmatrix_set_btn_ctrl_all(o->bm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(o->bm, true);
    lv_btnmatrix_set_btn_ctrl(o->bm, PARADA_COBRO_NOCHE, LV_BTNMATRIX_CTRL_CHECKED);
    /* En RELEASED y no en VALUE_CHANGED: lv_btnmatrix avisa ya al presionar y
     * la marca no se aplica hasta soltar (ver el comentario de las ruedas). */
    lv_obj_add_event_cb(o->bm, cobro_cb, LV_EVENT_RELEASED, NULL);

    return cont;
}

/* Pantalla de parada. Se llega desde Viaje y su Volver regresa alli, no al
 * menu: es la vuelta natural de donde has venido.
 *
 * Reparto de los 320 px, que van justos: cabecera 48 + cinco casillas en tres
 * filas 116 + precio ~62 + la fila de acciones 54 = 280, mas 12 de huecos,
 * dentro de los 304 utiles. Por eso los servicios del area viven en OTRA
 * pantalla: seis casillas mas (otros 116) no caben de ninguna manera.
 *
 * "Servicios del area" comparte fila con Guardar en vez de llevar la suya: asi
 * no cuesta ni un pixel de alto, y cuando no hay area marcada Guardar se queda
 * con toda la fila. */
/* Como se paga la noche AHORA MISMO en el formulario de la pernocta. En un
 * camping siempre por noches, asi que el selector ni se ve. */
static uint8_t pern_cobro_actual(void)
{
    if (s_pern_sitio == SITIO_CAMPING) return PARADA_COBRO_NOCHE;
    return (uint8_t)btnmatrix_checked(s_pern_cobro_bm, 2);
}

/* El rotulo lo completa el boton marcado ("Precio por [Noche|24 h]"), salvo en
 * un camping, donde no hay boton y lo dice entero. El titulo del editor a
 * pantalla completa si cabe entero siempre. */
static void pern_refresh_precio(void)
{
    /* En un sitio GRATIS se van el importe y el selector, pero la moneda se
     * queda: dormir es gratis y el agua puede costar 1 euro -- o 1 franco. Sin
     * el selector, un area gratis en Suiza anotaria francos como euros. */
    bool gratis  = !parada_sitio_es_de_pago(s_pern_sitio);
    bool camping = (s_pern_sitio == SITIO_CAMPING);
    set_hidden(s_pern_precio_ta, gratis);
    set_hidden(s_pern_cobro_bm, camping || gratis);
    if (gratis) {
        lv_label_set_text(s_pern_precio_lbl, "Moneda de los servicios");
        return;
    }
    lv_label_set_text(s_pern_precio_lbl, camping ? "Precio por noche" : "Precio");
    lv_obj_set_user_data(s_pern_precio_ta,
                         (void *)(pern_cobro_actual() == PARADA_COBRO_24H
                                  ? "Precio por 24 h" : "Precio por noche"));
}

static void pern_cobro_cb(lv_event_t *e)
{
    (void)e;
    pern_refresh_precio();
}

/* Los servicios de la pernocta: en un area es lo que OFRECE y en un camping lo
 * que va INCLUIDO en el precio. Misma pantalla, distinto rotulo. */
static void pern_servicios_open_cb(lv_event_t *e)
{
    (void)e;
    s_serv_desde = CAT_PERNOCTA;
    show_form(CAT_SERVICIOS);
}

static void build_pernocta(lv_obj_t *form)
{
    add_header(form, "PERNOCTA", lv_color_hex(COL_VIAJE), BACK_TO_GRID);

    /* Donde y cuantas noches, escrito y no preguntado: los dijiste al llegar y
     * el aparato ya sabe la hora de entrada y la de salida. Aqui solo estan
     * para que se vea que se esta cerrando LA de anoche y no otra cosa. */
    s_pern_info_lbl = lv_label_create(form);
    lv_label_set_text(s_pern_info_lbl, "");
    lv_obj_set_style_text_color(s_pern_info_lbl, lv_color_hex(COL_LABEL), 0);
    lv_obj_set_style_text_font(s_pern_info_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(s_pern_info_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_pern_info_lbl, lv_pct(100));

    precio_row_t o;
    s_pern_precio_row    = make_precio_row(form, &o, pern_cobro_cb);
    /* LA PERNOCTA, REPARTIDA POR TODO EL ALTO (8-oct-2026).
     *
     * Con el contenido a su tamano natural, los campos acababan a media
     * pantalla y quedaban ~150 px negros debajo, con los botones flotando en
     * medio. Aqui se reparte: la fila del precio se estira (es la que puede
     * crecer sin romperse: sus controles se centran solos) y la fila de
     * acciones se queda con su alto y se va AL FONDO. Asi la pantalla queda
     * llena y los botones donde los busca el dedo. */
    lv_obj_set_flex_grow(s_pern_precio_row, 1);
    lv_obj_set_style_min_height(s_pern_precio_row, 96, 0);
    s_pern_precio_lbl    = o.lbl;
    s_pern_precio_ta     = o.ta;
    s_pern_currency_dd   = o.dd;
    s_pern_cobro_bm      = o.bm;

    lv_obj_t *acciones = lv_obj_create(form);
    /* Alto 76 y no 50: los rotulos de estos dos botones van con letra 26 y con
     * 50 el texto quedaba pegado a los bordes.
     *
     * Y CON flex_grow(1): la fila del precio y esta se reparten el hueco que
     * sobra, de forma que el contenido llega al fondo de la pantalla en vez de
     * quedarse a media altura (era lo que se veia: "no ocupa en vertical toda
     * la pantalla"). */
    lv_obj_set_size(acciones, lv_pct(100), 76);
    /* Alto = todo lo que sobra: la fila del precio se queda con su minimo y esta
     * se estira hasta el fondo. Medido antes: el contenido acababa en y=285 de
     * 480 y quedaban ~150 px vacios abajo. */
    lv_obj_set_flex_grow(acciones, 3);
    lv_obj_set_content_height(acciones, lv_pct(100));
    lv_obj_set_style_bg_opa(acciones, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(acciones, 0, 0);
    lv_obj_set_style_pad_all(acciones, 0, 0);
    lv_obj_set_style_pad_column(acciones, 8, 0);
    lv_obj_clear_flag(acciones, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(acciones, LV_FLEX_FLOW_ROW);

    lv_obj_t *serv_btn = lv_btn_create(acciones);
    lv_obj_set_height(serv_btn, lv_pct(100));
    lv_obj_set_flex_grow(serv_btn, 1);
    lv_obj_set_style_bg_color(serv_btn, lv_color_hex(COL_VIAJE), 0);
    lv_obj_set_style_bg_color(serv_btn,
                              lv_color_darken(lv_color_hex(COL_VIAJE), LV_OPA_30),
                              LV_STATE_PRESSED);
    lv_obj_add_event_cb(serv_btn, pern_servicios_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *serv_lbl = lv_label_create(serv_btn);
    lv_label_set_text(serv_lbl, "Servicios " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(serv_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(serv_lbl, lv_color_hex(COL_TILE_FG), 0);
    lv_obj_center(serv_lbl);

    lv_obj_t *guardar = make_save_button(acciones, "Guardar noche",
                                         save_generic_cb,
                                         (void *)(uintptr_t)CAT_PERNOCTA);
    lv_obj_set_flex_grow(guardar, 2);
}

/* Una linea por servicio, con su casilla y su importe (24-ago-2026). Antes eran
 * seis casillas en dos columnas y solo se podia decir que los HABIA; en un area
 * el agua puede costar 1 euro y la luz 2, y eso se perdia.
 *
 * Sin rotulo explicativo arriba y con las filas apretadas a 34 px, y no es
 * capricho: 48 de cabecera + seis filas + la de valoracion + los huecos suman
 * los 304 utiles CLAVADOS. Con el rotulo o con filas de 40 habria que deslizar
 * para llegar a la ultima, y esto se toca con la autocaravana parada pero de
 * noche y con prisa.
 *
 * La moneda no esta aqui: es la misma de la pernocta, que es la misma parada.
 * Por eso su selector se queda a la vista aunque el sitio sea gratis (ver
 * pern_refresh_precio). */
#define SERV_ROW_H  34

static void build_servicios(lv_obj_t *form)
{
    /* "SERVICIOS" a secas: "SERVICIOS DEL AREA" no cabe sin pisar el Volver
     * (ver el tope de add_header). */
    add_header(form, "SERVICIOS", lv_color_hex(COL_VIAJE), BACK_TO_ORIGEN);

    /* Sin boton de guardar: lo marcado aqui se guarda con la pernocta. El
     * Volver de la cabecera devuelve a ella con todo puesto. */
    lv_obj_t *bloque = lv_obj_create(form);
    lv_obj_set_width(bloque, lv_pct(100));
    lv_obj_set_height(bloque, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bloque, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bloque, 0, 0);
    lv_obj_set_style_pad_all(bloque, 0, 0);
    lv_obj_set_style_pad_row(bloque, 2, 0);
    lv_obj_clear_flag(bloque, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bloque, LV_FLEX_FLOW_COLUMN);

    for (uint8_t i = 0; i < SERV_COUNT; i++) {
        make_check_money_row(bloque, SERV_OPCIONES[i], &s_serv_chk[i],
                             &s_serv_precio_ta[i], SERV_ROW_H);
        lv_obj_add_event_cb(s_serv_precio_ta[i], precio_marca_cb,
                            LV_EVENT_VALUE_CHANGED, s_serv_chk[i]);
    }

    /* Y la ultima linea es la NOTA del sitio: tres botones de color, verde /
     * ambar / rojo. Antes era una casilla mas de la lista que en realidad abria
     * otra pantalla, y eso no se entendia: aqui se ve lo que hay y se elige de
     * un toque. Pulsar cualquiera abre ademas las pegas.
     *
     * Fuera del bloque de los servicios, para que se vea que no es uno mas. */
    lv_obj_t *notas = lv_obj_create(form);
    lv_obj_set_size(notas, lv_pct(100), SERV_ROW_H);
    lv_obj_set_style_bg_opa(notas, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(notas, 0, 0);
    lv_obj_set_style_pad_all(notas, 0, 0);
    lv_obj_set_style_pad_column(notas, 6, 0);
    lv_obj_clear_flag(notas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(notas, LV_FLEX_FLOW_ROW);

    for (uint8_t i = 0; i < VALORACION_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(notas);
        lv_obj_set_height(btn, lv_pct(100));
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(VALORACION_COL[i]), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), 0);
        lv_obj_add_event_cb(btn, valoracion_nota_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TILE_FG), 0);
        lv_obj_center(lbl);

        s_val_btn[i]     = btn;
        s_val_btn_lbl[i] = lbl;
    }
    valoracion_pinta();
}

/* Pantalla de valoracion. Tres botones de un dedo con su color -- verde,
 * ambar, rojo -- y debajo las dos pegas del sitio.
 *
 * Reparto de los 304 utiles: cabecera 48 + tres botones que se reparten lo que
 * sobra (~66 cada uno) + la fila de casillas 40 + 12 de huecos. Los botones son
 * elasticos a proposito: si algun dia se anade una nota, encogen solos en vez
 * de salirse. */
/* Ya no es la pantalla de la nota -- esa se elige de un toque en servicios --
 * sino la de las PEGAS: lo que le pasa al sitio y no depende de la nota. Un
 * sitio bueno puede ser ruidoso o estar inclinado, y eso es lo que uno quiere
 * recordar antes de volver.
 *
 * Sale sola al pulsar cualquiera de las tres notas. Se sale con Volver, sin
 * marcar nada si no hay nada que marcar. Solo lleva cabecera y cuatro casillas,
 * asi que se pueden separar bien: mas hueco es menos fallo al tocar. */
static void build_valoracion(lv_obj_t *form)
{
    add_header(form, "PEGAS DEL SITIO", lv_color_hex(COL_VIAJE), CAT_SERVICIOS);

    lv_obj_t *grid = make_check_grid(form, VAL_EXTRAS, VAL_EXTRA_COUNT,
                                     s_val_extra_chk, COL_VIAJE, SERV_CHK_GAP);
    lv_obj_set_flex_align(lv_obj_get_parent(grid), LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
}

/* === Menus de la salida ===================================================
 *
 * El cuaderno se organiza alrededor de la SALIDA y no de las categorias. Ver
 * docs/superpowers/specs/2026-08-23-pantalla-registros-salidas-design.md; en
 * corto: la autocaravana se mueve por un motivo, y el viaje es solo uno de
 * ellos. Y como la pantalla se apaga con el contacto, DECLARAS AL LLEGAR y
 * RELLENAS AL SALIR.
 *
 * Un boton grande por pantalla: lo que se hace siempre tiene que verse desde
 * lejos y acertarse con el dedo con la autocaravana en marcha. Configuracion
 * queda pequeno y gris en todas partes.
 *
 * Los fondos CLAROS con contenido negro no son un capricho estetico: ver el
 * comentario de los colores al principio del fichero. Las tarjetas oscuras se
 * probaron y se veian apagadas con sol de lado.
 */


static lv_obj_t *s_menus[PAN_COUNT];
static lv_obj_t *s_bar_hora[PAN_COUNT];
static lv_obj_t *s_bar_gps[PAN_COUNT];
static lv_obj_t *s_bar_wifi[PAN_COUNT];
/* La tira de estado. Dos, porque las dos pantallas que hacen de menu principal
 * de una salida la llevan: la de viaje y la de puntual. */
#define TIRA_SALIDA   0
#define TIRA_PUNTUAL  1
static lv_obj_t *s_tiras[2];        /* el texto: nombre y dia */
static lv_obj_t *s_tira_chip[2];    /* la etiqueta de "N sin cerrar" */
static lv_obj_t *s_tira_chip_lbl[2];

/* Filas de PAN_ABIERTOS: se crean las cuatro y se ocultan las que sobren, que
 * es mas simple y mas seguro que crearlas y destruirlas cada vez. */
static lv_obj_t *s_ab_fila[SALIDA_EVENTOS_MAX];
static lv_obj_t *s_ab_tipo[SALIDA_EVENTOS_MAX];
static lv_obj_t *s_ab_hora[SALIDA_EVENTOS_MAX];
/* Boton "Terminar" de la fila. Solo sale en las paradas: es lo unico que hoy se
 * sabe cerrar. Los demas tipos esperan a tener su formulario. */
static lv_obj_t *s_ab_fin[SALIDA_EVENTOS_MAX];

#define BAR_H     26
#define CONN_MS   5000   /* mismo criterio que view_info.c */

static void mostrar_menu(pantalla_t p);
static void puntual_cancelar_cb(lv_event_t *e);
static void deshacer_ultimo(void *ud);
static bool cierre_sabe(uint8_t tipo);
static void volver_al_menu(void);

/* --- La franja de arriba -------------------------------------------------
 *
 * Cuesta 26 px de los 320 y ahorra cambiar de pantalla para mirar la hora o si
 * hay enlace con la P4. Los dos puntos CADUCAN con el enlace: un indicador que
 * miente cuando se cae la comunicacion es peor que no tenerlo, porque el
 * momento en que se mira es justo cuando algo va mal. */
static lv_obj_t *punto_crear(lv_obj_t *padre, const char *texto)
{
    lv_obj_t *w = lv_obj_create(padre);
    lv_obj_remove_style_all(w);
    lv_obj_set_size(w, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(w, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(w, 4, 0);
    lv_obj_clear_flag(w, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dot = lv_obj_create(w);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x666666), 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l = lv_label_create(w);
    lv_label_set_text(l, texto);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x888888), 0);
    return dot;
}

/* PAN_COUNT como destino NO es navegar: cancela la salida puntual. Es la
 * unica pantalla desde la que la flecha no lleva a ningun sitio, porque la
 * salida ya esta abierta y no hay menu anterior al que volver. */
static void atras_cb(lv_event_t *e)
{
    unsigned destino = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    if (destino == PAN_CANCELA_PUNTUAL)   puntual_cancelar_cb(e);
    else if (destino == PAN_VOLVER)       volver_al_menu();
    else                                  mostrar_menu((pantalla_t)destino);
}

/* Crea una pantalla de menu entera (oculta) y devuelve su CUERPO, que es donde
 * se cuelga el contenido. 'atras' < 0 = sin boton de volver. */
static lv_obj_t *pantalla_crear(lv_obj_t *parent, pantalla_t id,
                                const char *titulo, int atras)
{
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_HIDDEN);
    s_menus[id] = scr;

    /* --- franja --- */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), BAR_H);
    lv_obj_set_style_pad_hor(bar, 10, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 10, 0);

    if (atras >= 0) {
        lv_obj_t *b = lv_label_create(bar);
        lv_label_set_text(b, LV_SYMBOL_LEFT " Atras");
        lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(b, lv_color_hex(0xB0BEC5), 0);
        /* Area de toque generosa: el rotulo solo son 60x14 px y con la
         * autocaravana en marcha eso no se acierta. */
        lv_obj_set_ext_click_area(b, 14);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, atras_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)atras);
    } else {
        s_bar_hora[id] = lv_label_create(bar);
        lv_label_set_text(s_bar_hora[id], "--:--");
        lv_obj_set_style_text_font(s_bar_hora[id], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_bar_hora[id], lv_color_hex(0xDDDDDD), 0);
    }

    if (titulo) {
        lv_obj_t *t = lv_label_create(bar);
        lv_label_set_text(t, titulo);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_letter_space(t, 1, 0);
        lv_obj_set_flex_grow(t, 1);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    } else {
        lv_obj_t *sp = lv_obj_create(bar);
        lv_obj_remove_style_all(sp);
        lv_obj_set_height(sp, 1);
        lv_obj_set_flex_grow(sp, 1);
    }

    s_bar_gps[id]  = punto_crear(bar, "GPS");
    s_bar_wifi[id] = punto_crear(bar, "P4");

    /* --- cuerpo --- */
    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_all(body, PAN_PAD, 0);
    /* SE DESLIZA EN VERTICAL (8-oct-2026). Antes no hacia falta -- todo cabia
     * en 480x320 -- pero con la letra un escalon mas, los formularios con mas
     * campos (Repostaje, Mantenimiento) pasan de la pantalla y sin esto el
     * boton de Guardar quedaria fuera, inalcanzable. Solo en vertical: en
     * horizontal el gesto es del carrusel. */
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, PAN_GAP, 0);
    return body;
}

/* Refresca hora y puntos de TODAS las pantallas de menu. Una sola pasada por
 * segundo: son siete etiquetas, no compensa afinar mas. */
static void barra_timer_cb(lv_timer_t *t)
{
    (void)t;
    mini_data_t d;
    data_model_get(&d);
    uint32_t ms     = (uint32_t)(esp_timer_get_time() / 1000);
    /* El enlace, no el shunt: ver la nota larga en view_info.c. */
    bool     fresco = d.last_update_ms != 0 && (ms - d.last_update_ms < CONN_MS);

    uint32_t c_wifi = fresco ? 0x4CD964 : 0x666666;
    uint32_t c_gps  = !fresco ? 0x666666
                              : (d.gps_estado == 2) ? 0x4CD964
                              /* Ambar, el mismo que la P4 y que la pantalla de
                               * datos: el mismo estado, el mismo color. */
                              : (d.gps_estado == 1) ? 0xFF9800 : 0x666666;

    char hora[8] = "--:--";
    uint32_t ahora;
    if (reloj_ahora(&ahora)) {
        /* El epoch ya viene en hora local de la P4 (ver mini_proto.h): la
         * division entera basta y no hay que saber nada de husos. */
        snprintf(hora, sizeof(hora), "%02u:%02u",
                 (unsigned)((ahora / 3600) % 24), (unsigned)((ahora / 60) % 60));
    }

    for (int i = 0; i < PAN_COUNT; i++) {
        if (s_bar_gps[i])  lv_obj_set_style_bg_color(s_bar_gps[i],  lv_color_hex(c_gps), 0);
        if (s_bar_wifi[i]) lv_obj_set_style_bg_color(s_bar_wifi[i], lv_color_hex(c_wifi), 0);
        if (s_bar_hora[i]) lv_label_set_text(s_bar_hora[i], hora);
    }
}

/* --- Botones -------------------------------------------------------------- */

/* Boton grande: el que manda en su pantalla. Icono + rotulo + una linea de
 * apoyo opcional. */
static lv_obj_t *boton_grande(lv_obj_t *padre, const char *icono,
                              const char *texto, const char *apoyo,
                              uint32_t color, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_btn_create(padre);
    lv_obj_set_width(b, lv_pct(100));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(b, lv_color_darken(lv_color_hex(color), LV_OPA_30),
                              LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_pad_all(b, 6, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b, 8, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);

    if (icono) {
        lv_obj_t *ic = lv_label_create(b);
        lv_label_set_text(ic, icono);
        lv_obj_set_style_text_color(ic, lv_color_hex(COL_TILE_FG), 0);
        lv_obj_set_style_text_font(ic, &iconos_44, 0);
    }
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, texto);
    lv_obj_set_style_text_color(l, lv_color_hex(COL_TILE_FG), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_32, 0);
    rotulo_autoajustable(b, l);
    if (apoyo) {
        lv_obj_t *s = lv_label_create(b);
        lv_label_set_text(s, apoyo);
        lv_obj_set_style_text_color(s, lv_color_hex(COL_TILE_FG), 0);
        lv_obj_set_style_text_opa(s, LV_OPA_70, 0);
        lv_obj_set_style_text_font(s, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
    }
    return b;
}


/* Boton pequeno de abajo. Alto fijo: no debe competir con el grande.
 *
 * 64 de alto y no 46 (8-oct-2026): con letra 22, "Terminar salida" no cabia en
 * un boton de 46 ni de ancho ni de alto -- el texto se salia por los lados (lo
 * vio el usuario en la pantalla de salida). 64 es el alto que pide esa letra
 * con aire DEBAJO del texto, que es lo que faltaba: los dos botones de esa fila
 * llevan rotulos largos ("Terminar salida", "Configuracion"). */
#define BOTON_CHICO_H  64
static lv_obj_t *boton_chico(lv_obj_t *padre, const char *texto, uint32_t color,
                             lv_coord_t ancho, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_btn_create(padre);
    lv_obj_set_size(b, ancho, BOTON_CHICO_H);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(b, lv_color_darken(lv_color_hex(color), LV_OPA_30),
                              LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 10, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, texto);
    lv_obj_set_style_text_color(l, lv_color_hex(COL_TILE_FG), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_center(l);
    /* El ancho de estos botones a veces lo decide el reparto de una fila
     * flexible (los dos de abajo de la pantalla de salida), asi que el rotulo
     * se ajusta al pintar, no aqui. */
    rotulo_autoajustable(b, l);
    return b;
}

/* Casilla de rejilla. 'icono' puede ser NULL: la pantalla de donde duermes no
 * lo lleva -- ahi el color ya dice el sitio y la etiqueta el precio, y un
 * tercer elemento no cabe en 130 px de alto. Sin icono, el rotulo va en letra
 * 22 en vez de 16 y se lee mejor.
 *
 * Los iconos salen de iconos.h, una fuente propia: los LV_SYMBOL_* de LVGL no
 * tienen surtidor, bombona ni peaje, y se estaban usando por parecido. */
static lv_obj_t *casilla(lv_obj_t *padre, const char *icono, const char *texto,
                         const char *apoyo, uint32_t color,
                         lv_coord_t w, lv_coord_t h, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_btn_create(padre);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(b, lv_color_darken(lv_color_hex(color), LV_OPA_30),
                              LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_pad_all(b, 4, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b, 8, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);

    if (icono) {
        lv_obj_t *ic = lv_label_create(b);
        lv_label_set_text(ic, icono);
        lv_obj_set_style_text_color(ic, lv_color_hex(COL_TILE_FG), 0);
        lv_obj_set_style_text_font(ic, &iconos_44, 0);
        /* Se probo lv_obj_set_style_transform_zoom para agrandarlo sin
         * generar una fuente nueva: esta fuente de iconos no se escala bien
         * por software con zoom (el glifo desaparece). Revertido -- para
         * agrandarlo de verdad hace falta generar una iconos_XX mayor. */
    }
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, texto);
    lv_obj_set_style_text_color(l, lv_color_hex(COL_TILE_FG), 0);
    /* Las casillas CON apoyo (VIAJE/PUNTUAL) son mas grandes y tenian sitio
     * de sobra: suben un escalon respecto al resto de casillas normales. */
    lv_obj_set_style_text_font(l, apoyo ? &lv_font_montserrat_32
                                : icono ? &lv_font_montserrat_24
                                        : &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));

    if (apoyo) {
        lv_obj_t *s = lv_label_create(b);
        lv_label_set_text(s, apoyo);
        lv_obj_set_style_text_color(s, lv_color_hex(COL_TILE_FG), 0);
        lv_obj_set_style_text_opa(s, LV_OPA_70, 0);
        lv_obj_set_style_text_font(s, &lv_font_montserrat_24, 0);
        /* Centrado (el texto trae un \n interno: sin esto la linea mas
         * corta queda pegada a la izquierda) y un respiro respecto al
         * titulo de arriba, sin tocar el pad_row general de la casilla
         * (afectaria tambien al hueco icono-titulo del resto de casillas). */
        lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(s, 8, 0);
    }
    return b;
}

/* Casilla de la pantalla de donde duermes.
 *
 * Aqui el color dice el SITIO y no el precio, al reves que en la primera
 * version. Con verde/ambar las cinco casillas eran dos colores repetidos y con
 * los rotulos casi iguales ("Parking", "Parking", "Area", "Area"): habia que
 * LEERLAS para acertar, que es justo lo que no se puede hacer con la
 * autocaravana en marcha. Con un color por sitio, la mano va sola.
 *
 * El precio no se pierde: va en una etiqueta, y la de pago en NEGATIVO -- fondo
 * negro y letra blanca -- para que se vea antes de leerse. */
static lv_obj_t *casilla_sitio(lv_obj_t *padre, const char *nombre, bool de_pago,
                               uint32_t color, lv_coord_t w,
                               lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_btn_create(padre);
    lv_obj_set_size(b, w, lv_pct(100));
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(b, lv_color_darken(lv_color_hex(color), LV_OPA_30),
                              LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_pad_all(b, 4, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b, 8, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, nombre);
    lv_obj_set_style_text_color(l, lv_color_hex(COL_TILE_FG), 0);
    lv_obj_set_style_text_font(l, &FUENTE_NEGRITA_GRANDE, 0);

    lv_obj_t *tag = lv_obj_create(b);
    lv_obj_remove_style_all(tag);
    lv_obj_set_size(tag, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(tag, 8, 0);
    lv_obj_set_style_pad_ver(tag, 3, 0);
    lv_obj_set_style_radius(tag, 9, 0);
    lv_obj_clear_flag(tag, LV_OBJ_FLAG_SCROLLABLE);
    /* Sin esto la etiqueta se come el toque y la casilla no responde en el
     * centro, que es justo donde se pulsa. */
    lv_obj_clear_flag(tag, LV_OBJ_FLAG_CLICKABLE);
    if (de_pago) {
        lv_obj_set_style_bg_color(tag, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(tag, LV_OPA_COVER, 0);
    }

    lv_obj_t *t = lv_label_create(tag);
    lv_label_set_text(t, de_pago ? "DE PAGO" : "gratis");
    lv_obj_set_style_text_font(t, &FUENTE_NEGRITA, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(de_pago ? 0xFFFFFF : COL_TILE_FG), 0);
    if (!de_pago) lv_obj_set_style_text_opa(t, LV_OPA_70, 0);
    return b;
}

/* Fila elastica para repartir casillas a lo ancho. */
static lv_obj_t *fila(lv_obj_t *padre)
{
    lv_obj_t *f = lv_obj_create(padre);
    lv_obj_remove_style_all(f);
    lv_obj_set_width(f, lv_pct(100));
    lv_obj_set_flex_grow(f, 1);
    lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(f, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(f, PAN_GAP, 0);
    return f;
}

/* === Que hace cada boton ==================================================
 *
 * Declarar es lo UNICO que se hace al llegar: se abre el evento y se vuelve al
 * menu. Los numeros (importe, litros, lo que costo el agua...) se piden al
 * volver a dar el contacto, que es cuando ya se saben: de eso se encarga la
 * tabla CIERRE, mas abajo. Falta el precio y los servicios de una PERNOCTA,
 * que siguen preguntandose solo como "prolongar o terminar".
 */

static const char *const EV_NOMBRE[EV_COUNT] = {
    "Parada", "Aguas", "Repostaje", "Peaje", "Bombona", "Averia", "ITV"
};

/* Todas las pantallas de menu ocultas. Lo llama show_form() antes de sacar un
 * formulario. */
static void ocultar_menus(void)
{
    for (int i = 0; i < PAN_COUNT; i++) {
        if (s_menus[i]) lv_obj_add_flag(s_menus[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* La tira de PAN_SALIDA: nombre y dia del viaje, y cuantas cosas quedan sin
 * cerrar.
 *
 * NO lleva lo gastado, aunque la maqueta lo pintaba: esta pantalla no lleva la
 * cuenta del dinero -- los importes se rellenan al arrancar y viven en la P4 --
 * y poner un 0,00 seria mentir con autoridad. */
/* "hh:mm" a partir de un epoch local de la P4. */
static void hora_corta(uint32_t epoch, char *buf, size_t n)
{
    snprintf(buf, n, "%02u:%02u", (unsigned)((epoch / 3600) % 24),
             (unsigned)((epoch / 60) % 60));
}

static void salida_tira_refresh(void)
{
    const salida_vista_t *s = salida_get();
    char txt[SALIDA_NOMBRE_MAX + 32];
    /* Una salida puntual SI tiene nombre desde que se trata como un viaje de
     * una sola parada (el tipo elegido: Repostaje, ITV...), asi que se
     * ensena igual que el de un viaje. Antes no lo tenia y aqui se ponia el
     * generico "Salida puntual" para no dejar la tira en blanco. */
    int  n = snprintf(txt, sizeof(txt), "%s", s->nombre);
    /* snprintf devuelve lo que HABRIA escrito: si truncase, txt+n se saldria
     * del buffer. Con los tamanos de ahora no llega a pasar, pero el dia que
     * el nombre crezca esto seria una pisada de memoria muy dificil de ver. */
    if (n < 0 || (size_t)n >= sizeof(txt)) return;

    /* El dia SOLO en un viaje. Una salida puntual es ir y volver -- "dia 1" no
     * significa nada ahi, y contar dias de algo que dura una hora confunde. */
    uint32_t ahora = reloj_p4();
    if (s->tipo == SALIDA_VIAJE && ahora && s->epoch_ini) {
        n += snprintf(txt + n, sizeof(txt) - (size_t)n, "  -  dia %u",
                      (unsigned)(salida_noches(s->epoch_ini, ahora) + 1));
        if (n < 0 || (size_t)n >= sizeof(txt)) return;
    }
    int abiertos = salida_eventos_abiertos();
    /* Y que se entere la pantalla de datos, que es la que esta puesta mientras
     * conduces. Aqui, porque por esta funcion pasan TODOS los cambios: abrir,
     * deshacer, borrar, cerrar una parada y terminar la salida. */
    view_info_set_sin_cerrar((size_t)abiertos);
    /* Aviso "toca al llegar a <categoria>": solo mientras la puntual esta
     * en camino y todavia no se ha declarado (ver view_registro_puntual_
     * declarar_llegada). NULL lo esconde -- en cualquier otro momento (sin
     * salida, viaje real, o puntual ya declarada) no pinta nada aqui. */
    view_info_set_puntual_pendiente(
        (s->tipo == SALIDA_PUNTUAL && !s->declarado) ? s->nombre : NULL);
    /* La flecha no es adorno: avisa de que la etiqueta se toca, que es por
     * donde se llega a borrar un apunte puesto por error. */
    char chip[32];
    snprintf(chip, sizeof(chip), "%d sin cerrar  >", abiertos);

    for (int i = 0; i < 2; i++) {
        if (s_tiras[i]) lv_label_set_text(s_tiras[i], txt);
        if (!s_tira_chip[i]) continue;
        if (abiertos > 0) {
            lv_label_set_text(s_tira_chip_lbl[i], chip);
            lv_obj_clear_flag(s_tira_chip[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_tira_chip[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* Pinta la lista de lo que queda abierto. Las filas que sobran se esconden. */
static void abiertos_refresh(void)
{
    int n = salida_eventos_abiertos();
    for (int i = 0; i < SALIDA_EVENTOS_MAX; i++) {
        if (!s_ab_fila[i]) continue;
        const salida_evento_t *ev = salida_evento_en(i);
        if (!ev) { lv_obj_add_flag(s_ab_fila[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(s_ab_fila[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_ab_tipo[i],
                          ev->tipo < EV_COUNT ? EV_NOMBRE[ev->tipo] : "?");
        char h[8];
        hora_corta(ev->epoch_ini, h, sizeof(h));
        char linea[32];
        snprintf(linea, sizeof(linea), "anotado a las %s", h);
        lv_label_set_text(s_ab_hora[i], linea);
        if (s_ab_fin[i]) {
            if (cierre_sabe(ev->tipo)) lv_obj_clear_flag(s_ab_fin[i], LV_OBJ_FLAG_HIDDEN);
            else                       lv_obj_add_flag(s_ab_fin[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    (void)n;
}

static void mostrar_menu(pantalla_t p)
{
    /* Salirse de la eleccion de motivo CANCELA el aviso de "estuviste parado":
     * si no, la marca se quedaria puesta y la siguiente parada que declarases
     * -- horas despues y por tu cuenta -- se anotaria con la hora de aquel
     * apagon. PAN_SITIO cuenta como parte de la eleccion: la pernocta pregunta
     * el motivo y luego el sitio. */
    if (p != PAN_MOTIVO && p != PAN_SITIO) s_olvido_anotando = false;

    salida_tira_refresh();
    if (p == PAN_ABIERTOS) abiertos_refresh();
    if (p == PAN_PUNTUAL) puntual_refresh();
    for (int i = 0; i < PAN_COUNT; i++) {
        if (!s_menus[i]) continue;
        if (i == (int)p) lv_obj_clear_flag(s_menus[i], LV_OBJ_FLAG_HIDDEN);
        else             lv_obj_add_flag(s_menus[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* A donde se vuelve al cerrar un formulario o al terminar algo. Lo decide el
 * ESTADO y no por donde se vino: si la salida se abrio o se cerro por el medio,
 * el menu de antes ya no es el que toca. */
static void volver_al_menu(void)
{
    switch (salida_get()->tipo) {
    case SALIDA_VIAJE:   mostrar_menu(PAN_SALIDA);    break;
    case SALIDA_PUNTUAL: mostrar_menu(PAN_PUNTUAL);   break;
    default:             mostrar_menu(PAN_PRINCIPAL); break;
    }
}

/* Abre el evento y vuelve al menu. Los tres motivos por los que puede no
 * poder se dicen por separado: "no se ha podido" a secas deja al usuario sin
 * saber si insistir, encender la P4 o cerrar algo. */
/* Devuelve si el evento ha quedado realmente abierto. Lo necesita
 * view_registro_puntual_declarar_llegada(): marcar la salida como
 * "declarada" cuando esto falla dejaria la puntual sin ningun apunte y sin
 * forma de volver a intentarlo (ver el comentario de esa funcion). */
static bool declarar(evento_tipo_t tipo, uint8_t sub, uint8_t sub2)
{
    if (reloj_p4() == 0) {
        confirm_screen_aviso("Enciende la P4 primero",
                             "Sin ella no se que hora es,\ny el apunte va con su hora\nde inicio.",
                             COL_ACCION_STOP, "Entendido");
        return false;
    }
    if (salida_eventos_abiertos() >= SALIDA_EVENTOS_MAX) {
        confirm_screen_aviso("Ya hay cuatro sin cerrar",
                             "No cabe otra. Te preguntare\npor ellas cuando vuelvas a\ndar el contacto.",
                             COL_ACCION_STOP, "Entendido");
        return false;
    }
    if (salida_evento_abrir(tipo, sub, sub2) == 0) {
        confirm_screen_aviso("No he podido anotarlo",
                             "El apunte no se ha guardado.\nAvisa de esto, es un fallo\ndel programa.",
                             COL_ACCION_STOP, "Entendido");
        return false;
    }

    /* Viene del aviso de "estuviste parado": la parada no empieza ahora, empieza
     * cuando se fue la luz, y ya ha terminado. Se le corrige la hora de inicio y
     * se cierra en el acto -- para una pernocta eso abre su formulario, que es
     * justo lo que hace falta.
     *
     * El indice es el ULTIMO de la cola porque acabamos de abrirlo. No se da por
     * hecho que sea el primero: entre el aviso y el motivo se puede haber
     * declarado otra cosa. */
    if (s_olvido_anotando && tipo == EV_PARADA) {
        int idx = salida_eventos_abiertos() - 1;
        s_olvido_anotando = false;
        salida_evento_set_inicio(idx, s_olvido_ini);
        parada_terminar((void *)(intptr_t)idx);
        return true;
    }

    /* El cartel no es solo para enterarse: lleva DESHACER. El evento se abre de
     * un toque, y sin esto un dedo equivocado no tendria vuelta atras. Va en
     * rojo y a la derecha, que es donde estan el resto de acciones que
     * descartan. */
    static char cuerpo[96];
    snprintf(cuerpo, sizeof(cuerpo),
             "Queda abierto: %s.\nCuando vuelvas a dar el\ncontacto te pedire los datos.",
             EV_NOMBRE[tipo]);
    /* El titulo en VERDE: "Anotado" es una buena noticia, y en rojo parecia que
     * algo habia salido mal. */
    confirm_screen_open("Anotado", cuerpo, COL_ACCION_OK,
                        "Deshacer", "Vale", deshacer_ultimo, NULL);
    confirm_screen_ok_destructivo();
    volver_al_menu();
    return true;
}

/* tipo, sub y sub2 caben de sobra en el user_data del evento. */
#define DECL(t, s, s2)  ((void *)(uintptr_t)((t) | ((s) << 8) | ((s2) << 16)))

static void declarar_cb(lv_event_t *e)
{
    uint32_t v = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    declarar((evento_tipo_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF),
             (uint8_t)((v >> 16) & 0xFF));
}

/* --- Declarar dentro de una SALIDA PUNTUAL ---------------------------------
 *
 * Se trata como un viaje de una sola parada: la carpeta en la P4 se abre AL
 * TOCAR el boton (Repostaje/Bombona/ITV/Averia), no al rellenar los datos --
 * igual que un viaje exige la P4 antes de nada porque la carpeta lleva la
 * fecha en el nombre. El "destino" que se manda es el nombre del tipo
 * (EV_NOMBRE); la P4 le antepone la fecha ella sola (op_inicio), asi que sale
 * "AAAA-MM-DD_Repostaje" igual que un viaje de verdad -- descargable igual.
 *
 * p4_api_done_cb lleva user_data, pero p4_api_viaje_inicio no lo usa (se
 * envia a NULL), asi que el tipo/sub/sub2 elegido se guarda aqui para cuando
 * llegue la respuesta. */
static evento_tipo_t s_puntual_tipo;
static uint8_t       s_puntual_sub, s_puntual_sub2;

/* Single-flight del inicio: un doble toque en una casilla (facil con la
 * autocaravana en marcha) disparaba dos p4_api_viaje_inicio() con ids
 * distintos -- la segunda solia recibir 409 y mandaba a "la P4 ya tiene un
 * viaje abierto" justo despues de que la primera hubiera abierto bien la
 * misma salida, ofreciendo apartar lo que se acababa de empezar. Puesto a
 * true justo antes de la llamada (puntual_iniciar_real) y a false en
 * cuanto llega la respuesta, por cualquier camino. Detectado el
 * 09-sep-2026, antes de la primera salida real. */
static bool s_puntual_iniciando;

static void inicio_puntual_resultado_cb(bool ok, int estado, void *user_data)
{
    (void)user_data;
    s_puntual_iniciando = false;

    /* Mismo callejon sin salida que un viaje real: si la P4 ya tiene algo
     * abierto que esta pantalla no conocia, el unico sitio para resolverlo
     * es PAN_VIAJE_P4. */
    if (!ok && estado == 409) { mostrar_menu(PAN_VIAJE_P4); return; }
    if (!ok) { aviso_envio_fallo(estado, EV_NOMBRE[s_puntual_tipo]); return; }
    if (!salida_abrir_puntual(EV_NOMBRE[s_puntual_tipo])) {
        /* Sin hora no puede ser -- se acaba de usar para el inicio -- pero si
         * pasara, el "viaje" quedaria abierto en la P4 y no en la pantalla;
         * se veria al momento igual que en inicio_resultado_cb. */
        confirm_screen_aviso("No he podido empezarla",
                             "La salida no se ha guardado.\nAvisa de esto, es un fallo\ndel programa.",
                             COL_ACCION_STOP, "Entendido");
        return;
    }
    ESP_LOGI(TAG, "salida puntual iniciada en la P4: '%s'", EV_NOMBRE[s_puntual_tipo]);
    /* Mismo contador que un viaje real (trip_eventos), reseteado a 1 (el
     * inicio ya cuenta) igual que hace viaje_marcar_iniciado() -- para que
     * puntual_do_terminar() pueda mandar trip_eventos_get()+1 en vez de un
     * "3" fijo. El fijo asumia SIEMPRE inicio+registro+fin, pero si la
     * parada se descarta (avisa_de_lo_abierto en Terminar salida, o un
     * Deshacer que no llega a redeclararse) el registro de en medio nunca
     * se manda, y el "3" quedaba de mas -- INCOMPLETO falso en la P4 sin
     * haberse perdido nada de verdad. 'destino' aqui es solo la categoria
     * (Repostaje...), no se usa para nada mas que este contador. Detectado
     * el 09-sep-2026. */
    esp_err_t err_cnt = save_trip_inicio(EV_NOMBRE[s_puntual_tipo]);
    if (err_cnt != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo resetear el contador de la puntual: %s",
                 esp_err_to_name(err_cnt));
    }
    /* Mismo motivo que en viaje_marcar_iniciado(): s_suelto_pendiente es
     * estado de sesion, no debe sobrevivir a un viaje/puntual anterior. */
    memset(s_suelto_pendiente, 0, sizeof(s_suelto_pendiente));
    /* AQUI NO se declara todavia: esto es solo la salida del garaje, y la
     * ruta ya se esta grabando desde este instante. Declarar (guardar la
     * hora/sitio de la accion) se hace luego, al llegar -- ver
     * view_registro_puntual_declarar_llegada(), disparado por el aviso de
     * la pantalla principal o por el boton "Ya he llegado" de esta misma
     * pantalla. Separarlo en dos pasos es lo que permite grabar el trayecto
     * de ida completo en vez de solo la vuelta. */

    /* Deja el menu de Registro en PAN_PUNTUAL (no PAN_PRINCIPAL, que es solo
     * "Nueva salida"/"Configuracion" y no dice nada de la salida en curso):
     * si el usuario vuelve a esta pagina deslizando, vera el panel "en
     * marcha" en vez de un menu que no cuenta nada. */
    volver_al_menu();

    /* Y ADEMAS cambia de PAGINA del carrusel a la de info: es la que trae
     * datos (bateria, aguas, temperaturas...) y la pastilla de "toca al
     * llegar" -- quedarse en Registro tras abrir la salida no aporta nada
     * mientras conduces hacia el sitio. El aviso se muestra DESPUES de
     * cambiar de pagina (y no antes) porque se reengancha el a la pantalla
     * que este activa en ESE momento (ver el comentario de
     * confirm_screen_open sobre "reparentar"); si se mostrase antes, se
     * quedaria colgado de Registro y desaparecia de la vista al cambiar de
     * pagina sin que el usuario llegase a leerlo. */
    nav_ir_a_info();
    confirm_screen_aviso("Salida abierta",
                         "Toca el aviso de abajo en\ncuanto llegues al sitio.",
                         COL_ACCION_OK, "Vale");
}

static void puntual_iniciar_real(void *ud)
{
    (void)ud;
    s_puntual_iniciando = true;
    uint32_t ahora = reloj_p4();
    if (!p4_api_viaje_inicio(next_trip_seq(), EV_NOMBRE[s_puntual_tipo],
                             ahora / 86400u, inicio_puntual_resultado_cb)) {
        /* lanzar() ha fallado en el acto (sin memoria para la tarea): la
         * respuesta nunca va a llegar, asi que aqui nadie mas va a soltar
         * el guardian -- hay que hacerlo ya mismo. */
        s_puntual_iniciando = false;
        aviso_envio_fallo(0, EV_NOMBRE[s_puntual_tipo]);
    }
}

static void puntual_declarar_cb(lv_event_t *e)
{
    if (s_puntual_iniciando) return;   /* ya hay un inicio en el aire, ignora el segundo toque */

    uint32_t v = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    s_puntual_tipo = (evento_tipo_t)(v & 0xFF);
    s_puntual_sub  = (uint8_t)((v >> 8) & 0xFF);
    s_puntual_sub2 = (uint8_t)((v >> 16) & 0xFF);

    if (reloj_p4() == 0) {   /* comprobado tambien en puntual_cb; puede caerse en medio */
        confirm_screen_aviso("Enciende la P4 primero",
                             "Sin ella no se que dia es,\ny la carpeta lleva la fecha\nen el nombre.",
                             COL_ACCION_STOP, "Entendido");
        return;
    }
    con_gps_o_seguir(puntual_iniciar_real);
}

/* --- Declarar la LLEGADA de una SALIDA PUNTUAL ya abierta -------------------
 *
 * s_puntual_tipo es una variable de RAM que no sobrevive un apagon, y esta
 * funcion se puede llamar mucho despues del toque que abrio la salida
 * (incluso tras reiniciar el contacto por el camino). Por eso el tipo se
 * recupera del NOMBRE que ya quedo guardado en la propia salida (NVS),
 * buscandolo en EV_NOMBRE -- no hace falta guardar nada mas nuevo. */
static bool puntual_tipo_por_nombre(const char *nombre, evento_tipo_t *out)
{
    for (int i = 0; i < EV_COUNT; i++) {
        if (EV_NOMBRE[i] && strcmp(EV_NOMBRE[i], nombre) == 0) {
            *out = (evento_tipo_t)i;
            return true;
        }
    }
    return false;
}

/* La llama el aviso "Toca al llegar a <categoria>" de la pantalla principal
 * (ver view_info_set_puntual_pendiente) y tambien el boton equivalente de
 * esta pantalla, por si se toca desde aqui en vez de desde alli. Guarda la
 * hora de llegada (declarar) y marca la salida como declarada para que no
 * se pueda volver a tocar -- una puntual es SIEMPRE una sola parada, a
 * diferencia de un viaje real que admite cualquier numero de ellas. */
void view_registro_puntual_declarar_llegada(void)
{
    if (salida_get()->tipo != SALIDA_PUNTUAL || salida_get()->declarado) return;

    evento_tipo_t tipo;
    if (!puntual_tipo_por_nombre(salida_get()->nombre, &tipo)) {
        ESP_LOGE(TAG, "llegada de puntual: nombre '%s' no reconocido",
                 salida_get()->nombre);
        return;
    }
    /* Marcar declarada SOLO si el evento ha quedado realmente abierto: si
     * declarar() falla (P4 se fue justo ahora, etc.) ya ensena su propio
     * aviso, y aqui no se toca el estado -- para que la pastilla y "Ya he
     * llegado" se lo sigan ofreciendo en el siguiente intento en vez de
     * dejar la puntual "declarada" sin ningun apunte dentro. */
    if (declarar(tipo, 0, 0)) {
        salida_puntual_marcar_declarada();
        /* declarar() ya hizo su propio volver_al_menu() para ensenar el
         * cartel "Anotado" -- pero eso corrio ANTES de esta marca, asi que
         * la pastilla y el panel de Puntual se refrescaron con declarado
         * todavia en false: pastilla muerta (toque silencioso, ver el
         * guardian de arriba) y "Terminar salida" oculto hasta la proxima
         * navegacion o un reinicio. Refrescar otra vez, ya con el estado
         * bueno. Detectado el 09-sep-2026 (flanco de 7439984). */
        volver_al_menu();
    }
}

static void puntual_llegada_cb(lv_event_t *e)
{
    (void)e;
    view_registro_puntual_declarar_llegada();
}

/* --- Terminar una SALIDA PUNTUAL --------------------------------------------
 *
 * Rellenar el dato de la parada (arriba, do_save) NO termina la salida: eso
 * es solo la gestion en si (el surtidor, el taller...), y de ahi a volver al
 * garaje puede haber horas o dias. El cierre de verdad es este boton, que
 * solo se ensena cuando ya hay una puntual en curso -- ver puntual_refresh(),
 * enganchado a mostrar_menu(). Mismo patron que terminar_salida_cb/
 * viaje_do_finalizar para un viaje real. */
static void puntual_do_terminar(void *ud)
{
    (void)ud;
    char cuerpo[80];
    /* trip_eventos_get()+1, NO un "3" fijo: una puntual SIEMPRE tiene como
     * mucho una parada, pero no siempre la tiene DE VERDAD mandada -- si se
     * descarta con eventos abiertos (avisa_de_lo_abierto ya lo permite,
     * confirmando que se pierden) el registro de en medio nunca llega a la
     * cola, y un "3" fijo ahi habria dejado esperados de mas: INCOMPLETO
     * falso en la P4 sin haberse perdido nada de verdad (aplicados si que
     * habria sido menor, pero solo porque el propio contador mentia). El
     * contador se reseteo a 1 al abrir (ver inicio_puntual_resultado_cb), y
     * apunte_encolar() ya lo incrementa con el mismo trip_eventos_inc() que
     * usa un viaje real -- mismo patron que viaje_do_finalizar. Detectado
     * el 09-sep-2026. */
    p4_api_cuerpo_fin(cuerpo, sizeof(cuerpo), next_trip_seq(), trip_eventos_get() + 1);

    viaje_cola_error_t motivo;
    if (!viaje_cola_push(cuerpo, &motivo)) {
        confirm_screen_aviso("No he podido apuntarlo", cola_fallo_texto(motivo),
                             COL_ACCION_STOP, "Entendido");
        return;
    }
    save_trip_fin();   /* mismo cierre del contador que un viaje real; ver arriba */
    salida_cerrar();
    mostrar_menu(PAN_PRINCIPAL);
}

static void puntual_terminar_cb(lv_event_t *e)
{
    (void)e;
    if (avisa_de_lo_abierto("Terminar la salida?", puntual_do_terminar)) return;
    confirm_screen_open("Terminar la salida?", NULL, COL_ACCION_STOP,
                        "Si, terminar", "Cancelar", puntual_do_terminar, NULL);
    confirm_screen_ok_destructivo();
}

/* Filas de las 4 casillas (ocultas mientras la puntual esta en curso: solo
 * hay una parada por salida, no tiene sentido ofrecer elegir otra) y el
 * bloque de "Terminar salida" (oculto hasta que SI hay una en curso).
 * Handles guardados en crear_menus(). */
static lv_obj_t *s_puntual_filas[2];
static lv_obj_t *s_puntual_fin_cont;
static lv_obj_t *s_puntual_fin_lbl;
static lv_obj_t *s_puntual_llegada_btn;   /* "Ya he llegado", solo antes de declarar */
static lv_obj_t *s_puntual_fin_btn;       /* "Terminar salida", solo tras declarar */

static void puntual_refresh(void)
{
    const salida_vista_t *s = salida_get();
    bool en_curso  = (s->tipo == SALIDA_PUNTUAL);
    bool declarado = en_curso && s->declarado;

    for (int i = 0; i < 2; i++) {
        if (!s_puntual_filas[i]) continue;
        if (en_curso) lv_obj_add_flag(s_puntual_filas[i], LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_clear_flag(s_puntual_filas[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (!s_puntual_fin_cont) return;
    if (!en_curso) { lv_obj_add_flag(s_puntual_fin_cont, LV_OBJ_FLAG_HIDDEN); return; }

    /* Antes de declarar: en camino, la ruta ya se esta grabando. Despues de
     * declarar: falta solo volver -- una puntual es siempre una parada,
     * asi que no se vuelve a ofrecer "Ya he llegado" una vez usado. */
    lv_label_set_text_fmt(s_puntual_fin_lbl, declarado
        ? "%s en marcha.\nTermina al volver al garaje."
        : "%s en marcha.\nToca 'Ya he llegado' al llegar\nal sitio (o el aviso de la\npantalla principal).",
        s->nombre);
    if (declarado) {
        lv_obj_add_flag(s_puntual_llegada_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_puntual_fin_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_puntual_llegada_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_puntual_fin_btn, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(s_puntual_fin_cont, LV_OBJ_FLAG_HIDDEN);
}

static void ir_a_cb(lv_event_t *e)
{
    mostrar_menu((pantalla_t)(uintptr_t)lv_event_get_user_data(e));
}

/* Tocar la tira solo lleva a algun sitio si hay algo que ver. Sin nada abierto
 * no hace nada, en vez de abrir una lista vacia. */
static void tira_cb(lv_event_t *e)
{
    (void)e;
    if (salida_eventos_abiertos() > 0) mostrar_menu(PAN_ABIERTOS);
}

/* --- Borrar un apunte puesto por error ------------------------------------ */

static char s_borrar_txt[96];

static void borrar_do(void *ud)
{
    salida_evento_borrar((int)(intptr_t)ud);
    if (salida_eventos_abiertos() == 0) volver_al_menu();   /* ya no hay lista */
    else                                { abiertos_refresh(); salida_tira_refresh(); }
}

static void borrar_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const salida_evento_t *ev = salida_evento_en(idx);
    if (!ev) return;
    char h[8];
    hora_corta(ev->epoch_ini, h, sizeof(h));
    snprintf(s_borrar_txt, sizeof(s_borrar_txt),
             "%s, anotado a las %s.\nNo se guardara nada de el.",
             ev->tipo < EV_COUNT ? EV_NOMBRE[ev->tipo] : "?", h);
    confirm_screen_open("Borrar el apunte?", s_borrar_txt, COL_ACCION_STOP,
                        "Si, borrar", "No, dejarlo", borrar_do,
                        (void *)(intptr_t)idx);
    confirm_screen_ok_destructivo();
}

/* --- El viaje que la P4 tiene abierto y esta pantalla no --------------------
 *
 * Las dos van por la COLA y no directas: asi valen aunque la P4 conteste tarde,
 * y entran detras de lo que hubiera pendiente. La cola despacha cada 15 s como
 * mucho, de ahi el "unos segundos" del cartel. */
static void viaje_p4_manda(const char *cuerpo, const char *titulo)
{
    viaje_cola_error_t motivo;
    if (!viaje_cola_push(cuerpo, &motivo)) {
        confirm_screen_aviso("No he podido pedirlo", cola_fallo_texto(motivo),
                             COL_ACCION_STOP, "Entendido");
        return;
    }
    confirm_screen_aviso(titulo,
                         "Se lo he pedido a la P4.\nEn unos segundos podras\nempezar el viaje nuevo.",
                         COL_ACCION_OK, "Vale");
    volver_al_menu();
}

/* Cerrarlo como bueno: la P4 le escribe su resumen y lo deja terminado.
 *
 * Va SIN el recuento de apuntes a proposito: no sabemos cuantos genero ese
 * viaje -- no es nuestro -- y mandar un numero inventado lo marcaria como
 * INCOMPLETO sin serlo. */
static void viaje_p4_guardar(void *ud)
{
    (void)ud;
    char cuerpo[80];
    p4_api_cuerpo_fin_ajeno(cuerpo, sizeof(cuerpo), next_trip_seq());
    viaje_p4_manda(cuerpo, "Viaje guardado");
}

static void viaje_p4_descartar(void *ud)
{
    (void)ud;
    char cuerpo[80];
    p4_api_cuerpo_descartar(cuerpo, sizeof(cuerpo), next_trip_seq());
    viaje_p4_manda(cuerpo, "Viaje apartado");
}

static void viaje_p4_guardar_cb(lv_event_t *e)
{
    (void)e;
    confirm_screen_open("Guardar el viaje?", "La P4 lo cierra y le escribe\nsu resumen. Luego ya puedes\nempezar el nuevo.",
                        COL_ACCION_OK, "Si, guardarlo", "Cancelar", viaje_p4_guardar, NULL);
}

static void viaje_p4_descartar_cb(lv_event_t *e)
{
    (void)e;
    /* Se dice EXACTAMENTE lo que pasa con la carpeta. "Descartar" a secas suena
     * a borrar, y no borra: aparta. */
    confirm_screen_open("Apartar el viaje?", "Su carpeta pasa a llamarse\nDESCARTADO_... y deja de\ncontar. No se borra nada.",
                        COL_ACCION_STOP, "Si, apartarlo", "Cancelar",
                        viaje_p4_descartar, NULL);
    confirm_screen_ok_destructivo();
}

/* === La parada, al volver a dar el contacto ==============================
 *
 * Declaras al llegar y rellenas al salir: aqui esta la segunda mitad. Al
 * encender, si quedo una parada abierta, se pregunta si sigues ahi o si ya te
 * vas. La hora de fin es la de AHORA, real y no tecleada, que es de lo que va
 * todo el diseno.
 *
 * "Prolongar" no pregunta nada y no toca la hora de inicio: prolongar
 * significa justo que sigue contando desde el principio.
 *
 * Nombres de pantalla y CLAVES del CSV van separados a proposito, por lo mismo
 * que el resto del fichero: la redaccion de un rotulo puede cambiar, y si las
 * columnas fueran los rotulos ese cambio partiria el historico en dos. */
static const char *const MOTIVO_NOMBRE[MOTIVO_COUNT] = {
    "Visita", "Descanso", "Comer", "Cenar", "Compras", "Pernocta"
};
static const char *const MOTIVO_CLAVE[MOTIVO_COUNT] = {
    "visita", "descanso", "comer", "cenar", "compras", "pernocta"
};

static char s_parada_txt[192];
static bool s_parada_ya_preguntada;   /* una sola vez por encendido */

/* Eventos ya ofrecidos ESTE arranque (Rellenarlo o Luego, da igual): sin
 * esto, con dos o mas eventos abiertos a la vez (pernocta + repostaje...) el
 * timer de abajo solo preguntaba por el primero y se paraba -- el diseno
 * pide preguntar por CADA UNO, en orden (docs/superpowers/specs/2026-08-23).
 * Vive en RAM, no en NVS: es solo para no repetir la pregunta cada 2s dentro
 * del mismo encendido, no para recordarla entre apagones. Detectado
 * auditando el 07-sep-2026. */
static uint32_t s_boot_preguntados[SALIDA_EVENTOS_MAX];
static int      s_boot_n_preguntados;

static bool boot_ya_preguntado(uint32_t id)
{
    for (int i = 0; i < s_boot_n_preguntados; i++) {
        if (s_boot_preguntados[i] == id) return true;
    }
    return false;
}

static void boot_marcar_preguntado(uint32_t id)
{
    if (boot_ya_preguntado(id)) return;
    if (s_boot_n_preguntados < SALIDA_EVENTOS_MAX) {
        s_boot_preguntados[s_boot_n_preguntados++] = id;
    }
}

/* "45 min" o "2 h 15 min". Las horas sueltas se leen mucho peor en minutos. */
static void duracion_texto(uint32_t seg, char *buf, size_t n)
{
    uint32_t m = seg / 60;
    if (m < 60) snprintf(buf, n, "%u min", (unsigned)m);
    else        snprintf(buf, n, "%u h %u min", (unsigned)(m / 60), (unsigned)(m % 60));
}

/* Terminar una PERNOCTA no guarda nada todavia: abre el formulario donde se
 * piden el precio, los servicios y la nota del sitio. Lo que se guarda lo monta
 * despues apunte_encolar(CAT_PERNOCTA), con estos mismos datos.
 *
 * La hora de fin se congela AQUI, en el momento de decir "Terminarla", no
 * cuando se pulse Guardar: rellenar el formulario son dos minutos y la noche no
 * duro dos minutos mas por eso. */
static void pernocta_abrir(int idx, const salida_evento_t *ev, uint32_t ahora)
{
    s_cerrando    = idx;
    s_cerrando_id = ev->id;
    s_pern_sitio  = ev->sub2 < SITIO_COUNT ? ev->sub2 : 0;
    s_pern_ini    = ev->epoch_ini;
    s_pern_fin    = ahora;

    uint32_t noches = salida_noches(s_pern_ini, s_pern_fin);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s\n%u noche%s", SITIO_NOMBRE[s_pern_sitio],
             (unsigned)noches, noches == 1 ? "" : "s");
    lv_label_set_text(s_pern_info_lbl, buf);

    /* Sitio gratis: fuera el precio, que no hay nada que teclear. Los servicios
     * y la nota SI se preguntan igual -- un parking gratis con agua y buena
     * noche es justo lo que interesa recordar para la proxima vez. */
    set_hidden(s_pern_precio_row, !parada_sitio_es_de_pago(s_pern_sitio));
    pern_refresh_precio();

    /* Al carrusel primero, como todos los cierres: el formulario vive en la
     * pagina de registros y el cartel salta sobre la que estes mirando. */
    nav_ir_a_registros();
    show_form(CAT_PERNOCTA);
}

static void parada_terminar(void *ud)
{
    int idx = (int)(intptr_t)ud;
    const salida_evento_t *ev = salida_evento_en(idx);
    if (!ev) return;

    uint32_t ahora = reloj_p4();
    if (!ahora) {
        confirm_screen_aviso("Sin la P4",
                             "No se que hora es, asi que\nno puedo cerrar la parada.",
                             COL_ACCION_STOP, "Entendido");
        return;
    }

    uint8_t motivo = ev->sub < MOTIVO_COUNT ? ev->sub : MOTIVO_VISITA;

    /* La pernocta se va por su camino: tiene mas que contar y va a su propia
     * hoja. De aqui abajo, motivo NUNCA es MOTIVO_PERNOCTA. */
    if (motivo == MOTIVO_PERNOCTA) {
        pernocta_abrir(idx, ev, ahora);
        return;
    }

    char hi[8], hf[8];
    hora_corta(ev->epoch_ini, hi, sizeof(hi));
    hora_corta(ahora,         hf, sizeof(hf));

    char cuerpo[384];
    size_t u = apunte_cabecera(cuerpo, sizeof(cuerpo), ev->id, "parada");
    u = apunte_campo_txt(cuerpo, sizeof(cuerpo), u, "motivo", MOTIVO_CLAVE[motivo]);
    u = apunte_campo_txt(cuerpo, sizeof(cuerpo), u, "inicio", hi);
    u = apunte_campo_txt(cuerpo, sizeof(cuerpo), u, "fin", hf);
    u = apunte_campo_num(cuerpo, sizeof(cuerpo), u, "minutos",
                         (long)((ahora - ev->epoch_ini) / 60));

    char resumen[96];
    snprintf(resumen, sizeof(resumen), "Parada: %s, %s a %s",
             MOTIVO_NOMBRE[motivo], hi, hf);
    u = apunte_cerrar(cuerpo, sizeof(cuerpo), u, resumen);

    /* Se cuenta ANTES de encolar y pase lo que pase, igual que build_resumen()
     * para el resto de categorias (ver su comentario): antes esta cuenta no
     * incluia las paradas, asi que una parada perdida no hacia bajar
     * "esperados" y podia compensar la perdida de otro apunte, dando el viaje
     * por completo sin estarlo. Pero solo la PRIMERA vez: si el push de mas
     * abajo falla, la parada sigue abierta para reintentar con el mismo id, y
     * sin este guard cada reintento la volveria a contar. Detectado auditando
     * el 07-sep-2026. */
    if (!ev->contado) {
        /* Marcar ANTES de contar, no al reves: son dos commits NVS
         * independientes (blobs de modulos distintos), y un apagon justo
         * entre medias no se puede evitar del todo. Con el orden viejo
         * (contar, luego marcar), un corte ahi dejaba el contador YA
         * incrementado pero sin la marca -- el reintento del siguiente
         * arranque volvia a incrementar, "esperados" quedaba UNO DE MAS
         * respecto a lo que de verdad se manda, y la P4 daba el viaje por
         * INCOMPLETO sin haber perdido nada (alli aplicados < esperados
         * dispara la marca, ver config_server_viaje.c). Con este orden, el
         * mismo corte deja la marca puesta pero el contador sin subir: el
         * reintento la ve ya marcada y no vuelve a tocar nada, asi que
         * "esperados" se queda UNO DE MENOS -- inofensivo, la P4 solo mira
         * si aplicados < esperados, nunca al reves. Detectado el
         * 09-sep-2026. */
        salida_evento_marcar_contado(idx);
        trip_eventos_inc();
    }

    /* u == 0 significa que el JSON no cabia y quedo cortado: NO se manda. */
    viaje_cola_error_t motivo_cola = VIAJE_COLA_ERR_NINGUNO;
    if (!u || !viaje_cola_push(cuerpo, &motivo_cola)) {
        /* Mismo motivo que cola_fallo_texto(), pero con la coletilla de que
         * la parada sigue abierta -- aqui SI importa decirlo, porque no hay
         * temporizador ni nada que la cierre sola. */
        const char *razon = (motivo_cola == VIAJE_COLA_ERR_NVS)
            ? "La parada NO se ha guardado\ny sigue abierta: fallo al escribir\nen la memoria del aparato."
            : "La parada NO se ha guardado\ny sigue abierta. Enciende la\nP4 para vaciar la cola.";
        confirm_screen_aviso("No he podido apuntarlo", razon,
                             COL_ACCION_STOP, "Entendido");
        return;
    }

    salida_evento_borrar(idx);
    ESP_LOGI(TAG, "parada cerrada: %s", resumen);
    volver_al_menu();
}

/* Arma y saca la pregunta. false si ahora mismo no se puede -- sin la hora de
 * la P4 no hay nada que calcular -- para que quien llama vuelva a intentarlo. */
static bool parada_preguntar_en(int idx)
{
    uint32_t ahora = reloj_p4();
    if (!ahora) return false;

    const salida_evento_t *ev = salida_evento_en(idx);
    uint8_t motivo = ev->sub  < MOTIVO_COUNT ? ev->sub  : MOTIVO_VISITA;
    uint8_t sitio  = ev->sub2 < SITIO_COUNT  ? ev->sub2 : 0;

    char hi[8], dur[24];
    hora_corta(ev->epoch_ini, hi, sizeof(hi));
    duracion_texto(ahora - ev->epoch_ini, dur, sizeof(dur));

    if (motivo == MOTIVO_PERNOCTA) {
        uint32_t noches = salida_noches(ev->epoch_ini, ahora);
        snprintf(s_parada_txt, sizeof(s_parada_txt),
                 "%s\ndesde las %s  -  %u noche%s\n\n"
                 "Al terminarla te pido el precio\ny que habia.",
                 SITIO_NOMBRE[sitio], hi, (unsigned)noches, noches == 1 ? "" : "s");
    } else {
        snprintf(s_parada_txt, sizeof(s_parada_txt), "%s\ndesde las %s  -  %s",
                 MOTIVO_NOMBRE[motivo], hi, dur);
    }

    /* El "no" no es corregir nada: es que sigues ahi. La parada se queda
     * abierta y se vuelve a preguntar en el siguiente contacto. */
    confirm_screen_open("Parada en curso", s_parada_txt, COL_VIAJE,
                        "Terminarla", "Prolongar", parada_terminar,
                        (void *)(intptr_t)idx);
    return true;
}

/* --- Cerrar un evento rellenando su formulario ---------------------------- */

/* Abre el formulario EN MODO CIERRE: lo que se guarde llevara el id del evento
 * y lo sacara de la cola. */
static void cierre_empezar(int idx, categoria_t cat)
{
    const salida_evento_t *ev = salida_evento_en(idx);
    if (!ev) return;
    s_cerrando    = idx;
    s_cerrando_id = ev->id;
    /* AL CARRUSEL PRIMERO. El formulario vive en la pagina de registros, pero la
     * pregunta del arranque salta encima de la pagina que estes mirando (el
     * dialogo se muda a la pantalla activa, ver confirm_screen.c). Sin esto,
     * "Rellenarlo" abria el formulario en una pagina que no se ve y parecia que
     * el boton cerraba el cartel sin hacer nada. Visto en la placa el
     * 24-ago-2026 con las aguas; le pasaba igual al repostaje, la bombona y la
     * averia. */
    nav_ir_a_registros();
    show_form(cat);
}

/* Que formulario cierra cada tipo, y como se pregunta.
 *
 * El repostaje es el caso que da nombre a todo el diseno: pulsas al llegar al
 * surtidor y los numeros te los pide despues, que es cuando los sabes. Los
 * demas funcionan igual, cada uno con su formulario de siempre.
 *
 * cat == CAT_COUNT significa que no se cierra con un formulario: la parada va
 * por su camino (prolongar o terminarla) y el peaje no llega nunca aqui, porque
 * se rellena en el momento y no abre evento. */
typedef struct {
    categoria_t cat;
    const char *titulo;
    const char *ya_sabes;      /* completa "Ahora ya sabes ..." */
} cierre_t;

static const cierre_t CIERRE[EV_COUNT] = {
    [EV_PARADA]    = { CAT_COUNT,          NULL,                 NULL },
    [EV_AGUAS]     = { CAT_AGUAS,          "Aguas terminadas",
                       "lo que has hecho y lo\nque te ha costado" },
    [EV_REPOSTAJE] = { CAT_REPOSTAJE,      "Finalizar repostaje",
                       "el importe, los litros\ny los kilometros" },
    [EV_PEAJE]     = { CAT_COUNT,          NULL,                 NULL },
    [EV_BOMBONA]   = { CAT_BOMBONA,        "Bombonas cargadas",
                       "cuantas son y lo que\nhan costado" },
    [EV_AVERIA]    = { CAT_MANTENIMIENTO,  "Averia terminada",
                       "que se ha hecho y lo\nque ha costado" },
    [EV_ITV]       = { CAT_ITV,            "ITV pasada",
                       "el resultado, los km\ny lo que ha costado" },
};

/* idx y categoria caben de sobra en el user_data. */
static void cierre_rellenar(void *ud)
{
    unsigned v = (unsigned)(uintptr_t)ud;
    cierre_empezar((int)(v & 0xFF), (categoria_t)(v >> 8));
}

static bool cierre_form_preguntar(int idx, const salida_evento_t *ev)
{
    const cierre_t *c = &CIERRE[ev->tipo];
    if (reloj_p4() == 0) return false;

    char h[8];
    hora_corta(ev->epoch_ini, h, sizeof(h));
    snprintf(s_parada_txt, sizeof(s_parada_txt),
             "Lo anotaste a las %s.\nAhora ya sabes %s.", h, c->ya_sabes);
    /* El "no" no descarta nada: sigue abierto y se vuelve a preguntar.
     * Rellenarlo con el surtidor delante no siempre se puede. */
    confirm_screen_open(c->titulo, s_parada_txt, cat_color(c->cat),
                        "Rellenarlo", "Luego", cierre_rellenar,
                        (void *)(uintptr_t)((unsigned)c->cat << 8 | (unsigned)idx));
    return true;
}

/* Lo que hoy se sabe cerrar. Los demas tipos siguen esperando su formulario. */
static bool cierre_sabe(uint8_t tipo)
{
    if (tipo >= EV_COUNT) return false;
    return tipo == EV_PARADA || CIERRE[tipo].cat != CAT_COUNT;
}

static bool cierre_preguntar_en(int idx)
{
    const salida_evento_t *ev = salida_evento_en(idx);
    if (!ev) return true;
    if (ev->tipo == EV_PARADA)     return parada_preguntar_en(idx);
    if (cierre_sabe(ev->tipo))     return cierre_form_preguntar(idx, ev);
    return true;
}

/* La del arranque: el PRIMERO de la cola que se sepa cerrar Y que no se haya
 * ofrecido ya este mismo encendido. En orden de apertura, que es como manda
 * el diseno; los que no se saben cerrar todavia se saltan en vez de bloquear
 * a los de detras. El timer que llama a esto (parada_boot_timer_cb) sigue
 * vivo mientras queden por preguntar, asi que esta funcion "consume" uno cada
 * vez que consigue mostrar el dialogo -- solo se marca como preguntado si
 * cierre_preguntar_en() de verdad lo enseño (si devuelve false, p.ej. sin
 * hora de la P4 todavia, se reintenta el mismo en el siguiente tick). */
static bool parada_preguntar(void)
{
    int n = salida_eventos_abiertos();
    for (int i = 0; i < n; i++) {
        const salida_evento_t *ev = salida_evento_en(i);
        if (ev && cierre_sabe(ev->tipo) && !boot_ya_preguntado(ev->id)) {
            uint32_t id = ev->id;
            bool mostrado = cierre_preguntar_en(i);
            if (mostrado) boot_marcar_preguntado(id);
            return mostrado;
        }
    }
    return true;                          /* nada que preguntar */
}

/* El boton de la lista. A diferencia del aviso del arranque, aqui lo has
 * pedido tu: si no se puede, hay que decir por que en vez de no hacer nada,
 * que parece que el boton esta roto. */
static void ab_terminar_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (cierre_preguntar_en(idx)) return;
    confirm_screen_aviso("Sin la P4",
                         "No se que hora es, asi que\nno puedo cerrarlo.",
                         COL_ACCION_STOP, "Entendido");
}

/* Al encender: esperar a que la P4 diga la hora y preguntar por CADA evento
 * abierto que se sepa cerrar, en orden -- no solo el primero (ver el
 * comentario de s_boot_preguntados). Cada 2 s, que la fecha llega a 1 Hz y no
 * hay ninguna prisa. */
static bool hay_algo_que_cerrar(void)
{
    int n = salida_eventos_abiertos();
    for (int i = 0; i < n; i++) {
        const salida_evento_t *ev = salida_evento_en(i);
        if (ev && cierre_sabe(ev->tipo) && !boot_ya_preguntado(ev->id)) return true;
    }
    return false;
}

/* "Si" al aviso: a la pantalla de motivos, con la marca puesta para que lo que
 * elijas se anote con la hora del apagon (ver declarar). */
static void olvido_anotar(void *ud)
{
    (void)ud;
    s_olvido_anotando = true;
    nav_ir_a_registros();
    mostrar_menu(PAN_MOTIVO);
}

/* false si no hay ningun hueco que ofrecer. */
static bool olvido_preguntar(void)
{
    uint32_t seg = 0, desde = 0;
    if (!salida_olvido_pendiente(&seg, &desde)) return false;

    char h[8], dur[24];
    hora_corta(desde, h, sizeof(h));
    duracion_texto(seg, dur, sizeof(dur));
    snprintf(s_parada_txt, sizeof(s_parada_txt),
             "Estuviste parado desde las %s,\n%s.\n\n"
             "Si fue una parada, dime de que\ny la anoto con esa hora.", h, dur);

    s_olvido_ini = desde;
    /* Preguntado queda: se conteste lo que se conteste, no se vuelve a sacar en
     * este encendido. El "no" del dialogo no lleva callback, asi que la marca se
     * pone aqui y no en la respuesta. */
    salida_olvido_descartar();

    confirm_screen_open("Estuviste parado?", s_parada_txt, COL_VIAJE,
                        "Anotarlo", "No", olvido_anotar, NULL);
    return true;
}

/* Cuantas veces se mira antes de rendirse. Cada 2 s, o sea cinco minutos: si en
 * ese rato la P4 no ha dicho la hora, es que no esta, y sin hora no hay nada que
 * ofrecer. Sin este tope el temporizador se quedaria vivo para siempre. */
#define OLVIDO_INTENTOS_MAX  150

static void parada_boot_timer_cb(lv_timer_t *t)
{
    static uint16_t intentos;

    if (s_parada_ya_preguntada) { lv_timer_del(t); return; }

    /* Si ya hay un dialogo esperando respuesta -- el que abrio este mismo
     * timer en un tick anterior, tipicamente -- no lo pises: con dos o mas
     * eventos sin cerrar, hay_algo_que_cerrar() seguia dando true en el
     * SIGUIENTE tick (el que se acaba de preguntar ya cuenta como
     * "preguntado", pero quedan mas), y esto sustituia la pregunta en
     * pantalla por la siguiente antes de que diera tiempo a contestarla --
     * riesgo real de responder a la que no tocaba. Se reintenta el mismo
     * tick 2s despues. Detectado el 09-sep-2026. */
    if (confirm_screen_is_open()) return;

    /* Sin hora de la P4 no hay NADA que se pueda ofrecer todavia: ni
     * "hay algo que cerrar" (cierre_preguntar_en devuelve false sin hora, ver
     * mas abajo) ni "parada olvidada" (necesita calcular cuanto duro el
     * apagon). Un solo tope para las dos ramas: antes esta comprobacion solo
     * cubria la rama de "parada olvidada", y si habia algun evento SIN
     * cerrar (hay_algo_que_cerrar()==true) el timer se quedaba reintentando
     * cada 2s para siempre si la P4 nunca llegaba a dar la hora -- el evento
     * nunca se marca como "preguntado" porque cierre_preguntar_en() nunca
     * consigue mostrar el dialogo. Al agotarse, se deja para el siguiente
     * arranque con contacto (los eventos siguen abiertos, no se pierde
     * nada). Detectado por el usuario el 09-sep-2026. */
    if (reloj_p4() == 0) {
        if (++intentos < OLVIDO_INTENTOS_MAX) return;
        ESP_LOGW(TAG, "boot: sin hora de la P4 tras %d intentos, dejo de preguntar por ahora",
                 OLVIDO_INTENTOS_MAX);
        s_parada_ya_preguntada = true;
        lv_timer_del(t);
        return;
    }

    /* Ya hay hora: primero lo que quedo abierto, uno por tick hasta
     * agotarlos (ver s_boot_preguntados). El hueco de "parada olvidada" solo
     * se mira si no quedaba nada que cerrar, que es ademas la condicion que
     * pone salida_olvido_pendiente(). */
    if (hay_algo_que_cerrar()) {
        parada_preguntar();
        return;
    }

    olvido_preguntar();
    s_parada_ya_preguntada = true;
    lv_timer_del(t);
}

/* Deshacer lo que se acaba de anotar: el ultimo de la cola. */
static void deshacer_ultimo(void *ud)
{
    (void)ud;
    salida_evento_borrar(salida_eventos_abiertos() - 1);
    /* Si lo que se acaba de deshacer era justo la declaracion de una
     * puntual (Anotado -> Deshacer, mismo dialogo modal, nada de por
     * medio), hay que quitar tambien la marca de "declarada": si no, se
     * queda en el mismo estado muerto que evita salida_puntual_marcar_
     * declarada por el otro lado -- declarada pero sin ningun apunte
     * dentro, sin forma de volver a intentarlo. Comprobar aqui "declarado
     * && 0 abiertos" es seguro SOLO en este sitio: un apunte cerrado
     * normalmente (do_save) nunca pasa por deshacer_ultimo, asi que no hay
     * riesgo de desdeclarar una puntual que ya termino su parada de
     * verdad. Detectado el 09-sep-2026. */
    if (salida_get()->tipo == SALIDA_PUNTUAL && salida_get()->declarado &&
        salida_eventos_abiertos() == 0) {
        salida_puntual_desmarcar_declarada();
    }
    volver_al_menu();
}

/* --- Abrir y cerrar la salida --------------------------------------------- */

static void puntual_cb(lv_event_t *e)
{
    (void)e;
    /* La salida puntual ya NO se abre aqui: se trata como un viaje de una
     * sola parada (carpeta propia y descargable en la P4), y un viaje no se
     * abre hasta saber CUAL es -- aqui solo se elige el tipo, el nombre (el
     * asunto: Repostaje/Bombona/ITV/Averia) sale del boton que se toque en
     * PAN_PUNTUAL. Ver puntual_declarar_cb. Igual se exige la P4 ya aqui,
     * para no dejar entrar a elegir un asunto sin ella. */
    if (reloj_p4() == 0) {
        confirm_screen_aviso("Enciende la P4 primero",
                             "Sin ella no se que hora es,\ny el apunte va con su hora.",
                             COL_ACCION_STOP, "Entendido");
        return;
    }
    mostrar_menu(PAN_PUNTUAL);
}

/* Cerrar la salida OLVIDA lo que quedase abierto (ver salida_cerrar). Cuando
 * hay algo abierto no se impide -- si se impidiera no habria forma de salir,
 * porque cerrar un apunte solo se puede al arrancar y esa parte aun no esta --
 * pero se dice CUANTO se pierde y se pide el segundo toque.
 *
 * Buffer estatico porque el cartel lo lee mientras esta abierto. */
static char s_perder_txt[80];

static bool avisa_de_lo_abierto(const char *titulo, confirm_cb_t si)
{
    int n = salida_eventos_abiertos();
    if (n == 0) return false;
    snprintf(s_perder_txt, sizeof(s_perder_txt),
             "Hay %d apunte%s sin cerrar.\nSi sigues, se pierde%s.",
             n, n == 1 ? "" : "s", n == 1 ? "" : "n");
    confirm_screen_open(titulo, s_perder_txt, COL_ACCION_STOP,
                        "Si, descartar", "No, dejarlo", si, NULL);
    confirm_screen_ok_destructivo();
    return true;
}

static void puntual_do_cancelar(void *ud)
{
    (void)ud;
    /* Si ya se llego a tocar un tipo (Repostaje/Bombona/ITV/Averia), la P4
     * tiene una carpeta abierta de verdad (ver puntual_declarar_cb) y hay
     * que decirselo -- si no, se quedaria "viaje" fantasma abierto para
     * siempre. Si se cancela ANTES de elegir tipo, salida_get()->tipo sigue
     * en SALIDA_NINGUNA y no hay nada que avisar. Mismo criterio que usa
     * op_descartar en la P4: la carpeta pasa a DESCARTADO_<nombre>, no se
     * borra nada. */
    if (salida_get()->tipo == SALIDA_PUNTUAL) {
        char cuerpo[64];
        p4_api_cuerpo_descartar(cuerpo, sizeof(cuerpo), next_trip_seq());
        viaje_cola_error_t motivo;
        if (!viaje_cola_push(cuerpo, &motivo)) {
            /* Si esto no se encola, la carpeta se queda abierta de verdad
             * en la P4 -- justo lo que el comentario de arriba dice que hay
             * que evitar. Cerrar la salida aqui igualmente la dejaria como
             * "fantasma": nada en la pantalla, pero abierta en la P4 (se
             * descubriria con el 409 la proxima vez). Se avisa y NO se
             * cierra, para que se pueda reintentar con la flecha atras. */
            confirm_screen_aviso("No he podido cancelarla", cola_fallo_texto(motivo),
                                 COL_ACCION_STOP, "Entendido");
            return;
        }
        /* Simetria con puntual_do_terminar(): si llego a abrirse carpeta en
         * la P4 (tipo==SALIDA_PUNTUAL), save_trip_inicio() ya puso activo=1
         * en el blob del contador (inicio_puntual_resultado_cb). Sin este
         * cierre, cancelar dejaba ese blob diciendo "viaje activo" para
         * siempre -- mismo orden que terminar: solo tras el push CON EXITO,
         * antes de salida_cerrar(). Detectado por el usuario el
         * 09-sep-2026. */
        save_trip_fin();
    }
    salida_cerrar();
    mostrar_menu(PAN_PRINCIPAL);
}

/* En una salida puntual SIN DECLARAR la flecha de arriba no navega: la
 * CANCELA. Antes de llegar al sitio no hay ningun progreso real que
 * proteger, asi que deshacerla es lo unico que tiene sentido (te
 * arrepientes antes de haber hecho nada).
 *
 * En cuanto se declara (Ya he llegado) esto cambia: la pantalla de Puntual
 * solo ensena un estado, no lleva ningun dato util para pasarte todo el
 * trayecto de vuelta mirandola -- lo logico es volver a la pantalla
 * principal, que si trae informacion. Por eso, una vez declarada, la
 * flecha vuelve a navegar sin mas: "Terminar salida" sigue esperando en
 * Puntual para cuando vuelvas a entrar. Perderia sentido cancelar aqui una
 * salida que ya tiene progreso real -- para eso esta el boton explicito. */
static void puntual_cancelar_cb(lv_event_t *e)
{
    (void)e;
    if (salida_get()->tipo == SALIDA_PUNTUAL && salida_get()->declarado) {
        /* A la de info (bateria/aguas/temperaturas), no a PAN_PRINCIPAL --
         * esa es solo "Nueva salida"/"Configuracion" y no trae nada util
         * para el trayecto de vuelta. El menu de Registro se queda tal cual
         * (en PAN_PUNTUAL, mostrando "Terminar salida") para cuando se
         * vuelva a esta pagina deslizando. */
        nav_ir_a_info();
        return;
    }
    if (avisa_de_lo_abierto("Cancelar la salida?", puntual_do_cancelar)) return;
    puntual_do_cancelar(NULL);
}

static void terminar_salida_cb(lv_event_t *e)
{
    (void)e;
    if (avisa_de_lo_abierto("Terminar el viaje?", viaje_do_finalizar)) return;
    viaje_finalizar_cb(e);   /* el mismo cartel de confirmacion de siempre */
}

/* --- Las siete pantallas --------------------------------------------------
 *
 * Geometria (800x480, ver estilos.h). La franja de arriba se come BAR_H, y con
 * PAN_PAD a cada lado el cuerpo util es de 776 px de ancho. De ahi salen los
 * tamanos, que estan definidos ARRIBA del todo (CEL3_W/CEL2_W, junto a
 * MENU_PAD): aqui solo queda dicho de donde salen, para no tener dos sitios
 * donde cambiarlos.
 *
 * No van en porcentaje: en LVGL el hueco entre celdas NO se descuenta del
 * porcentaje y la tercera columna se caeria de fila (ver el bloque del menu
 * viejo, mismo motivo).
 */

/* La tira de estado de un menu de salida. Es un rotulo, pero se TOCA: es la
 * puerta a la lista de lo que queda abierto. Area de toque generosa por lo de
 * siempre -- 16 px de alto no se aciertan con el vehiculo en marcha. */
static lv_obj_t *tira_crear(lv_obj_t *body, int idx)
{
    lv_obj_t *fila_t = lv_obj_create(body);
    lv_obj_remove_style_all(fila_t);
    lv_obj_set_width(fila_t, lv_pct(100));
    lv_obj_set_height(fila_t, LV_SIZE_CONTENT);
    lv_obj_clear_flag(fila_t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(fila_t, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fila_t, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(fila_t, 8, 0);
    lv_obj_add_flag(fila_t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(fila_t, 10);
    lv_obj_add_event_cb(fila_t, tira_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l = lv_label_create(fila_t);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(COL_LABEL), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(l, 1);
    s_tiras[idx] = l;

    /* "1 sin cerrar" en AMBAR y no en gris con el resto. Escrito como texto
     * corrido no se veia: 16 px grises arriba del todo, encima de dos casillas
     * de color, no son un aviso -- son ruido. Es lo unico de esta pantalla que
     * reclama algo del usuario, asi que se pinta como tal. */
    lv_obj_t *chip = lv_obj_create(fila_t);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(chip, 10, 0);
    lv_obj_set_style_pad_ver(chip, 4, 0);
    lv_obj_set_style_radius(chip, 10, 0);
    lv_obj_set_style_bg_color(chip, lv_color_hex(COL_BOMBONA), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);   /* el toque es de la fila */
    lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);
    s_tira_chip[idx] = chip;

    lv_obj_t *cl = lv_label_create(chip);
    lv_label_set_text(cl, "");
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(cl, lv_color_hex(COL_TILE_FG), 0);
    s_tira_chip_lbl[idx] = cl;
    return fila_t;
}

/* Una fila de la lista de lo abierto: que es, a que hora se anoto, y Borrar. */
static void abiertos_fila_crear(lv_obj_t *body, int idx)
{
    lv_obj_t *f = lv_obj_create(body);
    lv_obj_remove_style_all(f);
    lv_obj_set_width(f, lv_pct(100));
    lv_obj_set_height(f, ESC(58));
    lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(f, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(f, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(f, LV_OBJ_FLAG_HIDDEN);
    s_ab_fila[idx] = f;

    lv_obj_t *col = lv_obj_create(f);
    lv_obj_remove_style_all(col);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);

    s_ab_tipo[idx] = lv_label_create(col);
    lv_obj_set_style_text_font(s_ab_tipo[idx], &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s_ab_tipo[idx], lv_color_hex(0xFFFFFF), 0);

    s_ab_hora[idx] = lv_label_create(col);
    lv_obj_set_style_text_font(s_ab_hora[idx], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ab_hora[idx], lv_color_hex(COL_LABEL), 0);

    /* Terminar antes que Borrar y en verde: es lo que se quiere hacer casi
     * siempre. Borrar es para el dedo equivocado. */
    s_ab_fin[idx] = boton_chico(f, "Terminar", COL_ACCION_OK, 120,
                                ab_terminar_cb, (void *)(intptr_t)idx);
    lv_obj_add_flag(s_ab_fin[idx], LV_OBJ_FLAG_HIDDEN);
    boton_chico(f, "Borrar", COL_ACCION_STOP, 110, borrar_cb, (void *)(intptr_t)idx);
}

/* Salir del paseo: deja de pasear, limpia lo que hubiera a medias y vuelve al
 * menu que toque segun la salida en curso (ver view_registro_paseo_salir). */
static void paseo_salir_cb(lv_event_t *e)
{
    (void)e;
    view_registro_paseo_salir();
}

static void crear_menus(lv_obj_t *parent)
{
    lv_obj_t *body, *f;

    /* --- 1. Principal: sin salida en marcha --- */
    body = pantalla_crear(parent, PAN_PRINCIPAL, NULL, -1);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    boton_grande(body, ICO_MAS, "NUEVA SALIDA", "viaje o gestion suelta",
                 COL_ACCION_OK, ir_a_cb, (void *)(uintptr_t)PAN_TIPO);
    /* EL BOTON SE MIDE SOLO A SU TEXTO (8-oct-2026), y no lleva ancho clavado.
     *
     * Historia de este boton, porque el detalle importa: con 160 px de ancho el
     * rotulo "Configuracion" no cabia NI en la letra mas pequena del ajuste
     * automatico -- medido con las metricas de la fuente, 197 px en letra 22 y
     * 161 en la 18, contra 152 de hueco. El usuario lo vio como "el texto es
     * mas grande que el boton" y luego pidio lo contrario de lo que yo habia
     * hecho: NO acortar el rotulo ni encogerlo, sino ensanchar el boton para
     * que el texto se lea grande.
     *
     * La forma de que eso no se vuelva a desajustar es LV_SIZE_CONTENT: el
     * boton mide lo que mide su texto mas el relleno, asi que ni se sale ni se
     * queda corto, y el ajuste automatico de rotulos no tiene nada que encoger.
     * 24 px de relleno por lado: con 8 el texto quedaba pegado al canto. */
    lv_obj_t *b_ajustes = boton_chico(body, "Configuracion", COL_AJUSTES, LV_SIZE_CONTENT,
                                      ajustes_click_cb, NULL);
    lv_obj_set_style_pad_hor(b_ajustes, 24, 0);

    /* --- 2. Tipo de salida --- */
    body = pantalla_crear(parent, PAN_TIPO, "TIPO DE SALIDA", PAN_PRINCIPAL);
    f = fila(body);
    /* Las dos del mismo tamano: ninguna manda sobre la otra. */
    /* Cadena vacia y no NULL: el tamano del titulo (32) va ligado a si hay
     * apoyo, y se queria mantener grande al quitar el texto descriptivo. */
    casilla(f, ICO_VIAJE, "VIAJE", "",
            COL_VIAJE, CEL2_W, lv_pct(100), viaje_iniciar_cb, NULL);
    casilla(f, ICO_PUNTUAL, "PUNTUAL", "repostar, ITV,\nbombona o taller",
            COL_BOMBONA, CEL2_W, lv_pct(100), puntual_cb, NULL);

    /* --- 3. Menu de salida: el principal mientras dure el viaje --- */
    body = pantalla_crear(parent, PAN_SALIDA, NULL, -1);
    tira_crear(body, TIRA_SALIDA);

    boton_grande(body, ICO_MAS, "ANADIR PARADA", NULL,
                 COL_ACCION_OK, ir_a_cb, (void *)(uintptr_t)PAN_TIPOS);

    /* Los dos secundarios comparten fila: entre los dos gastan lo que gastaria
     * uno solo, y asi el grande se queda con el sitio. */
    f = fila(body);
    lv_obj_set_flex_grow(f, 0);
    lv_obj_set_height(f, BOTON_CHICO_H);
    lv_obj_set_flex_grow(boton_chico(f, "Terminar salida", COL_ACCION_STOP, 0,
                                     terminar_salida_cb, NULL), 1);
    /* Aqui si cabe "Configuracion": los dos botones de esta fila se reparten
     * los 560 px de la columna, o sea ~275 cada uno, y el rotulo mide 197 en
     * letra 22. En el menu principal (160 px) no cabia y se llama "Ajustes". */
    lv_obj_set_flex_grow(boton_chico(f, "Configuracion", COL_AJUSTES, 0,
                                     ajustes_click_cb, NULL), 1);

    /* --- 4. Las seis cosas que se anotan en un viaje --- */
    body = pantalla_crear(parent, PAN_TIPOS, "QUE ANOTAS?", PAN_SALIDA);
    f = fila(body);
    casilla(f, ICO_PARADA,    "Parada",    NULL, COL_ACCION_OK,
            CEL3_W, lv_pct(100), ir_a_cb, (void *)(uintptr_t)PAN_MOTIVO);
    casilla(f, ICO_AGUAS,     "Aguas",     NULL, COL_VIAJE,
            CEL3_W, lv_pct(100), declarar_cb, DECL(EV_AGUAS, 0, 0));
    casilla(f, ICO_REPOSTAJE, "Repostaje", NULL, COL_BOMBONA,
            CEL3_W, lv_pct(100), declarar_cb, DECL(EV_REPOSTAJE, 0, 0));
    f = fila(body);
    /* El peaje es la excepcion: se paga con el motor en marcha y lo rellena el
     * copiloto en el momento, asi que abre su formulario y no un evento. */
    casilla(f, ICO_PEAJE,     "Peaje",     NULL, COL_PEAJE,
            CEL3_W, lv_pct(100), icon_click_cb, (void *)(uintptr_t)CAT_PEAJE);
    casilla(f, ICO_BOMBONA,   "Bombona",   NULL, COL_AJUSTES,
            CEL3_W, lv_pct(100), declarar_cb, DECL(EV_BOMBONA, 0, 0));
    casilla(f, ICO_AVERIA,    "Averia",    NULL, COL_AJUSTES,
            CEL3_W, lv_pct(100), declarar_cb, DECL(EV_AVERIA, 0, 0));

    /* --- 5. Las cuatro de una salida puntual --- */
    body = pantalla_crear(parent, PAN_PUNTUAL, "SALIDA PUNTUAL", PAN_CANCELA_PUNTUAL);
    tira_crear(body, TIRA_PUNTUAL);
    f = fila(body);
    s_puntual_filas[0] = f;
    casilla(f, ICO_REPOSTAJE, "Repostaje", NULL, COL_BOMBONA,
            CEL2_W, lv_pct(100), puntual_declarar_cb, DECL(EV_REPOSTAJE, 0, 0));
    casilla(f, ICO_BOMBONA,   "Bombona",   NULL, COL_AJUSTES,
            CEL2_W, lv_pct(100), puntual_declarar_cb, DECL(EV_BOMBONA, 0, 0));
    f = fila(body);
    s_puntual_filas[1] = f;
    casilla(f, ICO_ITV,       "ITV",       NULL, COL_VIAJE,
            CEL2_W, lv_pct(100), puntual_declarar_cb, DECL(EV_ITV, 0, 0));
    casilla(f, ICO_AVERIA,    "Averia/Mant.", NULL, COL_AJUSTES,
            CEL2_W, lv_pct(100), puntual_declarar_cb, DECL(EV_AVERIA, 0, 0));

    /* Se ensena en vez de las 4 casillas de arriba mientras hay una puntual
     * en curso (puntual_refresh(), enganchado a mostrar_menu): solo hay una
     * parada por salida, elegir ya no pinta nada, lo que toca es terminar al
     * volver. Empieza oculto -- todavia no hay ninguna en marcha. */
    s_puntual_fin_cont = lv_obj_create(body);
    lv_obj_remove_style_all(s_puntual_fin_cont);
    lv_obj_set_size(s_puntual_fin_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(s_puntual_fin_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_puntual_fin_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_puntual_fin_cont, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_puntual_fin_cont, 16, 0);
    lv_obj_clear_flag(s_puntual_fin_cont, LV_OBJ_FLAG_SCROLLABLE);

    s_puntual_fin_lbl = lv_label_create(s_puntual_fin_cont);
    lv_obj_set_style_text_font(s_puntual_fin_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_puntual_fin_lbl, lv_color_hex(COL_LABEL), 0);
    lv_obj_set_style_text_align(s_puntual_fin_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_puntual_fin_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_puntual_fin_lbl, lv_pct(90));

    /* Los dos son excluyentes -- ver puntual_refresh(): mientras no se ha
     * declarado se ofrece "Ya he llegado" (lo mismo que hace el aviso de la
     * pantalla principal); en cuanto se usa, se cambia por "Terminar salida"
     * y no vuelve a ensenarse.
     *
     * "Terminar salida" va con boton_grande y no boton_chico (a diferencia
     * de "Terminar salida" en un viaje real, que SI es chico porque ahi
     * comparte pantalla con "Anadir parada", la accion principal mientras
     * el viaje sigue en marcha): en una puntual ya declarada no hay nada
     * mas que hacer en esta pantalla salvo terminar, asi que es ella la
     * accion principal. */
    s_puntual_llegada_btn = boton_chico(s_puntual_fin_cont, "Ya he llegado",
                                        COL_ACCION_OK, 220, puntual_llegada_cb, NULL);
    s_puntual_fin_btn = boton_grande(s_puntual_fin_cont, NULL, "Terminar salida", NULL,
                                     COL_ACCION_STOP, puntual_terminar_cb, NULL);
    /* boton_grande trae flex_grow(1): pensado para cuando ES el unico
     * contenido de una pantalla y se reparte todo el alto disponible. Aqui,
     * dentro de s_puntual_fin_cont (alto SIZE_CONTENT), ese grow se
     * descontrolaba y el boton se comia la pantalla entera. Se anula el
     * grow y se fija un tamano concreto -- mas grande que boton_chico
     * (46 px) pero acotado. */
    lv_obj_set_flex_grow(s_puntual_fin_btn, 0);
    lv_obj_set_width(s_puntual_fin_btn, ESC(260));
    lv_obj_set_height(s_puntual_fin_btn, ESC(90));
    lv_obj_add_flag(s_puntual_fin_cont, LV_OBJ_FLAG_HIDDEN);

    /* --- 6. Por que paras --- */
    body = pantalla_crear(parent, PAN_MOTIVO, "POR QUE PARAS?", PAN_TIPOS);
    f = fila(body);
    casilla(f, ICO_VISITA,   "Visita",   NULL, COL_AJUSTES, CEL3_W, lv_pct(100),
            declarar_cb, DECL(EV_PARADA, MOTIVO_VISITA, 0));
    casilla(f, ICO_DESCANSO, "Descanso", NULL, COL_AJUSTES, CEL3_W, lv_pct(100),
            declarar_cb, DECL(EV_PARADA, MOTIVO_DESCANSO, 0));
    casilla(f, ICO_COMER,    "Comer",    NULL, COL_AJUSTES, CEL3_W, lv_pct(100),
            declarar_cb, DECL(EV_PARADA, MOTIVO_COMER, 0));
    f = fila(body);
    casilla(f, ICO_CENAR,    "Cenar",    NULL, COL_AJUSTES, CEL3_W, lv_pct(100),
            declarar_cb, DECL(EV_PARADA, MOTIVO_CENAR, 0));
    casilla(f, ICO_COMPRAS,  "Compras",  NULL, COL_AJUSTES, CEL3_W, lv_pct(100),
            declarar_cb, DECL(EV_PARADA, MOTIVO_COMPRAS, 0));
    /* Pernocta va en verde y no en gris como las otras cinco: es la unica que
     * lleva cola -- donde, servicios, precio y valoracion -- y no debe
     * pulsarse por error creyendo que es un descanso. */
    casilla(f, ICO_PERNOCTA, "PERNOCTA", NULL, COL_ACCION_OK, CEL3_W, lv_pct(100),
            ir_a_cb, (void *)(uintptr_t)PAN_SITIO);

    /* --- 7. Donde pasas la noche --- */
    body = pantalla_crear(parent, PAN_SITIO, "DONDE DUERMES?", PAN_MOTIVO);
    f = fila(body);
    casilla_sitio(f, "Parking", false, COL_VIAJE,         CEL3_W,
                  declarar_cb, DECL(EV_PARADA, MOTIVO_PERNOCTA, SITIO_PARKING_GRATIS));
    casilla_sitio(f, "Parking", true,  COL_VIAJE,         CEL3_W,
                  declarar_cb, DECL(EV_PARADA, MOTIVO_PERNOCTA, SITIO_PARKING_PAGO));
    casilla_sitio(f, "Area",    false, COL_MANTENIMIENTO, CEL3_W,
                  declarar_cb, DECL(EV_PARADA, MOTIVO_PERNOCTA, SITIO_AREA_GRATIS));
    f = fila(body);
    casilla_sitio(f, "Area",    true,  COL_MANTENIMIENTO, CEL3_W,
                  declarar_cb, DECL(EV_PARADA, MOTIVO_PERNOCTA, SITIO_AREA_PAGO));
    /* El camping ocupa lo que dos: sobra sitio y no tiene pareja gratis -- no
     * hay campings gratis. 145 + 10 + 300 = 455, los 456 utiles. */
    casilla_sitio(f, "Camping", true,  COL_PEAJE,         300,
                  declarar_cb, DECL(EV_PARADA, MOTIVO_PERNOCTA, SITIO_CAMPING));

    /* --- 8. Lo que queda sin cerrar ---
     * Se llega tocando la tira. Existe para PODER DESHACER: hasta que estén
     * las pantallas de al volver a dar el contacto, este es el unico sitio
     * desde donde quitar un apunte puesto por error. La flecha vuelve al menu
     * que toque, que no es siempre el mismo (viaje o puntual). */
    body = pantalla_crear(parent, PAN_ABIERTOS, "SIN CERRAR", PAN_VOLVER);
    for (int i = 0; i < SALIDA_EVENTOS_MAX; i++) abiertos_fila_crear(body, i);

    /* --- 0. PASEO: el indice de TODAS las pantallas (ver view_registro.h) ----
     *
     * Existe para poder RECORRER el cuaderno sin una salida abierta: los menus
     * de registro solo se abren desde dentro de un viaje o una puntual, asi que
     * sin esto no hay manera de revisarlos. No toca ningun dato: solo cambia
     * que pantalla se ve.
     *
     * Se entra desde Ajustes ("Ver todas las pantallas") y se sale con el boton
     * "Salir del paseo", aqui abajo del todo.
     *
     * El cuerpo SE DESLIZA porque 18 botones no caben de alto en 480 px: se
     * reparten en tres columnas y aun asi hay que bajar un poco. */
    body = pantalla_crear(parent, PAN_PASEO, "PASEO: TODAS LAS PANTALLAS", -1);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *titulo_p = lv_label_create(body);
    lv_label_set_text(titulo_p, "MENUS (sin salida abierta)");
    lv_obj_set_style_text_font(titulo_p, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(titulo_p, lv_color_hex(COL_LABEL), 0);

    /* Los nueve menus, en tres filas de tres. Se empieza por PAN_PRINCIPAL:
     * PAN_PASEO es este mismo indice y volver a el no aporta nada. */
    for (int p = PAN_PRINCIPAL; p < PAN_COUNT; p += 3) {
        lv_obj_t *fp = fila(body);
        lv_obj_set_flex_grow(fp, 0);
        lv_obj_set_height(fp, ESC(58));
        for (int k = p; k < p + 3 && k < PAN_COUNT; k++) {
            char rot[40];
            snprintf(rot, sizeof(rot), "%d. %s", k, PAN_NOMBRE[k]);
            lv_obj_set_flex_grow(casilla(fp, NULL, rot, NULL, COL_VIAJE,
                                         CEL3_W, lv_pct(100), ir_a_cb,
                                         (void *)(uintptr_t)k), 1);
        }
    }

    lv_obj_t *titulo_f = lv_label_create(body);
    lv_label_set_text(titulo_f, "FORMULARIOS");
    lv_obj_set_style_text_font(titulo_f, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(titulo_f, lv_color_hex(COL_LABEL), 0);

    for (int c = 0; c < CAT_COUNT; c += 3) {
        lv_obj_t *fc = fila(body);
        lv_obj_set_flex_grow(fc, 0);
        lv_obj_set_height(fc, ESC(58));
        for (int k = c; k < c + 3 && k < CAT_COUNT; k++) {
            lv_obj_set_flex_grow(casilla(fc, NULL, CAT_NOMBRE[k], NULL, COL_BOMBONA,
                                         CEL3_W, lv_pct(100), icon_click_cb,
                                         (void *)(uintptr_t)k), 1);
        }
    }

    lv_obj_set_flex_grow(boton_chico(body, "Salir del paseo", COL_AJUSTES,
                                     lv_pct(100), paseo_salir_cb, NULL), 0);

    /* --- 9. La P4 tiene un viaje abierto ---
     * Se llega sola cuando la P4 contesta 409 al empezar un viaje. Se resuelve
     * DESDE AQUI porque la P4 esta en la parte de atras: levantarse del asiento
     * del conductor para pulsar un boton no es una opcion. */
    body = pantalla_crear(parent, PAN_VIAJE_P4, "VIAJE EN LA P4", PAN_VOLVER);
    lv_obj_t *aviso = lv_label_create(body);
    lv_label_set_text(aviso, "La P4 tiene un viaje abierto.\nHay que cerrarlo antes de\nempezar otro.");
    lv_obj_set_style_text_font(aviso, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(aviso, lv_color_hex(COL_LABEL), 0);
    boton_grande(body, ICO_GUARDAR, "GUARDARLO", "lo cierra con su resumen",
                 COL_ACCION_OK, viaje_p4_guardar_cb, NULL);
    lv_obj_set_flex_grow(boton_chico(body, "Apartarlo (era una prueba)",
                                     COL_ACCION_STOP, lv_pct(100),
                                     viaje_p4_descartar_cb, NULL), 0);
}

/* ── Modo paseo (ver view_registro.h) ───────────────────────────────────────
 *
 * Entra desde Ajustes: se va al carrusel de registro y se abre el indice. No
 * comprueba nada de la salida en curso a proposito -- su razon de ser es
 * precisamente poder ver las pantallas SIN salida.
 */
void view_registro_diag_arbol(int idx)
{
    if (idx < 0 || idx >= CAT_COUNT) return;
    lv_obj_t *f = s_forms[idx];
    ESP_LOGW(TAG, "MARCA-1417 ARBOL '%s': %dx%d", CAT_NOMBRE[idx],
             (int)lv_obj_get_width(f), (int)lv_obj_get_height(f));
    form_volcar(f, 0);
}

void view_registro_paseo_mostrar(void)
{
    s_paseo = true;
    mostrar_menu(PAN_PASEO);
}

bool view_registro_paseo_activo(void) { return s_paseo; }

void view_registro_paseo_salir(void)
{
    if (!s_paseo) return;
    s_paseo = false;
    /* Al salir SI se limpia (es lo que hace reset, que en paseo no hace nada) y
     * se vuelve al menu que toque segun la salida en curso. */
    view_registro_reset();
    mostrar_menu(PAN_PRINCIPAL);
}

void view_registro_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    /* El tema de LVGL mete padding y borde propios en la pantalla. El reparto
     * 3+2 de abajo encaja al pixel (458 de 460 utiles), asi que unos pocos px
     * comidos aqui bajarian la tercera celda de fila. Se anula explicitamente
     * para que la geometria no dependa del tema. */
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_border_width(parent, 0, 0);

    /* Campo INVISIBLE para el destino del viaje. Vivia dentro del formulario
     * viejo de Viaje, que ya no existe; pero lo sigue usando "Iniciar viaje"
     * del menu nuevo, porque el editor a pantalla completa vuelca sobre un
     * textarea y aqui no hay formulario donde ponerlo. Fuera del layout y de
     * tamano 0 para que no ocupe ni un pixel. */
    s_viaje_destino_ta = lv_textarea_create(parent);
    lv_obj_add_flag(s_viaje_destino_ta, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_viaje_destino_ta, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(s_viaje_destino_ta, 0, 0);
    lv_textarea_set_one_line(s_viaje_destino_ta, true);
    /* 20 caracteres: es el limite del diseno, y la ruta en la SD de la P4 no
     * debe crecer sin control. */
    lv_textarea_set_max_length(s_viaje_destino_ta, 20);
    /* SOLO ASCII, y no es un descuido: las fuentes Montserrat compiladas no
     * traen acentos ni la n con virgulilla, y saldrian cuadrados. Ademas esto
     * acaba siendo un NOMBRE DE CARPETA, asi que los caracteres que romperian
     * la ruta no se dejan ni teclear. La P4 vuelve a filtrar por su cuenta. */
    lv_textarea_set_accepted_chars(s_viaje_destino_ta,
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_");
    lv_obj_add_event_cb(s_viaje_destino_ta, viaje_destino_listo_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* --- Menus de la salida ---
     * Se crean ANTES que los formularios para que estos queden por encima en
     * el orden de dibujo. Cual se ve lo decide volver_al_menu(), al final. */
    crear_menus(parent);

    /* --- Formularios (ocultos hasta que se elige un icono) --- */
    s_forms[CAT_REPOSTAJE] = make_form_container(parent);
    build_repostaje(form_col(s_forms[CAT_REPOSTAJE]));

    s_forms[CAT_PEAJE] = make_form_container(parent);
    build_peaje(form_col(s_forms[CAT_PEAJE]));

    s_forms[CAT_BOMBONA] = make_form_container(parent);
    build_bombona(form_col(s_forms[CAT_BOMBONA]));

    s_forms[CAT_MANTENIMIENTO] = make_form_container(parent);
    build_mantenimiento(form_col_ancha(s_forms[CAT_MANTENIMIENTO]));

    s_forms[CAT_AGUAS] = make_form_container(parent);
    build_aguas(form_col_ancha(s_forms[CAT_AGUAS]));

    s_forms[CAT_ITV] = make_form_container(parent);
    build_itv(form_col(s_forms[CAT_ITV]));

    s_forms[CAT_PERNOCTA] = make_form_container(parent);
    /* La pernocta va con la columna ANCHA: su fila de precio lleva importe,
     * moneda y "Noche / 24 h" uno al lado del otro, y con el margen ancho esos
     * tres controles se quedaban en 320 px (apretados y a la izquierda). */
    build_pernocta(form_col_ancha(s_forms[CAT_PERNOCTA]));

    s_forms[CAT_SERVICIOS] = make_form_container(parent);
    build_servicios(form_col_ancha(s_forms[CAT_SERVICIOS]));

    s_forms[CAT_VALORACION] = make_form_container(parent);
    build_valoracion(form_col(s_forms[CAT_VALORACION]));

    /* Estado de partida (si se fue la luz en mitad de un viaje, sigue
     * habiendo viaje) lo recupera salida_init() via salida_get(), no aqui
     * -- load_trip_active()/s_viaje_activo eran de antes de ese modulo y
     * ya no los leia nadie; borrados el 09-sep-2026. */

    /* La parada del modelo VIEJO (namespace "parada" de NVS) ya no existe: el
     * cuaderno se organiza por salidas y las paradas son eventos de salida.c.
     * Si quedo una marcada de antes, se suelta aqui y no se pregunta por ella;
     * dejarla puesta sacaria DOS dialogos de fin de parada al encender, uno por
     * modelo, y el viejo no sabe cerrar nada que exista hoy. */
    parada_abierta_t pendiente;
    load_parada_abierta(&pendiente);
    if (pendiente.abierta) {
        ESP_LOGW(TAG, "habia una parada del modelo viejo sin cerrar: la suelto");
        clear_parada_abierta();
    }

    /* Vigilar en segundo plano hasta que la P4 diga la hora y preguntar
     * entonces -- por si quedo una parada abierta de VERDAD (un evento de la
     * salida), O por si no quedo nada abierto pero hay un hueco sin explicar
     * (parada olvidada, ver salida_olvido_pendiente). parada_boot_timer_cb ya
     * distingue los dos casos por dentro; el timer se crea SIEMPRE, no solo
     * si hay_algo_que_cerrar(): antes esa condicion tapaba justo el caso que
     * la parada olvidada necesita (nada abierto), asi que esa oferta nunca
     * llegaba a dispararse. Detectado auditando el 07-sep-2026. El dialogo se
     * muda solo a la pantalla que este activa (ver confirm_screen.c). Cada
     * 2 s: la fecha llega a 1 Hz y no hay prisa. */
    lv_timer_create(parada_boot_timer_cb, 2000, NULL);

    /* --- Editor de campo --- */
    /* Editor de campo a pantalla completa. Se crea el ULTIMO a proposito: asi
     * queda por encima de los formularios en el orden de dibujo. */
    entry_screen_init(parent);
    confirm_screen_init(parent);

    /* Hora y puntos de la franja, una pasada por segundo. */
    lv_timer_create(barra_timer_cb, 1000, NULL);

    /* El menu de partida sale del ESTADO, no de un valor por defecto: si se
     * fue la luz en mitad de un viaje, se vuelve al menu de salida y no a la
     * pantalla principal. */
    volver_al_menu();
    s_ui_lista = true;
}

/* ── Modo captura de pantallas ────────────────────────────────────────────
 * Solo para capture_carousel.c (ver capture_carousel.h): recorrer TODOS los
 * menus y formularios de este carrusel para documentar el proyecto, sin
 * pasar por la navegacion normal (que exige tener una salida de verdad
 * abierta para llegar a la mayoria). mostrar_menu()/show_form() ya eran
 * puros interruptores de visibilidad sobre contenedores creados de
 * antemano -- no dependen de que haya una salida abierta, asi que se
 * pueden llamar directamente pase lo que pase en salida_get(). */


int view_registro_num_pantallas(void) { return PAN_COUNT; }
int view_registro_num_formularios(void) { return CAT_COUNT; }

void view_registro_mostrar_pantalla(int p)
{
    if (p < 0 || p >= PAN_COUNT) return;
    mostrar_menu((pantalla_t)p);
}

void view_registro_mostrar_formulario(int idx)
{
    if (idx < 0 || idx >= CAT_COUNT) return;
    show_form(idx);
}

const char *view_registro_nombre_pantalla(int p)
{
    return (p >= 0 && p < PAN_COUNT) ? PAN_NOMBRE[p] : "?";
}

const char *view_registro_nombre_formulario(int idx)
{
    return (idx >= 0 && idx < CAT_COUNT) ? CAT_NOMBRE[idx] : "?";
}
