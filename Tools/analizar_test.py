#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Cruza el CSV del generador de la BBB con el log de la consola del datalogger y
contesta la pregunta del test: SE PERDIO ALGUN PULSO?

  python3 analizar_test.py pulsos_2026....csv equipo_2026....log

ESTE SCRIPT SE ESCRIBIO ANTES DE CORRER EL TEST, A PROPOSITO: es la forma de
verificar que los dos logs son cruzables. Si faltara un dato, se descubre ahora
y no despues de 48 h.

EL CRUCE: por timestamp absoluto. El equipo emite en cada poleo

    CNT:: total=14402 pendientes=3 cortos=0 imposibles=0

y el logger le pone SU hora delante. Para cada una de esas lineas se cuenta
cuantos cierres habia emitido la BBB hasta ese instante y se comparan.

LA RESTA NO ES EL RESULTADO, ES EL PRINCIPIO: lo que importa es COMO evoluciona.
Una resta constante es un desfase de arranque (el equipo ya tenia cuenta, o el
generador arranco antes) y es inofensiva. Una resta que CRECE son pulsos que se
pierden, y ahi el momento en que empieza a crecer dice dONDE mirar.

Y LOS DESCARTES CIERRAN EL CASO SOLOS: si faltan 5 pulsos y 'cortos' subio 5,
no hay misterio -- el contacto reboto y el filtro RC los comio. 'cortos' acusa
al circuito de entrada; 'imposibles', a la cadencia (ver caudal.h).
"""

import csv
import re
import sys
from datetime import datetime, timedelta

RE_CNT = re.compile(r"CNT::\s*total=(\d+)\s+pendientes=(\d+)\s+cortos=(\d+)\s+imposibles=(\d+)")
RE_TS_LOG = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})")
# El registro del equipo: "12/10/26 15:38:07;CAU0=29.950;V0=0;..."
RE_REG = re.compile(r"(\d\d/\d\d/\d\d \d\d:\d\d:\d\d);(.*)")
RE_CAMPO = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=(-?[\d.]+|SIN_DATO)")
RE_STACK = re.compile(r"(tk\w+)\s*:\s*(\d+) de (\d+)")
RE_UART = re.compile(r"(\w+)\s+errores\s*:\s*(\d+)")


def parse_ts(s):
    return datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f")


def leer_generador(ruta):
    """Devuelve [(datetime, seq, dt_ms, q_real)] ordenado."""
    out = []
    with open(ruta, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            try:
                out.append((
                    parse_ts(r["timestamp"]),
                    int(r["seq"]),
                    float(r["dt_real_ms"]),
                    float(r["q_real_m3h"]),
                    r["tramo"],
                ))
            except (ValueError, KeyError):
                continue
    out.sort(key=lambda x: x[0])
    return out


def leer_equipo(ruta):
    """Devuelve (checkpoints, registros, incidentes)."""
    checkpoints, registros, incidentes = [], [], []
    with open(ruta, encoding="utf-8", errors="replace") as f:
        for ln in f:
            mt = RE_TS_LOG.match(ln)
            t = parse_ts(mt.group(1)) if mt else None

            m = RE_CNT.search(ln)
            if m and t:
                checkpoints.append((t, int(m.group(1)), int(m.group(2)),
                                    int(m.group(3)), int(m.group(4))))
                continue

            m = RE_REG.search(ln)
            if m and t:
                campos = dict(RE_CAMPO.findall(m.group(2)))
                if campos:
                    registros.append((t, m.group(1), campos))

            # Lo que hay que mirar sin que nadie lo pida
            bajo = ln.lower()
            for clave in ("watchdog:", "reset por", "arrancando", "[x]", "[!]",
                          "se perdio", "no valida", "error", "brownout", "bor"):
                if clave in bajo:
                    incidentes.append((t, ln.rstrip()))
                    break
    return checkpoints, registros, incidentes


def emitidos_hasta(pulsos, t):
    """Cuantos cierres habia emitido la BBB hasta el instante t (binaria)."""
    lo, hi = 0, len(pulsos)
    while lo < hi:
        mid = (lo + hi) // 2
        if pulsos[mid][0] <= t:
            lo = mid + 1
        else:
            hi = mid
    return lo


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)

    pulsos = leer_generador(sys.argv[1])
    checkpoints, registros, incidentes = leer_equipo(sys.argv[2])

    if not pulsos:
        sys.exit("el CSV del generador no tiene pulsos")

    print("=" * 74)
    print("GENERADOR (BBB)")
    print(f"  cierres emitidos : {len(pulsos)}")
    print(f"  desde            : {pulsos[0][0]}")
    print(f"  hasta            : {pulsos[-1][0]}")
    dur = (pulsos[-1][0] - pulsos[0][0]).total_seconds()
    print(f"  duracion         : {dur/3600:.2f} h")

    print("\nEQUIPO (consola)")
    print(f"  checkpoints CNT:: : {len(checkpoints)}")
    print(f"  registros de poleo: {len(registros)}")
    if not checkpoints:
        print("\n  [!] NO HAY NINGUNA LINEA 'CNT::' EN EL LOG.")
        print("      Sin eso el conteo NO se puede verificar. Causas posibles:")
        print("        - el contador esta DESHABILITADO en la config del equipo")
        print("          ('config counter ...' + 'config save'): el latido solo")
        print("          sale si xCfgCounter.bEnabled es true")
        print("        - el firmware es anterior al 0.0.19+ (el latido no existe)")
        return 1

    # ---------- EL CRITERIO DE ACEPTACION ----------
    print("\n" + "=" * 74)
    print("CONTEO: emitidos por la BBB  vs  contados por el equipo")
    print("=" * 74)
    print(f"{'hora':^12} {'t+h':>6} {'emitidos':>9} {'contados':>9} {'resta':>7} "
          f"{'cortos':>7} {'imposib':>8}")

    t_ini = checkpoints[0][0]
    filas = []
    for (t, total, pend, cortos, imp) in checkpoints:
        em = emitidos_hasta(pulsos, t)
        filas.append((t, em, total, em - total, cortos, imp))

    # Se muestran las primeras, las ultimas, y TODAS donde la resta cambia: una
    # tabla de 192 checkpoints no se lee, y lo que importa son los cambios.
    resta0 = filas[0][3]
    mostrar = set([0, len(filas) - 1])
    for i, f in enumerate(filas):
        if f[3] != resta0:
            mostrar.add(max(0, i - 1)); mostrar.add(i)
            resta0 = f[3]
    for i in list(range(min(3, len(filas)))) + list(range(max(0, len(filas) - 3), len(filas))):
        mostrar.add(i)

    ult = -2
    for i in sorted(mostrar):
        if i > ult + 1:
            print("      ...")
        t, em, total, resta, cortos, imp = filas[i]
        print(f"{t.strftime('%H:%M:%S'):^12} {(t-t_ini).total_seconds()/3600:6.2f} "
              f"{em:9d} {total:9d} {resta:7d} {cortos:7d} {imp:8d}")
        ult = i

    r_ini_, r_fin_ = filas[0][3], filas[-1][3]
    deriva_conteo = r_fin_ - r_ini_

    # ======================================================================
    # LA DERIVA DE LOS RELOJES, MEDIDA CON LOS PROPIOS DATOS
    # ======================================================================
    # La BBB no siempre tiene NTP (por USB al laptop no sale a internet), asi que
    # su reloj corre libre: 10-50 ppm son 1-4 s/dia, hasta ~8 s en 48 h. Y con
    # pulsos cada 1,8-36 s, 8 s mueven varios pulsos de lado en el cruce -- lo
    # que se leeria como una resta que CRECE, o sea perdidas que no existen.
    #
    # Se mide al reves: en vez de usar los relojes para verificar el conteo, se
    # usa EL CONTEO para medir el desfase de los relojes. Para cada checkpoint,
    # el equipo dice que conto N pulsos; el instante en que la BBB emitio SU
    # pulso N es un punto de referencia comun. La diferencia entre ese instante y
    # la hora del checkpoint es (latencia del poleo + desfase de relojes).
    #
    # El termino de latencia es ruidoso --el poleo cae en cualquier momento del
    # periodo-- asi que se usa la MEDIANA de los primeros y de los ultimos
    # checkpoints: el ruido se promedia y queda el desfase.
    def desfases(sub):
        out = []
        for (t, total, _p, _c, _i) in sub:
            if 1 <= total <= len(pulsos):
                out.append((t - pulsos[total - 1][0]).total_seconds())
        return sorted(out)

    def mediana(v):
        return v[len(v) // 2] if v else None

    k = max(3, len(checkpoints) // 10)
    d_ini = mediana(desfases(checkpoints[:k]))
    d_fin = mediana(desfases(checkpoints[-k:]))
    deriva_relojes = (d_fin - d_ini) if (d_ini is not None and d_fin is not None) else None

    if deriva_relojes is not None:
        print("\n" + "-" * 74)
        print("RELOJES")
        print("-" * 74)
        print(f"  desfase mediano al empezar : {d_ini:+.1f} s")
        print(f"  desfase mediano al terminar: {d_fin:+.1f} s")
        print(f"  DERIVA entre los dos relojes: {deriva_relojes:+.1f} s")
        # LAS DOS CAUSAS DE QUE ESE DESFASE CREZCA SON DISTINTAS, y se separan
        # mirando la resta de CONTEOS:
        #
        #   conteos cuadran + desfase crece  -> son los RELOJES
        #   conteos NO cuadran               -> el desfase crece COMO CONSECUENCIA
        #                                       (el total apunta a un pulso
        #                                       anterior al que corresponde)
        #
        # Sin esta distincion, una perdida de pulsos se informaba como "los
        # relojes no estan sincronizados" -- acusando al instrumento equivocado,
        # que es justo lo que este script existe para evitar.
        if abs(deriva_conteo) > 2:
            print("  -> OJO: este desfase NO mide los relojes. Los conteos no cuadran")
            print(f"     (deriva {deriva_conteo:+d} pulsos), asi que el desfase crece")
            print("     porque el total del equipo apunta a un pulso anterior.")
            print("     La causa esta abajo, en el conteo.")
        elif abs(deriva_relojes) > 3.0:
            print("  -> los relojes NO estan sincronizados entre si.")
            # El ppm solo tiene sentido sobre una corrida larga: con poco tiempo
            # el ruido del poleo se come el resultado. Es el mismo criterio que
            # wan_rtc_sincronizar(), que no calcula ppm con menos de una hora.
            if dur > 3600:
                ppm = deriva_relojes / dur * 1e6
                print(f"     {ppm:+.1f} ppm sobre {dur/3600:.1f} h de corrida.")
            else:
                print("     (corrida demasiado corta para estimar ppm)")
            print("     El conteo de abajo sigue siendo valido -- compara TOTALES,")
            print("     que no dependen de la hora -- pero el 'cuando' de una")
            print("     perdida tiene esa incertidumbre. Para la proxima: NTP en la")
            print("     BBB, o 'ssh bbb \"sudo date -u -s ...\"' antes de arrancar.")
        else:
            print("  -> los dos relojes se mantuvieron juntos: el cruce es confiable")

    r_ini, r_fin = filas[0][3], filas[-1][3]
    deriva = r_fin - r_ini
    cortos_tot = filas[-1][4] - filas[0][4]
    imp_tot = filas[-1][5] - filas[0][5]

    print("\n" + "-" * 74)
    print("VEREDICTO")
    print("-" * 74)
    print(f"  resta al empezar : {r_ini}")
    print(f"  resta al terminar: {r_fin}")
    print(f"  DERIVA           : {deriva}")
    print(f"  descartes del equipo en el periodo: cortos={cortos_tot} imposibles={imp_tot}")

    if deriva == 0:
        print("\n  ** NO SE PERDIO NI UN PULSO en toda la corrida. **")
        print("  (una resta constante distinta de cero es solo el desfase de")
        print("   arranque: el equipo ya tenia cuenta, o un log empezo antes)")
    elif abs(deriva) <= 2:
        # Con relojes que corren libres, +/-1 pulso es el pulso que cae del lado
        # equivocado del borde de un checkpoint. Llamarlo perdida mandaria a
        # buscar un bug por un artefacto del cruce.
        print(f"\n  ** NO SE PERDIERON PULSOS (deriva {deriva:+d}, dentro del ruido). **")
        print("  Una deriva de +/-1 o 2 es el pulso que cae del lado equivocado del")
        print("  borde de un checkpoint, sobre todo si los relojes derivaron.")
        print("  LA VERIFICACION QUE NO DEPENDE DE LA HORA es la de los totales:")
        if checkpoints:
            print(f"    emitidos por la BBB      : {len(pulsos)}")
            print(f"    ultimo total del equipo  : {checkpoints[-1][1]}")
            print("    (difieren en los pulsos emitidos DESPUES del ultimo poleo)")
    elif deriva > 0:
        print(f"\n  [!] EL EQUIPO CONTO {deriva} PULSOS MENOS que los emitidos.")
        if cortos_tot >= deriva * 0.8:
            print(f"      Y 'cortos' subio {cortos_tot}: el caso esta cerrado -- el")
            print("      contacto reboto y el anti-rebote de 500 ms los descarto.")
            print("      Eso acusa al CIRCUITO DE ENTRADA, no al firmware.")
        elif imp_tot >= deriva * 0.8:
            print(f"      Y 'imposibles' subio {imp_tot}: pulsos mas rapidos que")
            print("      CAUDAL_MAX_M3H. Revisar el magpp del equipo contra el del")
            print("      generador: tienen que ser EL MISMO.")
        else:
            print("      Y los descartes NO lo explican, asi que es lo que hay que")
            print("      investigar. El primer checkpoint donde la resta crece dice")
            print("      cuando empezo: cruzar esa hora contra el tramo del CSV.")
    else:
        print(f"\n  [!] EL EQUIPO CONTO {-deriva} PULSOS MAS que los emitidos.")
        print("      Eso NO puede venir del generador: o hay ruido entrando por el")
        print("      borne, o el contacto rebota y el filtro no lo alcanza a comer.")
        print("      Mirar con 'cnt' y el osciloscopio en PA12.")

    # ---------- caudal ----------
    con_caudal = [(t, c) for (t, _, c) in registros
                  if any(k not in ("V0", "bt3v3", "bt12v") for k in c)]
    if con_caudal:
        print("\n" + "=" * 74)
        print("CAUDAL REPORTADO (para cruzar contra q_real del CSV)")
        print("=" * 74)
        nombres = [k for k in con_caudal[0][1] if k not in ("V0", "bt3v3", "bt12v")]
        print(f"  campos de medida en el registro: {', '.join(nombres)}")
        print(f"  registros con medida: {len(con_caudal)}")
        print("\n  ⏳ El reproceso del EMA (alpha=1 en el primer intervalo, 0.5")
        print("     despues, decay por silencio, descarte de Q>200, piso de 3)")
        print("     se corre sobre la columna dt_real_ms del CSV y se compara")
        print("     contra estos valores. Si coinciden, el firmware implementa lo")
        print("     que dice; si no, el bug esta en el firmware y no en el analisis.")

    # ---------- estabilidad ----------
    print("\n" + "=" * 74)
    print("ESTABILIDAD (el soak)")
    print("=" * 74)
    stacks, uarts, arranques = {}, {}, 0
    with open(sys.argv[2], encoding="utf-8", errors="replace") as f:
        for ln in f:
            for tk, libre, tot in RE_STACK.findall(ln):
                v = int(libre)
                if tk not in stacks or v < stacks[tk][0]:
                    stacks[tk] = (v, int(tot))
            if "arrancando" in ln:
                arranques += 1
            m = RE_UART.search(ln)
            if m and m.group(1) in ("UART4", "TERM", "RS485"):
                uarts[m.group(1)] = max(uarts.get(m.group(1), 0), int(m.group(2)))

    if stacks:
        print("  stack libre MINIMO por tarea (el peor de toda la corrida):")
        for tk, (libre, tot) in sorted(stacks.items()):
            usado = tot - libre
            print(f"    {tk:<10} {libre:5d} de {tot:5d} libres -> usado {usado:4d} "
                  f"({100.0*usado/tot:4.1f} %)")
    if uarts:
        print(f"  errores de UART (maximo visto): {uarts}")
    print(f"  lineas 'arrancando' (indicio de resets): {arranques}")

    if incidentes:
        print(f"\n  INCIDENTES ({len(incidentes)}), los primeros 25:")
        for t, ln in incidentes[:25]:
            h = t.strftime("%H:%M:%S") if t else "  ?  "
            print(f"    {h}  {ln[:110]}")
    else:
        print("\n  sin incidentes")

    return 0


if __name__ == "__main__":
    sys.exit(main())
