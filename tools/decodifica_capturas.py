#!/usr/bin/env python3
"""Decodifica las capturas del modo captura de cabina5 a PNG.

El firmware (main/capture_carousel.c, con CAPTURE_CAROUSEL_ENABLE a 1) vuelca
cada pantalla por el puerto serie como:

    ===CAPTURE:<nombre>:<ancho>x<alto>===
    <base64 en lineas de 512 caracteres>
    ===END===

...y el base64 son los pixeles en RGB565 **little endian**, tal cual los deja
`lv_port_snapshot()` en la PSRAM. Este script lo convierte a PNG.

    python3 decodifica_capturas.py capturas/capturas_serie.txt -o capturas/png

Se apoya en PIL (Pillow). Si no estuviera instalado: apt install python3-pil.
"""
import argparse
import os
import re
import sys

CAB = re.compile(rb'===BIN:([A-Za-z0-9_\-]+):(\d+)x(\d+):(\d+)===\r?\n')

# El volcado es binario con la longitud declarada en la cabecera:
#
#     ===BIN:<nombre>:<ancho>x<alto>:<bytes>===\n
#     <bytes en crudo, RGB565 little endian>
#     \n===FIN===\n
#
# Asi un corte se detecta (faltan bytes) en vez de corromper la imagen, y lo que
# escriba otra tarea en el puerto se descarta al buscar la cabecera siguiente.


def decodifica(datos):
    """Devuelve [(nombre, w, h, bytes)] del volcado. Avisa de lo que se pierda."""
    salida = []
    pos = 0
    while True:
        m = CAB.search(datos, pos)
        if not m:
            break
        nombre = m.group(1).decode()
        w, h, n = int(m.group(2)), int(m.group(3)), int(m.group(4))
        ini = m.end()
        fin = ini + n
        if fin > len(datos):
            print(f"AVISO: '{nombre}' cortado: llegaron {len(datos) - ini} de {n} "
                  f"bytes", file=sys.stderr)
            break
        crudo = datos[ini:fin]
        if datos[fin:fin + 10].find(b'===FIN===') < 0:
            print(f"AVISO: '{nombre}' sin marca de fin (bytes de mas o de menos)",
                  file=sys.stderr)
        if len(crudo) != w * h * 2:
            print(f"AVISO: '{nombre}' tiene {len(crudo)} bytes y {w}x{h} piden "
                  f"{w * h * 2}", file=sys.stderr)
        else:
            salida.append((nombre, w, h, crudo))
        pos = fin
    return salida


def a_png(nombre, w, h, crudo, destino):
    """RGB565 little endian -> PNG. El valor va en BGR;16 porque PIL lee los
    canales de 16 bits en orden little endian."""
    from PIL import Image
    img = Image.frombytes('RGB', (w, h), crudo, 'raw', 'BGR;16')
    ruta = os.path.join(destino, f"{nombre}.png")
    img.save(ruta)
    return ruta


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('log', help='fichero con el volcado del puerto serie')
    ap.add_argument('-o', '--salida', default='capturas/png',
                    help='carpeta donde dejar los PNG (por defecto capturas/png)')
    args = ap.parse_args()

    with open(args.log, 'rb') as f:
        datos = f.read()

    pantallas = decodifica(datos)
    if not pantallas:
        print("No hay ninguna captura en el fichero.", file=sys.stderr)
        return 1

    os.makedirs(args.salida, exist_ok=True)
    for nombre, w, h, raw in pantallas:
        ruta = a_png(nombre, w, h, raw, args.salida)
        print(f"{nombre}: {w}x{h} -> {ruta}")
    print(f"{len(pantallas)} captura(s) en {args.salida}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
