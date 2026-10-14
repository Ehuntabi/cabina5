#!/usr/bin/env sh
#
# grabar.sh — graba la pantalla de 5" en la placa, pero SOLO si lo que se va a
# grabar esta publicado (commit, tag y Release en GitHub).
#
# ── POR QUE EXISTE (16-oct-2026, el usuario: "esto no puede pasar") ──────────
# La P4 del banco quedo grabada con la v4.38 -- el trabajo de ese dia -- mientras
# que en GitHub la ultima Release era la v4.37. El CODIGO si estaba subido; lo
# que faltaba era la Release, que es lo que se descarga y lo que enseña el repo.
# La causa es de proceso: grabar es local e instantaneo, y publicar depende de
# que alguien se acuerde de lanzar release.sh, asi que el aparato se adelanta al
# repo sin que nadie lo note hasta que se mira.
#
# La solucion no es acordarse mas: es que GRABAR exija estar publicado. Este
# script es el camino comodo para grabar, y comprueba antes:
#
#   1. que el arbol este limpio (nada sin commitear),
#   2. que la version de esta copia (el tag, o el ultimo tag +0.1) TENGA tag,
#   3. que ese tag TENGA Release publicada en GitHub,
# y solo entonces compila y graba. Si algo falla, dice que falta y como
# arreglarlo (./release.sh X.Y "mensaje"), y NO graba nada.
#
# Para grabar a proposito algo sin publicar (una prueba en el banco) esta
# GRABAR_SIN_PUBLICAR=1, que salta los pasos 2 y 3 y AVISA de que lo que queda en
# la placa no esta en GitHub.
#
# Uso:  scripts/grabar.sh [puerto] [--rapido]
#         puerto    por defecto /dev/ttyUSB0 (la 5"; la P4 es ttyACM0)
#         --rapido  sin compilar: graba el build/ que ya hay
set -eu

AQUI="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$AQUI/.." && pwd)"
REPO_GH="Ehuntabi/cabina5"
IDF_EXPORT="${IDF_EXPORT:-$HOME/.espressif/esp-idf-5.5/export.sh}"
APP_BIN="build/cabina5.bin"

PUERTO="/dev/ttyUSB0"
RAPIDO=0
for a in "$@"; do
    case "$a" in
        --rapido) RAPIDO=1 ;;
        *) PUERTO="$a" ;;
    esac
done

cd "$REPO"

# ── 1) arbol limpio ─────────────────────────────────────────────────────────
git checkout dependencies.lock 2>/dev/null || true
SUCIO="$(git status --porcelain | grep -v 'dependencies.lock' || true)"
if [ -n "$SUCIO" ]; then
    echo "NO GRABO: hay cambios sin commitear. Lo que se graba tiene que estar"
    echo "         en GitHub, y sin commitear no lo esta:"
    printf '%s\n' "$SUCIO" | sed 's/^/           /'
    echo
    echo "         Commitea (y sube) antes de grabar."
    exit 1
fi

# ── 2) la version de esta copia, con la regla del CMakeLists ────────────────
DESC="$(git describe --tags --match 'v*.*' 2>/dev/null || true)"
if [ -z "$DESC" ]; then
    echo "NO GRABO: este arbol no tiene ningun tag v*.* del que sacar la version."
    exit 1
fi
case "$DESC" in
    *-*) VER="$(printf '%s' "$DESC" | awk -F'[.-]' '{ printf "v%s.%d", $1, $2 + 1 }')" ;;
    *)   VER="$DESC" ;;
esac
echo "[i] esta copia es la $VER   ($(git describe --tags --always --match 'v*.*'))"

if [ "${GRABAR_SIN_PUBLICAR:-0}" = "1" ]; then
    echo "AVISO: GRABAR_SIN_PUBLICAR=1 -- me salto la comprobacion de que $VER este"
    echo "       publicada. Lo que quede en la placa NO estara en GitHub."
else
    if ! git rev-parse -q --verify "refs/tags/$VER" >/dev/null; then
        echo
        echo "NO GRABO: la $VER NO tiene tag. El aparato se adelantaria al repo, que"
        echo "         es justo lo que paso el 16-oct-2026 (la P4 con la v4.38 grabada"
        echo "         y GitHub enseñando la v4.37)."
        echo
        echo "         Publica primero:   ./release.sh ${VER#v} \"mensaje\""
        exit 1
    fi
    if command -v gh >/dev/null 2>&1; then
        if ! gh release view "$VER" -R "$REPO_GH" >/dev/null 2>&1; then
            echo
            echo "NO GRABO: el tag $VER existe pero NO hay Release publicada en GitHub."
            echo "         (Pasa cuando release.sh creo el tag y luego fallo el push o no"
            echo "          estaba 'gh'.)"
            exit 1
        fi
        echo "[ok] $VER tiene tag y Release publicada"
    else
        echo "AVISO: no hay 'gh', asi que no puedo comprobar la Release de $VER."
        echo "       El tag esta; mira a mano que la Release exista antes de grabar."
    fi
fi

# ── 3) compilar y grabar ───────────────────────────────────────────────────
if [ "$RAPIDO" = "0" ]; then
    if [ ! -f "$IDF_EXPORT" ]; then
        echo "ERROR: no encuentro ESP-IDF en $IDF_EXPORT"; exit 1
    fi
    # shellcheck disable=SC1090
    # OJO con el "|| true": export.sh de ESP-IDF termina con un codigo
    # distinto de cero y con "set -e" eso mataba este script en silencio
    # (se quedo en la comprobacion y no compilaba nada, visto el 16-oct).
    . "$IDF_EXPORT" >/dev/null 2>&1 || true
    command -v idf.py >/dev/null 2>&1 || {
        echo "ERROR: ESP-IDF no ha cargado (idf.py no esta en el PATH)."
        exit 1
    }
    echo "[..] compilando"
    idf.py build
fi
if [ ! -f "$APP_BIN" ]; then
    echo "ERROR: no hay $APP_BIN (compila primero, sin --rapido)"; exit 1
fi
V_BIN="$(python3 - "$APP_BIN" <<'PYBIN'
import sys
with open(sys.argv[1],'rb') as f: d=f.read(0x120)
print(d[0x20+16:0x20+48].split(b'\x00')[0].decode('ascii','replace'))
PYBIN
)"
echo "[i] el binario lleva dentro: $V_BIN   (esta copia es $VER)"
if [ "$V_BIN" != "$VER" ]; then
    echo "ERROR: el .bin dice '$V_BIN' y esta copia es '$VER'."
    echo "       Casi seguro es un build/ viejo: borra build/ y repite (asi lo hace"
    echo "       release.sh, y por eso lo que se publica es lo que se cree que es)."
    exit 1
fi

echo "[..] grabando en $PUERTO"
idf.py -p "$PUERTO" flash

# LAS DOS MITADES DE LA OTA, con el mismo firmware. Esta placa tiene ota_0 en
# 0x20000 y ota_1 en 0x410000: `idf.py flash` escribe la que toca AHORA y la
# otra se queda con la version de antes. Si despues la OTA falla y arranca la
# copia de atras, la pantalla volveria a una version vieja sin que nadie lo
# relacione. Se graban las dos para que no haya sorpresa.
echo "[..] la otra mitad de la OTA (0x410000), para que no quede una version vieja"
python -m esptool --chip esp32s3 -p "$PUERTO" -b 460800 \
    write_flash 0x410000 "$APP_BIN" | tail -3

echo
echo "GRABADO $V_BIN en $PUERTO (las dos mitades de la OTA)."
if [ "${GRABAR_SIN_PUBLICAR:-0}" = "1" ]; then
    echo "OJO: es una copia SIN PUBLICAR (GRABAR_SIN_PUBLICAR=1). GitHub no la tiene."
else
    echo "Y publicado: release.sh la subio con su tag y su Release."
fi
