#!/usr/bin/env bash
# auditar.sh — Verificador de la cabina (el mismo en la 5" y en la 3,5"). Reglas
# fallos ya conocidos de ESTE repo (auditoria del 3-oct-2026).
#
# La P4 tiene su propio test/auditar.sh; este es el equivalente aqui, donde hasta
# ahora solo habia compilacion en el CI y los dos chequeos de protocolo.
#
# Uso:  bash test/auditar.sh        (desde la raiz del repo)
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
# El .bin del proyecto se llama como el repo (cabina5.bin / 35cabina.bin). Antes
# ponia 35cabina.bin a pelo, asi que en la 5" estas comprobaciones del binario no
# se hacian nunca y decia "sin build".
APP=$(basename "$PWD")
fallos=0
ok()  { printf '  ok    %s\n' "$1"; }
mal() { printf '  FALLO %s\n' "$1"; fallos=$((fallos+1)); }

# ── EN CI, NO TENER BUILD ES UN FALLO (16-oct-2026) ─────────────────────────
# Las reglas que miran el BINARIO o el sdkconfig.h se saltaban en silencio si no
# existia build/ ("se omite"), asi que el dia que el step del build falle, se
# renombre o se cambie el orden, esta auditoria diria OK sin haber mirado nada.
# En CI el build va ANTES: si falta, es un fallo del workflow y se dice.
if [ -n "${CI:-}" ] && [ ! -f "build/$APP.bin" ]; then
    mal "no hay build/$APP.bin y en CI se compila antes de auditar: revisa el workflow (o el step del build)"
fi

echo "=== 1. El binario es el de la IDF que pide el proyecto (v5.5.5) ==="
# 7-oct-2026: este proyecto se migro de v5.4.4 a v5.5.5 para que los tres
# (P4, cabina 3,5" y cabina 5") compartan un solo entorno. La regla sigue igual
# de importante: compilar con otra IDF deja el firmware sin probar, y ahora la
# que vale es la misma que la de la P4.
BIN=$(ls "build/$APP.bin" 2>/dev/null | head -1)
if [ -n "$BIN" ]; then
    # Se acepta el sufijo "-dirty": el IDF de este PC lleva los parches del
    # proyecto de la P4 sin commitear (scripts/aplicar_parche_idf.sh), asi que
    # git describe devuelve v5.5.5-dirty. Lo que importa es el numero.
    strings "$BIN" | grep -qE '^v5\.5\.5(-dirty)?$' \
        && ok "el .bin lleva IDF v5.5.5 (el mismo arbol parcheado que la P4)" \
        || mal "el .bin NO lleva IDF v5.5.5 ($(strings "$BIN" | grep -oE '^v5\.[0-9]\.[0-9]+(-dirty)?$' | head -1))"
    ver=$(strings "$BIN" | grep -oE '^v[0-9]+\.[0-9]+$' | head -1)
    [ -n "$ver" ] && ok "version embebida: $ver" || mal "el .bin no lleva version embebida (¿build sin tags?)"
else
    echo "  (sin build: se omite; compila antes con el release.sh del repo)"
fi

echo "=== 2. LVGL fijado en la version que pide el manifiesto (la UI va con ella) ==="
# La UI esta escrita para UNA version de LVGL: la 5" va con la 9.2 y la 3,5" con
# la 8.4, y saltar de rama cambia el dibujado y la API. El pin bueno esta en
# main/idf_component.yml; dependencies.lock tiene que haberlo resuelto a la
# MISMA rama (mismo major.minor), que es la que acaba metiendo el build.
# Antes ponia "8.4.0" a pelo y la 5" (LVGL 9 desde el 8-oct-2026) fallaba aqui.
pedida=$(awk '
    /^[[:space:]]*lvgl\/lvgl:/ {
        if ($0 ~ /[0-9]+\.[0-9]+\.[0-9]+/) { print; exit }
        dentro = 1; next
    }
    dentro && /version:/ { print; exit }
' main/idf_component.yml 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
fijada=$(grep -A 9 '^  lvgl/lvgl:' dependencies.lock 2>/dev/null | grep -oE 'version: [0-9]+\.[0-9]+\.[0-9]+' | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
if [ -z "$pedida" ] || [ -z "$fijada" ]; then
    mal "no encuentro el pin de LVGL (manifiesto '$pedida', lock '$fijada')"
else
    rama=${pedida%.*}
    case "$fijada" in
        "$rama".*) ok "lvgl/lvgl $fijada fijado en dependencies.lock (el manifiesto pide $pedida)" ;;
        *) mal "lvgl/lvgl $fijada NO es la rama $rama del manifiesto ($pedida): subir LVGL cambia la UI" ;;
    esac
fi

echo "=== 3. Particion de coredump (para poder diagnosticar un cuelgue) ==="
# Sin esta particion, un panic en el campo no deja volcado y solo se sabe que
# "algo peta". La P4 la tiene por la misma razon.
grep -qE "^coredump" partitions.csv && ok "particion coredump presente" \
    || mal "sin particion coredump: un panic no dejara volcado"

echo "=== 4. Task WDT encendido y con panic (el anti-cuelgue depende de el) ==="
H=build/config/sdkconfig.h
if [ -f "$H" ]; then
    grep -q "^#define CONFIG_ESP_TASK_WDT_EN 1" "$H" && ok "Task WDT encendido" || mal "Task WDT apagado"
    grep -q "^#define CONFIG_ESP_TASK_WDT_PANIC 1" "$H" && ok "Task WDT con panic (reinicia)" || mal "Task WDT sin panic: un cuelgue no reinicia"
fi

echo "=== 5. Pilas de tarea (ninguna por debajo de 3072) ==="
baja=$(grep -rE 'xTaskCreate\([a-zA-Z_0-9]+, *"[a-z_0-9]+", *[0-9]{3,5}' main components --include="*.c" 2>/dev/null \
    | grep -v managed_components | grep -v espressif__ \
    | sed -E 's/^([^:]+):.*"([a-z_0-9]+)", *([0-9]+).*/\3 \2 \1/' \
    | awk '$1+0 < 3072 {print "        " $2 " (" $1 ") en " $3}' | head -5)
[ -z "$baja" ] && ok "ninguna tarea propia con pila < 3072" || { mal "tareas con pila < 3072:"; echo "$baja" | sed 's/^/        /'; }

echo "=== 6. Copias de cadenas sin limite ==="
malas=$(grep -rnE '\b(strcpy|strcat|sprintf)\s*\(' main components --include="*.c" 2>/dev/null \
    | grep -v managed_components | grep -vE '"[^"]*"\)' | head -5)
[ -z "$malas" ] && ok "ninguna copia sin limite con origen variable" || { mal "copias sin limite:"; echo "$malas" | sed 's/^/        /'; }

echo "=== 7. LVGL desde tareas: siempre bajo el cerrojo del port ==="
# Medido a golpes en la P4 el 2-oct-2026: llamar a LVGL desde una tarea que no es
# la de LVGL sin el lock acaba en panic (get_prop_core). Aqui p4_api.c ya lo
# documenta y usa lvgl_port_lock(); esta regla vigila que ningun fichero que
# cree tareas y toque LVGL se deje el cerrojo.
sospechosos=""
for f in $(grep -rlE 'xTaskCreate\(' main components --include="*.c" 2>/dev/null | grep -v managed_components); do
    nlv=$(grep -vE '^[[:space:]]*(/\*|\*|//)' "$f" | grep -cE '\blv_[a-z_]+\(' || true)
    nlock=$(grep -cE 'lvgl_port_lock|bsp_display_lock' "$f" || true)
    # lv_port.c y esp_bsp.c son el port de LVGL y su montaje: ahi el lock no
    # hace falta (y esp_bsp.c lo usa igual, que es lo que esta regla mira).
    # main/main.c ESTABA EN ESTA LISTA Y NO PINTA NADA AQUI (16-oct-2026): no es
    # la tarea de LVGL, es la tarea de arranque -- de ahi salen los temporizadores
    # y la reconciliacion. Eximirla escondia justo el fallo que se cazo ese dia:
    # un lv_timer_create() sin cerrojo tres lineas despues de otro CON cerrojo.
    case "$f" in main/lv_port.c|main/esp_bsp.c) continue;; esac
    [ "$nlv" -gt 0 ] && [ "$nlock" -eq 0 ] && sospechosos="$sospechosos $f"
done
[ -z "$sospechosos" ] && ok "ningun fichero con tareas toca LVGL sin cerrojo" \
    || mal "tocan LVGL sin lvgl_port_lock:$sospechosos"

echo "=== 8. Parametros del enlace con la P4 ==="
# La P4 levanta el AP en 192.168.4.1 y su DHCP reparte .2-.101, asi que la IP
# estatica de la cabina tiene que quedar por encima (se usa .200) o se pelea con
# una concesion y el enlace se cae sin decir por que.
grep -q "192.168.4.1" main/net/p4_api.c && ok "la P4 se busca en 192.168.4.1" \
    || mal "p4_api.c no apunta a 192.168.4.1"
ip=$(grep -oE 'IP_FIJA_CABINA +"192\.168\.4\.[0-9]+"' main/net/udp_rx.c | grep -oE '[0-9]+"$' | tr -d '"')
if [ -z "$ip" ]; then
    mal "no encuentro IP_FIJA_CABINA en udp_rx.c"
elif [ "$ip" -lt 150 ]; then
    mal "IP de respaldo = .$ip: DENTRO del rango DHCP de la P4 (.2-.101)"
else
    ok "IP de emergencia (respaldo) = .$ip (fuera del DHCP .2-.101)"
fi
# Desde el 13-oct-2026 la IP de emergencia NO es una sola: se deriva de la MAC
# (.200 + ultimo octeto & 0x1F = .200-.231) porque con dos pantallas encendidas
# las dos se ponian la .200 y se pisaban. Lo que hay que vigilar ahora es que
# ESE calculo siga fuera del rango del DHCP (.2-.101) y dentro de la subred.
if grep -q '192\.168\.4\.%u", 200u + (unsigned)(mac\[5\] & 0x1F)' main/net/udp_rx.c; then
    ok "la IP de emergencia se deriva de la MAC: .200-.231 (fuera del DHCP .2-.101)"
else
    mal "la IP de emergencia ya no se deriva de la MAC como se espera (udp_rx.c)"
fi

echo "=== 9. mini_proto.h y el contrato, contra el repo de la P4 (si esta al lado) ==="
if [ -f ../victron/main/net/mini_proto.h ]; then
    diff -q main/net/mini_proto.h ../victron/main/net/mini_proto.h >/dev/null 2>&1 \
        && ok "mini_proto.h identico al de la P4" \
        || mal "mini_proto.h DISTINTO del de la P4 (se sincroniza a mano)"
    # Y con la 5", que desde el 13-oct-2026 tambien se comprueba en el CI (antes
    # no la miraba nadie desde aqui).
    if [ -f ../cabina5/main/net/mini_proto.h ]; then
        diff -q main/net/mini_proto.h ../cabina5/main/net/mini_proto.h >/dev/null 2>&1 \
            && ok "mini_proto.h identico al de la 5\"" \
            || mal "mini_proto.h DISTINTO del de la 5\" (se sincroniza a mano)"
    fi
    if [ -x ../victron/test/protocolo_cabina.sh ]; then
        if bash ../victron/test/protocolo_cabina.sh ../victron . >/dev/null 2>&1; then
            ok "el contrato completo con la P4 pasa (ops, JSON, campos)"
        else
            mal "el contrato con la P4 FALLA: corre 'bash ../victron/test/protocolo_cabina.sh ../victron .'"
        fi
    fi
else
    echo "  (sin el repo de la P4 al lado: se omite)"
fi

echo "=== 10. CUERPO_MAX tiene que caber en VIAJE_BODY_MAX ==="
# Lo vigila el CI (job viaje_body_sync) pero es barato comprobarlo aqui tambien:
# este repo es el unico que puede romper el limite subiendo CUERPO_MAX.
if [ -f ../victron/main/portal/config_server_viaje.c ]; then
    cmax=$(grep -oE '#define +CUERPO_MAX +[0-9]+' main/net/viaje_cola.h | grep -oE '[0-9]+' | head -1)
    vmax=$(grep -oE '#define +VIAJE_BODY_MAX +[0-9]+' ../victron/main/portal/config_server_viaje.c | grep -oE '[0-9]+' | head -1)
    if [ -n "$cmax" ] && [ -n "$vmax" ]; then
        [ "$cmax" -lt "$vmax" ] && ok "CUERPO_MAX=$cmax < VIAJE_BODY_MAX=$vmax" \
            || mal "CUERPO_MAX=$cmax NO cabe en VIAJE_BODY_MAX=$vmax"
    fi
fi

echo "=== 11. dependencies.lock commiteado, sin rutas absolutas ==="
# Este repo ya tiene un hook de pre-commit para esto (CLAUDE.md); la regla lo
# vuelve a comprobar por si el hook no esta activado en un clon nuevo.
if git show HEAD:dependencies.lock >/dev/null 2>&1; then
    git show HEAD:dependencies.lock | grep -qE "$HOME|/home/" \
        && mal "el dependencies.lock COMMITEADO lleva ruta absoluta: el CI fallara" \
        || ok "el dependencies.lock commiteado solo tiene rutas relativas"
fi

echo "=== 12. El build no puede ser mas viejo que el codigo ==="
# Se compara con los FICHEROS FUENTE, no con la fecha del commit: un commit que
# no cambia el binario (documentacion, o un fichero que se toco y se dejo igual)
# dejaba esta regla en rojo para siempre, porque el build no regenera el .bin si
# no hay nada que recompilar. Visto el 7-oct-2026. Lo que importa es que ningun
# fichero que entre en el binario sea mas nuevo que el .bin.
if [ -n "$BIN" ]; then
    mas_nuevo=$(find main components partitions.csv sdkconfig -newer "$BIN" \
                \( -name '*.c' -o -name '*.h' -o -name '*.csv' -o -name 'sdkconfig' \) \
                2>/dev/null | grep -v managed_components | head -3)
    if [ -n "$mas_nuevo" ]; then
        mal "hay fuentes mas nuevos que el .bin: recompila antes de auditarlo ($(echo $mas_nuevo | tr '\n' ' '))"
    else
        ok "el .bin es posterior a todo el codigo (nada que recompilar)"
    fi
fi

echo
if [ "$fallos" -eq 0 ]; then echo "AUDITORIA OK"; exit 0; else echo "AUDITORIA: $fallos FALLOS"; exit 1; fi
