#!/usr/bin/env bash
#
# sello_version.sh — el sello (tag, commit y FECHA DEL COMMIT) no puede mentir.
#
# 22-nov-2026: la pantalla ensenaba app_desc->date/time (la fecha de compilacion
# de ese componente), que se queda congelada: con codigo del 22 dentro, la placa
# decia "14 NOV" y el dueno lo leyo como "no esta en la ultima version". El sello
# nuevo sale de scripts/gen_version_header.cmake, y esto prueba ESE script y su
# enganche al build en un repo de usar y tirar, caso por caso, porque un rotulo
# que miente es peor que no tenerlo:
#
#   1.  arbol justo en el tag     -> el tag limpio, sin "sucio"
#   2.  commits por encima        -> "vX.Y-N-g<hash>" (se ve que no es el tag)
#   2b. un tag que NO es version  -> se ignora (sdkconfig-known-good-*)
#   3.  fuente tocada             -> DIRTY=1 (se avisa en la pantalla)
#   3b. fuente NUEVA sin add      -> DIRTY=1 (los GLOB la compilan)
#   4.  solo dependencies.lock   -> DIRTY=0 (lo reescribe el build; no es codigo)
#   5.  nada cambia               -> no reescribe el fichero (no recompila de balde)
#   5b. cambia el commit          -> SI reescribe (el fallo original era congelarse)
#   6.  copia sin .git + stamp    -> usa el sello que dejo el script del build
#   6b. stamp a medias            -> el generador FALLA (no inventa "sin-fecha")
#   7.  copia sin .git y sin stamp-> "sin-git", y NO hereda el repo de encima
#   8.  nadie vuelve a pintar app->date/__DATE__ (la fecha congelada)
#   9.  las pantallas ENSENAN el sello, y el describe con -N-g no sale en pantalla
#   10. los dos CMakeLists de verdad lo enganchan (y el espejo recibe el stamp)
#   11. un mini proyecto CMake: crear un tag RECONFIGURA solo (el tag no toca el
#       indice; sin vigilar .git/refs/tags el sello se quedaba con el tag de antes)
#
# Uso: bash test/sello_version.sh   (desde el repo de la cabina; si el repo de la
# P4 esta al lado, comprueba ademas que las dos copias del generador son iguales:
# una pantalla y otra no pueden contar cosas distintas con el mismo build).
set -uo pipefail
AQUI="$(cd "$(dirname "$0")/.." && pwd)"
GEN="$AQUI/scripts/gen_version_header.cmake"
P4="$(cd "$AQUI/.." 2>/dev/null && pwd)/p4"

fallos=0
ok()  { printf '  ok    %s\n' "$1"; }
mal() { printf '  FALLO %s\n' "$1"; fallos=$((fallos+1)); }

# Quita las lineas que son COMENTARIO: un `app->date` dentro de un comentario no
# pinta nada, y un `JB_BUILD_STAMP` comentado tampoco sirve de nada. Vale para
# `grep -rn` (prefijo "fichero:linea:") y para `grep -n` de un solo fichero
# (prefijo "linea:"): el filtro de antes solo entendia el primero y dejaba pasar
# comentarios al comprobar el About (lo cazo el arbitro, 22-nov-2026).
solo_codigo() { grep -vE '(^|:)[0-9]+: *(\*|/\*|//)'; }

command -v cmake >/dev/null || { echo "no hay cmake: no puedo probar el sello"; exit 1; }
[ -f "$GEN" ] || { echo "no encuentro $GEN"; exit 1; }

# ── 0) las dos copias del generador son el mismo fichero ────────────────────
if [ -d "$P4" ]; then
    if [ ! -f "$P4/scripts/gen_version_header.cmake" ]; then
        mal "existe el repo de la P4 pero le falta scripts/gen_version_header.cmake"
    elif diff -q "$GEN" "$P4/scripts/gen_version_header.cmake" >/dev/null; then
        ok "la P4 y la cabina5 usan el MISMO generador del sello"
    else
        mal "el generador del sello se ha separado entre la P4 y la cabina5 (cada pantalla diria una cosa)"
    fi
fi

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
R="$T/repo"

git init -q "$R"
git -C "$R" config user.email sello@joint
git -C "$R" config user.name Sello
printf 'int a;\n' > "$R/a.c"
printf 'lock v1\n' > "$R/dependencies.lock"
git -C "$R" add a.c dependencies.lock
GIT_AUTHOR_DATE="2026-01-01T09:00:00+0100" GIT_COMMITTER_DATE="2026-01-01T09:00:00+0100" \
    git -C "$R" commit -q -m "uno"
git -C "$R" tag v7.1

gen() {   # $1=repo  $2=salida  [$3=version_stamp.txt]
    cmake -DREPO="$1" -DOUT="$2" -DSTAMP_FILE="${3:-$T/no-existe.txt}" -P "$GEN" >/dev/null 2>&1
}

tag_de()    { sed -n 's/^#define JB_VERSION_TAG  *"\(.*\)"$/\1/p' "$1"; }
commit_de() { sed -n 's/^#define JB_COMMIT  *"\(.*\)"$/\1/p' "$1"; }
fecha_de()  { sed -n 's/^#define JB_BUILD_STAMP  *"\(.*\)"$/\1/p' "$1"; }
dirty_de()  { sed -n 's/^#define JB_BUILD_DIRTY  *\([01]\)$/\1/p' "$1"; }

# ── 1) arbol justo en el tag ────────────────────────────────────────────────
if gen "$R" "$T/s1.h"; then
    [ "$(tag_de "$T/s1.h")" = "v7.1" ] \
        && ok "en el tag: el sello es el tag limpio (v7.1)" \
        || mal "en el tag el sello dice '$(tag_de "$T/s1.h")' y deberia decir v7.1"
    [ "$(commit_de "$T/s1.h")" = "$(git -C "$R" rev-parse --short HEAD)" ] \
        && ok "en el tag: el commit es el de HEAD" \
        || mal "en el tag el commit no es el de HEAD"
    [ "$(fecha_de "$T/s1.h")" = "01-ene-2026 09:00" ] \
        && ok "la fecha es la DEL COMMIT (01-ene-2026 09:00), no la de hoy" \
        || mal "la fecha del sello es '$(fecha_de "$T/s1.h")' y deberia ser la del commit (01-ene-2026 09:00)"
    [ "$(dirty_de "$T/s1.h")" = "0" ] \
        && ok "arbol limpio: no se marca sucio" \
        || mal "arbol limpio y el sello dice sucio"
else
    mal "el generador no escribio el sello en un repo normal"
fi

# ── 2) commits por encima del tag ───────────────────────────────────────────
printf 'int b;\n' > "$R/b.c"
git -C "$R" add b.c
GIT_AUTHOR_DATE="2026-01-02T10:00:00+0100" GIT_COMMITTER_DATE="2026-01-02T10:00:00+0100" \
    git -C "$R" commit -q -m "dos"
if gen "$R" "$T/s2.h"; then
    case "$(tag_de "$T/s2.h")" in
        v7.1-1-g*) ok "por encima del tag se ve 'v7.1-1-g<hash>' (no dice que sea v7.1)" ;;
        *) mal "por encima del tag el sello dice '$(tag_de "$T/s2.h")'" ;;
    esac
else
    mal "el generador no escribio el sello con commits por encima"
fi

# ── 2b) un tag que NO es una version no puede colarse ───────────────────────
# La P4 tiene tags `sdkconfig-known-good-*` desde julio; si uno queda mas cerca
# que el ultimo v*.*, el sello diria eso como "version".
git -C "$R" tag sdkconfig-known-good-2026-11-22
gen "$R" "$T/s2b.h"
case "$(tag_de "$T/s2b.h")" in
    v7.1-1-g*) ok "un tag sdkconfig-known-good-* NO se cuela como version" ;;
    *) mal "el sello se trago un tag que no es version: '$(tag_de "$T/s2b.h")'" ;;
esac
git -C "$R" tag -d sdkconfig-known-good-2026-11-22 >/dev/null

# ── 3) fuente tocada sin commitear ──────────────────────────────────────────
printf 'int c;\n' >> "$R/a.c"
gen "$R" "$T/s3.h"
[ "$(dirty_de "$T/s3.h")" = "1" ] \
    && ok "fuente sin commitear: DIRTY=1 (la pantalla lo dice)" \
    || mal "fuente sin commitear y el sello NO lo marca"
git -C "$R" checkout -q -- a.c

# ── 3b) fuente NUEVA sin "git add" (los GLOB la compilan) ───────────────────
printf 'int nuevo;\n' > "$R/nuevo.c"
gen "$R" "$T/s3b.h"
[ "$(dirty_de "$T/s3b.h")" = "1" ] \
    && ok "un .c nuevo sin git add: DIRTY=1 (se compila y no esta en el commit)" \
    || mal "un .c nuevo sin git add NO marca el sello como sucio"
rm -f "$R/nuevo.c"
printf 'notas\n' > "$R/notas.txt"
gen "$R" "$T/s3c.h"
[ "$(dirty_de "$T/s3c.h")" = "0" ] \
    && ok "un fichero suelto que no es fuente (.txt): DIRTY=0" \
    || mal "un .txt suelto marca el sello como sucio (el aviso perderia sentido)"
rm -f "$R/notas.txt"

# ── 4) solo dependencies.lock (lo reescribe el build) ───────────────────────
printf 'lock v2 (ruta de esta maquina)\n' > "$R/dependencies.lock"
gen "$R" "$T/s4.h"
[ "$(dirty_de "$T/s4.h")" = "0" ] \
    && ok "solo dependencies.lock: DIRTY=0 (no es codigo, lo reescribe el build)" \
    || mal "dependencies.lock marca el sello como sucio (todos los builds dirian 'sin commitear')"
git -C "$R" checkout -q -- dependencies.lock

# ── 5) nada cambia -> no se reescribe (no recompila de balde) ───────────────
gen "$R" "$T/s5a.h"
ANTES="$(stat -c %Y "$T/s5a.h" 2>/dev/null || echo mal)"
case "$ANTES" in
    ''|*[!0-9]*) mal "no puedo leer la fecha del fichero (stat raro): el caso 5 no vale" ;;
    *)
        sleep 1
        gen "$R" "$T/s5a.h"
        DESPUES="$(stat -c %Y "$T/s5a.h" 2>/dev/null || echo mal)"
        if [ "$ANTES" = "$DESPUES" ]; then
            ok "sin cambios en el git, el fichero no se reescribe"
        else
            mal "el generador reescribe el sello aunque no cambie nada (recompilaria de balde)"
        fi ;;
esac

# ── 5b) cambia el commit -> SI se reescribe (no se congela) ─────────────────
# Este es el fallo original: un sello que se queda en el primer build. La
# mutacion "si ya existe el fichero, sal" tiene que cantar aqui.
gen "$R" "$T/s5b.h"
TAG_ANTES="$(tag_de "$T/s5b.h")"
printf 'int d;\n' > "$R/d.c"
git -C "$R" add d.c
GIT_AUTHOR_DATE="2026-01-03T11:00:00+0100" GIT_COMMITTER_DATE="2026-01-03T11:00:00+0100" \
    git -C "$R" commit -q -m "tres"
gen "$R" "$T/s5b.h"
TAG_DESPUES="$(tag_de "$T/s5b.h")"
if [ "$TAG_ANTES" != "$TAG_DESPUES" ] && [ "$(commit_de "$T/s5b.h")" = "$(git -C "$R" rev-parse --short HEAD)" ]; then
    ok "al cambiar el commit, el sello se reescribe ($TAG_ANTES -> $TAG_DESPUES)"
else
    mal "el sello NO se actualiza al cambiar el commit ('$TAG_ANTES' -> '$TAG_DESPUES')"
fi

# ── 6) copia sin .git, con el sello que dejo el script del build ────────────
COPIA="$T/copia"
mkdir -p "$COPIA"
printf 'v9.9-2-gabcdef\nabcdef\n03-mar-2026_10:00\n1\n' > "$COPIA/version_stamp.txt"
if gen "$COPIA" "$T/s6.h" "$COPIA/version_stamp.txt"; then
    [ "$(tag_de "$T/s6.h")" = "v9.9-2-gabcdef" ] && [ "$(fecha_de "$T/s6.h")" = "03-mar-2026 10:00" ] \
        && [ "$(dirty_de "$T/s6.h")" = "1" ] \
        && ok "copia sin .git: usa el sello de version_stamp.txt" \
        || mal "copia sin .git: el sello no es el de version_stamp.txt ('$(tag_de "$T/s6.h")', '$(fecha_de "$T/s6.h")', sucio $(dirty_de "$T/s6.h"))"
else
    mal "copia sin .git: el generador no escribio nada"
fi

# ── 6b) un stamp a medias NO se convierte en "sin-fecha" en silencio ────────
printf 'v9.9\nabcdef\n03-mar-2026_10:00\n' > "$COPIA/version_stamp.txt"   # 3 lineas
if gen "$COPIA" "$T/s6b.h" "$COPIA/version_stamp.txt"; then
    mal "un version_stamp.txt de 3 lineas deberia hacer FALLAR el generador"
else
    ok "version_stamp.txt incompleto: el generador falla (no inventa 'sin-fecha')"
fi
printf 'v9.9\nabcdef\n03-mar-2026_10:00\n2\n' > "$COPIA/version_stamp.txt" # sucio=2
if gen "$COPIA" "$T/s6b.h" "$COPIA/version_stamp.txt"; then
    mal "un 'sucio' que no es 0/1 deberia hacer FALLAR el generador"
else
    ok "version_stamp.txt con sucio invalido: el generador falla"
fi

# ── 7) copia sin .git Y sin sello, colgando DEBAJO de un repo ───────────────
# (el arbol de .scratch/p4 cuelga de ~/joint, que no es repo, pero el dia que lo
# sea no puede heredar el commit de OTRO arbol)
SUB="$R/arbol-de-scratch"
mkdir -p "$SUB"
gen "$SUB" "$T/s7.h"
[ "$(tag_de "$T/s7.h")" = "sin-git" ] \
    && ok "sin .git no hereda el repo de encima: dice 'sin-git'" \
    || mal "sin .git heredo el repo de encima: dice '$(tag_de "$T/s7.h")'"

# ── 8) nadie vuelve a pintar la fecha CONGELADA de app_desc ─────────────────
# El generador puede estar perfecto y la pantalla seguir ensenando app->date
# (que era el fallo original: "14 NOV" con codigo del 22 dentro). Se busca en el
# codigo de la UI de los dos repos; los comentarios que lo explican no cuentan.
FUENTES=("$AQUI/main")
[ -d "$P4/main" ] && FUENTES+=("$P4/main")
CONGELADA="$(grep -rnE 'app(_desc)?->(date|time)\b|esp_app_get_description\(\)->(date|time)\b|__DATE__|__TIME__' \
                 "${FUENTES[@]}" 2>/dev/null | solo_codigo || true)"
if [ -z "$CONGELADA" ]; then
    ok "nadie pinta app->date/app->time/__DATE__ (la fecha que se queda vieja)"
else
    mal "hay codigo que pinta la fecha congelada de app_desc:"
    printf '%s\n' "$CONGELADA" | head -5 | sed 's/^/          /'
fi

# ── 8b) y ninguna fecha escrita A MANO en la UI ─────────────────────────────
# El arbitro (22-nov-2026) probo a comentar el sello y pintar el literal
# "v1.89  14 NOV 2026 12:34": compilaba y el test no lo veia. Una fecha de
# verdad se pinta con JB_BUILD_STAMP, nunca escrita en el codigo. Se buscan
# fechas DENTRO DE UNA CADENA (entre comillas): los comentarios del codigo
# llevan fechas sueltas ("10-nov-2026") y esas no se pintan.
FECHAS_A_MANO="$(grep -rniE '"[^"]*([0-9]{1,2}[-/ ](ene|feb|mar|abr|may|jun|jul|ago|sep|oct|nov|dic|jan|apr|aug|dec)[-/ ][0-9]{4}|(ene|feb|mar|abr|may|jun|jul|ago|sep|oct|nov|dic|jan|apr|aug|dec)[ ]+[0-9]{1,2},?[ ]+[0-9]{4})[^"]*"' \
                     "${FUENTES[@]}" 2>/dev/null | solo_codigo || true)"
if [ -z "$FECHAS_A_MANO" ]; then
    ok "no hay fechas escritas a mano en la UI (solo sale JB_BUILD_STAMP)"
else
    mal "hay una fecha escrita a mano en la UI (la del fallo original):"
    printf '%s\n' "$FECHAS_A_MANO" | head -5 | sed 's/^/          /'
fi

# ── 9) las pantallas ENSENAN el sello, y sin "-N-g" ─────────────────────────
# Sin esto, borrar la linea del sello compilaria igual y el test seguiria verde:
# el rotulo volveria a quedarse sin la verdad que hacia falta. Y la regla del
# 13-sep sigue: el describe con "-N-g" no sale en pantalla (se lee como una
# version que se contradice con el numero grande). -Hn para que cada linea lleve
# "fichero:linea:" y el filtro de comentarios valga tambien con un solo fichero.
for pareja in "$AQUI/main/ui/view_ajustes.c:cabina5 (Ajustes)" \
              "$P4/main/ui/settings/settings_about.c:P4 (Acerca de)" \
              "$P4/main/portal/ota_update.c:P4 (portal /ota)" \
              "$P4/main/main.c:P4 (log de arranque)"; do
    FICHERO="${pareja%:*}"
    NOMBRE="${pareja#*:}"
    [ -f "$FICHERO" ] || continue
    if grep -Hn 'JB_BUILD_STAMP' "$FICHERO" 2>/dev/null | solo_codigo | grep -q .; then
        ok "$NOMBRE ensena el sello (JB_BUILD_STAMP)"
    else
        mal "$NOMBRE NO ensena el sello: el rotulo se quedaria sin la fecha del commit"
    fi
    if grep -Hn 'JB_VERSION_TAG' "$FICHERO" 2>/dev/null | solo_codigo | grep -q .; then
        mal "$NOMBRE pinta JB_VERSION_TAG (el describe 'vX.Y-N-g...'): se lee como dos versiones que se contradicen"
    fi
done

# ── 10) los dos CMakeLists de verdad lo enganchan ──────────────────────────
# Si alguien borra la llamada, la compilacion falla por falta de la cabecera...
# salvo que borre tambien el #include, y entonces el rotulo se queda sin sello
# sin que nada se queje. Se exige la invocacion REAL (no un comentario) y el
# respaldo del espejo (-DSTAMP_FILE).
for repo in "$AQUI" "$P4"; do
    CL="$repo/CMakeLists.txt"
    [ -f "$CL" ] || continue
    NOMBRE="$(basename "$repo")"
    if grep -qE '^[[:space:]]*-P .*gen_version_header\.cmake' "$CL"; then
        ok "$NOMBRE: el configure invoca gen_version_header.cmake"
    else
        mal "$NOMBRE: CMakeLists.txt ya NO invoca gen_version_header.cmake (o solo lo nombra en un comentario)"
    fi
    if grep -q -- '-DSTAMP_FILE=' "$CL"; then
        ok "$NOMBRE: el sello admite el respaldo del espejo (version_stamp.txt)"
    else
        mal "$NOMBRE: se ha perdido -DSTAMP_FILE (el build en copia diria 'sin-git')"
    fi
    if grep -q 'CMAKE_BINARY_DIR' "$repo/main/CMakeLists.txt" 2>/dev/null; then
        ok "$NOMBRE: main/ tiene en cuenta el directorio donde se genera el sello"
    else
        mal "$NOMBRE: main/CMakeLists.txt no incluye \${CMAKE_BINARY_DIR} (no encontraria version_build.h)"
    fi
done

# ── 11) un tag RECONFIGURA solo (el tag no toca HEAD ni el indice) ─────────
# Esto prueba el CONFIGURE_DEPENDS de verdad, con CMake. Se configura con un
# commit POR ENCIMA del tag (describe "v3.1-1-g..."), y despues se crea el tag
# v3.2 SOBRE ESE MISMO COMMIT (sin commit nuevo: asi lo que dispara el
# reconfigure solo puede ser la ref del tag) y se compila. Sin vigilar
# .git/refs/tags, el sello se quedaba con "v3.1-1-g..." para siempre.
MINI="$T/mini"
mkdir -p "$MINI"
(
    cd "$MINI" || exit 1
    git init -q .
    git config user.email sello@joint
    git config user.name Sello
    printf 'x\n' > a.txt
    git add a.txt
    git commit -q -m uno
    git tag v3.1
    printf 'y\n' >> a.txt
    git add a.txt
    git commit -q -m dos
    cat > gen.cmake <<'CM'
execute_process(COMMAND git -C "${REPO}" describe --tags --always --match "v*.*"
                OUTPUT_VARIABLE TAG OUTPUT_STRIP_TRAILING_WHITESPACE)
file(WRITE "${OUT}" "${TAG}\n")
CM
    cat > CMakeLists.txt <<'CM'
cmake_minimum_required(VERSION 3.16)
project(prueba_sello NONE)
execute_process(COMMAND ${CMAKE_COMMAND}
    -DREPO=${CMAKE_CURRENT_LIST_DIR}
    -DOUT=${CMAKE_BINARY_DIR}/sello.txt
    -P ${CMAKE_CURRENT_LIST_DIR}/gen.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/.git/HEAD"
    "${CMAKE_CURRENT_LIST_DIR}/.git/index"
    "${CMAKE_CURRENT_LIST_DIR}/.git/refs/heads"
    "${CMAKE_CURRENT_LIST_DIR}/.git/refs/tags"
    "${CMAKE_CURRENT_LIST_DIR}/.git/packed-refs")
add_custom_target(todos ALL)
CM
    cmake -S . -B build >/dev/null 2>&1 || exit 1
    case "$(cat build/sello.txt 2>/dev/null)" in
        v3.1-1-g*) ;;
        *) exit 1 ;;
    esac
    # SOLO el tag (mismo commit): tiene que reconfigurar y pasar a v3.2.
    git tag v3.2
    cmake --build build >/dev/null 2>&1 || exit 1
    [ "$(cat build/sello.txt 2>/dev/null)" = "v3.2" ] || exit 1
)
if [ $? -eq 0 ]; then
    ok "crear un tag (sin commit) dispara el reconfigure y el sello pasa a v3.2"
else
    mal "crear un tag NO actualiza el sello: faltan .git/refs/tags/.git/refs/heads en CONFIGURE_DEPENDS"
fi

echo
if [ "$fallos" = 0 ]; then
    echo "  sello de version: OK"
    exit 0
fi
echo "  sello de version: $fallos FALLO(S)"
exit 1
