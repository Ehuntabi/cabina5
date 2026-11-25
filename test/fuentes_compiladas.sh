#!/usr/bin/env bash
# ¿Están TODOS los .c de main/ dentro del binario? (auditoría del 25-nov-2026, H-40)
#
# main/CMakeLists.txt usa file(GLOB ...), que se evalúa al CONFIGURAR: un .c nuevo
# no entra hasta el siguiente "idf.py reconfigure" y el fallo es silencioso si
# además nadie lo referencia (si lo llamas, el enlazador se queja). CONFIGURE_DEPENDS
# no vale aquí (IDF compila ese fichero en modo script), así que la red de
# seguridad es esta comprobación: cada .c de main/ tiene que tener su .obj en
# build/. Si el build no existe, el test lo dice y no falla (no compila).
#
# Uso: bash test/fuentes_compiladas.sh
set -euo pipefail
AQUI=$(cd "$(dirname "$0")" && pwd)
RAIZ=$(cd "$AQUI/.." && pwd)
OBJ="$RAIZ/build/esp-idf/main/CMakeFiles/__idf_main.dir"

if [ ! -d "$OBJ" ]; then
    echo "ok (sin build): no hay build/ que comparar; compila y vuelve a lanzarlo"
    exit 0
fi

faltan=0
while IFS= read -r src; do
    rel="${src#$RAIZ/main/}"
    o="$OBJ/$rel.obj"
    if [ ! -f "$o" ]; then
        echo "FALTA en el binario: main/$rel (no hay .obj) -> idf.py reconfigure"
        faltan=$((faltan+1))
    fi
done < <(find "$RAIZ/main" -name '*.c')

total=$(find "$RAIZ/main" -name '*.c' | wc -l)
if [ "$faltan" -ne 0 ]; then
    echo "$faltan de $total .c no estan compilados"
    exit 1
fi
echo "ok: los $total .c de main/ estan compilados"
