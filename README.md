# cabina5 — pantalla táctil de 5" para la autocaravana (Guition JC8048W550C)

Firmware para la placa **Guition JC8048W550C**: **ESP32-S3 con pantalla de 5",
800×480** y táctil capacitivo. Es una **adaptación a esta pantalla** dentro del
proyecto de la autocaravana: la pantalla P4 del salón y esta pantalla de cabina
se hablan por UDP.

> **Estado: bring-up, sin verificar en hardware.** Está montado y compila el
> arranque de la placa (pantalla, táctil, brillo y red), con una pantalla de
> prueba para comprobarlo. Los pines y el timing salen de la documentación de
> esta familia de placas y **están sin confirmar en la placa**: el primer
> arranque es una prueba, no una instalación. La aplicación se monta encima
> cuando el arranque esté verificado, y **qué se enseña en ella depende de lo
> que pida la cabina**: aquí no hay funcionalidades heredadas que respetar.

## La placa

| | |
|---|---|
| Modelo | Guition **JC8048W550C** (la "C" es táctil capacitiva) |
| Chip | **ESP32-S3**, 16 MB de flash + 8 MB de PSRAM **octal (OPI)** |
| Pantalla | **5" IPS 800×480**, RGB paralelo, controlador **ST7262** (sin comandos: solo timing) |
| Táctil | **GT911** por I2C |
| Extra | microSD, conector de cámara, altavoz, 2 USB (USB + UART1), GPIOs expuestos |

Dos consecuencias prácticas de que el panel sea RGB:

- El panel **lee su framebuffer de la PSRAM continuamente**. Cualquier saturación
  de la PSRAM se ve como parpadeo, así que el framebuffer (768 KB) va en PSRAM y
  los buffers de dibujo de LVGL en **RAM interna**.
- No hay "tabla de comandos" del panel: si algo se ve mal (desplazado, colores
  cambiados), el problema es el **timing o los pines**, no la inicialización.

## Estado (8-oct-2026)

**La placa ya arranca y se ve la prueba**: panel RGB 800×480, táctil GT911 en
0x5D y UDP en el 4242, sin un solo panic. Ver `## Arranque: la placa NO arranca
sin QIO` antes de tocar el `sdkconfig`.

- Proyecto ESP-IDF para `esp32s3` (16 MB, PSRAM octal, dos huecos de OTA).
- `main/display.h`: pines y timing **ya verificados en esta placa** (el mapa
  coincide con el xlsx oficial de Guition: B IO8/3/46/9/1, G IO5/6/7/15/16/4,
  R IO45/48/47/21/14, HSYNC 39, VSYNC 41, DE 40, PCLK 42, táctil SCL 20 /
  SDA 19 / RST 38, retroiluminación 2).
- `main/esp_bsp.c` + `esp_bsp.h`: panel RGB, GT911, brillo por LEDC, bus I2C
  compartido y el contrato de BSP (arranque, cerrojo de LVGL, brillo). Incluye
  el **puente de panel IO** que necesita `esp_lvgl_port` con paneles RGB (los
  paneles RGB no tienen panel IO en IDF 5.5; el porqué está comentado en el
  propio fichero, no lo borres).
- `main/main.c`: **pantalla de prueba** (no la aplicación): barra de colores,
  rejilla de 100 px, coordenadas del táctil en vivo, IP y contador de paquetes
  UDP de la P4 en el puerto 4242.

## Arranque: la placa NO arranca sin QIO (leer antes de tocar nada)

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

Dos detalles que confunden y por eso están aquí escritos:

- El `sdkconfig` generado **sigue diciendo `CONFIG_ESPTOOLPY_FLASHMODE="dio"`**
  aunque QIO esté puesto. Es correcto: esptool graba siempre el bootloader en
  DIO y es el bootloader de 2ª etapa el que activa QIO él mismo. En el arranque
  bueno se ve `qio_mode: Enabling default flash chip QIO` y luego
  `SPI Mode : QIO`. Si esa línea no sale, QIO no se aplicó.
- **La consola de esta placa va por UART, no por USB.** La placa se conecta por
  un **CH340** (`/dev/ttyUSB0`, GPIO43/44), no por el USB nativo del chip. Con
  `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` (lo que traía el proyecto) el log de
  arranque se va a un USB que no está conectado al PC y **parece que la placa
  está muerta cuando en realidad está arrancando bien**. Por eso el proyecto
  usa `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`.

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

Esta pantalla tiene **181 ppp** (5" a 800×480), así que los tamaños de letra
habituales se quedan pequeños: el juego de fuentes va un escalón por encima
(16/20/24/28/32/40/48) y conviene declararlas **por papel** en un solo fichero de
estilo, no por número repartido por las vistas. Los iconos y los huecos suben en
la misma proporción.

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
