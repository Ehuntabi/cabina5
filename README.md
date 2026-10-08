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

## Estado (8-oct-2026)

**Verificado en la placa**, con estos números medidos:

| | |
|---|---|
| Refresco de LVGL | **22 fotogramas/s** (el panel barre a **39,0 Hz**, estable) |
| Heap interno libre | **53 KB** (con WiFi arrancado; el margen se lo lleva la UI nueva) |
| Táctil | Funciona en toda la superficie, en **modo sondeo** (ver *Trampas*, apartado 5) |
| UI | Las 4 vistas y los 2 overlays, adaptados a los **800×480 reales** y con la letra un escalón más |
| Splash | La autocaravana (`main/icons/splash_logo_5.c`), sin texto |
| Red | UDP en el 4242 a la espera de la P4, WiFi conectando |
| Rearranques | **Ninguno** en marcha normal (los únicos resets son los que provoca abrir el puerto serie) |
| Flash | 2,2 MB de app: **46 % de la partición libre** |

- Proyecto ESP-IDF para `esp32s3` (16 MB, PSRAM octal, dos huecos de OTA).
- `main/display.h`: pines y timing **verificados en esta placa** (B IO8/3/46/9/1,
  G IO5/6/7/15/16/4, R IO45/48/47/21/14, HSYNC 39, VSYNC 41, DE 40, PCLK 42,
  táctil SCL 20 / SDA 19 / INT 38, retroiluminación 2).
- `main/esp_bsp.c` + `esp_bsp.h`: panel RGB, GT911, brillo por LEDC, bus I2C
  compartido y el contrato de BSP (arranque, cerrojo de LVGL, brillo). Incluye
  el **puente de panel IO** que necesita `esp_lvgl_port` con paneles RGB (los
  paneles RGB no tienen panel IO en IDF 5.5; el porqué está comentado en el
  propio fichero, no lo borres).
- `main/main.c`: **pantalla de prueba** (no la aplicación): barra de colores,
  rejilla de 100 px, coordenadas del táctil en vivo, IP y contador de paquetes
  UDP de la P4 en el puerto 4242.

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

Con el tema oscuro de esta interfaz, el nivel bajo de retroiluminación se ve
como una pantalla apagada. Pasó el 8-oct-2026: la placa arrancó con el brillo
guardado y el usuario lo describió como "la pantalla está negra".

Antes de tocar nada, dos comprobaciones que ahora están en el log de arranque:

1. `brillo: Brillo inicial N%` — si es bajo y la pantalla parece muerta, es esto.
2. `bsp: framebuffer: N de M muestras negras (X%)` — si el framebuffer **no**
   está negro, se está pintando y el problema es de luz (o del panel), no de la
   UI. Si está negro de verdad, entonces sí: ni LVGL ni la app están dibujando.

El brillo se cambia con el botón **"Brillo y contraste"** de la pantalla de
Ajustes (o con el doble toque/long press en la de datos), y se recuerda al
reiniciar. El nivel bajo es 60 %: menos que eso no se ve con este tema.

## Cómo se prueba en la placa

```bash
export IDF_TARGET=esp32s3
. ~/.espressif/esp-idf-5.5/export.sh
export PATH="$HOME/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin:$PATH"
cd ~/joint/cabina5
idf.py -p /dev/ttyUSB0 flash monitor    # esta placa es la S3 por CH340
```

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

- La negrita es **generada a mano** (`main/fonts/montserrat_bold.h`, con la
  receta): solo existen los tamaños que se usan.
- Los iconos son una fuente propia a 44 px (`main/icons/iconos.h`).
- LVGL **no trae Montserrat 56** (llega hasta la 48): ese escalón no existe y el
  titular de 48 se queda como está.

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
