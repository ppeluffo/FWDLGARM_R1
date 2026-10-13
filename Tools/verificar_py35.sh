#!/bin/bash
#
# Verifica que las herramientas del test del contador corran en PYTHON 3.5,
# que es lo que tiene ulises-sat (3.5.2, Ubuntu 16.04).
#
#   ./verificar_py35.sh
#
# POR QUE EXISTE: la incompatibilidad con 3.5 mordio TRES veces seguidas, y cada
# vez por una construccion distinta -- f-strings, open() con un Path, y el
# separador de millares 50_000_000. Ir por sintomas las encuentra de a una; el
# interprete real las da todas de un saque.
#
# Y NO alcanza con compilar en 3.12: ninguna de las tres se detecta ahi. Tampoco
# sirve ast.parse(feature_version=(3,5)) -- se probo y NO rechaza f-strings.
#
# Necesita docker. La imagen se baja la primera vez.

set -u
cd "$(dirname "$0")"
FALLOS=0

echo "=== Python 3.5: compilan los tres? ==="
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/w:ro" python:3.5 \
    python3 -c '
import ast, sys
malos = 0
for f in ("logger_serie", "analizar_test", "generador_pulsos"):
    try:
        ast.parse(open("/w/%s.py" % f).read())
        print("   %-22s OK" % (f + ".py"))
    except SyntaxError as e:
        print("   %-22s SyntaxError linea %s: %s" % (f + ".py", e.lineno, e.msg))
        malos += 1
sys.exit(1 if malos else 0)
' || FALLOS=1

echo
echo "=== Python 3.5: corren? ==="
TMP=$(mktemp -d)
cat > "$TMP/serial.py" <<'PYEOF'
# Stub de pyserial: ejercita el logger sin hardware ni acceso a PyPI.
class SerialException(Exception):
    pass


class Serial(object):
    def __init__(self, *a, **k):
        self.port = None
        self.baudrate = 9600
        self.timeout = 0.3
        self.dtr = False
        self.rts = False

    def open(self):
        raise SerialException("stub: no hay puerto")

    def close(self):
        pass
PYEOF

docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/w:ro" -v "$TMP:/t" -w /t python:3.5 \
    bash -c '
    python3 /w/generador_pulsos.py --laptop --prueba --escala 2000 --horas 1 --dir /t/g \
        > /t/g.out 2>&1 && echo "   generador_pulsos.py  corrio" \
        || { echo "   generador_pulsos.py  FALLO"; tail -5 /t/g.out; exit 1; }
    PYTHONPATH=/t python3 /w/logger_serie.py --puerto /dev/noexiste --horas 0.003 --dir /t/l \
        > /t/l.out 2>&1 && echo "   logger_serie.py      corrio" \
        || { echo "   logger_serie.py      FALLO"; tail -5 /t/l.out; exit 1; }
    ' || FALLOS=1

rm -rf "$TMP"

echo
if [ "$FALLOS" -eq 0 ]; then
    echo "TODO OK: las herramientas corren en Python 3.5."
    echo "(el analizador se verifica con datos reales: correrlo en 3.5 y en 3.12"
    echo " sobre los mismos logs tiene que dar una salida IDENTICA)"
else
    echo "HAY FALLOS: ver arriba. NO copiar a ulises-sat sin arreglarlos."
fi
exit "$FALLOS"
