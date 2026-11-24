#!/usr/bin/env python3
"""splash_fondo_tema.py — que el fondo de la imagen del splash sea EL de la pantalla.

Problema (24-nov-2026, el dueño): "en el splash de la cabina el fondo de la imagen
y el de la pantalla no es el mismo". La imagen (main/icons/splash_logo_5.c) es
RGB565 sin alfa y su fondo es un color plano (0x0041 = #000808, casi negro) que
se recortaba contra una franja visible, porque el fondo de la pagina es
UI_COLOR_BG_HEX (#0C1421).

Que hace: cambia por el color de fondo de la interfaz los pixeles del fondo
plano Y su penumbra: los del borde de la silueta, que son mezclas del fondo con
la chapa y, si se dejan, dibujan un halo. Se recorre desde los cuatro bordes
(relleno por inundacion) mientras el pixel siga siendo "casi el fondo", asi que
la operacion se para sola en la chapa de la autocaravana y no toca los negros de
dentro de la foto (cristales, sombras). No cambia tamano ni formato.

Como se usa (desde cabina5/):
    scripts/splash_fondo_tema.py            # reescribe el .c
    scripts/splash_fondo_tema.py --comprobar # solo dice cuantos quedan, sin tocar

Se puede volver a lanzar las veces que haga falta: es idempotente. Si algun dia
cambia UI_COLOR_BG_HEX, se lanza otra vez y las dos cosas vuelven a cuadrar.
"""
import argparse
import re
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
ESTILO = RAIZ / "main/ui/ui_style.h"
LOGO = RAIZ / "main/icons/splash_logo_5.c"
ANCHO, ALTO = 665, 394
POR_LINEA = 16  # bytes por linea, como lo dejo el generador
# Cuanto se aparta un pixel del fondo para seguir contandolo como fondo (RGB565).
# Con 3 el relleno alcanza la penumbra (0x0021, 0x0020, 0x0040...) y se para en
# la chapa, que esta a mas de 8 niveles.
TOLERANCIA = 3


def color_tema() -> int:
    """UI_COLOR_BG_HEX de ui_style.h, en RGB565."""
    texto = ESTILO.read_text(encoding="utf-8")
    m = re.search(r"#define\s+UI_COLOR_BG_HEX\s+0x([0-9A-Fa-f]{6})", texto)
    if not m:
        sys.exit(f"no encuentro UI_COLOR_BG_HEX en {ESTILO}")
    rgb = int(m.group(1), 16)
    r, g, b = (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def leer_logo():
    texto = LOGO.read_text(encoding="utf-8")
    i = texto.index("splash_logo_5_map[]")
    j = texto.index("};", i)
    crudo = re.findall(r"0x([0-9A-Fa-f]{2})", texto[i:j])
    pixeles = [int(crudo[k], 16) | (int(crudo[k + 1], 16) << 8)
               for k in range(0, len(crudo) - 1, 2)]
    if len(pixeles) != ANCHO * ALTO:
        sys.exit(f"la imagen no es {ANCHO}x{ALTO}: {len(pixeles)} pixeles")
    return texto, i, j, pixeles


def _cerca(p: int, fondo: int, tolerancia: int) -> bool:
    """¿Este pixel es "casi" el fondo? Comparado canal a canal en RGB565."""
    def canales(v):
        return ((v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F)
    a, b = canales(p), canales(fondo)
    return all(abs(x - y) <= tolerancia for x, y in zip(a, b))


def rellenar_desde_los_bordes(pixeles, fondo: int, tema: int, tolerancia: int):
    """El fondo y su penumbra, andando desde los bordes hacia dentro.

    Se para en la chapa porque la chapa es clara: no hace falta silueta ni
    umbral global, que se comeria los negros de dentro de la foto."""
    W, H = ANCHO, ALTO
    visto = bytearray(W * H)
    pila = []
    for x in range(W):
        pila += [x, (H - 1) * W + x]
    for y in range(H):
        pila += [y * W, y * W + W - 1]
    cambiados = 0
    while pila:
        k = pila.pop()
        if visto[k]:
            continue
        visto[k] = 1
        if not _cerca(pixeles[k], fondo, tolerancia):
            continue
        pixeles[k] = tema
        cambiados += 1
        x, y = k % W, k // W
        if x > 0: pila.append(k - 1)
        if x < W - 1: pila.append(k + 1)
        if y > 0: pila.append(k - W)
        if y < H - 1: pila.append(k + W)
    return cambiados


def fondo_del_logo(pixeles) -> int:
    """El color de las cuatro esquinas; han de coincidir (fondo plano)."""
    esquinas = [pixeles[0], pixeles[ANCHO - 1],
                pixeles[(ALTO - 1) * ANCHO], pixeles[ALTO * ANCHO - 1]]
    if len(set(esquinas)) != 1:
        sys.exit(f"el fondo no es liso, esquinas {[hex(e) for e in esquinas]}")
    return esquinas[0]


def escribir_logo(texto: str, i: int, j: int, pixeles) -> None:
    """Vuelve a escribir el array con el mismo formato del generador."""
    bytes_ = []
    for p in pixeles:
        bytes_ += [p & 0xFF, p >> 8]
    lineas = []
    for k in range(0, len(bytes_), POR_LINEA):
        trozo = ", ".join(f"0x{c:02X}" for c in bytes_[k:k + POR_LINEA])
        fin = "," if k + POR_LINEA < len(bytes_) else ""
        lineas.append(f"  {trozo}{fin}")
    nuevo = texto[:i] + "splash_logo_5_map[] = {\n" + "\n".join(lineas) + "\n" + texto[j:]
    LOGO.write_text(nuevo, encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--comprobar", action="store_true",
                    help="no escribe nada; solo cuenta los pixeles del fondo")
    args = ap.parse_args()

    tema = color_tema()
    texto, i, j, pixeles = leer_logo()
    fondo = fondo_del_logo(pixeles)
    n = sum(1 for p in pixeles if p == fondo)

    print(f"fondo de la imagen: 0x{fondo:04X} ({n} pixeles de {len(pixeles)})")
    print(f"fondo de la pantalla (UI_COLOR_BG_HEX): 0x{tema:04X}")
    if fondo == tema:
        print("ya coinciden: no hay nada que hacer")
        return 0
    if args.comprobar:
        print("hay que cambiarlo (sin escribir); lanza sin --comprobar")
        return 1

    # 1) el fondo y su penumbra (andando desde los bordes)
    penumbra = rellenar_desde_los_bordes(pixeles, fondo, tema, TOLERANCIA)
    # 2) algun pixel suelto del mismo color que quede por dentro
    sueltos = 0
    for k, p in enumerate(pixeles):
        if p == fondo:
            pixeles[k] = tema
            sueltos += 1
    escribir_logo(texto, i, j, pixeles)
    print(f"hecho: {penumbra} pixeles ({n} eran fondo exacto, {penumbra - n + sueltos} "
          f"de la penumbra) pasan a 0x{tema:04X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
