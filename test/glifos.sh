#!/usr/bin/env bash
# Los textos de la UI, ¿se pueden pintar con las fuentes que lleva la placa?
#
# 24-nov-2026: el dueño mando una foto del cartel de actualización preguntando
# por "el rectángulo entre v3.4 y 0 %". Era el punto medio (·) que las fuentes
# Inter de esta placa NO traen: LVGL pinta un cuadro vacío en su lugar. Lo mismo
# pasaba con los "…" del mismo cartel. Esto lo caza en el PC, antes de grabar.
#
# Uso: bash test/glifos.sh
set -euo pipefail
AQUI=$(cd "$(dirname "$0")" && pwd)
exec python3 "$AQUI/glifos.py" "$@"
