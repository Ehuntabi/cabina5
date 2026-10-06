# cabina5 — pantalla táctil de 5" para la autocaravana (Guition JC8048W550C)

Firmware para la placa **Guition JC8048W550C**: **ESP32-S3 con pantalla de 5",
800×480** y táctil capacitivo. Es una **adaptación a esta pantalla** dentro del
proyecto de la autocaravana: la pantalla P4 del salón y esta pantalla de cabina
se hablan por UDP.

> **La placa todavía no ha llegado** (pedida el 6-oct-2026). Lo que hay es el
> bring-up: el día que llegue, se graba, se comprueban pantalla, táctil y red, y
> a partir de ahí se monta encima la aplicación. **Qué se enseña en ella depende
> de lo que pida la cabina**: aquí no hay funcionalidades heredadas que respetar,
> se deciden sobre esta pantalla.

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

## Estado (6-oct-2026)

Compila y está listo para el primer arranque:

- Proyecto ESP-IDF para `esp32s3` (16 MB, PSRAM octal, dos huecos de OTA).
- `main/display.h`: pines y timing, **sin verificar en esta placa** (ver aviso).
- `main/esp_bsp.c` + `esp_bsp.h`: panel RGB, GT911, brillo por LEDC, bus I2C
  compartido y el contrato de BSP (arranque, cerrojo de LVGL, brillo).
- `main/main.c`: **pantalla de prueba** (no la aplicación): barra de colores,
  rejilla de 100 px, coordenadas del táctil en vivo, IP y contador de paquetes
  UDP de la P4 en el puerto 4242.

## Cómo se prueba (el día que llegue la placa)

```bash
. ~/.espressif/esp-idf-5.5/export.sh
cd ~/joint/cabina5
idf.py -p /dev/ttyACM0 flash monitor     # esta placa es la S3; la P4 está en ttyACM0/1
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
