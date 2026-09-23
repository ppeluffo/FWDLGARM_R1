# -*- coding: utf-8 -*-
"""
Área G - Transmisión: el equipo contra el servidor real.

⭐ ESTA ÁREA MIRA LAS DOS PUNTAS, y ésa es toda su gracia. Tres veces en este
proyecto el síntoma señaló al lugar equivocado y lo cerró la otra punta: la FAT
acusando a la pila del MCP79410, el `CSQ 31` mandando a mirar la cobertura, y la
fecha de los `DATANR` señalando al frame cuando era una carrera de respuestas.
Un test que sólo le pregunta al equipo repite ese error.

⚠ PERO EL CONTRATO QUE SE VALIDA ES EL DEL DATALOGGER, no el del backend. Se
verifica que el frame **llegó y fue aceptado**, leyendo el log de `apicomms`. El
tramo Redis -> `apicomms_process` -> PostgreSQL es del servidor: si ahí algo
falla no es una falla del firmware, y reportarlo como FAIL sería acusar al
componente equivocado.

CÓMO SE LEVANTA EL SERVIDOR (corre en la misma PC):

    cd /home/pablo/Spymovil/python/proyectos/APICOMMS_2025
    source .venv/bin/activate
    python -m apicomms.app > /tmp/apicomms.log 2>&1 &

Y se le dice a la suite dónde está el log:

    APICOMMS_LOG=/tmp/apicomms.log ./suite.py -p /dev/ttyUSB0 -a G

⚠ Redis, PostgreSQL y el worker `process` van aparte, en Docker; para esta área
alcanza con `apicomms`, porque lo que se lee es su log.
"""

import os
import re
import time
import urllib.error
import urllib.request
from pathlib import Path

from runner import Salteado, test

URL = os.environ.get("APICOMMS_URL", "http://192.168.0.20:5000/apidlg")
LOG = os.environ.get("APICOMMS_LOG", "/tmp/apicomms.log")

# La línea de acceso de werkzeug: trae el frame ENTERO.
#   192.168.0.20 - - [23/Sep/2026 11:11:14] "GET /apidlg?ID=...&CLASS=PING HTTP/1.1" 200 -
#
# ⭐ Se usa ésta y no el `D_DATALINE` del código, porque aquél sale por
# `slogger()`, que sólo loguea para la unidad marcada como DEBUG_ID en Redis: si
# el equipo bajo prueba no es ésa, no habría una sola línea y el test fallaría
# por algo que no tiene nada que ver con el firmware.
RE_GET = re.compile(r'"GET (/apidlg\?\S*) HTTP/[\d.]+" (\d{3})')

# Lo que el equipo imprime al transmitir.
RE_FRAME_TX = re.compile(r"->\s*(ID=\S+)")


class LogServidor:
    """Lee sólo lo que el servidor escribió DESDE que se lo marcó.

    Sin la marca, un test vería frames de corridas anteriores y daría PASS sin
    que el equipo hubiera transmitido nada.
    """

    def __init__(self, path=LOG):
        self.path = Path(path)
        self.pos = 0

    def disponible(self):
        return self.path.exists()

    def marcar(self):
        self.pos = self.path.stat().st_size if self.path.exists() else 0

    def nuevo(self):
        if not self.path.exists():
            return ""
        with open(self.path, "r", encoding="utf-8", errors="replace") as f:
            f.seek(self.pos)
            return f.read()

    def gets(self):
        """Los frames que llegaron desde la marca: [(query, status), ...]"""
        return RE_GET.findall(self.nuevo())


def campo(query, clave):
    m = re.search(rf"[?&]{clave}=([^&\s]*)", query)
    return m.group(1) if m else None


def log_o_skip():
    srv = LogServidor()

    if not srv.disponible():
        raise Salteado(
            f"no se encuentra el log del servidor en {LOG}. "
            "Levantá apicomms y pasá APICOMMS_LOG=<ruta>"
        )

    return srv


@test("G", "el servidor está vivo (GET directo desde la PC, sin modem)")
def test_servidor_vivo(ctx):
    """⭐ Va PRIMERO a propósito. Si el `lte ping` falla, sin este test no se
    sabe si el problema es el equipo, el enlace LTE o el servidor. Con él, la
    duda se parte en dos en un segundo — y si el servidor no contesta, ni vale
    la pena encender el modem.
    """
    url = f"{URL}?ID=000000000000000&HW=SPQ_ARM_R1&TYPE=FWDLGARM&VER=0.0.0&CLASS=PING"

    try:
        with urllib.request.urlopen(url, timeout=10) as r:
            cuerpo = r.read().decode("utf-8", errors="replace")
    except urllib.error.URLError as e:
        raise AssertionError(
            f"el servidor no contesta en {URL}: {e}\n"
            "Levantalo:  cd ~/Spymovil/python/proyectos/APICOMMS_2025 && "
            "source .venv/bin/activate && python -m apicomms.app"
        ) from None

    print(f"    {cuerpo.strip()[:60]}")
    assert "PONG" in cuerpo, f"el servidor contestó algo que no es un PONG: {cuerpo[:80]}"


@test("G", "`lte info`: identidad y red, con veredicto", destructivo=True)
def test_info(ctx):
    dlg = ctx["dlg"]

    dlg.cmd("kill wan", timeout=15)      # que la FSM no pise el módulo
    time.sleep(2)
    dlg.cmd("lte on", timeout=20)
    time.sleep(8)                        # el módulo tarda en arrancar

    salida = dlg.cmd("lte esc", timeout=30)
    assert "MODO COMANDO" in salida or "+ok" in salida, (
        "no se pudo entrar en modo comando.\n"
        "lteESC_SIN_A no dice nada del TX; lteESC_SIN_OK PRUEBA que el TX anda."
    )

    salida = dlg.cmd("lte info", timeout=90)

    m = re.search(r"IMEI\D*(\d{15})", salida)
    assert m, "no se pudo leer el IMEI"
    ctx["imei"] = m.group(1)
    print(f"    IMEI {ctx['imei']}")

    # ⚠ Tener señal NO es tener conexión: lo que decide es AT+CIP?. El CSQ 31 es
    # el centinela de "todavía no registrado", no señal excelente.
    assert "ICCID" in salida.upper(), "no se leyó el ICCID: ¿el módulo ve su SIM?"

    ctx["modo_at"] = True


@test("G", "`lte ping`: el PONG vuelve, y el servidor lo registra", destructivo=True)
def test_ping(ctx):
    dlg = ctx["dlg"]
    srv = log_o_skip()

    if ctx.get("modo_at"):
        dlg.cmd("lte exit", timeout=30)   # el ping asume modo TRANSPARENTE
        ctx["modo_at"] = False
        time.sleep(2)

    srv.marcar()
    salida = dlg.cmd("lte ping", timeout=90)

    assert "PONG" in salida, (
        "el equipo no recibió el PONG.\n"
        "⚠ Si la respuesta es el frame IDÉNTICO al enviado, el módulo quedó en\n"
        "modo AT y está ecoando: el `AT+ENTM` no entró."
    )

    time.sleep(1)
    pings = [q for q, st in srv.gets() if campo(q, "CLASS") == "PING"]
    print(f"    el servidor registró {len(pings)} PING")

    assert pings, (
        "el equipo dice que recibió PONG pero el servidor no registró ningún PING.\n"
        "Si el GET directo de esta misma área pasó, el servidor está bien."
    )


@test("G", "⭐ `lte conf`: el contrato del hash cierra (CONFIG=OK)", destructivo=True)
def test_conf(ctx):
    """⭐ `CONFIG=OK` es el criterio de aceptación más fuerte que tiene el
    equipo: significa que **los cinco hashes coinciden con los del servidor**.
    Hasta el 2026-09-22 era estructuralmente imposible, porque el servidor pedía
    un `FLOWC` que el equipo no mandaba.
    """
    dlg = ctx["dlg"]
    srv = log_o_skip()

    srv.marcar()
    salida = dlg.cmd("lte conf", timeout=180)

    if "CONFIG=OK" in salida:
        print("    CONFIG=OK: la configuración coincide con la del servidor")
        return

    # Si pide reconfigurar, se informa QUÉ pide: un bloque que se pide en todas
    # las sesiones es el "tráfico infinito en campo" contra el que advierte
    # cfg_hash.h, y el string del hash es lo único comparable (`config hash`).
    m = re.search(r"el servidor pide reconfigurar:([^\r\n]*)", salida)
    pedidos = m.group(1).strip() if m else "?"

    raise AssertionError(
        f"el servidor NO devolvió CONFIG=OK; pide reconfigurar: {pedidos}\n"
        "Si vuelve a pedir lo mismo en la sesión siguiente, algún campo del hash\n"
        "difiere. Comparar con `config hash`, que imprime el STRING sobre el que\n"
        "se calcula el Pearson — el valor no dice en qué carácter está la\n"
        "diferencia, el string sí."
    )


@test("G", "⭐ `lte data`: los registros llegan, y el servidor los recibe", destructivo=True)
def test_data(ctx):
    """⭐ EL TEST QUE CIERRA EL LAZO: no alcanza con que el equipo diga que
    transmitió. Se cotejan los frames que IMPRIMIÓ contra los que el servidor
    REGISTRÓ, uno por uno y por fecha y hora.
    """
    dlg = ctx["dlg"]
    srv = log_o_skip()

    # Que haya algo que transmitir.
    dlg.cmd("poll", timeout=90)
    dlg.cmd("poll", timeout=90)

    srv.marcar()
    salida = dlg.cmd("lte data", timeout=300)

    # Lo que el equipo dice haber mandado.
    enviados = {
        (campo(f, "DATE"), campo(f, "TIME"))
        for f in RE_FRAME_TX.findall(salida)
        if campo(f, "CLASS") in ("DATA", "DATANR")
    }

    m = re.search(r"OK:\s*(\d+)\s+de\s+(\d+)", salida)
    if m:
        print(f"    el equipo informa: {m.group(1)} de {m.group(2)} confirmados")
        assert m.group(1) == m.group(2), (
            f"sólo {m.group(1)} de {m.group(2)} confirmados: el enlace se cayó a la mitad.\n"
            "⚠ Los no confirmados NO se borran: quedan en la ventana para la próxima."
        )

    # Lo que el servidor dice haber recibido.
    time.sleep(2)
    recibidos = {
        (campo(q, "DATE"), campo(q, "TIME"))
        for q, st in srv.gets()
        if campo(q, "CLASS") in ("DATA", "DATANR")
    }

    print(f"    transmitidos {len(enviados)} · registrados por el servidor {len(recibidos)}")

    assert enviados, "el equipo no transmitió ningún frame de datos"

    faltan = enviados - recibidos
    assert not faltan, (
        f"⛔ {len(faltan)} frame(s) que el equipo dio por transmitidos NO llegaron:\n  "
        + "\n  ".join(f"DATE={d} TIME={t}" for d, t in sorted(faltan))
        + "\n\nSi el equipo confirmó y el servidor no los tiene, mirar la pausa\n"
        "entre frames: sin silencio suficiente el módulo junta dos en un GET.\n"
        "(LTE_DATA_MS_ENTRE_FRAMES son 500 ms contra los 250 de UARTFT.)"
    )


@test("G", "la FSM hace una vuelta entera sola", destructivo=True)
def test_fsm(ctx):
    """El equipo transmitiendo SOLO, que es el criterio del paso 5d: abre la
    sesión, se configura, transmite y se apaga sin que nadie tipee nada."""
    dlg = ctx["dlg"]
    srv = log_o_skip()

    # `kill wan` no tiene vuelta atrás: hay que resetear para que la FSM reviva.
    dlg.reset()
    srv.marcar()

    print("    esperando una vuelta completa de tkWan (hasta 5 min)...")
    dlg.esperar("ONLINE_DATA", timeout=300)
    print("    ✓ llegó a ONLINE_DATA")

    time.sleep(5)
    clases = [campo(q, "CLASS") for q, st in srv.gets()]
    print(f"    el servidor vio: {', '.join(dict.fromkeys(c for c in clases if c))}")

    assert "PING" in clases, "la FSM no mandó el PING"
    assert "CONF_ALL" in clases, "la FSM no mandó el CONF_ALL"
