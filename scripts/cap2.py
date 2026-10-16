#!/usr/bin/env python3
"""cap2.py — captura N segundos del puerto serie a un fichero (crudo).

Uso:  python3 scripts/cap2.py <puerto> <segundos> <fichero> [reset]

Lo llama scripts/vigilar_panicos.sh. Existe porque la traza de un panic de la
placa solo sale por el serie EN EL MOMENTO (la pantalla de Ajustes da el motivo,
no el backtrace), asi que hay que dejar algo grabando antes de reproducirlo.

Vivia en /tmp/cap2.py, SIN versionar (auditoria del 16-oct-2026): si faltaba,
vigilar_panicos.sh imprimia "grabando..." y no vigilaba nada -- el peor fallo
posible en una herramienta que se usa justo cuando algo se cae. Ahora esta aqui
y el script se niega a seguir si no lo encuentra.

El cuarto argumento ("reset") hace un pulso de reset por DTR/RTS antes de
capturar: util para grabar el arranque entero (bootloader + panic).

Necesita pyserial (viene con el python del entorno de ESP-IDF):
    python3 -c 'import serial' || echo "falta pyserial"
"""
import sys
import time

import serial


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    puerto, segundos, salida = sys.argv[1], float(sys.argv[2]), sys.argv[3]
    reset = len(sys.argv) > 4 and sys.argv[4] == "reset"

    s = serial.Serial(puerto, 115200, timeout=0.2)
    if reset:
        s.setDTR(False)
        s.setRTS(True)
        time.sleep(0.15)
        s.setRTS(False)
        time.sleep(0.05)

    t0 = time.time()
    with open(salida, "wb") as f:
        while time.time() - t0 < segundos:
            d = s.read(4096)
            if d:
                f.write(d)
    s.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
