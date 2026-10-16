#!/usr/bin/env bash
# Auditoria de ESTILO de la cabina (16-oct-2026).
#
# No prueba comportamiento: vigila las tres reglas que se acordaron al unificar
# el estilo de las placas (ver docs/GUIA_ESTILO.md de la P4 y main/ui/ui_style.h).
# Existe porque las tres se rompieron de verdad, y sobre todo la primera:
#
#   1. TODO NOMBRE DE FUENTE TIENE QUE ESTAR ALIASADO. La UI llama a las fuentes
#      por su nombre viejo (lv_font_montserrat_16 y compañia) en mas de 100
#      sitios. Si ese nombre no esta #define'd en alguna cabecera, NO da error de
#      compilacion: tira de la Montserrat del SDK, que sigue compilada, y el texto
#      sale al tamaño LITERAL (16 px) en vez del papel que le tocaba (30). Paso el
#      16-oct-2026 y el usuario lo vio como "el texto es pequeñisimo".
#   2. NINGUN COLOR A MANO: los colores salen de main/ui/ui_style.h. Antes habia 29
#      tonos distintos entre las dos cabinas; ahora 14 con nombre.
#   3. LOS TITULOS VAN CENTRADOS: un rotulo en mayusculas (titulo de pantalla) no
#      puede estar alineado a la derecha. Paso con "CONFIGURACIÓN" y "AJUSTES
#      WI-FI" en la 3,5".
set -uo pipefail
cd "$(dirname "$0")/.."

fallos=0
ok()  { echo "  ok    $1"; }
mal() { echo "  FALLO $1"; fallos=$((fallos+1)); }

FUENTES=$(grep -rhoE 'lv_font_montserrat_(bold_)?[0-9]+' main --include=*.c --include=*.h \
          | sort -u)
echo "== 1. Nombres de fuente aliasados (los del SDK no valen) =="
SIN_ALIAS=""
for f in $FUENTES; do
    # ¿hay un #define de ese nombre en alguna cabecera o fuente de main/?
    if ! grep -rqE "^\s*#define\s+$f\b" main --include=*.c --include=*.h; then
        SIN_ALIAS="$SIN_ALIAS $f"
    fi
done
if [ -n "$SIN_ALIAS" ]; then
    mal "estos nombres caen a la fuente del SDK (texto al tamaño literal):$SIN_ALIAS"
else
    ok "los $(echo "$FUENTES" | wc -l) nombres de fuente que usa la UI estan aliasados"
fi

echo "== 2. Colores: ninguno a mano =="
COLORES=$(grep -rnE "lv_color_hex\(0x[0-9A-Fa-f]{6}\)" main --include=*.c --include=*.h \
          | grep -v "ui/ui_style.h" || true)
if [ -n "$COLORES" ]; then
    mal "hay colores escritos a mano (van en ui_style.h):"
    echo "$COLORES" | sed 's/^/          /'
else
    ok "todos los colores salen de ui_style.h"
fi

echo "== 3. Titulos (rotulos en MAYUSCULAS) centrados =="
DESC=$(python3 - <<'PY'
import pathlib, re
malos = []
for f in sorted(pathlib.Path('main').rglob('*.c')):
    if 'managed_components' in str(f): continue
    lineas = f.read_text(errors='ignore').split('\n')
    for i, l in enumerate(lineas):
        m = re.search(r'lv_label_set_text(?:_fmt)?\(\s*(\w+)\s*,\s*"([A-ZÁÉÍÓÚÑ][A-ZÁÉÍÓÚÑ ()0-9_-]{3,})"', l)
        if not m: continue
        var, txt = m.group(1), m.group(2)
        ctx = "\n".join(lineas[max(0, i-8):i+10])
        al = re.findall(r'lv_obj_align\(\s*' + var + r'\s*,\s*(LV_ALIGN_\w+)', ctx)
        if al and 'RIGHT' in al[0]:
            malos.append(f"{f.name}:{i+1} {txt}")
print("\n".join(malos))
PY
)
if [ -n "$DESC" ]; then
    mal "titulos alineados a la derecha:"; echo "$DESC" | sed 's/^/          /'
else
    ok "ningun titulo en mayusculas esta alineado a la derecha"
fi

echo
if [ "$fallos" = 0 ]; then echo "ESTILO OK"; else echo "FALLOS: $fallos"; fi
exit $([ "$fallos" = 0 ] && echo 0 || echo 1)
