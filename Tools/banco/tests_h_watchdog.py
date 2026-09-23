# -*- coding: utf-8 -*-
"""
Área H - El watchdog.

⚠ EL RIESGO PRINCIPAL DE ESTA ÁREA NO ES QUE EL PERRO NO MUERDA: es que muerda
cuando no debe. Un falso positivo resetea al equipo en operación sana, y eso es
**peor que no tener watchdog**. Por eso la mayoría de estos tests verifican que
el equipo SIGUE VIVO después de algo que antes lo reseteaba.
"""

import re
import time

from dlg import Timeout
from runner import pedir, test

RE_VIGILADA = re.compile(r"(tk\w+)\s+(vigilada|MATADA|sin registrar)")
RE_RESTAN = re.compile(r"(tk\w+)\s+vigilada, le quedan (-?\d+) ms")


def sigue_vivo(dlg, segundos, cada=20):
    """Espera `segundos` confirmando que el equipo no se reinició.

    Detecta el reset por el BANNER: si aparece, el equipo arrancó de nuevo y el
    test falló. Mirar sólo "responde al final" no alcanza — un equipo que se
    reseteó también responde.
    """
    dlg.limpiar()
    fin = time.time() + segundos

    while time.time() < fin:
        time.sleep(min(cada, max(0, fin - time.time())))
        texto = dlg.texto()

        assert "consola TERM" not in texto, (
            f"⛔ EL EQUIPO SE REINICIÓ durante la espera.\n"
            "Es un FALSO POSITIVO del watchdog: reseteó en operación sana.\n"
            "El aviso de tkCtl dice qué tarea se pasó del plazo."
        )

        print(f"    ... {int(fin - time.time())} s")


@test("H", "el perro corre y vigila a las 5 tareas")
def test_perro(ctx):
    dlg = ctx["dlg"]
    salida = dlg.cmd("wdg", timeout=15)

    assert "CORRIENDO" in salida, "el IWDG no está corriendo"

    restan = {t: int(ms) for t, ms in RE_RESTAN.findall(salida)}
    assert len(restan) == 5, f"se vigilan {len(restan)} tareas, se esperaban 5"

    for t, ms in sorted(restan.items()):
        print(f"    {t}: {ms} ms")

    # Los valores normales oscilan entre 30 y 90 s: todas reportan cada 45 o 60
    # contra un plazo de 90. Por debajo de 20 s el margen está justo y hay que
    # mirar ESA tarea, no subir el plazo a ciegas.
    apretadas = [f"{t} ({ms} ms)" for t, ms in restan.items() if ms < 20000]
    assert not apretadas, f"margen apretado en: {', '.join(apretadas)}"


@test("H", "`kill` desregistra: matar una tarea NO resetea el equipo", destructivo=True)
def test_kill_desregistra(ctx):
    """⭐ Sin el `wdg_stop_task()` este test reinicia el equipo a los ~2 minutos.

    Una tarea suspendida deja de reportar, así que si el watchdog la siguiera
    vigilando la daría por colgada — y resetearía **justo mientras el operador
    trabaja el módulo a mano**, que es el síntoma más desconcertante posible.
    """
    dlg = ctx["dlg"]

    dlg.cmd("kill wan", timeout=15)

    # ⛔ LA MUERTE ES COOPERATIVA Y HAY QUE ESPERARLA. `wan_pedir_kill()` sólo
    # levanta una bandera; la tarea se suspende **en su propio lazo**, después de
    # apagar el modem y soltar el candado de energía. Y tkWan puede estar en un
    # trozo de espera de 60 s, así que tarda hasta una vuelta entera.
    #
    # ⚠ Ésa fue una falla de este test, no del firmware: leía `wdg` de inmediato
    # y veía `vigilada`, que es lo correcto — la tarea todavía corre y todavía
    # reporta. Durante esa ventana NO hay riesgo de reset, justamente porque
    # sigue viva.
    estado = None
    for _ in range(18):          # hasta 90 s
        salida = dlg.cmd("wdg", timeout=15)
        estado = dict(RE_VIGILADA.findall(salida)).get("tkWan")

        if estado == "MATADA":
            break

        time.sleep(5)

    print(f"    tkWan: {estado}")

    assert estado == "MATADA", (
        f"tras 90 s del `kill wan`, tkWan quedó como {estado!r} y no MATADA.\n"
        "Si sigue 'vigilada', falta el wdg_stop_task() en su prvMatarse() y el\n"
        "equipo se va a resetear en cuanto venza su plazo."
    )

    # El plazo son 90 s: 150 da margen de sobra para que se dispare si estuviera mal.
    print("    esperando 150 s para confirmar que NO se resetea...")
    sigue_vivo(dlg, 150)

    salida = dlg.cmd("status", timeout=20)
    assert "MATADA" in salida or "matada" in salida.lower(), "tkWan debería seguir matada"

    # ⚠ No hay forma de revivir una tarea, y es a propósito: después de trabajar
    # a mano se resetea para que el equipo arranque limpio.
    dlg.reset()


@test("H", "`lte bridge` abierto no resetea el equipo", destructivo=True)
def test_bridge(ctx):
    """⛔ Antes del kick propio esto reseteaba el equipo a los 90 s.

    Es el caso más realista de todos: el puente es la herramienta que un técnico
    deja abierta mientras piensa qué comando AT mandar.
    """
    dlg = ctx["dlg"]

    dlg.cmd("kill wan", timeout=15)   # que tkWan no pise el módulo

    dlg.limpiar()
    dlg.enviar("lte bridge")
    time.sleep(2)

    assert "puente abierto" in dlg.texto(), "no se abrió el puente"

    print("    puente abierto, esperando 150 s sin tipear...")
    sigue_vivo(dlg, 150)

    dlg._ser.write(b"\x04")           # Ctrl-D
    dlg._ser.flush()
    dlg.esperar("puente cerrado", timeout=15)
    print("    puente cerrado")

    dlg.reset()


@test("H", "el volcado a la microSD tiene su prórroga", manual=True, destructivo=True)
def test_dump_sd(ctx):
    """La prórroga de 120 s: `fs_sd_volcar_ventana()` no vuelve hasta terminar."""
    dlg = ctx["dlg"]

    pedir(
        "poné una microSD en el datalogger (cualquiera, se va a escribir).",
        "Si no tenés ninguna a mano, salteá con 's'.",
    )

    dlg.cmd("poll", timeout=60)       # que haya al menos un registro que volcar
    salida = dlg.cmd("fs sd dump", timeout=180)

    assert "consola TERM" not in salida, (
        "⛔ el equipo se RESETEÓ durante el volcado: la prórroga de 120 s se quedó corta"
    )
    print(f"    {salida.strip().splitlines()[-2][:70]}")


@test("H", "el formateo de la microSD tiene su prórroga", manual=True, destructivo=True)
def test_format_sd(ctx):
    """La prórroga de 300 s. Es la operación más larga del firmware: escribe las
    dos copias de la FAT **sector por sector**, porque `drv_sd` no expone
    escritura múltiple."""
    dlg = ctx["dlg"]

    pedir(
        "⚠ esto BORRA la microSD entera. ¿Seguimos?",
        "La tarjeta queda formateada en FAT32 por el propio datalogger.",
    )

    t0 = time.time()
    salida = dlg.cmd("fs sd format borrar", timeout=420)
    dt = time.time() - t0

    assert "consola TERM" not in salida, (
        f"⛔ el equipo se RESETEÓ a los {dt:.0f} s durante el formateo:\n"
        "la prórroga de 300 s se quedó corta para esta tarjeta."
    )
    print(f"    formateo terminado en {dt:.0f} s")


@test("H", "⭐ el perro MUERDE: cuelgue provocado y reset por IWDG", manual=True, destructivo=True)
def test_muerde(ctx):
    """⭐ LA PRUEBA QUE VALIDA EL MECANISMO ENTERO.

    Un watchdog que nunca se vio morder no es un watchdog validado: es código que
    compila. `wdg colgar` mete a `tkCmd` en un lazo infinito sin reportar; a los
    ~90 s `tkCtl` avisa y deja de patear, y ~32 s después el IWDG resetea.

    ⭐ Y lo que cierra la prueba no es el reset: es que la CAUSA quede registrada
    como IWDG. Ése es todo el argumento de haber usado el perro de hardware en
    vez de un `NVIC_SystemReset()`, que se informaría como `SOFT` —
    indistinguible de un `reset` tipeado por un técnico.
    """
    dlg = ctx["dlg"]

    salida = dlg.cmd("wdg colgar", timeout=15, espera_prompt=False)

    if "no reconocido" in salida.lower() or "not found" in salida.lower():
        raise AssertionError(
            "el firmware no tiene `wdg colgar`: hay que flashear 0.0.75 o posterior"
        )

    assert "COLGANDO" in salida, "el comando no avisó que iba a colgar"
    print("    tkCmd colgada. Esperando el aviso de tkCtl (~90 s)...")

    try:
        dlg.esperar("WATCHDOG", timeout=140)
        print("    ✓ tkCtl avisó que una tarea se pasó del plazo")
    except Timeout:
        print("    (no se vio el aviso de tkCtl; se sigue esperando el reset)")

    print("    esperando el reset del IWDG (~32 s más)...")
    nombre, version = dlg.esperar_arranque(timeout=90)
    print(f"    ✓ el equipo reinició: {nombre} {version}")

    salida = dlg.cmd("status", timeout=20)
    m = re.search(r"reset por\s*:(.*)", salida)
    causa = m.group(1).strip() if m else "?"
    print(f"    reset por: {causa}")

    assert "IWDG" in causa, (
        f"el equipo reinició pero la causa es {causa!r} y no IWDG.\n"
        "Sin esa marca, en campo no hay forma de saber si el equipo se reinició\n"
        "solo o lo reinició un técnico — y es lo que viaja en el campo WDG."
    )


@test("H", "⭐ la causa del reset viaja al servidor como WDG=3", manual=True, destructivo=True)
def test_wdg_al_servidor(ctx):
    """⭐ EL PASO QUE CIERRA EL ARGUMENTO ENTERO DE USAR EL IWDG.

    Que el perro muerda y que `status` diga `IWDG` se ve desde la consola. Pero
    **en campo no hay consola**: lo único que llega es el campo `WDG` del
    `CONF_BASE`, y ahí `3` (`wanRESET_IWDG`) es lo que distingue "el equipo se
    reinició solo" de `4` (`SOFT`), indistinguible de un `reset` tipeado por un
    técnico. Con un `NVIC_SystemReset()` este test sería imposible de pasar, y
    esa diferencia es toda la razón del diseño.

    ⭐ Se verifica sobre el frame que el equipo IMPRIME, no sobre el log del
    servidor: lo que el datalogger informa es responsabilidad suya, y así el test
    sigue sirviendo con el servidor en otra máquina o sin acceso a sus logs.
    """
    dlg = ctx["dlg"]

    pedir(
        "este test CUELGA el equipo a propósito y espera que el watchdog lo resetee.",
        "Son ~2 min de reset más ~2 min de sesión con el servidor.",
    )

    salida = dlg.cmd("wdg colgar", timeout=15, espera_prompt=False)
    assert "COLGANDO" in salida, "el firmware no tiene `wdg colgar` (hace falta 0.0.75+)"

    print("    colgada. Esperando el reset del watchdog (~2 min)...")
    dlg.esperar_arranque(timeout=180)

    salida = dlg.cmd("status", timeout=25)
    m = re.search(r"reset por\s*:(.*)", salida)
    causa = m.group(1).strip() if m else "?"
    print(f"    reset por: {causa}")
    assert "IWDG" in causa, f"la causa quedó como {causa!r} y no IWDG"

    # Y que lo CUENTE: tkWan arranca sola y manda el CONF_ALL con el campo WDG.
    print("    esperando que tkWan arme el CONF_ALL...")
    dlg.limpiar()
    m = dlg.esperar(re.compile(r"CLASS=CONF_ALL\S*"), timeout=300)

    wdg = re.search(r"[?&]WDG=(\d+)", m.group(0))
    print(f"    el equipo informa WDG={wdg.group(1) if wdg else '?'}")

    assert wdg and wdg.group(1) == "3", (
        f"el CONF_ALL lleva WDG={wdg.group(1) if wdg else '?'} y se esperaba 3.\n"
        "⚠ 4 es SOFT: es lo que informaría un NVIC_SystemReset(), o sea que la\n"
        "causa real del reinicio se habría perdido justo donde importa — en campo."
    )
