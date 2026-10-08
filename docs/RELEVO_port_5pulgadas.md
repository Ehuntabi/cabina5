# Estado del port a la placa de 5" (relevo, 8-oct-2026)

Documento de traspaso: qué está hecho, qué está medido y **qué queda**.
Resumen de una línea: **la UI ya está adaptada a 800×480, el táctil funciona
(era el modo de lectura del port) y el splash es la autocaravana otra vez.**

## Lo que YA funciona (verificado en la placa)

| | |
|---|---|
| Arranque | Sin QIO no arranca (issue #18806). Con `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y` arranca siempre |
| Consola | Por UART (CH340, `/dev/ttyUSB0`), no por USB: `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` |
| Pantalla | 800×480, sin *screen drift* gracias al **bounce buffer** (20 líneas, RAM interna) + `num_fbs=1` |
| Refresco | **22 fotogramas/s**, panel barriendo estable a 39 Hz |
| Táctil | Funciona en toda la superficie, en **modo sondeo** (ver abajo). Al arrancar se comprueba solo: `Tactil comprobado: 30 lecturas, 0 toques \| temporizador en marcha` |
| Red | WiFi asociado a la P4, IP 192.168.4.2, UDP :4242 recibiendo, reloj de la P4, viajes |
| Memoria | **53 KB internos libres** + 7,2 MB de PSRAM. LVGL usa PSRAM con el asignador propio (`components/lvgl_mem/`) |
| UI | Las 4 vistas y los 2 overlays adaptados a 800×480, con la letra un escalón más |
| Splash | La autocaravana (`main/icons/splash_logo_5.c`, 665×394) con la versión debajo |
| Flash | 2,2 MB de app: **46 % de la partición libre** |

## El táctil: qué pasaba de verdad (8-oct-2026)

Costó una tarde y no era ni el chip, ni el cableado, ni las coordenadas:

- El port de Espressif deja el dispositivo de entrada en **modo EVENT**
  (`lvgl_port_add_touch` → `lv_indev_set_mode(LV_INDEV_MODE_EVENT)`), o sea que
  **no se lee por temporizador**: se lee cuando llega el evento
  `LVGL_PORT_EVENT_TOUCH`, y ese evento sale **solo de la interrupción** del
  GT911 (IO38).
- En esta placa ese pin **no hace de interrupción**: medido con un contador en
  el callback, **0 interrupciones en 30 s** (el pin se queda a nivel alto).
- Resultado: LVGL leía el chip **1 vez en el arranque y ninguna más**, con el
  temporizador en PAUSADO. El chip funcionaba perfectamente por I2C — se veía
  contando toques a mano — pero nadie le preguntaba.

**El arreglo** (`esp_bsp.c`): `lv_indev_set_mode(s_indev, LV_INDEV_MODE_TIMER)`.
LVGL lee el chip cada 30 ms pase lo que pase con la interrupción. Medido
después: **32 lecturas/s** y los toques llegan.

Para no volver a tropezar, el BSP deja **una comprobación en el arranque**
(cuenta lecturas y toques, y avisa si en 5 s no ha leído nada). No se quita:
los dos fallos posibles —"el chip no detecta" y "LVGL no lee"— ya han costado
una tarde cada uno y solo se distinguen con esas dos cifras.

## La UI a 800×480 (lo que se hizo)

La UI venía de la pantalla de 3,5", que dibujaba en **480×320 lógicos**: en una
pantalla de 800×480 ocupaba los dos tercios de la izquierda, y de ahí que
"saliera estrecha y descolgada".

- **`main/ui/estilos.h`** es el único sitio con las medidas y las fuentes:
  `UI_ANCHO`, `UI_ALTO`, `UI_ANCHO_COLUMNA` y la escala de tamaños. Se subió
  **otro escalón entero** (14→20, 16→22, 20→26, 22→30, 24→34, 32→40, 40→48).
  **La 56 no existe en LVGL** (su fuente más grande es la 48), así que el
  titular de 48 se queda en 48: si algún día hace falta, hay que generar la
  fuente a mano.
- **Negritas**: se generaron las que faltaban (**26, 34 y 40**) con
  `lv_font_conv`; la receta exacta está en `main/fonts/montserrat_bold.h`.
- **`view_info`**: rejilla a pantalla completa (filas de 210 + 246 px), dibujo
  de la batería y LEDs de aguas más grandes, temperaturas repartidas en el alto
  nuevo y el icono del GPS dentro de la tarjeta (antes caía sobre el borde).
- **`view_inclinacion`**: dial de 240 a **320 px** de diámetro y las lecturas
  dejan de ser un rótulo de dos líneas: dos filas con etiqueta y cifra grande.
- **`view_registro`**: menús de **3 columnas** (253×210 por casilla), iconos a
  **44 px** (`iconos_44.c`), cabecera **fija** con el contenido deslizándose por
  debajo (Mantenimiento mide ~600 px: si se deslizara todo, el "Volver" se iría
  de la pantalla) y los formularios en una columna centrada de 560 px.
- **Rotulos que se ajustan solos**: con la letra nueva había textos que se
  salían del botón — medido: "Terminar salida" en letra 40 mide 350 px y su
  botón tiene 260. En vez de corregir botón a botón, el rótulo se mide al
  pintar (`LV_EVENT_DRAW_MAIN`) y se baja de escalón lo justo
  (`rotulo_autoajustable`, en `view_registro.c` y `confirm_screen.c`).
- **Splash**: era un rótulo de texto en esta versión; se recuperó la
  **autocaravana** del splash del proyecto de 3,5" (que solo existía como
  volcado de píxeles) y se rehízo a 665×394.

## Lo que QUEDA

1. **Probar a fondo en la placa**: los formularios de Registro (son muchos y
   algunos ocupan más de una pantalla), los avisos y el diálogo de confirmación.
   Cualquier texto que se salga de su caja se ve a la primera.
2. **La captura de pantallas del carrusel** (`capture_carousel.c`) sigue
   pensada para la pantalla vieja: revisar que el recorrido de pantallas y
   formularios siga siendo el que se quiere documentar.
3. Decidir si el **informe de refrescos/s** del BSP se queda (hoy sale cada 2 s;
   es barato y sirve para detectar que el panel se atasca, pero es ruido).
4. Publicar la primera versión con `./release.sh`.

## Si hay que rehacer el splash (receta)

El `.c` de la imagen no se toca a mano. Se regenera así: del
`splash_logo_3_5.c` del proyecto de 3,5" se sacan los píxeles (son RGB565 en
big endian, 480×320), se pasan a PPM, se recorta el marco negro
(`convert -fuzz 12% -trim`), se reescala a la medida que se quiera y se vuelca
a C como **RGB565 little endian** con el descriptor `lv_image_dsc_t`. Sin canal
alfa: el fondo de la foto ya es negro y el splash también.

## Trampas ya documentadas (no volver a tropezar)

En `README.md`, sección **Trampas de esta placa**: QIO, consola por UART,
*screen drift* (bounce buffer) y el pin de interrupción del táctil (IO38 es INT,
no RST). Y en el código, comentado en el sitio donde importa:

## Dos fallos que aparecieron al probar en la placa (y cómo quedaron)

### 1. La placa se quedaba colgada al tocar la pastilla "1 sin cerrar"

**Fue culpa de un cambio de esta misma sesión**, y conviene saber por qué, porque
el patrón se puede repetir: el ajuste automático del rótulo de los botones se
enganchó al evento `LV_EVENT_DRAW_MAIN`, o sea **dentro del pintado**. Cambiar
la fuente de un objeto lo invalida, la invalidación lo vuelve a pintar, y el
pintado volvía a entrar en el callback: **recursión infinita** hasta que la
tarea de LVGL se quedaba sin pila. Lo que se veía era "la pantalla deja de
responder" y la placa **no se reiniciaba**: el Task WDT saltaba cada pocos
segundos (`task_wdt: Task watchdog got triggered`) y seguía funcionando la red.

Diagnóstico: la traza del watchdog traía la misma pareja de direcciones
repetida (`lv_obj_redraw` ↔ `rotulo_ajustar_cb`) decodificada con
`xtensa-esp32s3-elf-addr2line`. El volcado está en
`capturas/log_cuelgue_ANTES_del_arreglo.txt`.

El arreglo: el ajuste vive ahora en `estilos.h` (`rotulo_autoajustable`) y corre
en `LV_EVENT_SIZE_CHANGED` / `LV_EVENT_STYLE_CHANGED` — fuera del pintado — con
un cerrojo de reentrada. Se hace en **un solo sitio** (antes había una copia en
`view_registro.c` y otra en `confirm_screen.c`, las dos mal).

### 2. "Terminar salida" no cabía en su botón

Medido con las métricas reales de las fuentes: en letra 40 ese rótulo mide
350 px y el botón mide 260. En vez de ir corrigiendo botón a botón, el rótulo se
mide y se le baja el escalón que haga falta (`rotulo_autoajustable`). De paso se
subieron las alturas fijas que se calcularon para la letra vieja (campos a 50,
cabecera a 64, botón chico a 46→64) y los formularios largos ahora se deslizan
**por debajo de una cabecera fija** (si se deslizara todo, el "Volver" se iría
de la pantalla).

## El modo captura de pantallas: estado y trampas

Sirve para revisar la UI sin estar delante de la placa, pero **hoy no es
fiable**, y las dos razones están medidas:

1. **`lv_snapshot_take()` devuelve ARGB8888, no RGB565**: para 800×480 son
   **1.152.000 bytes** (3 por píxel), no 768.000. El volcado lo declara en su
   cabecera, así que no hay que adivinar: el tamaño manda.
2. **A 115200 baudios se pierden bytes**: 1,15 MB tardan ~100 s en salir y el
   CH340 no los entrega todos (las imágenes salen con rayas horizontales). Por
   eso el volcado pasó a ser **binario con longitud declarada**
   (`===BIN:<nombre>:<W>x<H>:<bytes>===` … `===FIN===`), que al menos detecta el
   corte en vez de romper un base64 entero, y cede la CPU cada 2 KB para que no
   salte el watchdog a mitad.

Lo que falta para que sea útil: **entregar los bytes de otra forma** (el USB
nativo de la placa, o trocear la captura por zonas en vez de la pantalla
entera). Y OJO: en este banco el `/dev/ttyACM*` que aparece es **la P4**, no
esta placa — la cabina solo tiene el CH340.

Para volver a intentarlo: `CAPTURE_CAROUSEL_ENABLE` a 1 en
`main/capture_carousel.h`, grabar, y leer el puerto con
`tools/decodifica_capturas.py` (que ya entiende el formato binario y avisa de lo
que falte). Acuérdate de volver a ponerlo a 0: con el flag a 1 la pantalla
muestra datos falsos.
