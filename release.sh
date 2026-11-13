#!/usr/bin/env bash
#
# release.sh — publica una versión del satélite de 5" (cabina5).
#
# Uso:  ./release.sh X.Y  ["mensaje del tag"]
#   ej: ./release.sh 0.1 "Primer arranque en la placa: pantalla, táctil y UDP"
#
# Qué hace:
#   0. Comprueba ANTES de tocar nada (ni tag ni build) que están 'gh', el
#      entorno de ESP-IDF, el toolchain Xtensa y la clave REAL del AP
#      (main/wifi_credentials.h): abortar con el tag ya creado deja un tag
#      huérfano (16-oct-2026).
#   1. Exige el árbol limpio (lo que se publica tiene que ser lo que hay).
#   2. Crea el tag vX.Y, o reutiliza el que ya esté EN ESTE MISMO commit.
#   3. Compila con reconfigure (la versión se calcula al CONFIGURAR, no al
#      compilar: sin reconfigure se publicaría un binario con la versión vieja).
#   4. VERIFICA que la versión embebida en el .bin coincide con el tag.
#   5. Sube código y tag, y publica la Release en GitHub con el binario, con el
#      nombre EXACTO cabina5-vX.Y.bin. Sin 'gh' no se publica: se exige ANTES de
#      crear el tag, y al final se comprueba que la Release lleve ESE asset (si la
#      Release ya existía, también: antes decía "no la toco" y una Release sin
#      .bin acababa en "PUBLICADO").
#   6. Deja el .bin en ~/joint-releases SOLO cuando la Release está comprobada, y
#      retira entonces el de la versión anterior. Antes se borraba ANTES del
#      push: si el push fallaba se perdía el bueno y quedaba uno que nadie podía
#      descargar (auditoría del 16-oct-2026).
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
# El nombre EXACTO del asset que tiene que quedar colgado de la Release: es el
# que scripts/grabar.sh exige antes de grabar (16-oct-2026). Se usa aquí para
# comprobarlo también cuando la Release YA existía.
ASSET="cabina5-$TAG.bin"

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

# ── ¿estamos en una rama? (un HEAD desprendido no se puede publicar) ────────
# `git push origin "$(git branch --show-current)"` con HEAD desprendido deja el
# refspec vacio y el push muere con un error críptico ("refspec inválido: ''")
# DESPUÉS de haber creado el tag. Se mira aquí, con lo demás (16-oct-2026).
if [ -z "$(git branch --show-current)" ]; then
    echo "ERROR: HEAD está desprendido (detached): no sé a qué rama subir el código."
    echo "       Ponte en la rama de trabajo (git switch main) y vuelve a lanzarlo."
    exit 1
fi

# ── 0c) el entorno y la clave del AP, ANTES DE CREAR EL TAG ────────────────
# Todo lo que puede faltar se mira AQUI, antes del tag: abortar con el tag ya
# creado deja un tag huerfano en el repo (y en la 3,5" pasaba: el toolchain se
# comprobaba despues de crearlo -- auditoria del 16-oct-2026).
if [ ! -f "$IDF_EXPORT" ]; then
    echo "ERROR: no encuentro ESP-IDF en $IDF_EXPORT"; exit 1
fi
if [ ! -d "$XTENSA_BIN" ]; then
    echo "ERROR: no encuentro el toolchain Xtensa en $XTENSA_BIN"
    echo "       Mira qué versión hay: ls ~/.espressif/tools/xtensa-esp-elf/"
    exit 1
fi

# ── LA CLAVE DEL AP, ANTES DE COMPILAR NADA (16-oct-2026; cambiado 13-nov) ──
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
# LO CONTRARIO DESDE EL 13-NOV-2026: el binario publico NO LLEVA CREDENCIALES.
#
# Lo de arriba (exigir la cabecera con los datos reales) era peor que inutil: el
# que compila y publica es ESTE script, en la maquina donde vive la cabecera con
# la clave de verdad, asi que TODAS las Releases publicadas hasta la v1.87 (88
# versiones) llevaban la clave del AP de la P4 dentro del .bin, y el repo es
# PUBLICO: cualquiera que lo descargue y haga `strings` entra al AP de la
# autocaravana y al portal (viajes, SD y OTA de la P4).
#
# Ahora la version se compila con la PLANTILLA (sin secretos) y, antes de subir
# nada, se comprueba que el binario no contenga las credenciales reales. La
# cabecera local se aparta y se restaura sola al terminar (trap), para que el
# arbol de trabajo quede como estaba.
#
# Una placa nueva que se grabe con la version publicada arranca con los valores
# de la plantilla y se configura desde su propia pantalla (Ajustes -> red), que
# es como se cambian ya hoy; lo que no se puede es repartir la clave en un .bin.
LOCAL_CRED="main/wifi_credentials.h"
CRED_TMP=""
CRED_REALES=""
CRED_HABIA=0        # 1 si la cabecera local existia al empezar
restaurar_cred() {
    if [ -n "$CRED_TMP" ] && [ -f "$CRED_TMP" ]; then
        # CON mtime NUEVA (cp, no mv). Con `mv` el fichero volvia con la fecha de
        # ANTES del build y ninja no recompilaba los .c que lo incluyen: paso en
        # esta maquina el 13-nov y el binario local salio MEZCLADO (plantilla en
        # p4_creds.c y clave real en udp_rx.c).
        cp -f "$CRED_TMP" "$LOCAL_CRED" && rm -f "$CRED_TMP"
    elif [ "${CRED_HABIA:-0}" = "0" ]; then
        rm -f "$LOCAL_CRED"   # no habia ninguna: que el arbol quede como estaba
    fi
}
trap restaurar_cred EXIT INT TERM
if [ -f "$LOCAL_CRED" ]; then
    CRED_HABIA=1
    CRED_TMP="$(mktemp)"
    cp "$LOCAL_CRED" "$CRED_TMP"
    if cmp -s "$LOCAL_CRED" main/wifi_credentials.h.example; then
        # Ya es la plantilla: no hay secreto que buscar (si no, el 4b se abortaba
        # a si mismo buscando CAMBIAME, que es justo lo que lleva el binario).
        CRED_REALES=""
        echo "[i] la cabecera local YA es la plantilla: no hay credenciales que comprobar"
    else
        # Lo que NO puede salir en el .bin: las claves reales de esa cabecera.
        CRED_REALES="$(sed -n 's/^#define[[:space:]]\{1,\}\(WIFI_CRED_PASS\|PORTAL_CRED_PASS\)[[:space:]]\{1,\}"\(.*\)".*/\2/p' "$LOCAL_CRED")"
        echo "[i] cabecera local apartada: la version se compila con la plantilla"
    fi
fi
cp main/wifi_credentials.h.example "$LOCAL_CRED"
if cmp -s "$LOCAL_CRED" main/wifi_credentials.h.example; then
    echo "[ok] compilando con la plantilla (sin credenciales dentro)"
fi


# sdkconfig se reescribe solo en cada build, asi que se restaura ANTES de mirar
# si el arbol esta limpio. OJO con hacerlo en UNA sola orden: en cabina5
# dependencies.lock NO esta versionado (esta en .gitignore) y `git checkout a b`
# aborta ENTERO si un pathspec no existe... con el `|| true` eso pasaba
# desapercibido y sdkconfig NO se restauraba, asi que cualquier churn del build
# dejaba el arbol "sucio" y el script se negaba a grabar/publicar (auditoria del
# 16-oct-2026). Dos ordenes separadas: cada una hace lo que puede.
git checkout -- sdkconfig 2>/dev/null || true
git checkout -- dependencies.lock 2>/dev/null || true

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
export IDF_TARGET=esp32s3
# shellcheck disable=SC1090
. "$IDF_EXPORT" >/dev/null 2>&1
export PATH="$XTENSA_BIN:$PATH"
command -v xtensa-esp32s3-elf-gcc >/dev/null || { echo "ERROR: el compilador del S3 no está en el PATH"; exit 1; }

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

# ── 4b) el binario NO puede llevar credenciales reales (13-nov-2026) ────────
if [ -n "$CRED_REALES" ]; then
    while IFS= read -r clave; do
        [ -n "$clave" ] || continue
        if strings "$APP_BIN" | grep -qF -- "$clave"; then
            echo "ABORTADO: el binario lleva una credencial real dentro."
            echo "          Buscada: la clave de $LOCAL_CRED (WIFI_CRED_PASS/PORTAL_CRED_PASS)."
            echo "          No se publica un firmware que reparte la clave del AP."
            exit 1
        fi
    done <<EOF
$CRED_REALES
EOF
    echo "[ok] el binario no lleva credenciales reales"
fi

# El binario que se sube a la Release se deja en build/ (gitignored) y NO se
# toca ~/joint-releases todavía: esa carpeta es "lo que se lleva a la
# autocaravana" y tiene que contener lo ULTIMO PUBLICADO. Antes se borraba la
# versión anterior de ahí y se copiaba la nueva AQUÍ, antes del push: si el push
# o la Release fallaban (pasó el 16-oct-2026 con un 5xx de GitHub), quedaba un
# binario que nadie podía descargar y el bueno ya estaba borrado. Ahora la copia
# y el borrado de la vieja se hacen al final (paso 6), cuando la Release está
# comprobada (auditoría del 16-oct-2026).
SUBIR="build/$ASSET"
cp "$APP_BIN" "$SUBIR"
OUT="$RELDIR/$ASSET"
echo "[ok] binario para subir en $SUBIR"

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
    echo "          --notes-from-tag \"$SUBIR\""
    exit 1
fi
if gh release view "$TAG" -R "$REPO" >/dev/null 2>&1; then
    # La Release YA existía: antes esto decía "[ok] no la toco" y seguía, así que
    # una Release sin su .bin (creada a mano, o con el asset borrado) acababa en
    # "PUBLICADO" -- y luego scripts/grabar.sh se negaba a grabar, sin decir por
    # qué (auditoría del 16-oct-2026). Ahora se comprueba su asset, y si falta se
    # sube.
    echo "[ok] la Release $TAG ya existe, compruebo que tenga su $ASSET"
    ASSETS="$(gh release view "$TAG" -R "$REPO" --json assets --jq '.assets[].name' 2>/dev/null || true)"
    if ! printf '%s\n' "$ASSETS" | grep -qxF "$ASSET"; then
        echo "[..] le falta $ASSET (tiene: ${ASSETS:-nada}); lo subo"
        gh release upload "$TAG" -R "$REPO" "$SUBIR" || true
        ASSETS="$(gh release view "$TAG" -R "$REPO" --json assets --jq '.assets[].name' 2>/dev/null || true)"
    fi
    if ! printf '%s\n' "$ASSETS" | grep -qxF "$ASSET"; then
        echo
        echo "ERROR: la Release $TAG existe pero NO tiene '$ASSET' (visto:"
        echo "       ${ASSETS:-nada}). El firmware está en $SUBIR; súbelo a mano:"
        echo "  gh release upload $TAG -R $REPO \"$SUBIR\""
        exit 1
    fi
    echo "[ok] la Release $TAG ya tenía su $ASSET"
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
         "$SUBIR"; then
        :
    else
        echo "AVISO: GitHub no ha creado la Release (¿un 5xx suyo? paso el 16-oct-2026)."
    fi
    # SE COMPRUEBA, no se da por hecho: si la Release no existe, el tag esta
    # subido y el .bin en build/, pero nadie puede descargarlo -- y
    # scripts/grabar.sh no dejaria grabar la placa. Mejor decirlo aqui, con el
    # comando exacto para reintentarlo.
    if ! gh release view "$TAG" -R "$REPO" >/dev/null 2>&1; then
        echo
        echo "LA RELEASE $TAG NO ESTA PUBLICADA (el tag si). Reintenta con:"
        echo "  gh release create $TAG -R $REPO --title \"cabina5 $TAG\" \\"
        echo "     --notes-file $NOTAS_TMP \"$SUBIR\""
        exit 1
    fi
    # Y ADEMÁS tiene que llevar SU .bin, con el nombre exacto: una Release sin
    # asset (o con el .bin de otra versión) no la puede usar nadie, y
    # scripts/grabar.sh la rechazaría (auditoría del 15/16-oct-2026). GitHub tarda
    # un momento en procesar lo que acaba de subir `gh release create`, así que se
    # hacen 5 comprobaciones con 2 s de espera (unos 10 s en total) antes de darla
    # por incompleta.
    ASSETS=""
    for _ in 1 2 3 4 5; do
        ASSETS="$(gh release view "$TAG" -R "$REPO" --json assets --jq '.assets[].name' 2>/dev/null || true)"
        if printf '%s\n' "$ASSETS" | grep -qxF "$ASSET"; then break; fi
        sleep 2
    done
    if ! printf '%s\n' "$ASSETS" | grep -qxF "$ASSET"; then
        echo
        echo "ERROR: el tag $TAG quedó subido, pero la Release está INCOMPLETA: no"
        echo "       tiene '$ASSET' (visto: ${ASSETS:-ninguno}). El firmware está en"
        echo "       $SUBIR; súbelo a mano:"
        echo "  gh release upload $TAG -R $REPO \"$SUBIR\""
        exit 1
    fi
    echo "[ok] Release $TAG publicada con su $ASSET ($(printf '%s' "$ASSETS" | tr '\n' ' '))"
fi

# ── 6) ahora sí: ~/joint-releases solo con LO PUBLICADO ─────────────────────
# Se llega aquí con el push y la Release ya comprobados, así que lo que se copia
# es descargable. Se borra la versión anterior de esa carpeta justo después de
# copiar la nueva (antes se borraba ANTES del push: auditoría del 16-oct-2026).
mkdir -p "$RELDIR"
rm -f "$RELDIR"/cabina5-v*.bin
cp "$SUBIR" "$OUT"
echo "[ok] $OUT ($(sha256sum "$OUT" | awk '{print $1}'))"
echo "[ok] $RELDIR actualizado: solo el .bin de $TAG"

echo
echo "────────────────────────────────────────────────────────────"
echo "PUBLICADO $TAG. Solo queda grabar:"
echo "  scripts/grabar.sh /dev/ttyUSB0     # la 5 pulgadas (CH340). La P4 es"\
     " ttyACM0 y la 3,5 pulgadas ttyACM1"
echo "────────────────────────────────────────────────────────────"
