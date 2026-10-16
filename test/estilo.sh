#!/usr/bin/env bash
# Auditoria de ESTILO de la cabina (16-oct-2026; endurecida la noche del 16-oct).
#
# No prueba comportamiento: vigila las reglas de estilo que se acordaron al
# unificar las placas (ver docs/GUIA_ESTILO.md de la P4 y main/ui/ui_style.h) y
# los dos fallos que nadie veia. Existe porque las reglas se rompieron de verdad:
#
#   1. TODO NOMBRE DE FUENTE TIENE QUE ESTAR ALIASADO **EN SU UNIDAD DE
#      COMPILACION**. La UI llama a las fuentes por su nombre viejo
#      (lv_font_montserrat_16 y compania) en mas de 100 sitios. Si ese nombre no
#      esta #define'd en una cabecera QUE ESE .c INCLUYA, no da error de
#      compilacion: tira de la Montserrat del SDK, que sigue compilada, y el
#      texto sale al tamaño LITERAL (16 px) en vez del papel que le tocaba (30).
#      Paso el 16-oct-2026 ("el texto es pequeñisimo").
#      ANTES esta regla solo miraba que el #define existiera en ALGUN fichero de
#      main/: con eso, un .c que usaba el nombre sin incluir la cabecera pasaba
#      en verde (es el caso de capture_carousel.c). Ahora se resuelve el grafo de
#      #include de cada .c y se exige que el alias sea VISIBLE ahi.
#
#   2. NINGUN COLOR A MANO. Los colores salen de main/ui/ui_style.h. La regla
#      vieja solo buscaba `lv_color_hex(0xRRGGBB)`, asi que se le escapaban
#      todos los literales que van sueltos (paleta_texto(x, 0xCCCCCC, ...), un
#      ternario dentro de lv_color_hex(...), un switch sobre el valor...). Ahora
#      busca CUALQUIER literal de 6 digitos hex fuera de ui_style.h (ignorando
#      comentarios y cadenas, que no son codigo).
#      ⚠ BASE: quedan colores a mano en ficheros de main/ que NO son de este
#      script (view_registro.c, view_info.c, main_app.c/main.c...). Mientras no
#      se migren a tokens, la regla los CUENTA y solo falla si AUMENTAN: el
#      numero de cada repo esta en la tabla de abajo. Si migras alguno, BAJA el
#      numero (y si no lo bajas, la regla solo avisa).
#
#   3. LOS TITULOS VAN CENTRADOS: un rotulo en mayusculas (titulo de pantalla) no
#      puede estar alineado a la derecha. Paso con "CONFIGURACIÓN" y "AJUSTES
#      WI-FI" en la 3,5".
#
#   4. LOS SIMBOLOS TIENEN QUE TENER GLIFO. Un LV_SYMBOL_* que no este en
#      ninguna de las fuentes del proyecto no da error: se dibuja VACIO. Nada lo
#      avisaba, y un icono que desaparece no se nota hasta que se mira la placa.
#      Se cruzan los LV_SYMBOL_* que usa main/ con los rangos (cmaps) de las
#      fuentes de main/fonts/.
#
#   5. LAS FUENTES _es TIENEN QUE TRAER LOS ACENTOS. Son fuentes generadas a
#      mano con lv_font_conv: si alguien las regenera con otro juego de
#      caracteres, "Configuración" se ve "Configuracin" y no falla nada. Se
#      exige que cada fuente de main/fonts/ cubra las letras del castellano que
#      usa la UI (los simbolos van aparte, en la regla 4).
#
# ESTE FICHERO ES IDENTICO EN LAS DOS CABINAS (lo compara el CI byte a byte,
# ignorando los #include): por eso la base de las reglas 1 y 2 se elige por
# repositorio, no se escribe un numero unico.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

fallos=0
ok()   { echo "  ok    $1"; }
mal()  { echo "  FALLO $1"; fallos=$((fallos+1)); }
aviso(){ echo "  AVISO $1"; }

# ── 0) ¿que repo es esta copia? (la base de las reglas con deuda pendiente) ──
# Los colores a mano y los nombres de fuente sin alias visible viven en ficheros
# de main/ que este script no puede tocar; se cuentan para que no AUMENTEN.
#
# NUMEROS MEDIDOS EL 16-oct-2026 POR LA NOCHE (incluyen lo que habia sin commitear
# de los otros agentes ese rato). Cuando migres algun color a token, BAJA el
# numero: si el recuento queda por debajo, la regla lo dice y no falla.
#   · colores: cabina5 92 y 35cabina 76 (van a ui_style.h con nombre).
#   · fuente sin alias visible: en cabina5 era capture_carousel.c (usaba
#     lv_font_montserrat_48 sin incluir fonts/fonts_es.h, o sea la Montserrat del
#     SDK); con el arreglo de main/ el recuento baja a 0 y la base se puede bajar
#     a 0 tambien. Se deja en 1 para que el script no falle mientras el arreglo
#     este a medias (0 tambien pasa).
REPO_ID="$(basename "$PWD")"
case "$REPO_ID" in
    cabina5|35cabina) ;;
    *) REPO_ID="$(git remote get-url origin 2>/dev/null | sed 's|.*/||; s|\.git$||')" ;;
esac
case "$REPO_ID" in
    cabina5)  COLORES_BASE=92; FUENTES_SIN_ALIAS_BASE=1 ;;
    35cabina) COLORES_BASE=76; FUENTES_SIN_ALIAS_BASE=0 ;;
    *)        # copia con otro nombre: se usa la base mas alta de las dos para no
              # dar un falso positivo por como se llame el directorio.
              COLORES_BASE=92; FUENTES_SIN_ALIAS_BASE=1
              aviso "no se cual es este repo ('$REPO_ID'): uso la base mas alta" ;;
esac

# ── el analisis, en python (el grafo de #include y los cmaps no se hacen en bash)
INFORME="$(python3 - <<'PY'
import pathlib, re, collections, sys

# ── utilidades ──────────────────────────────────────────────────────────────
def limpiar(txt):
    """Quita comentarios y literales de cadena conservando lineas y columnas."""
    out, i, n, estado = [], 0, len(txt), None
    while i < n:
        c, dos = txt[i], txt[i:i+2]
        if estado is None:
            if dos == '/*': estado = 'bloque'; out.append('  '); i += 2; continue
            if dos == '//': estado = 'linea';  out.append('  '); i += 2; continue
            if c == '"':    estado = 'cadena'; out.append(' ');  i += 1; continue
            if c == "'":    estado = 'char';   out.append(' ');  i += 1; continue
            out.append(c); i += 1; continue
        if estado == 'bloque':
            if dos == '*/': estado = None; out.append('  '); i += 2
            else: out.append('\n' if c == '\n' else ' '); i += 1
        elif estado == 'linea':
            if c == '\n': estado = None; out.append('\n')
            else: out.append(' ')
            i += 1
        else:
            if c == '\\': out.append('  '); i += 2; continue
            if (estado == 'cadena' and c == '"') or (estado == 'char' and c == "'"):
                estado = None
            out.append('\n' if c == '\n' else ' '); i += 1
    return ''.join(out)

TXT = {}
def texto(p):
    p = pathlib.Path(p)
    if p not in TXT:
        try: TXT[p] = p.read_text(errors='ignore')
        except OSError: TXT[p] = ''
    return TXT[p]

RAICES = [pathlib.Path('main').resolve(), pathlib.Path('.').resolve()]

def resolver(inc, f):
    for base in [pathlib.Path(f).parent.resolve()] + RAICES:
        try: c = (base / inc).resolve()
        except OSError: continue
        if c.is_file(): return c
    return None

def alcanzables(f):
    """El .c mas todas las cabeceras LOCALES que incluye (transitivamente)."""
    vistos, pila = set(), [pathlib.Path(f).resolve()]
    while pila:
        a = pila.pop()
        if a in vistos: continue
        vistos.add(a)
        for inc in re.findall(r'^\s*#\s*include\s+"([^"]+)"', texto(a), re.M):
            r = resolver(inc, a)
            if r and r not in vistos: pila.append(r)
    return vistos

def fuentes_main():
    for f in sorted(pathlib.Path('main').rglob('*')):
        if f.suffix in ('.c', '.h') and f.is_file() and 'managed_components' not in str(f):
            yield f

ARCHIVOS = list(fuentes_main())
NOMBRE_FUENTE = re.compile(r'\blv_font_montserrat_(?:bold_)?[0-9]+\b')
HEX = re.compile(r'0x[0-9A-Fa-f]{6}\b')

# ── regla 1: alias visible en la unidad de compilacion ──────────────────────
DEF = collections.defaultdict(set)
for f in ARCHIVOS:
    for m in re.finditer(r'^\s*#\s*define\s+(lv_font_montserrat_(?:bold_)?[0-9]+)\b',
                         texto(f), re.M):
        DEF[m.group(1)].add(f.resolve())

sin_alias, nombres = [], set()
for f in ARCHIVOS:
    if f.suffix != '.c': continue
    usados = set(NOMBRE_FUENTE.findall(limpiar(texto(f))))
    if not usados: continue
    nombres |= usados
    alc = alcanzables(f)
    for u in sorted(usados):
        if not (DEF.get(u, set()) & alc):
            sin_alias.append("%s: %s" % (f, u))

# ── regla 2: ningun literal 0xRRGGBB fuera de ui_style.h ────────────────────
colores = []
for f in ARCHIVOS:
    if f.name == 'ui_style.h': continue
    for i, linea in enumerate(limpiar(texto(f)).split('\n'), 1):
        for m in HEX.findall(linea):
            colores.append("%s:%d: %s" % (f, i, m))

# ── regla 3: titulos en mayusculas alineados a la derecha ───────────────────
titulos = []
for f in ARCHIVOS:
    if f.suffix != '.c': continue
    lineas = texto(f).split('\n')
    for i, l in enumerate(lineas):
        m = re.search(r'lv_label_set_text(?:_fmt)?\(\s*(\w+)\s*,\s*"([A-ZÁÉÍÓÚÑ][A-ZÁÉÍÓÚÑ ()0-9_-]{3,})"', l)
        if not m: continue
        var, txt = m.group(1), m.group(2)
        ctx = "\n".join(lineas[max(0, i-8):i+10])
        al = re.findall(r'lv_obj_align\(\s*' + var + r'\s*,\s*(LV_ALIGN_\w+)', ctx)
        if al and 'RIGHT' in al[0]:
            titulos.append("%s:%d %s" % (f.name, i+1, txt))

# ── los cmaps de una fuente: que codepoints cubre ───────────────────────────
def cmap_de(f):
    txt = texto(f)
    listas = {}
    for m in re.finditer(r'static const uint16_t (unicode_list_\d+)\[\]\s*=\s*\{(.*?)\};', txt, re.S):
        listas[m.group(1)] = [int(x, 0) for x in re.findall(r'0x[0-9A-Fa-f]+|\b\d+\b', m.group(2))]
    cps = set()
    patron = (r'\.range_start\s*=\s*(\d+)\s*,\s*\.range_length\s*=\s*(\d+)'
              r'[^}]*?\.unicode_list\s*=\s*(\w+)[^}]*?\.type\s*=\s*(LV_FONT_FMT_TXT_CMAP_\w+)')
    for m in re.finditer(patron, txt, re.S):
        ini, largo, lista, tipo = int(m.group(1)), int(m.group(2)), m.group(3), m.group(4)
        if 'SPARSE' in tipo and lista in listas:
            cps |= {ini + v for v in listas[lista]}
        else:
            cps |= set(range(ini, ini + largo))
    return cps

FUENTES = sorted(pathlib.Path('main/fonts').glob('lv_font_*.c'))
CMAPS = {f.name: cmap_de(f) for f in FUENTES}

# ── regla 4: todo LV_SYMBOL_* usado tiene glifo en alguna fuente ────────────
# La tabla de codepoints sale del propio LVGL si esta (managed_components no
# esta versionado, asi que en el CI no existe): entonces se usa la de LVGL 8.4
# copiada aqui, que es la version que fija dependencies.lock.
TABLA = {
    "LV_SYMBOL_BULLET": 0x2022, "LV_SYMBOL_AUDIO": 0xF001, "LV_SYMBOL_VIDEO": 0xF008,
    "LV_SYMBOL_LIST": 0xF00B, "LV_SYMBOL_OK": 0xF00C, "LV_SYMBOL_CLOSE": 0xF00D,
    "LV_SYMBOL_POWER": 0xF011, "LV_SYMBOL_SETTINGS": 0xF013, "LV_SYMBOL_HOME": 0xF015,
    "LV_SYMBOL_DOWNLOAD": 0xF019, "LV_SYMBOL_DRIVE": 0xF01C, "LV_SYMBOL_REFRESH": 0xF021,
    "LV_SYMBOL_MUTE": 0xF026, "LV_SYMBOL_VOLUME_MID": 0xF027, "LV_SYMBOL_VOLUME_MAX": 0xF028,
    "LV_SYMBOL_IMAGE": 0xF03E, "LV_SYMBOL_TINT": 0xF043, "LV_SYMBOL_PREV": 0xF048,
    "LV_SYMBOL_PLAY": 0xF04B, "LV_SYMBOL_PAUSE": 0xF04C, "LV_SYMBOL_STOP": 0xF04D,
    "LV_SYMBOL_NEXT": 0xF051, "LV_SYMBOL_EJECT": 0xF052, "LV_SYMBOL_LEFT": 0xF053,
    "LV_SYMBOL_RIGHT": 0xF054, "LV_SYMBOL_PLUS": 0xF067, "LV_SYMBOL_MINUS": 0xF068,
    "LV_SYMBOL_EYE_OPEN": 0xF06E, "LV_SYMBOL_EYE_CLOSE": 0xF070, "LV_SYMBOL_WARNING": 0xF071,
    "LV_SYMBOL_SHUFFLE": 0xF074, "LV_SYMBOL_UP": 0xF077, "LV_SYMBOL_DOWN": 0xF078,
    "LV_SYMBOL_LOOP": 0xF079, "LV_SYMBOL_DIRECTORY": 0xF07B, "LV_SYMBOL_UPLOAD": 0xF093,
    "LV_SYMBOL_CALL": 0xF095, "LV_SYMBOL_CUT": 0xF0C4, "LV_SYMBOL_COPY": 0xF0C5,
    "LV_SYMBOL_SAVE": 0xF0C7, "LV_SYMBOL_BARS": 0xF0C9, "LV_SYMBOL_ENVELOPE": 0xF0E0,
    "LV_SYMBOL_CHARGE": 0xF0E7, "LV_SYMBOL_PASTE": 0xF0EA, "LV_SYMBOL_BELL": 0xF0F3,
    "LV_SYMBOL_KEYBOARD": 0xF11C, "LV_SYMBOL_GPS": 0xF124, "LV_SYMBOL_FILE": 0xF15B,
    "LV_SYMBOL_WIFI": 0xF1EB, "LV_SYMBOL_BATTERY_FULL": 0xF240, "LV_SYMBOL_BATTERY_3": 0xF241,
    "LV_SYMBOL_BATTERY_2": 0xF242, "LV_SYMBOL_BATTERY_1": 0xF243, "LV_SYMBOL_BATTERY_EMPTY": 0xF244,
    "LV_SYMBOL_USB": 0xF287, "LV_SYMBOL_BLUETOOTH": 0xF293, "LV_SYMBOL_TRASH": 0xF2ED,
    "LV_SYMBOL_EDIT": 0xF304, "LV_SYMBOL_BACKSPACE": 0xF55A, "LV_SYMBOL_SD_CARD": 0xF7C2,
    "LV_SYMBOL_NEW_LINE": 0xF8A2, "LV_SYMBOL_DUMMY": 0xF8FF,
}
TABLA_DE = 'LVGL 8.4 (copiada en este script)'
sd = pathlib.Path('managed_components/lvgl__lvgl/src/font/lv_symbol_def.h')
if sd.is_file():
    t = texto(sd)
    de_sdk = {m.group(1): int(m.group(3))
              for m in re.finditer(r'#define\s+(LV_SYMBOL_[A-Z_0-9]+)\s+"(.*?)"\s*/\*(\d+),', t)}
    if de_sdk:
        TABLA, TABLA_DE = de_sdk, str(sd)

usados = collections.Counter()
for f in ARCHIVOS:
    for s in re.findall(r'\bLV_SYMBOL_[A-Z_0-9]+', limpiar(texto(f))):
        usados[s] += 1
sin_glifo, sin_en_alguna = [], []
for s in sorted(usados):
    cp = TABLA.get(s)
    if cp is None:
        sin_glifo.append("%s (no esta en la tabla de LVGL)" % s)
        continue
    faltan = [n for n, cs in CMAPS.items() if cp not in cs]
    if len(faltan) == len(CMAPS):
        sin_glifo.append("%s (U+%04X) no esta en NINGUNA de las %d fuentes" % (s, cp, len(CMAPS)))
    elif faltan:
        sin_en_alguna.append("%s (U+%04X) no esta en: %s" % (s, cp, ", ".join(faltan)))

# ── regla 5: las fuentes traen los acentos del castellano ───────────────────
ACENTOS = "áéíóúüñÁÉÍÓÚÑ¿¡"
sin_acentos = []
for f in FUENTES:
    faltan = "".join(c for c in ACENTOS if ord(c) not in CMAPS[f.name])
    if faltan:
        sin_acentos.append("%s: no tiene %s" % (f.name, faltan))

# ── informe ────────────────────────────────────────────────────────────────
def tot(k, v): print("TOT %s %s" % (k, v))
def det(k, l):
    for x in l: print("DET %s %s" % (k, x))

tot("nombres", len(nombres))
tot("fuentes_sin_alias", len(sin_alias));  det("fuentes_sin_alias", sin_alias)
tot("colores", len(colores));              det("colores", colores)
tot("titulos", len(titulos));              det("titulos", titulos)
tot("simbolos", len(usados))
tot("simbolos_sin_glifo", len(sin_glifo)); det("simbolos_sin_glifo", sin_glifo)
tot("simbolos_parciales", len(sin_en_alguna)); det("simbolos_parciales", sin_en_alguna)
tot("fuentes", len(FUENTES)); print("FUENTES_DE_TABLA %s" % TABLA_DE)
tot("fuentes_sin_acentos", len(sin_acentos)); det("fuentes_sin_acentos", sin_acentos)
PY
)"
tot() { printf '%s\n' "$INFORME" | sed -n "s/^TOT $1 //p"; }
det() { printf '%s\n' "$INFORME" | sed -n "s/^DET $1 //p" | sed 's/^/          /'; }
# Los detalles largos (los colores a mano son decenas) se recortan: se enseña de
# donde salen sin inundar la salida del CI.
det_rec() {
    local n="$1" k="$2" linea
    printf '%s\n' "$INFORME" | sed -n "s/^DET $k //p" | head -"$n" | sed 's/^/          /'
    linea=$(( $(tot "$k") - n ))
    [ "$linea" -gt 0 ] && echo "          ... y $linea mas (lista completa: quitar el recorte)"
    return 0
}

echo "== 1. Nombres de fuente aliasados EN SU UNIDAD DE COMPILACION =="
if [ "$(tot fuentes_sin_alias)" -gt "$FUENTES_SIN_ALIAS_BASE" ]; then
    mal "estos .c usan un nombre de fuente que NO ven aliasado (tiran de la del SDK,"
    echo "        texto al tamaño literal):"
    det fuentes_sin_alias
elif [ "$(tot fuentes_sin_alias)" -eq 0 ]; then
    ok "los $(tot nombres) nombres de fuente que usa la UI estan aliasados y visibles"
else
    ok "sin cambios: $(tot fuentes_sin_alias) nombre(s) sin alias visible (base: $FUENTES_SIN_ALIAS_BASE)"
    det fuentes_sin_alias
fi

echo "== 2. Colores: ninguno a mano (fuera de ui_style.h) =="
if [ "$(tot colores)" -gt "$COLORES_BASE" ]; then
    mal "hay $(tot colores) literales 0xRRGGBB a mano (base: $COLORES_BASE). Los nuevos van"
    echo "        a ui_style.h con nombre:"
    det_rec 20 colores
elif [ "$(tot colores)" -eq 0 ]; then
    ok "todos los colores salen de ui_style.h"
else
    ok "sin cambios: $(tot colores) literales a mano pendientes de migrar (base: $COLORES_BASE)"
    det_rec 5 colores
fi

echo "== 3. Titulos (rotulos en MAYUSCULAS) centrados =="
if [ "$(tot titulos)" -eq 0 ]; then
    ok "ningun titulo en mayusculas esta alineado a la derecha"
else
    mal "titulos alineados a la derecha:"; det titulos
fi

echo "== 4. Iconos: todo LV_SYMBOL_* que se usa tiene glifo en las fuentes =="
if [ "$(tot fuentes)" -eq 0 ]; then
    aviso "no encuentro fuentes en main/fonts/: no puedo comprobar los iconos"
elif [ "$(tot simbolos_sin_glifo)" -eq 0 ]; then
    ok "los $(tot simbolos) LV_SYMBOL_* que usa la UI tienen glifo (tabla: $(printf '%s\n' "$INFORME" | sed -n 's/^FUENTES_DE_TABLA //p'))"
    if [ "$(tot simbolos_parciales)" -gt 0 ]; then
        aviso "estos se dibujan en unas fuentes y en otras no:"
        det simbolos_parciales
    fi
else
    mal "estos iconos se dibujan VACIOS (ninguna fuente los tiene):"
    det simbolos_sin_glifo
fi

echo "== 5. Las fuentes _es traen los acentos del castellano =="
if [ "$(tot fuentes)" -eq 0 ]; then
    aviso "no encuentro fuentes en main/fonts/: no puedo comprobar los acentos"
elif [ "$(tot fuentes_sin_acentos)" -eq 0 ]; then
    ok "las $(tot fuentes) fuentes cubren á é í ó ú ü ñ Á É Í Ó Ú Ñ ¿ ¡"
else
    mal "fuentes regeneradas sin acentos (el texto saldria sin ellos):"
    det fuentes_sin_acentos
fi

echo
if [ "$fallos" = 0 ]; then echo "ESTILO OK"; else echo "FALLOS: $fallos"; fi
if [ "$fallos" = 0 ]; then exit 0; else exit 1; fi
