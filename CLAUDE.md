# cabina5 — notas de trabajo (para el siguiente que abra esto, incluido yo)

Firmware de la pantalla de **5" Guition JC8048W550C** (ESP32-S3, 800×480 RGB,
táctil GT911) para la autocaravana, dentro del proyecto de la P4 del salón.
**Es una adaptación a esta pantalla**, no una copia de otra interfaz: qué se
enseña en ella se decide sobre esta pantalla. El README cuenta el qué; aquí va el
cómo y las trampas.

## Compilar (¡ojo con el target y con el PATH!)

Este PC compila la P4 (RISC-V) a diario, y su instalación de IDF está pensada
para eso: **`export.sh` no mete el toolchain Xtensa en el PATH** (el
`idf_tools.py export` de esta máquina solo saca el RISC-V, aunque el Xtensa esté
instalado y `idf_tools.py list` lo dé por bueno). El build falla entonces con:

```
The CMAKE_CXX_COMPILER: xtensa-esp32s3-elf-g++ is not a full path and was not found in the PATH
```

Lo que funciona (comprobado el 6-oct-2026):

```bash
export IDF_TARGET=esp32s3
. ~/.espressif/esp-idf-5.5/export.sh
export PATH="$HOME/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin:$PATH"
cd ~/joint/cabina5 && idf.py build
```

Si algún día se actualiza el IDF o el toolchain, esa ruta lleva la versión dentro
(`esp-14.2.0_20260121`): mirar `ls ~/.espressif/tools/xtensa-esp-elf/` antes de
copiarla.

**Estado del build (6-oct-2026)**: compila entero y deja `cabina5.bin` de 1,1 MB
(73 % de la partición libre, de sobra para las fuentes grandes que pide la
pantalla). Sin placa todavía: compilado, no probado.

## Versiones de componentes fijadas

`main/idf_component.yml` clava **LVGL ~8.4.0 + esp_lvgl_port ~1.4.0** y
**esp_lcd_touch_gt911 ~1.0.4**. Dos trampas que costaron un build cada una:

- `esp_lcd_touch_gt911` **1.0.4 no tiene `esp_lcd_touch_io_gt911_config_t`** (el
  campo `driver_data` es de la 2.x): la config va sin él. La dirección del GT911
  de esta familia ya viene en el macro del componente (`0x5D`).
- `esp_lvgl_port` **no tiene versiones 1.5.x** (salta de 1.4.0 a 2.0.0), así que
  pedir `~1.5.0` rompe la resolución de dependencias.

## Red de seguridad del bring-up

`main/main.c` NO es la aplicación: es una pantalla de prueba a propósito. Enseña
barra de colores, rejilla de 100 px, coordenadas del táctil, IP y contador de
paquetes UDP de la P4. El orden de comprobación (y qué significa cada fallo) está
en el README. La razón de tenerla: los pines de `display.h` son de una **placa
hermana** (la 8048S050C, misma familia y misma pantalla), no de la nuestra, así
que el primer arranque es una prueba, no una instalación.

## Trampas que ya conocemos de esta familia de placas

- El pin de **interrupción del GT911 va a GND** por una resistencia en la placa:
  hay que usar la dirección **0x5D** y sondear por I2C (`TOUCH_PIN_INT` a NC).
  Con la 0x14 no responde.
- El panel RGB **no tiene comandos**: si algo se ve mal, es timing o pines, no
  una tabla de inicialización.
- El framebuffer va en PSRAM (768 KB) y el panel la lee constantemente: los
  buffers de dibujo de LVGL van en **RAM interna** y cualquier copia gorda desde
  PSRAM se nota en pantalla.
- Estos módulos se venden con 16 MB/8 MB (N16R8) y con menos memoria. Si el
  panel no arranca o el arranque se queja de particiones, mirar la serigrafía
  del módulo antes de tocar nada.
- Después de una OTA, el arranque cambia de partición (`ota_0` ↔ `ota_1`): un
  `write_flash` a 0x10000 puede quedarse sin arrancar (la placa sigue con el
  firmware viejo y parece que el cambio "no se ve"). Se graban las dos, o se
  graba `ota_data_initial.bin` en 0xd000.

## Cuando el hardware esté probado

1. **Decidir qué se enseña** en esta pantalla (es lo que manda el orden del
   trabajo): la aplicación se monta encima de este BSP, con red UDP contra la P4,
   modelo de datos y las vistas que pida la cabina.
2. La UI, a 800×480 y con las fuentes por papel (16/20/24/28/32/40/48), en un
   fichero de estilo; nada de medidas sueltas por las vistas.
3. `release.sh 0.1 "..."` para publicar la primera versión con su binario.
