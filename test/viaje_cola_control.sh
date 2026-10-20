#!/usr/bin/env bash
# ¿Que cuerpos pueden usar los HUECOS RESERVADOS de la cola? (auditoria del
# 14-oct-2026)
#
# La cola reserva VIAJE_COLA_RESERVA_CONTROL huecos para que el CIERRE del viaje
# entre siempre, aunque este llena de apuntes: con la P4 apagada el `fin` se
# quedaba fuera y el viaje no se podia cerrar desde la pantalla. Quien decide
# que es "de control" es es_op_de_control(), y de eso depende una garantia, asi
# que se prueba.
#
# El test NO prueba una copia: saca la funcion REAL de main/net/viaje_cola.c y
# la compila con el arnes. Si alguien cambia la funcion, el test cambia con
# ella.
set -euo pipefail

AQUI=$(cd "$(dirname "$0")" && pwd)

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

python3 - "$AQUI/viaje_cola_control_arnes.c" "$TMP/t.c" <<'PYEXT'
import re, sys, pathlib
sys.path.insert(0, str(pathlib.Path(sys.argv[1]).resolve().parent))
from extrae_cuerpo import desde
arnes_path = pathlib.Path(sys.argv[1]).resolve()
raiz = arnes_path.parent.parent
fuente = (raiz / 'main/net/viaje_cola.c').read_text()
m = re.search(r'static bool es_op_de_control\(const char \*cuerpo\)\s*\n\{', fuente)
if not m:
    raise SystemExit('no encuentro es_op_de_control() en main/net/viaje_cola.c')
pathlib.Path(sys.argv[2]).write_text(arnes_path.read_text().replace('__FN__', desde(fuente, m)))
PYEXT

gcc -std=gnu17 -Wall -Wextra -o "$TMP/t" "$TMP/t.c"
"$TMP/t"
