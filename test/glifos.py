#!/usr/bin/env python3
"""glifos.py — que ningun texto de la UI pida un glifo que la fuente no tiene.

POR QUE EXISTE (24-nov-2026). El dueño mando una foto del cartel de
actualizacion y pregunto: "¿el rectangulo al que me refiero es al que se ve
entre v3.4 y 0 %?". Ese rectangulo era un CARACTER QUE FALTA: el cartel ponia
"v3.4   ·   0 %" y las fuentes Inter de esta placa no traen el punto medio
(U+00B7), asi que LVGL pintaba su cuadro de "glifo que no existe". Con el mismo
problema estaban los "…" de "Preguntando a la P4 qué versión tiene…".

Esto lo caza sin placa y sin mirar la pantalla: saca los literales de cadena del
codigo (fuera comentarios), los compara con los glifos que de verdad llevan las
fuentes (se leen de los .c generados) y falla si alguno no se puede pintar.

Uso:
    python3 test/glifos.py            # 0 si todo se puede pintar
    python3 test/glifos.py --lista    # enseña los glifos que hay

Los glifos permitidos son ASCII imprimible, los acentos y letras espanolas, ¿¡°,
y el rango de iconos de FontAwesome (0xE000-0xF8FF), que va en otras fuentes.
"""
import argparse
import glob
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FUENTE = os.path.join(RAIZ, "main/fonts/lv_font_inter_34_es.c")

# Iconos: van en las fuentes de FontAwesome, no en las de texto.
FA_MIN, FA_MAX = 0xE000, 0xF8FF


def glifos_de_la_fuente(path: str) -> set:
    texto = open(path, encoding="utf-8", errors="replace").read()
    return {int(m.group(1), 16) for m in re.finditer(r"/\* U\+([0-9A-Fa-f]{4,6}) ", texto)}


def literales_de_cadena(codigo: str):
    """Los literales "..." de C, saltando comentarios. Devuelve (linea, texto)."""
    i, n, linea = 0, len(codigo), 1
    while i < n:
        c = codigo[i]
        if c == "\n":
            linea += 1
            i += 1
            continue
        if codigo.startswith("/*", i):
            fin = codigo.find("*/", i + 2)
            if fin < 0:
                return
            linea += codigo.count("\n", i, fin)
            i = fin + 2
            continue
        if codigo.startswith("//", i):
            fin = codigo.find("\n", i)
            i = n if fin < 0 else fin
            continue
        if c == '"':
            j, texto = i + 1, []
            while j < n:
                if codigo[j] == "\\" and j + 1 < n:
                    texto.append(codigo[j:j + 2])
                    j += 2
                    continue
                if codigo[j] == '"':
                    break
                texto.append(codigo[j])
                j += 1
            yield linea, "".join(texto)
            linea += codigo.count("\n", i, j)
            i = j + 1
            continue
        if c == "'":  # literal de caracter: se salta entero
            j = i + 1
            while j < n:
                if codigo[j] == "\\":
                    j += 2
                    continue
                if codigo[j] == "'":
                    break
                if codigo[j] == "\n":
                    linea += 1
                j += 1
            i = j + 1
            continue
        i += 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--lista", action="store_true", help="enseña los glifos cubiertos")
    args = ap.parse_args()

    cubiertos = glifos_de_la_fuente(FUENTE)
    if not cubiertos:
        print(f"no he podido leer los glifos de {FUENTE}", file=sys.stderr)
        return 2
    if args.lista:
        print(" ".join(sorted(chr(c) for c in cubiertos if 0x20 <= c < 0x2500)))

    problemas = []
    ficheros = sorted(glob.glob(os.path.join(RAIZ, "main/**/*.c"), recursive=True) +
                      glob.glob(os.path.join(RAIZ, "main/**/*.h"), recursive=True))
    for f in ficheros:
        if os.sep + "fonts" + os.sep in f:
            continue
        for linea, texto in literales_de_cadena(open(f, encoding="utf-8", errors="replace").read()):
            for ch in texto:
                o = ord(ch)
                if o in cubiertos or FA_MIN <= o <= FA_MAX:
                    continue
                if ch == "\\":       # secuencias de escape: no son glifos
                    continue
                problemas.append((os.path.relpath(f, RAIZ), linea, ch, o))

    for f, linea, ch, o in problemas:
        print(f"NO SE PUEDE PINTAR: {f}:{linea}: {ch!r} (U+{o:04X})")

    if problemas:
        print(f"\n{len(problemas)} texto(s) con glifos que la fuente no trae.")
        print("Cambia el caracter por uno del rango (ASCII, acentos, espanolas, ¿¡°)")
        print("o mete el glifo en la fuente con la receta de su cabecera.")
        return 1
    print(f"ok: {len(ficheros)} ficheros, todos los textos se pueden pintar")
    return 0


if __name__ == "__main__":
    sys.exit(main())
