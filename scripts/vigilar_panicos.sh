#!/usr/bin/env bash
# vigilar_panicos.sh — deja el serial grabando y, si la placa se cae, traduce la
# traza a nombres de funcion y linea (16-oct-2026).
#
# POR QUE EXISTE: el panic de la pernocta (lv_buttonmatrix sobre una botonera
# NULL) llevaba 417 reinicios contados en la NVS y nadie sabia donde estaba: el
# motivo se ve en la pantalla de Ajustes, pero la TRAZA solo sale por el serial,
# en el momento. Esto lo automatiza:
#
#   scripts/vigilar_panicos.sh [segundos] [puerto]
#
# Mientras corre, haz en la placa lo que sospeches que la tumba. Al terminar
# imprime las lineas del panic y la traza ya traducida (addr2line sobre el .elf).
#
# OJO CON LA CAPTURA (auditoria del 16-oct-2026): la captura la hace
# scripts/cap2.py, que ANTES vivia en /tmp y no estaba versionado -- si faltaba,
# este script imprimia "grabando..." y no vigilaba NADA, que es el peor fallo
# posible en una herramienta que se usa justo cuando algo se cae. Ahora se
# comprueba que el fichero esta y que pyserial funciona, y si no, se para.
#
# El puerto por defecto es el de ESTA placa (/dev/ttyUSB0, el CH340): la P4 es
# /dev/ttyACM0 y la 3,5" es /dev/ttyACM1 (comprobar con: ls -l /dev/serial/by-id/).
set -uo pipefail
SEGS="${1:-900}"; PUERTO="${2:-/dev/ttyUSB0}"
AQUI=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$AQUI/.." && pwd)
LOG=/tmp/panicos_$(date +%H%M%S).log
ELF=$(ls -t "$REPO"/build/*.elf 2>/dev/null | head -1)
[ -n "$ELF" ] || { echo "no encuentro el .elf en $REPO/build (compila primero)"; exit 2; }
[ -e "$PUERTO" ] || { echo "no existe $PUERTO (mira: ls -l /dev/serial/by-id/)"; exit 2; }
[ -f "$AQUI/cap2.py" ] || { echo "falta $AQUI/cap2.py (la captura del serial)"; exit 2; }
python3 -c 'import serial' 2>/dev/null || {
    echo "falta pyserial para python3 (viene con el entorno de ESP-IDF:"
    echo "  . ~/.espressif/esp-idf-5.5/export.sh )"
    exit 2
}
echo "grabando $SEGS s de $PUERTO en $LOG — haz ahora lo que la tumbe"
python3 "$AQUI/cap2.py" "$PUERTO" "$SEGS" "$LOG" >/dev/null 2>&1
echo "--- resumen:"
sed 's/\x1b\[[0-9;]*m//g' "$LOG" | grep -aE "Guru Meditation|panic|assert|Backtrace|rst:0x|reinicio sola" | tail -12
DIRS=$(sed 's/\x1b\[[0-9;]*m//g' "$LOG" | grep -a "Backtrace:" | tail -1 | sed 's/.*Backtrace: //' | tr ' ' '\n' | sed 's/:.*//' | grep -E "^0x" || true)
if [ -n "$DIRS" ]; then
    echo "--- traza traducida:"
    PATH="$HOME/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20260121/xtensa-esp-elf/bin:$PATH" \
        xtensa-esp32s3-elf-addr2line -pfiaC -e "$ELF" $DIRS 2>/dev/null | grep -v "^??" | head -16
fi
