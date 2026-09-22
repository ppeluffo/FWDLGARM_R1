# -*- coding: utf-8 -*-
"""
Área B - Configuración y hashes.  ⭐ El corazón de la regresión.

POR QUÉ ESTA ÁREA VALE MÁS QUE LAS DEMÁS: sus valores esperados **ya están
validados contra el servidor real**, no contra el propio equipo. `AH=0xD7` se
predijo corriendo la lógica de `get_ainputs_hash_from_config()` del servidor en
el host, se aplicó en el equipo y el servidor lo aceptó (2026-09-11). Una suite
cuyos números salen de la otra punta del contrato detecta cosas que una que
compara el equipo consigo mismo no puede ver.

⚠ EL HASH NO ES UN CHECKSUM DE LA STRUCT: es un Pearson de 8 bits sobre un
STRING FORMATEADO. Si difiere un solo carácter —un `%03d` que salga `%d`, un
`TRUE` en minúscula, un decimal de más— el servidor pide reconfigurar ese bloque
**en cada sesión, para siempre**. Eso no se ve en el banco: se ve como tráfico
infinito en campo. Por eso esta área existe.

⚠ Estos tests PISAN la configuración del equipo. Es lo que los hace
deterministas, y está acordado: la suite corre sobre equipos de prueba.
"""

import json
import re
from pathlib import Path

from runner import Salteado, test

RE_HASHES = re.compile(
    r"BH=0x([0-9A-Fa-f]{2})\s+AH=0x([0-9A-Fa-f]{2})\s+CH=0x([0-9A-Fa-f]{2})\s+"
    r"MH=0x([0-9A-Fa-f]{2})\s+PH=0x([0-9A-Fa-f]{2})"
)

REFERENCIA = Path(__file__).parent / "referencia.json"

# La base sobre la que están calculados los BH de abajo. No cambiarla sin
# recalcular los cinco valores.
BASE_REF = [
    "config timerpoll 60",
    "config timerdial 3600",
    "config pwron 0630",
    "config pwroff 1800",
]

# ⭐ Validados contra el servidor. El hash lleva el NÚMERO del modo
# (`[PWRMODO:%d]`), así que estos valores también prueban que el enum no se
# reordenó — reordenarlo invalidaría la configuración de todos los equipos.
BH_POR_MODO = {
    "continuo": "8D",
    "discreto": "66",
    "mixto": "41",
    "rtu": "DF",
    "silent": "D4",
}

# Los tres canales que producen AH=0xD7 con PST=15.
AINPUTS_REF = [
    "config ainput 0 false PPR 4 20 0 10 0",
    "config ainput 1 false X 4 20 0 25 0",
    "config ainput 2 false X 4 20 0 60 0",
]

# ⭐ Cuatro puntos, no uno: el PST cambia el hash ENTERO, así que cuatro valores
# coincidiendo es evidencia mucho más fuerte que uno solo.
AH_POR_PST = {"0": "3F", "10": "C1", "15": "D7", "20": "8D"}


def leer_hashes(dlg, timeout=25):
    """Los cinco hashes, tal como los imprime `config`."""
    salida = dlg.cmd("config", timeout=timeout)
    m = RE_HASHES.search(salida)

    assert m, (
        "no se pudieron leer los hashes de `config`.\n"
        f"lo último que llegó:\n  " + "\n  ".join(salida.strip().splitlines()[-10:])
    )

    return dict(zip(("BH", "AH", "CH", "MH", "PH"), (g.upper() for g in m.groups())))


# ⚠ Los errores del firmware salen al PRINCIPIO de línea, como `ERROR: ...` o
# `CFG:: ERROR: ...`. El patrón está anclado a propósito: el buffer incluye el
# log asíncrono de las otras tareas, y ahí aparecen cosas como `+CME ERROR:50`
# de `tkWan` que NO son errores de este comando. Buscar "ERROR" suelto daría
# FAILs falsos justo cuando el equipo está transmitiendo, que es lo normal.
RE_ERROR = re.compile(r"^\s*(?:ERROR:|\w+:: ERROR)", re.M)


def aplicar(dlg, comandos):
    for c in comandos:
        salida = dlg.cmd(c, timeout=15)
        m = RE_ERROR.search(salida)
        assert not m, (
            f"el equipo rechazó `{c}`:\n  "
            + "\n  ".join(salida[m.start():].strip().splitlines()[:3])
        )


@test("B", "los 5 modos de energía dan los BH validados contra el servidor", destructivo=True)
def test_bh_por_modo(ctx):
    dlg = ctx["dlg"]

    dlg.cmd("config default", timeout=20)
    aplicar(dlg, BASE_REF)

    malos = []
    for modo, esperado in BH_POR_MODO.items():
        dlg.cmd(f"config pwrmodo {modo}", timeout=15)
        h = leer_hashes(dlg)
        ok = h["BH"] == esperado
        print(f"    {modo:9s} BH=0x{h['BH']}  {'✓' if ok else f'✗ esperaba 0x{esperado}'}")

        if not ok:
            malos.append(f"{modo}: 0x{h['BH']} != 0x{esperado}")

    assert not malos, (
        "el hash de `base` no coincide con el del servidor:\n  " + "\n  ".join(malos)
        + "\n\nMirar `config hash`, que imprime el STRING sobre el que se calcula\n"
        "el Pearson: el valor no dice en qué carácter está la diferencia, el\n"
        "string sí."
    )

    dlg.cmd("config pwrmodo continuo", timeout=15)


@test("B", "el PST cambia el hash de ainputs como predice el servidor", destructivo=True)
def test_ah_por_pst(ctx):
    dlg = ctx["dlg"]

    # Los tres canales se fijan explícitamente en vez de confiar en los
    # defaults: así el test es determinista aunque los defaults cambien.
    aplicar(dlg, AINPUTS_REF)

    malos = []
    for pst, esperado in AH_POR_PST.items():
        dlg.cmd(f"config pst {pst}", timeout=15)
        h = leer_hashes(dlg)
        ok = h["AH"] == esperado
        print(f"    PST={pst:3s}  AH=0x{h['AH']}  {'✓' if ok else f'✗ esperaba 0x{esperado}'}")

        if not ok:
            malos.append(f"PST={pst}: 0x{h['AH']} != 0x{esperado}")

    assert not malos, (
        "el hash de `ainputs` no coincide con el del servidor:\n  " + "\n  ".join(malos)
    )

    dlg.cmd("config pst 15", timeout=15)


@test("B", "la configuración sobrevive a un reset", destructivo=True)
def test_persistencia(ctx):
    dlg = ctx["dlg"]

    antes = leer_hashes(dlg)
    salida = dlg.cmd("config save", timeout=25)
    assert "configuracion grabada" in salida, (
        "`config save` no confirmó la grabación con 'CFG:: configuracion grabada'"
    )

    dlg.reset()
    despues = leer_hashes(dlg)

    print(f"    antes:   {' '.join(f'{k}=0x{v}' for k, v in antes.items())}")
    print(f"    después: {' '.join(f'{k}=0x{v}' for k, v in despues.items())}")

    assert antes == despues, (
        "la configuración NO sobrevivió al reset.\n"
        "Si además la consola dijo que un bloque cayó a defaults, el checksum de\n"
        "ese bloque no cerró: mirar el mensaje del arranque, que dice cuál."
    )

    ctx["hashes_ref"] = despues


@test("B", "las validaciones rechazan lo que tienen que rechazar", destructivo=True)
def test_validaciones(ctx):
    dlg = ctx["dlg"]

    # imin >= imax no puede convertir nada: el AVR devolvería su centinela -999.
    salida = dlg.cmd("config ainput 0 true pA 20 4 0 10 0", timeout=15)

    assert RE_ERROR.search(salida), (
        "el equipo ACEPTÓ una calibración con imin >= imax.\n"
        "Una calibración imposible guardada pasa el checksum y falla recién en campo."
    )

    # Un codec mal tipeado tiene que rechazarse con la lista de válidos.
    salida = dlg.cmd("config modbus channel 0 true q 9 1 2 3 U32 C1023 0", timeout=15)
    assert RE_ERROR.search(salida) or "codec" in salida.lower(), (
        "el equipo aceptó el codec inexistente C1023 (dedazo por C1032)"
    )


@test("B", "los 5 hashes no se movieron desde la última corrida", destructivo=True)
def test_regresion_hashes(ctx):
    """⭐ El test de regresión propiamente dicho.

    Los BH y el AH de arriba tienen valores conocidos porque están validados
    contra el servidor. Para `CH`, `MH` y `PH` no hay un valor publicado, así que
    lo que se compara es **contra la corrida anterior**: el primer run escribe
    `referencia.json` y los siguientes detectan cualquier cambio.

    ⚠ Un cambio acá no es necesariamente un bug — puede ser un campo nuevo
    legítimo—, pero **tiene que ser deliberado**: si el hash se mueve sin que
    nadie lo haya querido, el servidor va a pedir reconfigurar ese bloque en
    todas las sesiones.
    """
    dlg = ctx["dlg"]
    actual = ctx.get("hashes_ref") or leer_hashes(dlg)

    if not REFERENCIA.exists():
        REFERENCIA.write_text(json.dumps(actual, indent=2) + "\n")
        print(f"    referencia creada: {REFERENCIA.name}")
        print(f"    {' '.join(f'{k}=0x{v}' for k, v in actual.items())}")
        raise Salteado("primera corrida: se guardó la referencia, no hay con qué comparar")

    previo = json.loads(REFERENCIA.read_text())
    cambios = [f"{k}: 0x{previo[k]} -> 0x{actual[k]}" for k in actual if previo.get(k) != actual[k]]

    print(f"    {' '.join(f'{k}=0x{v}' for k, v in actual.items())}")

    assert not cambios, (
        "hashes distintos de la referencia:\n  " + "\n  ".join(cambios)
        + f"\n\nSi el cambio es deliberado, borrá {REFERENCIA.name} y volvé a correr."
    )
