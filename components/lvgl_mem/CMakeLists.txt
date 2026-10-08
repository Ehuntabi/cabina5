# Asignador de LVGL sobre PSRAM. Va en su PROPIO componente a proposito: el
# componente main se enlaza el ULTIMO, asi que si estas funciones vivieran alli,
# la libreria de LVGL (que las pide) se enlazaria antes de que existieran y el
# enlazador daria "undefined reference to lv_malloc_core" aunque el codigo si
# estuviera compilado. Con un componente propio se enlaza antes que LVGL.
idf_component_register(
    SRCS "lv_mem_psram.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
    WHOLE_ARCHIVE   # <-- mete el .o entero en el enlace SIEMPRE
)

# WHOLE_ARCHIVE es necesario, y esto costo un rato: LVGL pide lv_malloc_core()
# desde SU libreria, que se enlaza ANTES que la de este componente; el enlazador
# solo coge de una libreria los objetos que resuelven simbolos que ya le faltan,
# asi que sin esto se queda con "undefined reference to lv_malloc_core" aunque
# el objeto este compilado y la libreria este en la linea de enlace.
