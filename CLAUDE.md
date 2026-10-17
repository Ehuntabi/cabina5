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

**Estado del build (17-oct-2026)**: compila entero y deja `cabina5.bin` de
**2,21 MB** (`0x21CAD0` = 2.214.608 B), con **46 % de la partición de app libre**
(`0x3F0000` = 4.128.768 B). Lo dice el propio `idf.py build`
(`0x1d3530 bytes (46%) free`) y coincide con el `.bin` publicado
(`~/joint-releases/cabina5-v1.30.bin`, mismo tamaño, 16-oct-2026). La placa está
probada desde el 8-oct-2026 (ver `README.md`).

## Versiones de componentes fijadas

`main/idf_component.yml` clava **LVGL `~9.2.0`**, **esp_lvgl_port `2.5.0`** (no la
última, 2.9.0) y **esp_lcd_touch_gt911 `^1.2.0`** (+ `esp_lcd_touch ^1.2.0`).
Motivos, que costaron un build cada uno:

- **LVGL 9 + port 2.x**: el `avoid_tearing` (la única forma de que un panel RGB no
  enseñe una banda moviéndose) solo existe en el port 2.x, y su camino de paneles
  RGB (`lvgl_port_add_disp_rgb`) solo está para LVGL 9. Se subió desde 8.4 + port
  1.x el 8-oct-2026.
- **port 2.5.0 y no 2.9.0**: la 2.9.0 pide `esp_lcd_touch_get_data()`, que no está
  en el `esp_lcd_touch` del registro de componentes (1.2.1); la 2.5.0 sigue
  teniendo `avoid_tearing` y compila.
- **gt911 `^1.2.0`**: el port 2.x lee el táctil con `esp_lcd_touch_get_data()`, que
  es API de la versión 2; con la 1.x no compila. (La trampa vieja de la 1.0.4, que
  no tenía `esp_lcd_touch_io_gt911_config_t`, ya no aplica: no se usa esa versión.)

## La aplicación

`main/main_app.c` **es la aplicación** (no hay `main/main.c`: la pantalla de
prueba se borró en el commit f406457 cuando el port estuvo probado, ver
`README.md`). Arranca la NVS, el modelo de datos, la UI del carrusel (3 pantallas,
`ui/nav.c`), el receptor UDP de la P4, el reloj, la salida/viaje, la cola de
apuntes y el auto-test del canal de subida.
Los pines de `main/display.h` están **verificados en esta placa**
(8-oct-2026: se ve bien, quieta, colores correctos y táctil en toda la
superficie), así que ya no son una suposición de la placa hermana. Para
diagnosticar sin arrancar la app están el **modo captura**
(`main/capture_carousel.c`) y el volcado de geometría de la UI.

## Trampas que ya conocemos de esta familia de placas

- El **reset** del GT911 no está conectado (`TOUCH_PIN_RST` a NC) y la
  **interrupción** es **IO38** (`TOUCH_PIN_INT`, `main/display.h:113`), no NC. En la
  práctica el driver acaba en **modo sondeo por I2C**: el pin no hace de
  interrupción (0 pulsos en 30 s, medido) y la dirección es **0x5D**; con la 0x14
  no responde.
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
  graba `ota_data_initial.bin` en **0x19000**, que es donde está la partición
  `otadata` (`partitions.csv`; 0xd000 cae dentro de `nvs` y no es eso).

## Lo que se enseña (hecho)

1. **Qué se enseña**: el carrusel de **3 pantallas** de `ui/nav.c` (más Ajustes,
   que se abre con un icono y no con gesto), con los datos que llegan de la P4 por
   UDP y el canal de vuelta de apuntes. Ya no es una decisión pendiente.
2. La UI a **800×480** con las **fuentes Inter de `main/fonts/fonts_es.h`** (los
   nombres viejos `lv_font_montserrat_NN` son alias) y los colores de
   `ui/ui_style.h`; nada de medidas sueltas por las vistas.
3. Se publica con `release.sh`; la última es la **v1.30** (tag `v1.30`,
   16-oct-2026), con su `cabina5-v1.30.bin` en `~/joint-releases`.

## Grabar en la placa (16-oct-2026)

Se graba con **`scripts/grabar.sh [puerto]`** (por defecto `/dev/ttyUSB0`), que
escribe las DOS mitades de la OTA (ota_0 en 0x20000 y ota_1 en 0x410000) y, antes
de nada, comprueba que lo que se va a grabar este publicado: arbol limpio, tag de
la version y Release en GitHub. Si falta algo, no graba y dice como arreglarlo.

Antes de escribir comprueba tres cosas mas (16-oct-2026): que la Release tenga el
asset EXACTO de esta version (`cabina5-<tag>.bin`) con su sha256, que en el puerto
haya un ESP32-S3 (la P4 es un ESP32-P4 y tambien sale como `/dev/ttyACM*`: con los
cables cambiados el script se niega), y que el .bin sea el publicado -- si no lo es
byte a byte, graba EL PUBLICADO para que la placa lleve justo lo descargable.
`--seco` hace todas las comprobaciones SIN escribir (util para probar el script).

El motivo: el 16-oct-2026 la P4 del banco estaba grabada con la v4.38 y en GitHub
la ultima Release era la v4.37. Grabar es local e instantaneo; publicar depende de
acordarse de lanzar `release.sh`. Que grabar EXIJA estar publicado es lo unico que
cierra ese hueco. Para pruebas en el banco con algo sin publicar:
`GRABAR_SIN_PUBLICAR=1 scripts/grabar.sh`.
