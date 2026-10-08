/* nav.c - Carrusel de 3 pantallas con gesto horizontal + pantalla de
 * Ajustes aparte (se abre/cierra por boton, no forma parte del gesto). */
#include "nav.h"
#include "view_info.h"
#include "view_registro.h"
#include "view_inclinacion.h"
#include "view_ajustes.h"
#include "lvgl.h"

#define NAV_COUNT       3
#define NAV_INCLINACION 0   /* izquierda */
#define NAV_INFO        1   /* centro (arranque) */
#define NAV_REGISTRO    2   /* derecha */

#define NAV_ANIM_MS     220

static lv_obj_t *s_screens[NAV_COUNT];
static lv_obj_t *s_ajustes_screen;
static uint8_t   s_current = NAV_INFO;

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);

    /* Un deslizamiento NO es un toque.
     *
     * LVGL manda el CLICKED al objeto donde se APOYO el dedo cuando se levanta,
     * aunque por el medio haya saltado un gesto (lv_indev.c:1005-1020: solo se
     * lo salta si hubo scroll, no si hubo gesto). Sin esto pasaba lo siguiente:
     * apoyabas el dedo sobre un campo del formulario, deslizabas, se limpiaba
     * el formulario y se cambiaba de pantalla -- correcto -- y al levantar el
     * dedo llegaba el clic al campo de origen, que volvia a abrir el
     * formulario en la pantalla ya oculta. Al regresar te lo encontrabas
     * abierto. Intermitente: solo si el dedo arrancaba encima de un widget.
     *
     * wait_release hace que al levantar el dedo se mande PRESS_LOST en vez de
     * CLICKED. Va antes de decidir la direccion a proposito: el toque se anula
     * aunque el gesto no lleve a ninguna parte (deslizar hacia la izquierda
     * estando ya en el ultimo cromo), que si no abriria un formulario por
     * sorpresa. */
    lv_indev_wait_release(indev);

    int next = s_current;
    lv_scr_load_anim_t anim;
    if (dir == LV_DIR_LEFT && s_current < NAV_COUNT - 1) {
        next = s_current + 1;
        anim = LV_SCR_LOAD_ANIM_MOVE_LEFT;
    } else if (dir == LV_DIR_RIGHT && s_current > 0) {
        next = s_current - 1;
        anim = LV_SCR_LOAD_ANIM_MOVE_RIGHT;
    } else {
        return;
    }
    /* Al abandonar la pagina de registros se vuelve a su menu de iconos: si no,
     * al regresar te encontrabas el formulario abierto donde lo dejaste, en vez
     * del menu. Cierra tambien el editor de campo y la confirmacion, y deja los
     * formularios en blanco (clear_forms(), dentro de show_grid()). */
    if (s_current == NAV_REGISTRO) view_registro_reset();

    s_current = (uint8_t)next;
    lv_scr_load_anim(s_screens[s_current], anim, NAV_ANIM_MS, 0, false);
}

void nav_ir_a_inclinacion(void)
{
    if (s_current == NAV_INCLINACION) return;
    if (s_current == NAV_REGISTRO) view_registro_reset();
    lv_scr_load_anim_t anim = (s_current > NAV_INCLINACION)
        ? LV_SCR_LOAD_ANIM_MOVE_RIGHT : LV_SCR_LOAD_ANIM_MOVE_LEFT;
    s_current = NAV_INCLINACION;
    lv_scr_load_anim(s_screens[NAV_INCLINACION], anim, NAV_ANIM_MS, 0, false);
}

void nav_ir_a_info(void)
{
    if (s_current == NAV_INFO) return;
    if (s_current == NAV_REGISTRO) view_registro_reset();
    lv_scr_load_anim_t anim = (s_current < NAV_INFO)
        ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT;
    s_current = NAV_INFO;
    lv_scr_load_anim(s_screens[NAV_INFO], anim, NAV_ANIM_MS, 0, false);
}

void nav_ir_a_registros(void)
{
    /* Ajustes se abre como overlay SIN tocar s_current (ver nav_open_ajustes):
     * si se abrio estando ya en NAV_REGISTRO, el early-return de aqui abajo
     * no hacia NADA -- la pantalla activa de verdad seguia siendo Ajustes, y
     * el formulario se montaba detras suya. "Rellenarlo" parecia no
     * responder. Cerrar Ajustes aqui sin avisar, aunque haya un campo a
     * medio escribir: coherente con que el propio carrusel ya descarta el
     * formulario de Registro al cambiar de pantalla sin preguntar (ver
     * gesture_cb/view_registro_reset). Detectado por el usuario el
     * 09-sep-2026. */
    if (lv_scr_act() == s_ajustes_screen) {
        s_current = NAV_REGISTRO;
        lv_scr_load_anim(s_screens[NAV_REGISTRO], LV_SCR_LOAD_ANIM_MOVE_BOTTOM,
                         NAV_ANIM_MS, 0, false);
        return;
    }

    /* Ya estando en registros no se recarga la pantalla: recargarla haria una
     * animacion de "cambio de pagina" hacia la misma pagina, que se ve como un
     * parpadeo sin motivo. */
    if (s_current == NAV_REGISTRO) return;
    s_current = NAV_REGISTRO;
    lv_scr_load_anim(s_screens[NAV_REGISTRO], LV_SCR_LOAD_ANIM_MOVE_LEFT,
                     NAV_ANIM_MS, 0, false);
}

void nav_ir_a_sin_cerrar(void)
{
    nav_ir_a_registros();
    view_registro_abrir_sin_cerrar();
}

void nav_init(void)
{
    for (int i = 0; i < NAV_COUNT; i++) {
        s_screens[i] = lv_obj_create(NULL);
        lv_obj_add_event_cb(s_screens[i], gesture_cb, LV_EVENT_GESTURE, NULL);
    }

    view_inclinacion_create(s_screens[NAV_INCLINACION]);
    view_info_create(s_screens[NAV_INFO]);
    view_registro_create(s_screens[NAV_REGISTRO]);

    s_ajustes_screen = lv_obj_create(NULL);
    view_ajustes_create(s_ajustes_screen);

    lv_scr_load(s_screens[NAV_INFO]);
}

void nav_open_ajustes(void)
{
    view_ajustes_refresh();
    lv_scr_load_anim(s_ajustes_screen, LV_SCR_LOAD_ANIM_MOVE_TOP, NAV_ANIM_MS, 0, false);
}

void nav_close_ajustes(void)
{
    s_current = NAV_INFO;
    lv_scr_load_anim(s_screens[NAV_INFO], LV_SCR_LOAD_ANIM_MOVE_BOTTOM, NAV_ANIM_MS, 0, false);
}
