#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
runner.py - el framework: registro de tests, pausas interactivas y reporte.

⭐ POR QUÉ UN RUNNER PROPIO Y NO PYTEST

Pytest daría reporting gratis, pero acá los tests son **largos, secuenciales y
con estado compartido** (el equipo), que es justo lo que pytest no modela bien:
no hay aislamiento posible entre casos, el orden importa, y varios necesitan que
una persona haga algo a la mitad. Son ~150 líneas contra un conjunto de
conceptos que no aportan nada a esto.

⚠ UN SKIP Y UN FAIL TIENEN QUE VERSE DISTINTO. Una corrida sin el esclavo Modbus
conectado no puede ensuciar el reporte con fallas falsas, o a la segunda vez
nadie mira el resultado.
"""

import argparse
import sys
import time
import traceback

from dlg import Timeout, abrir

PASS, FAIL, SKIP = "PASS", "FAIL", "SKIP"

_TESTS = []

VERDE = "\033[32m"
ROJO = "\033[31m"
AMAR = "\033[33m"
GRIS = "\033[2m"
NEGRITA = "\033[1m"
FIN = "\033[0m"


def test(area, nombre, manual=False, destructivo=False):
    """Registra un test. `area` es la letra del mapa (A..I)."""

    def deco(fn):
        _TESTS.append(
            {
                "area": area,
                "nombre": nombre,
                "fn": fn,
                "manual": manual,
                "destructivo": destructivo,
            }
        )
        return fn

    return deco


class Salteado(Exception):
    """El operador decidió saltear, o falta una condición previa."""


def pedir(mensaje, detalle=None):
    """Pausa para que el operador haga algo. Devuelve cuando confirma.

    Es lo que convierte al script en algo que ORQUESTA la sesión de banco en vez
    de limitarse a lo automatizable: el paso manual queda adentro de la secuencia
    y su resultado se verifica igual que el de cualquier otro.
    """
    print()
    print(f"{AMAR}{'─' * 66}{FIN}")
    print(f"{AMAR}✋ INTERVENCIÓN: {mensaje}{FIN}")

    if detalle:
        for linea in detalle.splitlines():
            print(f"{AMAR}   {linea}{FIN}")

    print(f"{AMAR}   [Enter] cuando esté listo   ·   [s] saltear este test{FIN}")
    print(f"{AMAR}{'─' * 66}{FIN}")

    try:
        rta = input("   > ").strip().lower()
    except (EOFError, KeyboardInterrupt):
        raise Salteado("el operador cortó")

    if rta == "s":
        raise Salteado("salteado por el operador")


def _fmt_contexto(exc, dlg):
    """El motivo del fallo, con lo último que dijo el equipo. Sin esto hay que
    volver a correr la prueba para entender qué pasó."""
    if isinstance(exc, Timeout):
        cola = exc.visto.strip().splitlines()[-12:]
        txt = [f"esperaba: {exc.que}"]
        txt.append("lo que llegó:")
        txt += [f"  │ {l}" for l in cola] or ["  │ (nada)"]
        return "\n".join(txt)

    if isinstance(exc, AssertionError) and str(exc):
        return str(exc)

    return "".join(traceback.format_exception_only(type(exc), exc)).strip()


def correr(dlg, areas=None, destructivos=True, manuales=True, version_esperada=None):
    ctx = {"dlg": dlg, "version_esperada": version_esperada}
    resultados = []

    tests = [t for t in _TESTS if not areas or t["area"] in areas]

    if not destructivos:
        tests = [t for t in tests if not t["destructivo"]]

    # Sin esto la suite no se puede dejar corriendo sola: un test manual bloquea
    # en `input()` hasta que alguien vuelva.
    if not manuales:
        tests = [t for t in tests if not t["manual"]]

    print(f"\n{NEGRITA}{len(tests)} tests · log en {dlg.log_path}{FIN}\n")

    for t in tests:
        etiqueta = f"{t['area']} · {t['nombre']}"
        marcas = []

        if t["manual"]:
            marcas.append("✋")
        if t["destructivo"]:
            marcas.append("⚠")

        print(f"\n{NEGRITA}▸ {etiqueta}{FIN} {' '.join(marcas)}")

        t0 = time.time()

        try:
            t["fn"](ctx)
            estado, motivo = PASS, ""
        except Salteado as e:
            estado, motivo = SKIP, str(e)
        except KeyboardInterrupt:
            print(f"\n{AMAR}interrumpido por el operador{FIN}")
            estado, motivo = SKIP, "interrumpido"
            resultados.append((t, estado, motivo, time.time() - t0))
            break
        except Exception as e:  # noqa: BLE001 - cualquier fallo es un FAIL
            estado, motivo = FAIL, _fmt_contexto(e, dlg)

        dt = time.time() - t0
        color = {PASS: VERDE, FAIL: ROJO, SKIP: AMAR}[estado]
        print(f"  {color}{estado}{FIN} {GRIS}({dt:.1f} s){FIN}")

        if motivo and estado != PASS:
            for linea in motivo.splitlines():
                print(f"    {color}{linea}{FIN}")

        resultados.append((t, estado, motivo, dt))

    return resultados


def reporte(resultados, dlg):
    print(f"\n{NEGRITA}{'═' * 66}{FIN}")
    print(f"{NEGRITA}RESUMEN{FIN}\n")

    por_area = {}
    for t, estado, _, _ in resultados:
        por_area.setdefault(t["area"], []).append(estado)

    for area in sorted(por_area):
        e = por_area[area]
        ok = e.count(PASS)
        mal = e.count(FAIL)
        sk = e.count(SKIP)
        linea = f"  {area}: {ok} PASS"

        if mal:
            linea += f"  {ROJO}{mal} FAIL{FIN}"
        if sk:
            linea += f"  {AMAR}{sk} SKIP{FIN}"

        print(linea)

    fallas = [(t, m) for t, e, m, _ in resultados if e == FAIL]

    if fallas:
        print(f"\n{ROJO}{NEGRITA}FALLARON:{FIN}")
        for t, motivo in fallas:
            print(f"  {ROJO}· {t['area']} · {t['nombre']}{FIN}")

    total = len(resultados)
    ok = sum(1 for _, e, _, _ in resultados if e == PASS)
    print(f"\n  {ok}/{total} PASS · log completo en {dlg.log_path}")

    # ⛔ Que nadie lea "todo PASS" como más de lo que es.
    print(
        f"\n{GRIS}  ⛔ Esta suite NO valida consumo. Con la terminal conectada el\n"
        f"     equipo corre en Sleep (~3,5 mA), nunca en Stop 2: el bug del\n"
        f"     tickless comiéndose bytes y los 82 µA del pull-up de SD_DET no\n"
        f"     habrían aparecido acá. Las mediciones siguen siendo con el tester\n"
        f"     y el ST-LINK desenchufado del USB.{FIN}"
    )

    return 1 if fallas else 0


def main(descripcion="suite de banco del datalogger"):
    ap = argparse.ArgumentParser(description=descripcion)
    ap.add_argument("-p", "--puerto", default="/dev/ttyUSB0")
    ap.add_argument(
        "-a",
        "--area",
        help="letras de área a correr, p.ej. 'AB' (por omisión, todas)",
    )
    ap.add_argument(
        "--version",
        dest="version_esperada",
        help="versión que se espera en el banner, p.ej. 0.0.75",
    )
    ap.add_argument(
        "--sin-destructivos",
        action="store_true",
        help="saltea lo que pisa configuración o formatea la microSD",
    )
    ap.add_argument(
        "--sin-manuales",
        action="store_true",
        help="saltea los tests que necesitan que alguien haga algo (para correr desatendido)",
    )
    ap.add_argument("--sin-eco", action="store_true", help="no vuelca el serial a pantalla")
    args = ap.parse_args()

    areas = set(args.area.upper()) if args.area else None

    dlg = abrir(args.puerto, eco=not args.sin_eco)

    try:
        resultados = correr(
            dlg,
            areas=areas,
            destructivos=not args.sin_destructivos,
            manuales=not args.sin_manuales,
            version_esperada=args.version_esperada,
        )
        return reporte(resultados, dlg)
    finally:
        dlg.cerrar()


if __name__ == "__main__":
    sys.exit(main())
