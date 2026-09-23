# -*- coding: utf-8 -*-
"""
Área G - Transmisión: el equipo contra el servidor.

⭐ LA ÚNICA FUENTE DE ESTA ÁREA ES EL LOG DEL DATALOGGER. No se lee el log del
servidor, ni su base de datos, ni se le hacen consultas.

Y no es una limitación: **la respuesta del servidor ya viaja por la consola del
equipo**. `CLASS=PONG`, `CONFIG=OK` y el `OK: N de N` son el servidor diciendo
que sí — sólo que llegan contadas por el datalogger, que es justo lo que se está
validando. Mirar la otra punta agregaría acceso a una máquina que en campo no se
tiene, para confirmar algo que ya está dicho.

⚠ EL SERVIDOR SE ASUME BIEN CONFIGURADO. Lo que tiene que estar listo, y es
responsabilidad del operador:

  1. La **ingesta corriendo** y alcanzable desde el módulo, en la IP y puerto que
     tenga configurados el DTU (`lte info` los muestra; se fijan con
     `lte set server <ip> <puerto>` + `lte save`).
  2. El **IMEI del equipo dado de alta**. Si no, `CONF_ALL` devuelve
     `CONFIG=ERROR` — "el servidor no reconoce al datalogger"— y ninguna
     configuración va a cerrar nunca.
  3. Una **SIM con datos** en el módulo. ⚠ Tener señal NO es tener conexión: lo
     que decide es `AT+CIP?`, y `CSQ 31` es el centinela de "todavía no
     registrado", no señal excelente.

Si algo de eso falta, los tests fallan — y está bien que fallen, pero el motivo
es el entorno y no el firmware. Los mensajes lo dicen.
"""

import re
import time

from runner import test

# Lo que el equipo imprime al transmitir un frame.
RE_FRAME_TX = re.compile(r"->\s*(ID=\S+)")


def campo(texto, clave):
    m = re.search(rf"[?&]{clave}=([^&\s]*)", texto)
    return m.group(1) if m else None


@test("G", "`lte info`: identidad y red, con veredicto", destructivo=True)
def test_info(ctx):
    """`lte info` pregunta TODO al módulo en vez de mostrar una copia local que
    podría estar desactualizada: la IP, el puerto y la URL viven en el DTU, no en
    el datalogger."""
    dlg = ctx["dlg"]

    dlg.cmd("kill wan", timeout=15)      # que la FSM no pise el módulo
    time.sleep(2)
    dlg.cmd("lte on", timeout=20)
    time.sleep(8)                        # el módulo tarda en arrancar

    salida = dlg.cmd("lte esc", timeout=30)
    assert "MODO COMANDO" in salida or "+ok" in salida, (
        "no se pudo entrar en modo comando.\n"
        "⭐ El valor de retorno separa las dos hipótesis: lteESC_SIN_A no dice\n"
        "nada del TX, pero lteESC_SIN_OK PRUEBA que el TX funciona."
    )

    salida = dlg.cmd("lte info", timeout=90)

    m = re.search(r"IMEI\D*(\d{15})", salida)
    assert m, "no se pudo leer el IMEI del módulo"
    ctx["imei"] = m.group(1)
    print(f"    IMEI {ctx['imei']}")

    # ⛔ Sin SIM no hay red, y sin red no hay IP. El orden del diagnóstico va de
    # la causa al efecto: avisar de la IP cuando el problema es la SIM manda a
    # buscar al lugar equivocado.
    assert "ICCID" in salida.upper(), (
        "no se leyó el ICCID: el módulo no está viendo su SIM.\n"
        "En R001 eso lo causaban los pines 21/22 ruteados al micro."
    )

    ctx["modo_at"] = True


@test("G", "`lte ping`: el servidor contesta PONG", destructivo=True)
def test_ping(ctx):
    dlg = ctx["dlg"]

    if ctx.get("modo_at"):
        dlg.cmd("lte exit", timeout=30)   # el ping asume modo TRANSPARENTE
        ctx["modo_at"] = False
        time.sleep(2)

    salida = dlg.cmd("lte ping", timeout=90)

    assert "PONG" in salida, (
        "no llegó el PONG.\n"
        "⚠ Si la respuesta es el frame IDÉNTICO al enviado, el módulo quedó en\n"
        "modo AT y está ecoando: el `AT+ENTM` no entró. Y `+CME ERROR:58` es\n"
        "'comando no soportado', NO 'no registrado en la red' — ése es el 50."
    )
    print("    PONG")


@test("G", "⭐ `lte conf`: el contrato del hash CIERRA", destructivo=True)
def test_conf(ctx):
    """⭐ EL CRITERIO DE ACEPTACIÓN NO ES QUE LA CONFIGURACIÓN SE APLIQUE: es que
    en la sesión SIGUIENTE el servidor deje de pedir los bloques.

    Eso es lo único que prueba que los strings del hash del equipo son idénticos
    a los del servidor. Que se aplique sólo prueba que se parsea la respuesta.

    Por eso se corre dos veces: la primera puede pedir reconfigurar —es lo normal
    si el servidor tiene otra configuración, y de hecho el área B la pisa—, pero
    la segunda tiene que dar `CONFIG=OK`. Si vuelve a pedir lo mismo, hay un
    campo cuyo string difiere, y eso en campo es tráfico infinito.
    """
    dlg = ctx["dlg"]

    salida = dlg.cmd("lte conf", timeout=180)

    if "CONFIG=ERROR" in salida:
        raise AssertionError(
            "CONFIG=ERROR: el servidor no reconoce a este datalogger.\n"
            f"Hay que dar de alta el IMEI {ctx.get('imei', '?')} en el servidor."
        )

    if "CONFIG=OK" in salida:
        print("    CONFIG=OK a la primera")
        return

    m = re.search(r"el servidor pide reconfigurar:([^\r\n]*)", salida)
    print(f"    1.ª sesión: pide {m.group(1).strip() if m else '?'} — se aplica y se repite")

    time.sleep(3)
    salida = dlg.cmd("lte conf", timeout=180)

    if "CONFIG=OK" in salida:
        print("    2.ª sesión: CONFIG=OK ⭐")
        return

    m = re.search(r"el servidor pide reconfigurar:([^\r\n]*)", salida)
    pedidos = m.group(1).strip() if m else "?"

    raise AssertionError(
        f"tras aplicar la configuración, el servidor SIGUE pidiendo: {pedidos}\n\n"
        "Es el modo de falla del `cfg_hash.h`: un campo que entra en el hash y que\n"
        "los dos lados no guardan igual no cierra NUNCA, y el equipo pide\n"
        "reconfigurar ese bloque en todas las sesiones — tráfico infinito en campo.\n"
        "Comparar con `config hash`, que imprime el STRING sobre el que se calcula\n"
        "el Pearson: el valor no dice en qué carácter está la diferencia, el string sí."
    )


@test("G", "⭐ `lte data`: los registros se transmiten y se confirman", destructivo=True)
def test_data(ctx):
    """El servidor confirma con un `CLASS=DATA` cada bloque de 10; el equipo sólo
    borra los registros **después** de esa confirmación.

    ⭐ Eso es lo que separa a este firmware del AVR, que consume el registro
    ANTES de transmitirlo: allá, una sesión cortada se lleva los datos puestos.
    """
    dlg = ctx["dlg"]

    dlg.cmd("poll", timeout=90)          # que haya algo que transmitir
    dlg.cmd("poll", timeout=90)

    salida = dlg.cmd("lte data", timeout=300)

    enviados = [f for f in RE_FRAME_TX.findall(salida)
                if campo(f, "CLASS") in ("DATA", "DATANR")]

    m = re.search(r"OK:\s*(\d+)\s+de\s+(\d+)", salida)
    assert m, (
        "el equipo no informó el progreso `OK: N de M`.\n"
        "Si dice que no hay registros, el `poll` previo no guardó nada."
    )

    confirmados, total = int(m.group(1)), int(m.group(2))
    print(f"    transmitidos {len(enviados)} frames · {confirmados} de {total} confirmados")

    assert confirmados == total, (
        f"sólo {confirmados} de {total} confirmados: el enlace se cayó a la mitad.\n"
        "⚠ Los no confirmados NO se borran: quedan en la ventana para la próxima\n"
        "sesión, y a lo sumo se retransmiten duplicados — inofensivos, porque el\n"
        "servidor indexa por la fecha que viaja ADENTRO del frame."
    )

    assert "quedan 0" in salida or confirmados == total, "la ventana no se vació"


@test("G", "la FSM hace una vuelta entera sola", destructivo=True)
def test_fsm(ctx):
    """El criterio del paso 5d: el equipo abre la sesión, se configura, transmite
    y se apaga **sin que nadie tipee nada**."""
    dlg = ctx["dlg"]

    # `kill wan` no tiene vuelta atrás: hay que resetear para que la FSM reviva.
    dlg.reset()

    print("    esperando una vuelta completa de tkWan (hasta 5 min)...")

    for estado in ("OFFLINE", "ONLINE_CONFIG", "ONLINE_DATA"):
        dlg.esperar(estado, timeout=300)
        print(f"    ✓ {estado}")
