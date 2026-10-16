# cabina5 — pantalla táctil de 5" para la autocaravana (Guition JC8048W550C)

Firmware para la placa **Guition JC8048W550C**: **ESP32-S3 con pantalla de 5",
800×480** y táctil capacitivo. Es una **adaptación a esta pantalla** dentro del
proyecto de la autocaravana: la pantalla P4 del salón y esta pantalla de cabina
se hablan por UDP.

> **Estado: bring-up TERMINADO y verificado en la placa (8-oct-2026).** La
> pantalla se ve bien, quieta, el táctil responde en toda la superficie y llegan
> los paquetes UDP de la P4. Lo que costó llegar aquí está contado abajo, en
> **Trampas de esta placa**: son cuatro cosas que no se ven venir y que ya nos
> costaron una tarde cada una. La aplicación del satélite se monta encima.

## Antes de grabar: la P4 va PRIMERO

La tarjeta del viaje lee cuatro bytes de la telemetría (km y horas de conducción).
En una P4 anterior a la **v4.44** esos cuatro bytes eran relleno y llegan a cero,
así que la tarjeta enseña `0,0 km / 0:00` en vez de `--`. **Graba la P4 primero** (o
las dos a la vez) y no verás ese número raro.

## La placa

| | |
|---|---|
| Modelo | Guition **JC8048W550C** (la "C" es táctil capacitiva) |
| Chip | **ESP32-S3**, 16 MB de flash + 8 MB de PSRAM **octal (OPI)** |
| Pantalla | **5" IPS 800×480**, RGB paralelo, controlador **ST7262** (sin comandos: solo timing) |
| Táctil | **GT911** por I2C (dirección 0x5D, sin reset conectado) |
| Extra | microSD, conector de cámara, altavoz, 2 USB (USB + UART1), GPIOs expuestos |

Dos consecuencias prácticas de que el panel sea RGB:

- El panel **lee su framebuffer continuamente**. Como ese framebuffer está en
  PSRAM, hay que darle de comer a tiempo o la imagen se desalinea (ver
  *Trampas*, apartado 3). El framebuffer (768 KB) va en PSRAM y los buffers de
  dibujo de LVGL en **RAM interna**, porque dibujar en PSRAM en esta placa da
  4 fotogramas por segundo (medido).
- No hay "tabla de comandos" del panel: si algo se ve mal (desplazado, colores
  cambiados), el problema es el **timing o los pines**, no la inicialización.

## Estado (16-oct-2026)

**Verificado en la placa**, con estos números medidos:

| | |
|---|---|
| Refresco de LVGL | **24 fotogramas/s** (el panel barre a **39,0 Hz**, estable) |
| Heap interno libre | **54 KB** (con WiFi arrancado) |
| Táctil | Funciona en toda la superficie, en **modo sondeo** (ver *Trampas*, apartado 5) |
| UI | **La aplicación entera** (las 4 vistas, los 2 overlays y los 9 formularios), no la pantalla de prueba del principio |
| Auto-test al arrancar | `CANAL DE VIAJES OK` y `CANAL DE ALARMAS OK` contra la P4 |
| Red | UDP 4242 (telemetría de la P4) y latido de vuelta al 4243 |
| Rearranques | **Ninguno** en marcha normal (los únicos resets son los que provoca abrir el puerto serie) |
| Flash | 2,2 MB de app: **47 % de la partición libre** |

Pantalla de Datos: la batería arriba a todo el ancho, **la tarjeta del viaje** en
medio (km recorridos a la izquierda, horas de conducción a la derecha) y aguas y
temperaturas abajo. Esa tarjeta es de esta pantalla y no de la 3,5": allí la de
Datos son dos filas y no cabe una tercera.

- Proyecto ESP-IDF para `esp32s3` (16 MB, PSRAM octal, dos huecos de OTA).
- `main/display.h`: pines y timing **verificados en esta placa** (B IO8/3/46/9/1,
  G IO5/6/7/15/16/4, R IO45/48/47/21/14, HSYNC 39, VSYNC 41, DE 40, PCLK 42,
  táctil SCL 20 / SDA 19 / INT 38, retroiluminación 2).
- `main/esp_bsp.c` + `esp_bsp.h`: panel RGB, GT911, brillo por LEDC, bus I2C
  compartido y el contrato de BSP (arranque, cerrojo de LVGL, brillo). Incluye
  el **puente de panel IO** que necesita `esp_lvgl_port` con paneles RGB (los
  paneles RGB no tienen panel IO en IDF 5.5; el porqué está comentado en el
  propio fichero, no lo borres).
- `main/main_app.c`: **la aplicación** (el `main.c` de la 3,5", portado): arranca
  la NVS, el modelo de datos, la UI del carrusel, el receptor UDP de la P4, la
  cola de apuntes y el auto-test del canal de subida.
- La **pantalla de prueba** del port (barra de colores, rejilla de 100 px,
  coordenadas del táctil en vivo) ya **no existe**: cuando la aplicación estuvo
  portada y probada se borro (`main.c` en el commit f406457). Lo que queda para
  diagnosticar sin arrancar la app es el **modo captura** de
  `main/capture_carousel.c` (datos de ejemplo y volcado de la pantalla por el
  puerto serie) y el volcado de geometría (`view_info_diag_arbol`).

## Trampas de esta placa (las seis nos costaron una tarde cada una)

Están en orden de aparición. Si algo va mal en el arranque, en la imagen, en el brillo o en el
táctil, casi seguro es una de estas seis.

### 1. No arranca: bucle de reset silencioso → falta QIO

Esta placa lleva un **ESP32-S3 rev v0.2 con PSRAM octal AP_3v3 de 8 MB**, y esa
combinación **no arranca en modo de flash DIO**, que es el que trae ESP-IDF por
defecto. El síntoma engaña mucho, porque la PSRAM *sí* se inicializa:

```
I (210) esp_psram: Found 8MB PSRAM device      <- la PSRAM va bien
I (571) esp_psram: Adding pool of 8192K of PSRAM memory to heap allocator
I (647) cpu_start: Multicore app
rst:0xc (RTC_SW_CPU_RST)                       <- reset silencioso, en bucle
```

No hay panic ni `Guru Meditation`: la placa se reinicia cada ~0,7 s sin decir
por qué. **No es culpa del programa** (una app vacía se reinicia igual) ni del
hardware. Es la lectura de flash por la MSPI compartida, que se corrompe en
cuanto se toca la PSRAM. Está reportado y explicado en
[espressif/esp-idf#18806](https://github.com/espressif/esp-idf/issues/18806);
lo que lo arregla es el **modo** de flash, no la velocidad (la velocidad ya se
probó a 40 y 80 MHz en el reporte, sin efecto):

```ini
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
```

El `sdkconfig` generado **sigue diciendo `CONFIG_ESPTOOLPY_FLASHMODE="dio"`**
aunque QIO esté puesto, y es correcto: esptool graba siempre el bootloader en
DIO y es el bootloader de 2ª etapa el que activa QIO él mismo. En el arranque
bueno se ve `qio_mode: Enabling default flash chip QIO` y luego
`SPI Mode : QIO`. Si esa línea no sale, QIO no se aplicó.

### 2. Parece muerta y está arrancando → la consola va por UART, no por USB

La placa se conecta por un **CH340** (`/dev/ttyUSB0`, GPIO43/44), no por el USB
nativo del chip. Con `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` (lo que traía el
proyecto) el log se va a un USB que no está conectado al PC y **la placa parece
muerta cuando en realidad está arrancando bien**. Por eso el proyecto usa
`CONFIG_ESP_CONSOLE_UART_DEFAULT=y`.

### 3. La imagen se corre sola hacia un lado (*screen drift*) → bounce buffer

Este es el peor de los cuatro, y el más fácil de diagnosticar mal. La imagen
entera se va desplazando sola, hacia un lado, como si hiciera *scroll*.

Lo que **no** es (todo comprobado en esta placa):

- No es efecto de escribir en el framebuffer mientras el panel lo lee: el fallo
  sigue igual con un patrón escrito **una sola vez** y el refresco de LVGL
  congelado.
- No es el reloj ni la PSRAM: el panel barre a **39,0 Hz clavados**, ventana a
  ventana.
- No es el modo de refresco: pasó igual con buffers parciales en RAM interna, en
  PSRAM, con framebuffer doble, sincronizado al retrazo y con `avoid_tearing`.

La causa la documenta Espressif como
[**screen drift**](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/advanced-development/lcd-application-note/rgb-summary.html):
el **GDMA no llega a tiempo a servir píxeles desde PSRAM**, el FIFO del LCD se
queda vacío (*under-run*) y su puntero se desalinea; a partir de ahí el
controlador lee de la dirección equivocada y muestra las líneas siguientes como
si fueran las primeras: la imagen se desplaza línea a línea.

El arreglo es un **bounce buffer** de al menos 20 líneas en RAM interna: el DMA
lee siempre de ahí (memoria rápida, nunca se queda seco) y la ISR lo rellena
desde PSRAM. En este proyecto está en `esp_bsp.c`
(`.bounce_buffer_size_px = 20 * LCD_H_RES`, `.num_fbs = 1`), y además está
abierto XIP desde PSRAM, que es la otra recomendación del mismo documento.

**Con `num_fbs = 0` la pantalla se queda NEGRA**: el driver solo avanza de
framebuffer cuando el buffer de dibujo está dentro de uno, así que con cero
framebuffers no avanza nunca. Está comentado en el código para no repetirlo.

### 4. El táctil no registra la interrupción → IO38 es INT, no RST

En esta placa el único pin de control del táctil (IO38) es la **interrupción**,
no el reset (la definición oficial de la placa dice `RESET_PIN = -1`,
`INTERRUPT_PIN = 38`). Sin reset, el GT911 se queda en su dirección por defecto
(0x5D), que es justo la que responde. Con el reset mal puesto el driver hace la
secuencia de selección de dirección, el táctil queda a medias y el port de LVGL
avisa con `Error in register touch interrupt`.

### 5. El táctil no responde aunque el chip funcione → el port lo lee por EVENTO

Esta es la que nos tuvo más tiempo, porque **todo parecía estar bien**: el GT911
contestaba por I2C, el driver leía coordenadas correctas, y el log decía
`Tactil: temporizador de lectura EN MARCHA`. Y aun así, los toques no hacían
nada.

La causa: `esp_lvgl_port` deja el dispositivo de entrada en
**`LV_INDEV_MODE_EVENT`**, o sea que **no se lee por temporizador**: se lee
cuando llega el evento `LVGL_PORT_EVENT_TOUCH`, y ese evento sale **solo de la
interrupción** del GT911 (IO38). En esta placa ese pin no hace de interrupción
—medido con un contador en el callback: **0 interrupciones en 30 s**— así que
LVGL leía el chip **una vez en el arranque y nunca más**.

El arreglo está en `esp_bsp.c`: `lv_indev_set_mode(s_indev, LV_INDEV_MODE_TIMER)`,
o sea **sondeo cada 30 ms** pase lo que pase con la interrupción. Medido después:
32 lecturas/s.

Para no volver a tropezar, el arranque **se comprueba solo** y lo dice en el log:

```
Tactil comprobado: 30 lecturas, 0 toques | temporizador en marcha
```

Si esa línea no sale (o sale el `E ... TACTIL: solo N lecturas`), el táctil no
está leyéndose y no hay que buscar el problema en el chip.

Y de propina, dos avisos sobre los **colores** y los **pines**:

- Los canales **R y B no se pueden deducir del xlsx** de Guition: los dos grupos
  llevan los mismos números de pin (`IO8/IO3/IO46/IO9/IO1` e
  `IO45/IO48/IO47/IO21/IO14`) y solo cambia la etiqueta. La asignación buena es
  la de `display.h`, comprobada pidiendo **azul puro**: con los grupos cruzados
  el azul sale rojo y el blanco sale amarillo.
- Si se toca el timing, `hsync_idle_low` y `vsync_idle_low` van en **false**,
  como en la definición oficial de la placa.

### 6. "La pantalla está negra" → mirar el brillo antes que nada

Con el tema oscuro de esta interfaz, un nivel bajo de retroiluminación se ve
como una pantalla apagada. Pasó el 8-oct-2026 (nivel bajo a 30 %) y otra vez el
9-oct-2026, esta vez por un cambio mío: al probar cinco pasos de brillo, el
arranque quedó en el 60 % y la pantalla se reportó como "está en negro".

Aquel día se midió de una vez para siempre, con la prueba de barrido de
`capture_carousel.c` (`PRUEBA_BRILLO`): pantalla negra, el valor en grande, y a
bajar el duty paso a paso. Respuesta del usuario: **"4000 por debajo de 80 ya muy
oscuro"**. O sea que el **rango útil de esta retroiluminación es 80–100 % y no hay
más**: el 50 se ve negro, el 30 negro del todo. Los dos niveles definitivos son
**100 % y 85 %**.

Antes de tocar nada, dos comprobaciones que están en el log de arranque:

1. `brillo: Brillo inicial N%` — si es bajo y la pantalla parece muerta, es esto.
2. `bsp: framebuffer: N de M muestras negras (X%)` — si el framebuffer **no**
   está negro, se está pintando y el problema es de luz (o del panel), no de la
   UI. Si está negro de verdad, entonces sí: ni LVGL ni la app están dibujando.
   Esta lectura sale del framebuffer **del propio panel**, así que vale como
   prueba: el 9-oct decía "32 % de muestras negras, brillo medio 29,7" mientras
   el usuario veía negro → era luz, no UI.

El brillo se cambia con el botón **"Brillo: N%"** de la pantalla de Ajustes (o
con el doble toque/long press en la de datos). **Cambia solo la luz**: el modo
contraste (blanco y negro) iba pegado a él y se quitó el 9-oct, porque hacía que
los dos niveles pareciesen dos pantallas distintas en vez del mismo cuadro más
claro o más oscuro. La pantalla de datos es **siempre en color**. El modo
contraste sigue en `view_info.c` por si algún día se quiere un "modo sol" con su
propio botón.
**Al encender siempre arranca al 100 %**, aunque lo último usado fuera el 85 %:
así una pantalla a oscuras nunca se queda grabada y siempre se sale
desenchufando. El cambio de nivel se aplica con una rampa de 8 tramos de 15 ms
(antes saltaba de golpe, y eso era el "no funciona bien").

## Credenciales: hay que copiar la plantilla antes de compilar

El repositorio es **público**, así que las claves de la P4 **no** se versionan.
Lo que sí está en el repositorio es la plantilla
`main/wifi_credentials.h.example`. Antes de compilar hay que copiarla y rellenar
los valores:

```bash
cp main/wifi_credentials.h.example main/wifi_credentials.h
$EDITOR main/wifi_credentials.h
```

`main/wifi_credentials.h` está en el `.gitignore`, y **el build falla a
propósito** si no existe: no se debe poder compilar y grabar con una contraseña
de broma. La única excepción es el CI, que copia la plantilla solo para que el
compilador tenga algo que leer (ese CI no publica binarios).

⚠ **Si añades un dato de configuración nuevo, añádelo también a la plantilla.**
El 9-oct-2026 el CI llevaba desde el 8 sin compilar por esto: `p4_creds.c` usa
`PORTAL_CRED_USER` y `PORTAL_CRED_PASS`, que estaban en el fichero de verdad
pero **no en la plantilla**, así que en GitHub salía
`error: 'PORTAL_CRED_USER' undeclared`. En el PC de casa no se veía porque allí
el fichero está completo.

## Cómo se prueba en la placa

```bash
export IDF_TARGET=esp32s3
. ~/.espressif/esp-idf-5.5/export.sh
export PATH="$HOME/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin:$PATH"
cd ~/joint/cabina5
# LO COMODO: graba y ADEMAS comprueba que esta version este publicada
scripts/grabar.sh                 # (usa /dev/ttyUSB0 y graba las DOS mitades de la OTA)

# a mano, sin comprobaciones:
idf.py -p /dev/ttyUSB0 flash monitor    # esta placa es la S3 por CH340
```

`grabar.sh` comprueba ademas (16-oct-2026) que la Release tenga el asset EXACTO de
esta version (`cabina5-<tag>.bin`, con su sha256), que en el puerto haya un
**ESP32-S3** (la P4 es un ESP32-P4 y tambien es `/dev/ttyACM*`: con los cables
cambiados se niega a grabar) y que el `.bin` de aqui sea el publicado -- si no lo
es byte a byte, graba EL PUBLICADO. Con `--seco` hace todas las comprobaciones sin
escribir nada en la placa.

Esta placa tiene **dos huecos de OTA**: si solo se graba el de arranque, tras un
OTA la placa arranca el otro y parece que "no grabó nada". Para dejarla
coherente se graban las dos:

```bash
idf.py -p /dev/ttyUSB0 flash
python -m esptool --chip esp32s3 -p /dev/ttyUSB0 -b 460800 write_flash 0x410000 build/cabina5.bin
```


Qué hay que mirar, en este orden:

1. **Barra de colores**: si sale todo blanco → pines de datos mal; si el rojo y
   el azul salen cambiados → R y B cruzados en `display.h`; si la imagen está
   desplazada o "bailando" → timing (`pclk_hz`, porches).
2. **Rejilla de 100 px**: confirma que 800×480 son de verdad 800×480 y que no
   hay recorte en los bordes.
3. **Táctil**: al tocar una esquina, las coordenadas tienen que parecerse a esa
   esquina. Si están cambiadas de eje o invertidas, se corrige con
   `swap_xy`/`mirror_x`/`mirror_y` en `esp_bsp.c`.
4. **UDP**: con la P4 encendida, el contador "P4: N paquetes" tiene que subir. Si
   sube, la red funciona. Si no: mirar el log (`Wi-Fi: IP ...`).

## Pantalla y textos

Esta pantalla tiene **181 ppp** (5" a 800×480) y se mira desde el asiento del
conductor, así que los tamaños de letra habituales se quedan pequeños: el juego
de fuentes va **dos escalones por encima** de lo normal y se declara **por papel**
en un solo fichero, `main/ui/estilos.h` — no por número repartido por las vistas.

Ahí están también las **medidas de la pantalla** (`UI_ANCHO`, `UI_ALTO`,
`UI_ANCHO_COLUMNA`) y el ajuste automático del rótulo de los botones: en una
pantalla apaisada de 800 px, un rótulo largo se sale de su botón, así que se
mide con las métricas reales de la fuente y se le baja el escalón que haga
falta. Las vistas siguen pidiendo `lv_font_montserrat_20` y lo que sale es el 26:
cambiar la escala entera es tocar **un fichero**.

- La negrita es **generada a mano** con `lv_font_conv` (ficheros
  `main/fonts/lv_font_inter_bold_*_es.c`; la receta va en la cabecera de cada uno
  y los alias, con el papel de cada tamaño, en `main/fonts/fonts_es.h`). Hasta el
  16-oct-2026 esto apuntaba a `main/fonts/montserrat_bold.h`, que ya no existe:
  ahora toda la familia es la misma Inter que la de la P4.
- Los iconos son una fuente propia a 44 px (`main/icons/iconos.h`).
- Las fuentes son **Inter generadas para este proyecto** (no las del SDK), así
  que traen los acentos del castellano: la UI los usa y se pintan bien. Lo que
  no traen son símbolos de moneda (€, £), y por eso la lista de monedas va con
  códigos.

## La pantalla se puede montar girada 180 grados

`main/display.h` tiene `PANTALLA_GIRADA_180`, **hoy a 0**: el soporte pedía
montarla al revés y la imagen iba girada media vuelta, pero el dueño rehizo el
soporte para que quede sin girar, así que se volvió a la orientación de siempre.
Ponerlo a 1 la gira otra vez (el código del giro se queda, y está probado).
Se hace con la rotación de LVGL, que en la versión 9 gira **las dos cosas**: lo que
se dibuja y las **coordenadas del táctil** (`lv_indev.c` mira la rotación del
display), así que no hay que tocar el GT911.

**OJO si algún día se toca el flush** (16-oct-2026): `lv_display_set_rotation()`
solo *apunta* la rotación; los **píxeles** los gira quien hace el flush. Los ports
de Espressif lo hacen ellos, pero esta placa tiene su propio flush, así que el giro
está en `bsp_lvgl_flush_cb()` (`lv_display_rotate_area()` + un giro 180 en el
sitio, sin memoria extra). Sin eso, la rotación se queda en el apunte y en la
pantalla no cambia nada — que es exactamente lo que pasó al probarlo.

180 y no 90/270 a propósito: al girar media vuelta el ancho y el alto no cambian,
así que los buffers valen los mismos.

Rendimiento medido en la placa: **39 Hz de barrido** (el panel) y el contador de
«refrescos LVGL» a ~24/s, que **no son fotogramas**: ese contador suma llamadas al
flush, y con trozos de 60 líneas una pantalla completa son 8 llamadas, así que
vienen a ser unas 3 pantallas por segundo. Se corrigió el 16-oct-2026 (un
arbitraje lo midió: los trozos son de 60 líneas y los buffers de 96 KB en PSRAM,
no de 40 y 64 KB en RAM interna, porque en LVGL 9 `lv_color_t` son 3 bytes).

## Compilar en este PC (trampa del toolchain)

Este PC compila la P4 (RISC-V) a diario, y su instalación de IDF está pensada
para eso: **`export.sh` no mete el compilador Xtensa en el PATH**. El build falla
con `The CMAKE_CXX_COMPILER: xtensa-esp32s3-elf-g++ ... was not found in the
PATH`. Lo que funciona (detalle en `CLAUDE.md`):

```bash
export IDF_TARGET=esp32s3
. ~/.espressif/esp-idf-5.5/export.sh
export PATH="$HOME/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin:$PATH"
idf.py build
```

## Publicar una versión

```bash
./release.sh 0.1 "Primer arranque en la placa: pantalla, tactil y UDP"
```

Compila, verifica que la versión embebida coincide con el tag, y publica la
Release en GitHub con el binario.
