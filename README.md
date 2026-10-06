# cabina5 — el satélite de la P4 en la placa de 5" (Guition JC8048W550C)

Proyecto **nuevo**, hermano de `~/joint/35cabina` (satélite actual en la placa de
3,5" con panel QSPI). Aquí se lleva el satélite a la placa de **5", 800×480**,
para que la cabina tenga una pantalla que se lea de verdad.

> **La placa todavía no ha llegado** (pedida el 6-oct-2026). Este repo existe
> para tener el bring-up listo: el día que llegue, se graba, se comprueba la
> pantalla y el táctil, y a partir de ahí se porta la app.

## La placa

| | |
|---|---|
| Modelo | Guition **JC8048W550C** (la "C" es táctil capacitiva) |
| Chip | **ESP32-S3** (módulo con 16 MB flash + 8 MB PSRAM **octal/OPI**) |
| Pantalla | **5" IPS 800×480, RGB paralelo, controlador ST7262** (sin comandos: solo timing) |
| Táctil | **GT911** por I2C |
| Extra | ranura microSD, conector de cámara, altavoz, 2 USB (USB + UART1), GPIOs expuestos |

**Diferencia importante con el 3,5"**: allí el panel es QSPI y se le mandan ~200
comandos de inicialización; aquí el panel es RGB y va leyendo solo de un
framebuffer en PSRAM. Consecuencia práctica: **cualquier saturación de la PSRAM
se ve como parpadeo**, así que los buffers de dibujo de LVGL van en RAM interna
y el framebuffer (768 KB) en PSRAM.

## Estado (6-oct-2026)

Hecho y sin probar en hardware (no hay placa):

- Proyecto ESP-IDF para `esp32s3` (`sdkconfig.defaults`, particiones de 16 MB
  con dos huecos de OTA, `idf_component.yml` con LVGL 8.4 + `esp_lvgl_port` +
  GT911).
- `main/display.h`: pines y timing **supuestos** (ver aviso ahí).
- `main/esp_bsp.c`: panel RGB, GT911, brillo por LEDC (GPIO 2), bus I2C
  compartido, y la misma API que espera la app del 3,5"
  (`bsp_display_start_with_config`, `bsp_display_lock/unlock`,
  `bsp_display_get_input_dev`, `bsp_display_brightness_set/get`).
- `main/main.c`: **pantalla de prueba** (no es la app): barra de colores, rejilla
  de 100 px, coordenadas del táctil en vivo, estado del Wi-Fi y contador de
  paquetes UDP de la P4 en el puerto 4242.

Pendiente: todo lo demás (copiar `net/`, `data_model`, `reloj`, `salida`… del
35cabina y rehacer la UI para 800×480).

## Cómo se prueba (el día que llegue la placa)

```bash
. ~/.espressif/esp-idf-5.5/export.sh
cd ~/joint/cabina5
idf.py -p /dev/ttyACM0 flash monitor     # OJO: el puerto puede ser ttyACM1
```

Qué hay que mirar, en este orden:

1. **Barra de colores**: si sale todo blanco → pines de datos mal; si el rojo y
   el azul salen cambiados → R y B cruzados en `display.h`; si la imagen está
   desplazada o "bailando" → timing (`pclk_hz`, porches).
2. **Rejilla de 100 px**: comprueba que 800×480 es de verdad 800×480 y que no
   hay recorte en los bordes.
3. **Táctil**: al tocar una esquina, las coordenadas tienen que parecerse a esa
   esquina. Si están cambiadas de eje o invertidas, se corrige con
   `swap_xy`/`mirror_x`/`mirror_y` en `esp_bsp.c`.
4. **UDP**: con la P4 encendida (AP `VictronConfig`), el contador "P4: N
   paquetes" tiene que subir. Si sube, la red funciona y ya se puede copiar la
   app. Si no sube: mirar el log (`Wi-Fi: IP ...`).

## Lo que se reutiliza del 35cabina (no se reescribe)

`net/` (`mini_proto.h`, `udp_rx.c`, `p4_api.c`, `viaje_cola.c`), `data_model.c`,
`reloj.c`, `salida.c`, `tilt.c`, `capture_carousel.c` y el módulo
`config_storage`. Todo eso es placa-agnóstico: por eso `mini_proto.h` **no se
duplica a mano**, se copia tal cual (su sincronización con la P4 ya ha dado
sustos como para tener dos versiones).

## UI: el tamaño de las fuentes SÍ hay que subirlo

La pantalla no es más grande en píxeles por capricho: son **181 ppp** frente a
los **233 ppp** del 3,5" (y 145 ppp de la P4). Es decir, la misma fuente de 20 px
se ve **un 22 % más pequeña** que en el satélite actual, y un 25 % más pequeña
que en la P4. Las fuentes actuales (14/16/20/22/24/32/40/48) se quedan cortas.

Plan: definirlas **por papel** (no por número) en un `ui_style.h`, con una tabla
de equivalencias medida contra la P4:

| Papel | 3,5" (hoy) | 5" (nuevo) | Uso |
|---|---|---|---|
| `UI_FONT_TINY` | 14 | **16** | pies, unidades, notas |
| `UI_FONT_SMALL` | 16 | **20** | etiquetas de dato |
| `UI_FONT_BODY` | 20 | **24** | texto normal |
| `UI_FONT_BODY_BIG` | 22 | **28** | valores |
| `UI_FONT_TITLE` | 24 | **32** | títulos de vista |
| `UI_FONT_HEAD` | 32 | **40** | cabecera |
| `UI_FONT_DISPLAY` | 40/48 | **56/64** | número grande (velocidad, inclinación) |

Los iconos suben en la misma proporción (los que hoy son de 40 px pasan a
~48-56), y los huecos/altos de fila también. Al estar todo en un solo fichero de
tokens, cambiar de pantalla vuelve a ser una tabla y no 8.000 líneas.

**Referencia ya medida**: la P4 usa 146 ppp y sus capturas están aprobadas por
el usuario; las de 5" a 181 ppp piden ~1,25× esas medidas.
