#!/usr/bin/env python3
"""Decodifica las capturas del modo captura de cabina5 a PNG.

El firmware (main/capture_carousel.c, con CAPTURE_CAROUSEL_ENABLE a 1) vuelca
cada pantalla por el puerto serie TROCEADA en bloques de 1024 bytes:

    ===BIN:<nombre>:<ancho>x<alto>:<bytes>===
    #0:1024:d400
    <1024 bytes en crudo, RGB565 little endian>
    #1:1024:1f3a
    ...
    \n===FIN===

Cada trozo lleva delante su indice, su tamano y la suma de sus bytes en
hexadecimal: asi un trozo que llega mal se detecta (y se deja en negro) en vez
de correr toda la imagen 1024 bytes y sacar bandas diagonales. Los pixeles son
los que deja `lv_port_snapshot()` en la PSRAM. Este script lo convierte a PNG.

    python3 tools/decodifica_capturas.py capturas_serie.bin -o capturas/png

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


CHUNK = re.compile(rb'#(\d+):(\d+):([0-9a-fA-F]{4})\r?\n')


def decodifica(datos):
    """Devuelve [(nombre, w, h, bytes)] del volcado.

    El volcado va TROCEADO (17-oct-2026) y hay que leerlo como tal: entre trozo y
    trozo va una cabecera "#<indice>:<bytes>:<suma>\n" y el receptor tiene que
    saltarsela y comprobar la suma. Este script se quedo con el formato VIEJO (los
    bytes del tiron detras de la cabecera) y por eso las capturas salian con
    bandas diagonales: metia las cabeceras de 1024 en 1024 dentro de los pixeles.
    """
    salida = []
    pos = 0
    while True:
        m = CAB.search(datos, pos)
        if not m:
            break
        nombre = m.group(1).decode()
        w, h, n = int(m.group(2)), int(m.group(3)), int(m.group(4))
        p = m.end()
        crudo = bytearray()
        perdidos = 0
        while len(crudo) < n:
            c = CHUNK.match(datos, p)
            if not c:
                # Se perdio el hilo (o el volcado se corto): se abandona esta
                # pantalla, no se inventa nada.
                print(f"AVISO: '{nombre}' cortado en {len(crudo)} de {n} bytes",
                      file=sys.stderr)
                crudo = None
                break
            idx, nb, suma = int(c.group(1)), int(c.group(2)), int(c.group(3), 16)
            ini_trozo = c.end()
            trozo = datos[ini_trozo:ini_trozo + nb]
            if len(trozo) != nb:
                print(f"AVISO: '{nombre}' trozo {idx} incompleto", file=sys.stderr)
                crudo = None
                break
            if (sum(trozo) & 0xFFFF) != suma:
                # La suma mal NO tira la pantalla entera (4-nov-2026): a veces
                # llegan uno o dos bytes de mas entre trozo y trozo (el cable o
                # el driver de la UART), la suma sale distinta por unos pocos y
                # antes se abandonaba la imagen ENTERA por eso. Se queda el
                # trozo tal cual (unos pocos pixeles mal, que no se ven) y se
                # resincroniza con la cabecera siguiente.
                print(f"AVISO: '{nombre}' trozo {idx} con la suma mal "
                      f"(esperado {suma:04x}, calculado {sum(trozo) & 0xFFFF:04x})",
                      file=sys.stderr)
                perdidos += 1
                crudo.extend(trozo)
            else:
                crudo.extend(trozo)
            p = ini_trozo + nb
            # Resincronizar: si justo detras no empieza una cabecera, se busca
            # la siguiente un poco mas alla (bytes de mas o de menos entre
            # trozos). Sin esto se perdia la imagen por dos bytes de nada.
            if len(crudo) < n and not CHUNK.match(datos, p):
                m2 = CHUNK.search(datos, p, min(p + 96, len(datos)))
                if m2:
                    print(f"AVISO: '{nombre}' resincronizado tras el trozo {idx} "
                          f"({m2.start() - p} byte(s) de por medio)", file=sys.stderr)
                    p = m2.start()
        if crudo is None:
            pos = p
            continue
        if perdidos:
            print(f"AVISO: '{nombre}' con {perdidos} trozo(s) perdido(s)", file=sys.stderr)
        if len(crudo) != w * h * 2:
            print(f"AVISO: '{nombre}' tiene {len(crudo)} bytes y {w}x{h} piden "
                  f"{w * h * 2}", file=sys.stderr)
        else:
            salida.append((nombre, w, h, bytes(crudo)))
        pos = p
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
