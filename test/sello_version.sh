#!/usr/bin/env bash
#
# sello_version.sh — el sello (tag, commit y FECHA DEL COMMIT) no puede mentir.
#
# 22-nov-2026: la pantalla ensenaba app_desc->date/date (la fecha de compilacion
# de ese componente), que se queda congelada: con codigo del 22 dentro, la placa
# decia "14 NOV" y el dueno lo leyo como "no esta en la ultima version". El sello
# nuevo sale de scripts/gen_version_header.cmake, y esto prueba ESE script en un
# repo de usar y tirar, caso por caso, porque un rotulo que miente es peor que no
# tenerlo:
#
#   1. arbol justo en el tag     -> el tag limpio, sin "sucio"
#   2. commits por encima        -> "vX.Y-N-g<hash>" (se ve que no es el tag)
#   3. fuente tocada             -> DIRTY=1 (se avisa en la pantalla)
#   4. solo dependencies.lock    -> DIRTY=0 (lo reescribe el build; no es codigo)
#   5. nada cambia               -> no reescribe el fichero (no recompila de balde)
#   6. copia sin .git + stamp    -> usa el sello que dejo el script del build
#   7. copia sin .git y sin stamp-> "sin-git", y NO hereda el repo de encima
#
# Uso: bash test/sello_version.sh   (desde el repo de la cabina; si el repo de la
# P4 esta al lado, comprueba ademas que las dos copias del generador son iguales:
# una pantalla y otra no pueden contar cosas distintas con el mismo build).
set -uo pipefail
AQUI="$(cd "$(dirname "$0")/.." && pwd)"
GEN="$AQUI/scripts/gen_version_header.cmake"
HERMANO="$(cd "$AQUI/.." 2>/dev/null && pwd)/p4/scripts/gen_version_header.cmake"

fallos=0
ok()  { printf '  ok    %s\n' "$1"; }
mal() { printf '  FALLO %s\n' "$1"; fallos=$((fallos+1)); }

[ -f "$GEN" ] || { echo "no encuentro $GEN"; exit 1; }

# ── 0) las dos copias del generador son el mismo fichero ────────────────────
if [ -f "$HERMANO" ]; then
    if diff -q "$GEN" "$HERMANO" >/dev/null; then
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
    [ -f "$2" ]
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

# ── 3) fuente tocada sin commitear ──────────────────────────────────────────
printf 'int c;\n' >> "$R/a.c"
gen "$R" "$T/s3.h"
[ "$(dirty_de "$T/s3.h")" = "1" ] \
    && ok "fuente sin commitear: DIRTY=1 (la pantalla lo dice)" \
    || mal "fuente sin commitear y el sello NO lo marca"
git -C "$R" checkout -q -- a.c

# ── 4) solo dependencies.lock (lo reescribe el build) ───────────────────────
printf 'lock v2 (ruta de esta maquina)\n' > "$R/dependencies.lock"
gen "$R" "$T/s4.h"
[ "$(dirty_de "$T/s4.h")" = "0" ] \
    && ok "solo dependencies.lock: DIRTY=0 (no es codigo, lo reescribe el build)" \
    || mal "dependencies.lock marca el sello como sucio (todos los builds dirian 'sin commitear')"
git -C "$R" checkout -q -- dependencies.lock

# ── 5) nada cambia -> no se reescribe (no recompila de balde) ───────────────
gen "$R" "$T/s5a.h"
ANTES="$(stat -c %Y "$T/s5a.h")"
sleep 1
gen "$R" "$T/s5a.h"
DESPUES="$(stat -c %Y "$T/s5a.h")"
if [ "$ANTES" = "$DESPUES" ]; then
    ok "sin cambios en el git, el fichero no se reescribe"
else
    mal "el generador reescribe el sello aunque no cambie nada (recompilaria de balde)"
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
P4="$(cd "$AQUI/.." 2>/dev/null && pwd)/p4"
FUENTES=("$AQUI/main")
[ -d "$P4/main" ] && FUENTES+=("$P4/main")
CONGELADA="$(grep -rnE 'app(_desc)?->(date|time)' "${FUENTES[@]}" 2>/dev/null \
             | grep -vE ':[0-9]+: *(\*|/\*|//)' || true)"
if [ -z "$CONGELADA" ]; then
    ok "nadie pinta app->date/app->time (la fecha que se queda vieja)"
else
    mal "hay codigo que pinta la fecha congelada de app_desc:"
    printf '%s\n' "$CONGELADA" | head -5 | sed 's/^/          /'
fi

# ── 9) y las pantallas ENSENAN el sello (no solo existe el fichero) ─────────
# Sin esto, borrar la linea del sello compilaria igual y el test seguiria verde:
# el rotulo volveria a quedarse sin la verdad que hacia falta.
for pareja in "$AQUI/main/ui/view_ajustes.c:cabina5 (Ajustes)" \
              "$P4/main/ui/settings/settings_about.c:P4 (Acerca de)"; do
    FICHERO="${pareja%:*}"
    NOMBRE="${pareja#*:}"
    [ -f "$FICHERO" ] || continue
    if grep -q 'JB_BUILD_STAMP' "$FICHERO"; then
        ok "$NOMBRE ensena el sello (JB_BUILD_STAMP)"
    else
        mal "$NOMBRE NO ensena el sello: el rotulo se quedaria sin la fecha del commit"
    fi
done

echo
if [ "$fallos" = 0 ]; then
    echo "  sello de version: OK"
    exit 0
fi
echo "  sello de version: $fallos FALLO(S)"
exit 1
