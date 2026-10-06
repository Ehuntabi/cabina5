#!/usr/bin/env bash
#
# release.sh — publica una versión del satélite de 5" (cabina5).
#
# Uso:  ./release.sh X.Y  ["mensaje del tag"]
#   ej: ./release.sh 0.1 "Primer arranque en la placa: pantalla, táctil y UDP"
#
# Qué hace (mismo guion que ~/joint/35cabina/release.sh, adaptado a esta placa):
#   1. Exige el árbol limpio (lo que se publica tiene que ser lo que hay).
#   2. Crea el tag vX.Y, o reutiliza el que ya esté EN ESTE MISMO commit.
#   3. Compila con reconfigure (la versión se calcula al CONFIGURAR, no al
#      compilar: sin reconfigure se publicaría un binario con la versión vieja).
#   4. VERIFICA que la versión embebida en el .bin coincide con el tag.
#   5. Sube código y tag, y publica la Release en GitHub con el binario.
#
# OJO (propio de este proyecto): hay que decirle a ESP-IDF que el target es
# esp32s3 y poner el toolchain Xtensa en el PATH. En este PC el export.sh no lo
# mete (la instalación de IDF se hizo para la P4), y sin eso el build aborta con
# "xtensa-esp32s3-elf-g++ was not found in the PATH" — ver CLAUDE.md.
#
set -euo pipefail
cd "$(dirname "$0")"

REPO="Ehuntabi/cabina5"
IDF_EXPORT="${IDF_EXPORT:-$HOME/.espressif/esp-idf-5.5/export.sh}"
XTENSA_BIN="${XTENSA_BIN:-$HOME/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin}"
APP_BIN="build/cabina5.bin"
RELDIR="$HOME/joint-releases"

# ── 0) argumentos ───────────────────────────────────────────────────────────
VER_IN="${1:-}"
if [ -z "$VER_IN" ]; then
    ULTIMO_TAG="$(git describe --tags --abbrev=0 --match 'v*.*' 2>/dev/null || true)"
    if [ -z "$ULTIMO_TAG" ]; then
        echo "Uso: ./release.sh X.Y [\"mensaje\"]   (sin X.Y se usa el último tag +0.1)"
        echo "     Ahora mismo no hay ningún tag v*: hay que decir la versión."
        exit 1
    fi
    VER_IN="$(printf '%s' "$ULTIMO_TAG" | awk -F. '{ printf "%s.%d", $1, $2 + 1 }')"
    echo "[i] sin argumento: toca $VER_IN (último tag $ULTIMO_TAG, +0.1)"
fi
VER="${VER_IN#v}"
if ! printf '%s' "$VER" | grep -Eq '^[0-9]+\.[0-9]+$'; then
    echo "ERROR: '$VER_IN' no tiene el formato X.Y (ej: 0.1). Dos números, no tres."
    exit 1
fi
TAG="v$VER"
MSG="${2:-Release $TAG}"

# ── 1) el árbol, limpio ─────────────────────────────────────────────────────
if [ -n "$(git status --porcelain)" ]; then
    echo "ABORTADO: hay cambios sin commitear. Lo que se publica tiene que ser"
    echo "          exactamente lo que hay en el repositorio."
    git status --short
    exit 1
fi

# ── 2) el tag ───────────────────────────────────────────────────────────────
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    if [ "$(git rev-list -n1 "$TAG")" != "$(git rev-parse HEAD)" ]; then
        echo "ABORTADO: el tag $TAG existe y apunta a OTRO commit."
        exit 1
    fi
    echo "[ok] el tag $TAG ya está en este commit, lo reutilizo"
else
    git tag -a "$TAG" -m "$MSG"
    echo "[ok] tag $TAG creado"
fi

# ── 3) compilar ─────────────────────────────────────────────────────────────
if [ ! -f "$IDF_EXPORT" ]; then
    echo "ERROR: no encuentro ESP-IDF en $IDF_EXPORT"; exit 1
fi
if [ ! -d "$XTENSA_BIN" ]; then
    echo "ERROR: no encuentro el toolchain Xtensa en $XTENSA_BIN"
    echo "       Mira qué versión hay: ls ~/.espressif/tools/xtensa-esp-elf/"
    exit 1
fi
export IDF_TARGET=esp32s3
# shellcheck disable=SC1090
. "$IDF_EXPORT" >/dev/null 2>&1
export PATH="$XTENSA_BIN:$PATH"
command -v xtensa-esp32s3-elf-gcc >/dev/null || { echo "ERROR: el compilador del S3 no está en el PATH"; exit 1; }

# La cabecera con la clave del AP no está versionada: si falta, se crea desde la
# plantilla para que el build no se pare (el binario con la clave de broma no se
# publica nunca: la Release se hace solo si el árbol estaba limpio y el .bin lo
# compila este script, con la cabecera de verdad).
if [ ! -f main/wifi_credentials.h ]; then
    echo "AVISO: falta main/wifi_credentials.h; lo creo desde la plantilla"
    echo "       (el AP no conectará hasta que pongas los datos reales)"
    cp main/wifi_credentials.h.example main/wifi_credentials.h
fi

idf.py reconfigure >/dev/null
idf.py build >/dev/null
echo "[ok] compilado"

# ── 4) verificar que la versión embebida == tag ─────────────────────────────
EMB="$(python3 - "$APP_BIN" <<'PY'
import sys
with open(sys.argv[1],'rb') as f: d=f.read(0x120)
print(d[0x20+16:0x20+48].split(b'\x00')[0].decode('ascii','replace'))
PY
)"
if [ "$EMB" != "$TAG" ]; then
    echo "ABORTADO: el binario lleva '$EMB' y el tag es '$TAG'. No se publica"
    echo "          un firmware que dice ser otra versión."
    exit 1
fi
echo "[ok] el binario dice '$EMB'"

mkdir -p "$RELDIR"
rm -f "$RELDIR"/cabina5-v*.bin
OUT="$RELDIR/cabina5-$TAG.bin"
cp "$APP_BIN" "$OUT"
echo "[ok] binario en $OUT"

# ── 5) publicar ─────────────────────────────────────────────────────────────
git push origin "$(git branch --show-current)" "$TAG"

if ! command -v gh >/dev/null 2>&1; then
    echo "AVISO: no está 'gh': la Release NO se ha publicado."
    exit 0
fi
if gh release view "$TAG" -R "$REPO" >/dev/null 2>&1; then
    echo "[ok] la Release $TAG ya existe, no la toco"
else
    NOTAS_TMP="/tmp/notas-cabina5-$TAG.md"
    if [ -n "${2:-}" ]; then
        printf '%s\n' "$MSG" > "$NOTAS_TMP"
    else
        ANTERIOR=$(git describe --tags --abbrev=0 --match "v*.*" "$TAG^" 2>/dev/null || true)
        {
            if [ -n "$ANTERIOR" ]; then
                echo "Cambios desde $ANTERIOR:"
                echo
                git log --no-merges --format='- %s' "$ANTERIOR..$TAG"
            else
                echo "Primera version publicada."
            fi
        } > "$NOTAS_TMP"
    fi
    gh release create "$TAG" -R "$REPO" \
        --title "cabina5 $TAG" \
        --notes-file "$NOTAS_TMP" \
        "$OUT"
    echo "[ok] Release $TAG publicada"
fi

echo
echo "────────────────────────────────────────────────────────────"
echo "PUBLICADO $TAG. Solo queda grabar:"
echo "  idf.py -p /dev/ttyACM1 flash    # la P4 es ttyACM0; esta placa es la S3"
echo "────────────────────────────────────────────────────────────"
