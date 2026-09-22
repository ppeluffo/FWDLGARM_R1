# -*- coding: utf-8 -*-
"""
Área A - Identidad y arranque.

Todo automático y sin nada conectado: es la que decide si vale la pena correr el
resto. Si el equipo no arranca sano, los fallos de las demás áreas no significan
nada.
"""

import re

from runner import test

# Los 24 comandos de la tabla de ayuda de `tkCmd.c`.
COMANDOS = [
    "help", "status", "sense", "i2c", "ee", "rtc", "rs485", "modbus",
    "cpres", "ina", "sd", "vin", "cnt", "ev", "lte", "config", "kill",
    "poll", "frame", "cls", "fs", "keys", "wdg", "reset",
]

RE_IDENTIDAD = re.compile(r"identidad\s*:\s*HW=(\S+)\s+TYPE=(\S+)\s+VER=(\S+)")
RE_TICK = re.compile(r"tick\s*:\s*\d+\s*\((\d+) Hz\)")
RE_CLOCK = re.compile(r"clock\s*:\s*(\d+) Hz")
RE_STACK = re.compile(r"(tk\w+)\s*:\s*(\d+) de (\d+)")
RE_HEAP = re.compile(r"heap libre\s*:\s*(\d+) bytes")
RE_WDG_TAREA = re.compile(r"(tk\w+)\s+(vigilada|MATADA|sin registrar)")


@test("A", "arranca y el banner dice la versión esperada", destructivo=True)
def test_banner(ctx):
    dlg = ctx["dlg"]
    nombre, version = dlg.reset()

    ctx["version"] = version
    print(f"    {nombre} {version}")

    esperada = ctx.get("version_esperada")

    # ⭐ Lo que esto evita: probar un binario distinto del que uno cree. Cuando
    # se puso la regla de subir FW_VERSION en cada entrega, el banner decía
    # 0.0.8 mientras los tags de git iban por v0.0.14.
    assert not esperada or version == esperada, (
        f"el equipo corre {version} y se esperaba {esperada}.\n"
        "¿Se flasheó el binario nuevo? ¿Se subió FW_VERSION en main.h?"
    )


@test("A", "la tabla de comandos NO se llenó")
def test_tabla_comandos(ctx):
    """⛔ Es el bug que ya pasó: al registrar el comando 17 con la tabla en 16,
    `reboot` quedó **sin registrar en silencio**. El driver lo avisaba, pero el
    mensaje sale en medio del chorro de arranque y pasa inadvertido; desde afuera
    el síntoma era un comando que 'no existe'.
    """
    dlg = ctx["dlg"]
    texto = dlg.texto()

    assert "tabla de comandos llena" not in texto, (
        "el firmware avisó que la tabla de comandos está llena: hay comandos\n"
        "registrados que NO existen. Subir CMDLINE_MAX_COMMANDS en frtos_cmd.h."
    )


@test("A", "`help` lista los 24 comandos")
def test_help(ctx):
    dlg = ctx["dlg"]
    salida = dlg.cmd("help", timeout=15)

    faltan = [c for c in COMANDOS if not re.search(rf"^\s*{c}\b", salida, re.M)]

    # ⚠ Registrar un comando y agregarlo al `help` son DOS pasos independientes,
    # y el segundo es fácil de olvidar porque el comando funciona igual. Ya pasó
    # con `modbus` y `cpres`.
    assert not faltan, f"no figuran en el help: {', '.join(faltan)}"


@test("A", "`status` es coherente")
def test_status(ctx):
    dlg = ctx["dlg"]
    salida = dlg.cmd("status", timeout=15)

    m = RE_IDENTIDAD.search(salida)
    assert m, "no se pudo leer la línea 'identidad'"

    hw, tipo, ver = m.groups()
    print(f"    HW={hw} TYPE={tipo} VER={ver}")

    # Los tres campos que viajan en el frame. El TYPE va SIN la revisión de
    # placa (se corrigió el 2026-09-11); el HW SÍ la lleva.
    assert hw == "SPQ_ARM_R1", f"HW={hw}, se esperaba SPQ_ARM_R1"
    assert tipo == "FWDLGARM", f"TYPE={tipo}, se esperaba FWDLGARM (sin _R1)"
    assert ver == ctx.get("version", ver), "la versión de status no coincide con el banner"

    m = RE_TICK.search(salida)
    assert m and m.group(1) == "512", (
        f"el tick está en {m.group(1) if m else '?'} Hz y tiene que ser 512.\n"
        "512 sale EXACTO del cristal de 32.768 kHz; otro valor deriva."
    )

    m = RE_CLOCK.search(salida)
    assert m and int(m.group(1)) == 60_000_000, (
        f"SystemCoreClock = {m.group(1) if m else '?'}, se esperaban 60 MHz"
    )


@test("A", "los stacks y el heap tienen margen")
def test_memoria(ctx):
    dlg = ctx["dlg"]
    salida = dlg.cmd("status", timeout=15)

    stacks = RE_STACK.findall(salida)
    assert stacks, "no se pudieron leer los stacks"

    # El *high water mark* es el MÍNIMO libre desde que arrancó la tarea. Un
    # 15 % de margen es holgado; por debajo hay que mirar esa tarea.
    apretados = []
    for tarea, libre, total in stacks:
        pct = 100 * int(libre) / int(total)
        print(f"    {tarea}: {libre}/{total} libres ({pct:.0f} %)")
        if pct < 15:
            apretados.append(f"{tarea} ({pct:.0f} %)")

    # ⚠ Estos números son de Debug (-O0), que usa MÁS stack que Release: van en
    # la dirección segura, pero no son los que van a valer en campo.
    assert not apretados, (
        f"stack apretado en: {', '.join(apretados)}.\n"
        "Un desborde en FreeRTOS no da error: corrompe la memoria de al lado."
    )

    m = RE_HEAP.search(salida)
    assert m, "no se pudo leer el heap"
    print(f"    heap: {m.group(1)} bytes libres")


@test("A", "el watchdog está corriendo y vigila a las 5 tareas")
def test_wdg(ctx):
    dlg = ctx["dlg"]
    salida = dlg.cmd("wdg", timeout=15)

    assert "CORRIENDO" in salida, (
        "el IWDG no está corriendo. Nada del área H tiene sentido sin esto."
    )

    tareas = dict(RE_WDG_TAREA.findall(salida))
    print(f"    {', '.join(f'{k}={v}' for k, v in tareas.items())}")

    esperadas = {"tkCmd", "tkSys", "tkWan", "tkCtlPres", "tkFlow"}
    faltan = esperadas - set(tareas)
    assert not faltan, f"no figuran en la tabla del watchdog: {', '.join(sorted(faltan))}"

    # ⭐ Lo más útil que informa `wdg`: una tarea que arrancó y quedó **sin
    # registrar** no se está vigilando, y eso desde afuera no se nota de
    # ninguna otra forma.
    sin_vigilar = [t for t, e in tareas.items() if e == "sin registrar"]
    assert not sin_vigilar, (
        f"NO se están vigilando: {', '.join(sin_vigilar)}.\n"
        "Falta su wdg_registrar(), o la tarea no llegó a arrancar."
    )
