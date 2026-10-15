#!/usr/bin/env bash
#
# release.sh — publica una versión del satélite de 5" (cabina5).
#
# Uso:  ./release.sh X.Y  ["mensaje del tag"]
#   ej: ./release.sh 0.1 "Primer arranque en la placa: pantalla, táctil y UDP"
#
# Qué hace:
#   1. Exige el árbol limpio (lo que se publica tiene que ser lo que hay).
#   2. Crea el tag vX.Y, o reutiliza el que ya esté EN ESTE MISMO commit.
#   3. Compila con reconfigure (la versión se calcula al CONFIGURAR, no al
#      compilar: sin reconfigure se publicaría un binario con la versión vieja).
#   4. VERIFICA que la versión embebida en el .bin coincide con el tag.
#   5. Sube código y tag, y publica la Release en GitHub con el binario.
#      Sin 'gh' no se publica: se exige ANTES de crear el tag, y al final se
#      comprueba que la Release lleve su .bin de verdad.
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

# ── 0b) hace falta 'gh': sin él no hay Release ──────────────────────────────
# Se mira AQUÍ, antes de crear el tag y de compilar, que es cuando abortar no
# cuesta nada. Antes se miraba al final y, si faltaba 'gh', el script se quedaba
# en "AVISO" y seguía: código y tag subidos, Release sin publicar, o sea una
# publicación a medias (auditoría del 15-oct-2026).
if ! command -v gh >/dev/null 2>&1; then
    echo "ERROR: no está 'gh' (GitHub CLI) y sin él no se puede publicar la Release."
    echo "       Instálalo (https://cli.github.com), entra con 'gh auth login' y"
    echo "       vuelve a lanzar ./release.sh $VER_IN. Todavía no se ha tocado nada."
    exit 1
fi

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

# ── LA CLAVE DEL AP, ANTES DE COMPILAR NADA (correccion del 16-oct-2026) ─────
#
# Este fichero no esta versionado (ni debe: el repo es publico), y su plantilla lo
# dice con todas las letras: "sin este fichero el build falla a proposito: no debe
# compilarse con una contrasena de broma".
#
# Aqui ponia lo contrario: si faltaba, copiaba la plantilla "para que el build no
# se pare" y seguia. El comentario se justificaba diciendo que ese binario no se
# publicaria nunca porque la Release exige el arbol limpio -- y es FALSO: este
# fichero esta en .gitignore, asi que el arbol sigue limpio y el script publicaba
# una Release con la clave de broma (WIFI_CRED_PASS "CAMBIAME") dentro. Ahora se
# para aqui, que es lo que pide la plantilla.
if [ ! -f main/wifi_credentials.h ]; then
    echo "ERROR: falta main/wifi_credentials.h (no versionado, ver .gitignore)."
    echo "       Copia la plantilla y pon los datos REALES del AP de la P4:"
    echo "           cp main/wifi_credentials.h.example main/wifi_credentials.h"
    echo "       NO se publica un binario con la clave de broma."
    exit 1
fi
if cmp -s main/wifi_credentials.h main/wifi_credentials.h.example; then
    echo "ERROR: main/wifi_credentials.h es la PLANTILLA sin rellenar."
    echo "       Pon los datos reales del AP antes de publicar una version."
    exit 1
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
    # No debería llegar aquí (se comprueba en el paso 0b, antes de crear el tag),
    # pero si pasa NO se sigue: el código y el tag YA están subidos, así que
    # dejarlo así es una publicación a medias, que es lo que este script evita.
    echo "ERROR: 'gh' ha desaparecido a mitad de la publicación (¿el PATH?)."
    echo "       El código y el tag $TAG YA están subidos; falta la Release. Instala"
    echo "       'gh', entra con 'gh auth login' y publícala con:"
    echo "       gh release create $TAG -R $REPO --title \"cabina5 $TAG\" \\"
    echo "          --notes-from-tag \"$OUT\""
    exit 1
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
    if gh release create "$TAG" -R "$REPO" \
         --title "cabina5 $TAG" \
         --notes-file "$NOTAS_TMP" \
         "$OUT"; then
        :
    else
        echo "AVISO: GitHub no ha creado la Release (¿un 5xx suyo? paso el 16-oct-2026)."
    fi
    # SE COMPRUEBA, no se da por hecho: si la Release no existe, el tag esta
    # subido y el .bin en ~/joint-releases, pero nadie puede descargarlo -- y
    # scripts/grabar.sh no dejaria grabar la placa. Mejor decirlo aqui, con el
    # comando exacto para reintentarlo.
    if ! gh release view "$TAG" -R "$REPO" >/dev/null 2>&1; then
        echo
        echo "LA RELEASE $TAG NO ESTA PUBLICADA (el tag si). Reintenta con:"
        echo "  gh release create $TAG -R $REPO --title \"cabina5 $TAG\" \\"
        echo "     --notes-file $NOTAS_TMP \"$OUT\""
        exit 1
    fi
    # Y ADEMÁS tiene que llevar su .bin: una Release sin asset no la puede
    # descargar nadie, y scripts/grabar.sh la rechazaría (auditoría del
    # 15-oct-2026). GitHub tarda un momento en procesar lo que acaba de subir
    # `gh release create`, así que se hacen 5 comprobaciones con 2 s de espera
    # (unos 10 s en total) antes de darla por incompleta.
    ASSETS=""
    for _ in 1 2 3 4 5; do
        ASSETS="$(gh release view "$TAG" -R "$REPO" --json assets --jq '.assets[].name' 2>/dev/null || true)"
        if printf '%s\n' "$ASSETS" | grep -q '\.bin$'; then break; fi
        sleep 2
    done
    if ! printf '%s\n' "$ASSETS" | grep -q '\.bin$'; then
        echo
        echo "ERROR: el tag $TAG quedó subido, pero la Release está INCOMPLETA: no"
        echo "       tiene ningún asset .bin (visto: ${ASSETS:-ninguno}). El firmware"
        echo "       está en $OUT; súbelo a mano:"
        echo "  gh release upload $TAG -R $REPO \"$OUT\""
        exit 1
    fi
    echo "[ok] Release $TAG publicada con su .bin ($(printf '%s' "$ASSETS" | tr '\n' ' '))"
fi

echo
echo "────────────────────────────────────────────────────────────"
echo "PUBLICADO $TAG. Solo queda grabar:"
echo "  scripts/grabar.sh /dev/ttyUSB0     # la 5 pulgadas (CH340). La P4 es"\
     " ttyACM0 y la 3,5 pulgadas ttyACM1"
echo "────────────────────────────────────────────────────────────"
