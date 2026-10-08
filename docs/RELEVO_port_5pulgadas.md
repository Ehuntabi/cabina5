# Estado del port a la placa de 5" (relevo, 8-oct-2026)

Documento de traspaso: qué está hecho, qué está medido y **qué queda**.
La placa arranca sola con la app y se ve, pero la UI aún no está adaptada.

## Lo que YA funciona (verificado en la placa hoy)

| | |
|---|---|
| Arranque | Sin QIO no arranca (issue #18806). Con `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y` arranca siempre |
| Consola | Por UART (CH340, `/dev/ttyUSB0`), no por USB: `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` |
| Pantalla | 800×480, sin *screen drift* gracias al **bounce buffer** (20 líneas, RAM interna) + `num_fbs=1` |
| Refresco | 20 fotogramas/s, panel barriendo estable a 39 Hz |
| Táctil | Chip y driver OK; el **temporizador de lectura de LVGL nace PAUSADO** en LVGL 9 y hay que reanudarlo (ver `esp_bsp.c`). Corregido y verificado: `Tactil: temporizador de lectura EN MARCHA` |
| Red | WiFi asociado a la P4, IP 192.168.4.2, UDP :4242 recibiendo, reloj de la P4, viajes |
| Memoria | 70 KB internos libres + 7,2 MB de PSRAM. LVGL usa PSRAM con el asignador propio (`components/lvgl_mem/`) |
| App portada | `net/`, `data_model`, `salida`, `tilt`, `reloj`, `diag_reset`, `ui/`, iconos y fuentes: compilan y arrancan |

## Lo que QUEDA (el trabajo de la próxima sesión)

1. **Adaptar la UI a 800×480.** La UI es de la pantalla de 3,5" (320×480 vertical).
   En 800×480 el contenido sale estrecho y descolgado, y por eso los toques no
   encuentran nada que pulsar. Hay que decidir el diseño (probablemente aprovechar
   el ancho: info a un lado, registro al otro) y adaptar las 4 vistas.
2. **Fuentes.** Ya está hecho el mecanismo de un solo sitio (`main/ui/estilos.h`,
   que traduce los nombres viejos a un escalón más). Falta **generar la negrita 40**
   (hoy solo existen la 20 y la 32) si el diseño la pide.
3. Quitar los `ESP_LOGI` de diagnóstico que quedan en `esp_bsp.c`
   (el informe de refrescos/s) cuando la UI esté estable.

## Trampas ya documentadas (no volver a tropezar)

En `README.md`, sección **Trampas de esta placa**: QIO, consola por UART,
*screen drift* (bounce buffer) y el pin de interrupción del táctil (IO38 es INT,
no RST). Y en el código, comentado en el sitio donde importa:

- `display.h`: los canales R y B **no se pueden deducir del xlsx** (mismos pines,
  etiqueta cambiada); la asignación buena está comprobada pidiendo azul puro.
- `esp_bsp.c`: por qué no se usa `lvgl_port_add_disp_rgb()`, por qué `num_fbs=0`
  deja la pantalla negra y por qué el temporizador de entrada hay que reanudarlo.
- `components/lvgl_mem/`: por qué LVGL tiene que coger su memoria de PSRAM (con
  las cuentas medidas) y por qué su componente lleva `WHOLE_ARCHIVE`.

## Cómo se compila y se graba

```bash
export IDF_TARGET=esp32s3
. ~/.espressif/esp-idf-5.5/export.sh
export PATH="$HOME/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin:$PATH"
cd ~/joint/cabina5
idf.py -p /dev/ttyUSB0 flash
python -m esptool --chip esp32s3 -p /dev/ttyUSB0 -b 460800 write_flash 0x410000 build/cabina5.bin
```

La segunda línea graba la otra ranura de OTA: si solo se graba una, tras un OTA la
placa arranca la otra y parece que "no grabó nada".

**Aviso**: abrir el puerto serie reinicia la placa (la CH340 conmuta DTR/RTS).
Los resets que se vean al leer el log son de eso, no de un fallo.
