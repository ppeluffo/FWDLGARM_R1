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

⛔ COMPATIBLE CON PYTHON 3.5 -- NO USAR f-STRINGS.
   El tercer equipo (ulises-sat) corre Python 3.5.2 (Ubuntu 16.04), y las
   f-strings son 3.6+. Un SyntaxError de f-string se detecta al PARSEAR el
   archivo, asi que falla ANTES de ejecutar una sola linea -- un chequeo de
   version adentro del script no se veria nunca. Formatear con .format().

   Verificar antes de copiarlo:  python3 -c "import ast;ast.parse(open('X').read())"
   Buscar f-strings:             grep -nE "f\"|f'" X
   (el `%f` de los formatos de fecha da falso positivo)
"""

import csv
import re
import sys
from collections import OrderedDict
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
    """Devuelve [(datetime, seq, dt_ms, q_real, tramo, q_teorico)] ordenado."""
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
                    float(r["q_teorico_m3h"]),
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
                # ⚠ OrderedDict y NO dict: en Python 3.5 un dict NO conserva
                #   el orden de insercion, asi que el orden de los campos del
                #   frame -- que es significativo -- saldria distinto en 3.5 que
                #   en 3.7+. Verificado: sin esto la salida DIFIERE entre las dos.
                # ⚠ Se corta en el "<-": la consola es compartida, y si tkWan
                #   imprime la respuesta del servidor en medio del registro,
                #   su CLOCK=2610180446 se leeria como un campo de caudal
                #   (paso en el Test 2: una ventana dio 17 millones de m3/h).
                campos = OrderedDict(RE_CAMPO.findall(m.group(2).split("<-")[0]))
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


# ---------------------------------------------------------------------------
# EL REPROCESO DEL EMA
#
# Reproduce `Application/tasks/caudal.{h,c}` sobre los dT que midio la BBB y lo
# compara contra lo que el equipo reporto. Si coinciden, el firmware implementa
# lo que su header dice; si no, el bug esta en el firmware y no en el analisis.
#
# ⭐ Las constantes son las del firmware, y cada una lleva su origen: si alguien
#    las cambia alla, hay que cambiarlas aca.
# ---------------------------------------------------------------------------
CAU_MAX_M3H       = 200.0     # caudal.h: CAUDAL_MAX_M3H
CAU_MIN_M3H       = 3.0       # caudal.h: CAUDAL_MIN_M3H
CAU_MS_ANTIRREB   = 500       # caudal.h: CAUDAL_MS_ANTIRREBOTE
CAU_ALPHA         = 0.5       # caudal.h: CAUDAL_ALPHA
TICK_RATE_HZ      = 512       # FreeRTOSConfig.h: configTICK_RATE_HZ
MS_POR_TICK       = 1000.0 / TICK_RATE_HZ
TICKS_ANTIRREBOTE = int(CAU_MS_ANTIRREB * TICK_RATE_HZ / 1000)   # pdMS_TO_TICKS

RE_TICK   = re.compile(r'tick\s*:\s*(\d+)')
RE_MAGPP  = re.compile(r'magpp\s*:\s*([\d.]+)')
RE_TPOLL  = re.compile(r'timerpoll\s*=\s*(\d+)')
RE_VALID  = re.compile(r'validos\s*:\s*(\d+)\s*pulsos')


def reprocesar(pulsos, magpp, seg_ventana):
    """Corre el algoritmo del firmware sobre los dT del CSV.

    ⭐ LA SUTILEZA QUE DECIDE EL CONTEO: cuando un pulso se descarta -- por
    corto o por imposible -- `caudal_pulso_desde_isr()` hace `return` ANTES de
    `ulUltimoTick = ulTicks`, o sea que **la referencia NO avanza**. El dT del
    pulso siguiente se mide desde el ultimo VALIDO, no desde el descartado, asi
    que NO PUEDE HABER DOS DESCARTES SEGUIDOS. Modelarlo mal mueve el conteo de
    descartes en cientos: con la referencia avanzando da 2009 y sin avanzar
    1328, sobre los mismos datos.

    Devuelve (contadores, ventanas), con ventanas = [(t_cierre, caudal)].
    """
    fK = magpp * 3600.0 * 1000.0          # caudal.c: fK, en ms * m3/h
    ms_zero = fK / CAU_MIN_M3H            # caudal.c: fMsZero

    validos = cortos = imposibles = 0
    ema = suma = acum_ms = 0.0
    muestras = 0
    ema_arrancado = hay_ref = False
    ventanas = []

    t_ult_valido = pulsos[0][0]
    prox_cierre = pulsos[0][0] + timedelta(seconds=seg_ventana)

    for p in pulsos:
        t, dt_ms = p[0], p[2]

        # --- el cierre de ventana: es `caudal_leer()`, que ademas DECAE ---
        while t >= prox_cierre:
            if hay_ref and ema > 0.0:
                silencio = (prox_cierre - t_ult_valido).total_seconds() * 1000.0
                if silencio > ms_zero:
                    ema = suma = 0.0
                    muestras = 0
                    ema_arrancado = hay_ref = False
            cau = (suma / muestras) if muestras > 0 else ema
            if cau < CAU_MIN_M3H:
                cau = 0.0                 # piso de reporte
            ventanas.append((prox_cierre, cau))
            suma = 0.0
            muestras = 0
            prox_cierre += timedelta(seconds=seg_ventana)

        # --- el pulso, tal como lo ve la ISR ---
        if not hay_ref:
            hay_ref = True
            t_ult_valido = t
            acum_ms = 0.0
            validos += 1
            continue                      # sin dT no hay caudal que calcular

        acum_ms += dt_ms
        ticks = int(round(acum_ms / MS_POR_TICK))

        if ticks < TICKS_ANTIRREBOTE:
            cortos += 1
            continue                      # ⭐ la referencia NO avanza
        q = fK / (ticks * MS_POR_TICK)
        if q > CAU_MAX_M3H:
            imposibles += 1
            continue                      # ⭐ la referencia NO avanza

        validos += 1
        t_ult_valido = t
        acum_ms = 0.0
        if not ema_arrancado:
            ema = q                       # alpha = 1 en el primer intervalo
            ema_arrancado = True
        else:
            ema = (CAU_ALPHA * q) + ((1.0 - CAU_ALPHA) * ema)
        suma += ema
        muestras += 1

    return (validos, cortos, imposibles), ventanas


def leer_ticks(ruta):
    """[(datetime, tick)] de las respuestas de 'status'.

    ⭐ Es el instrumento que mide si el TICK DEL KERNEL corre a 512 Hz. Y no es
    un detalle: el dT del caudal se mide EN TICKS, asi que un tick que atrasa
    sale como caudal de MAS.
    """
    out = []
    with open(ruta, encoding="utf-8", errors="replace") as f:
        for ln in f:
            mt = RE_TS_LOG.match(ln)
            mk = RE_TICK.search(ln)
            if mt and mk:
                out.append((parse_ts(mt.group(1)), int(mk.group(1))))
    return out


def leer_config_equipo(ruta):
    """(magpp, timerpoll) leidos del propio log; None si no estan."""
    magpp = tpoll = None
    with open(ruta, encoding="utf-8", errors="replace") as f:
        for ln in f:
            if magpp is None:
                m = RE_MAGPP.search(ln)
                if m:
                    magpp = float(m.group(1))
            if tpoll is None:
                m = RE_TPOLL.search(ln)
                if m:
                    tpoll = int(m.group(1))
            if magpp is not None and tpoll is not None:
                break
    return magpp, tpoll


def desfase_por_conteo(checkpoints, pulsos):
    """[(t_equipo, desfase_s)] medido CON EL CONTEO, no supuesto cero.

    La BBB no tiene NTP por USB, asi que los dos relojes derivan. El conteo es
    la unica referencia comun: si el equipo dice 'total = N', el instante en que
    la BBB emitio el pulso N - base es el mismo instante fisico.
    """
    if not checkpoints or not pulsos:
        return []
    base = checkpoints[0][1] - emitidos_hasta(pulsos, checkpoints[0][0])
    out = []
    for ck in checkpoints:
        n = ck[1] - base
        if 0 < n <= len(pulsos):
            out.append((ck[0], (ck[0] - pulsos[n - 1][0]).total_seconds()))
    return out


def tramos_del_csv(pulsos):
    """[(tramo, t_ini, t_fin, q_teorico)] en el orden en que corrieron."""
    out = []
    for p in pulsos:
        tr = p[4]
        qt = p[5] if len(p) > 5 else 0.0
        if not out or out[-1][0] != tr:
            out.append([tr, p[0], p[0], qt])
        else:
            out[-1][2] = p[0]
    return out


def reportar_reproceso(pulsos, checkpoints, registros, ruta_log):
    """Imprime la comparacion firmware vs reproceso, y mide el TICK."""
    magpp, tpoll = leer_config_equipo(ruta_log)
    if magpp is None or tpoll is None:
        print("\n  [!] no se pudo leer magpp/timerpoll del log (hace falta un")
        print("      checkpoint con 'cnt' y 'status' respondidos): sin eso el")
        print("      reproceso no se puede correr. Con TERM_SENSE al aire el")
        print("      equipo no recibe comandos y no hay respuestas.")
        return

    cont, ventanas = reprocesar(pulsos, magpp, tpoll)
    print('  constantes: magpp {:.3f} | alpha {:.2f} | Qmax {:.0f} | Qmin {:.0f}'
          ' | antirreb {} ms'.format(magpp, CAU_ALPHA, CAU_MAX_M3H,
                                     CAU_MIN_M3H, CAU_MS_ANTIRREB))
    print('              tick {} Hz -> {:.3f} ms por tick | ventana {} s'.format(
        TICK_RATE_HZ, MS_POR_TICK, tpoll))

    # ---------- el TICK: se mide primero, porque explica todo lo de abajo ----
    ticks = leer_ticks(ruta_log)
    tasa_global = None
    if len(ticks) >= 2:
        print("\n  EL TICK DEL KERNEL (de las respuestas de 'status')")
        dt = (ticks[-1][0] - ticks[0][0]).total_seconds()
        dk = ticks[-1][1] - ticks[0][1]
        if dt > 0:
            tasa_global = dk / dt
            print('    sobre {:.2f} h: {:.3f} ticks/s  ->  {:+.3f} % contra {} Hz'.format(
                dt / 3600.0, tasa_global, 100.0 * (tasa_global - TICK_RATE_HZ) / TICK_RATE_HZ,
                TICK_RATE_HZ))
            if abs(tasa_global - TICK_RATE_HZ) / TICK_RATE_HZ > 0.002:
                print("    ⛔ EL TICK ATRASA, y NO es cosmetico: el dT del caudal se")
                print("       mide EN TICKS, asi que un tick lento da dT cortos y")
                print("       caudal de MAS, en la misma proporcion.")
                print("       Causa conocida: `vPortSuppressTicksAndSleep()` descarta")
                print("       el resto al convertir cuentas del LPTIM en ticks, y con")
                print("       el contador despertando al micro eso pasa en cada pulso.")
                print("       El arreglo es arrastrar ese resto a la despertada")
                print("       siguiente (port_lptim_tick.c).")

    # ---------- contadores: la comparacion que NO depende de la hora --------
    ult = checkpoints[-1]
    base = checkpoints[0][1] - emitidos_hasta(pulsos, checkpoints[0][0])
    # ⚠ El latido 'CNT::' trae total y pendientes, NO validos: aquel cuenta
    #   TODOS los flancos (drv_pulsos) y este solo los que entraron al EMA
    #   (caudal.c). La identidad que los liga es
    #       total = validos + cortos + imposibles
    #   y 'validos' sale del comando 'cnt', que solo responde con TERM_SENSE.
    validos_eq = None
    with open(ruta_log, encoding="utf-8", errors="replace") as f:
        for ln in f:
            m = RE_VALID.search(ln)
            if m:
                validos_eq = int(m.group(1))
    filas = [("total", sum(cont), ult[1] - base),
             ("cortos", cont[1], ult[3]),
             ("imposibles", cont[2], ult[4])]
    if validos_eq is not None:
        filas.insert(1, ("validos", cont[0], validos_eq))
    print("\n  CONTADORES (no dependen de los relojes: es la comparacion dura)")
    print("                     reproceso    el equipo       dif")
    for etq, rep, eq in filas:
        print('    {:<14} {:10d}   {:10d}   {:+7d}'.format(etq, rep, eq, rep - eq))
    print("    (ultimo 'validos' del log; los demas, del ultimo latido)")

    # ---------- el caudal, por tramo ----------
    md = desfase_por_conteo(checkpoints, pulsos)
    if not md:
        return

    def desf(t):
        t0, d0 = md[0]
        t1, d1 = md[-1]
        span = max((t1 - t0).total_seconds(), 1.0)
        return d0 + (t - t0).total_seconds() / span * (d1 - d0)

    BORDE = timedelta(minutes=15)   # cubre la deriva de relojes en los bordes
    print("\n  CAUDAL POR TRAMO (15 min recortados en cada borde, por la deriva)")
    print("    tramo              Qteor    reproceso   el equipo      dif")
    print("    " + "-" * 60)
    for (tr, ini, fin, qteor) in tramos_del_csv(pulsos):
        if (fin - ini) < timedelta(minutes=45):
            continue
        a, b = ini + BORDE, fin - BORDE
        vs = [c for (t, c) in ventanas if a <= t <= b]
        es = []
        for (t_eq, _clave, campos) in registros:
            tb = t_eq - timedelta(seconds=desf(t_eq))
            if a <= tb <= b:
                for k, v in campos.items():
                    if k and k[0] in "QC":
                        try:
                            es.append(float(v))
                        except ValueError:
                            pass
        if not vs or not es:
            continue
        mr = sum(vs) / len(vs)
        me = sum(es) / len(es)
        dif = 100.0 * abs(me - mr) / mr if mr else 0.0
        print('    {:<16} {:7.2f}   {:9.3f}   {:9.3f}   {:6.3f} %'.format(
            tr, qteor, mr, me, dif))
    print()
    print("    ⭐ Una diferencia PAREJA en todos los tramos no es el EMA: es el")
    print("       tick de arriba. El EMA se valida donde el tick esta sano.")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)

    pulsos = leer_generador(sys.argv[1])
    checkpoints, registros, incidentes = leer_equipo(sys.argv[2])

    if not pulsos:
        sys.exit("el CSV del generador no tiene pulsos")

    print("=" * 74)
    print("GENERADOR (BBB)")
    print('  cierres emitidos : {}'.format(len(pulsos)))
    print('  desde            : {}'.format(pulsos[0][0]))
    print('  hasta            : {}'.format(pulsos[-1][0]))
    dur = (pulsos[-1][0] - pulsos[0][0]).total_seconds()
    print('  duracion         : {:.2f} h'.format(dur / 3600))

    print("\nEQUIPO (consola)")
    print('  checkpoints CNT:: : {}'.format(len(checkpoints)))
    print('  registros de poleo: {}'.format(len(registros)))
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
    print('{:^12} {:>6} {:>9} {:>9} {:>7} {:>7} {:>8}'.format('hora', 't+h', 'emitidos', 'contados', 'resta', 'cortos', 'imposib'))

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
        print('{:^12} {:6.2f} {:9d} {:9d} {:7d} {:7d} {:8d}'.format(
            t.strftime('%H:%M:%S'), (t - t_ini).total_seconds() / 3600,
            em, total, resta, cortos, imp))
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
        print('  desfase mediano al empezar : {:+.1f} s'.format(d_ini))
        print('  desfase mediano al terminar: {:+.1f} s'.format(d_fin))
        print('  DERIVA entre los dos relojes: {:+.1f} s'.format(deriva_relojes))
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
            print('     (deriva {:+d} pulsos), asi que el desfase crece'.format(deriva_conteo))
            print("     porque el total del equipo apunta a un pulso anterior.")
            print("     La causa esta abajo, en el conteo.")
        elif abs(deriva_relojes) > 3.0:
            print("  -> los relojes NO estan sincronizados entre si.")
            # El ppm solo tiene sentido sobre una corrida larga: con poco tiempo
            # el ruido del poleo se come el resultado. Es el mismo criterio que
            # wan_rtc_sincronizar(), que no calcula ppm con menos de una hora.
            if dur > 3600:
                ppm = deriva_relojes / dur * 1e6
                print('     {:+.1f} ppm sobre {:.1f} h de corrida.'.format(ppm, dur / 3600))
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
    print('  resta al empezar : {}'.format(r_ini))
    print('  resta al terminar: {}'.format(r_fin))
    print('  DERIVA           : {}'.format(deriva))
    print('  descartes del equipo en el periodo: cortos={} imposibles={}'.format(cortos_tot, imp_tot))

    if deriva == 0:
        print("\n  ** NO SE PERDIO NI UN PULSO en toda la corrida. **")
        print("  (una resta constante distinta de cero es solo el desfase de")
        print("   arranque: el equipo ya tenia cuenta, o un log empezo antes)")
    elif abs(deriva) <= 2:
        # Con relojes que corren libres, +/-1 pulso es el pulso que cae del lado
        # equivocado del borde de un checkpoint. Llamarlo perdida mandaria a
        # buscar un bug por un artefacto del cruce.
        print('\n  ** NO SE PERDIERON PULSOS (deriva {:+d}, dentro del ruido). **'.format(deriva))
        print("  Una deriva de +/-1 o 2 es el pulso que cae del lado equivocado del")
        print("  borde de un checkpoint, sobre todo si los relojes derivaron.")
        print("  LA VERIFICACION QUE NO DEPENDE DE LA HORA es la de los totales:")
        if checkpoints:
            print('    emitidos por la BBB      : {}'.format(len(pulsos)))
            print('    ultimo total del equipo  : {}'.format(checkpoints[-1][1]))
            print("    (difieren en los pulsos emitidos DESPUES del ultimo poleo)")
    elif deriva > 0:
        print('\n  [!] EL EQUIPO CONTO {} PULSOS MENOS que los emitidos.'.format(deriva))
        if cortos_tot >= deriva * 0.8:
            print("      Y 'cortos' subio {}: el caso esta cerrado -- el".format(cortos_tot))
            print("      contacto reboto y el anti-rebote de 500 ms los descarto.")
            print("      Eso acusa al CIRCUITO DE ENTRADA, no al firmware.")
        elif imp_tot >= deriva * 0.8:
            print("      Y 'imposibles' subio {}: pulsos mas rapidos que".format(imp_tot))
            print("      CAUDAL_MAX_M3H. Revisar el magpp del equipo contra el del")
            print("      generador: tienen que ser EL MISMO.")
        else:
            print("      Y los descartes NO lo explican, asi que es lo que hay que")
            print("      investigar. El primer checkpoint donde la resta crece dice")
            print("      cuando empezo: cruzar esa hora contra el tramo del CSV.")
    else:
        print('\n  [!] EL EQUIPO CONTO {} PULSOS MAS que los emitidos.'.format(-deriva))
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
        print('  campos de medida en el registro: {}'.format(', '.join(nombres)))
        print('  registros con medida: {}'.format(len(con_caudal)))
        reportar_reproceso(pulsos, checkpoints, registros, sys.argv[2])

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
            print('    {:<10} {:5d} de {:5d} libres -> usado {:4d} ({:4.1f} %)'.format(tk, libre, tot, usado, 100.0 * usado / tot))
    if uarts:
        print('  errores de UART (maximo visto): {}'.format(uarts))
    print("  lineas 'arrancando' (indicio de resets): {}".format(arranques))

    if incidentes:
        print('\n  INCIDENTES ({}), los primeros 25:'.format(len(incidentes)))
        for t, ln in incidentes[:25]:
            h = t.strftime("%H:%M:%S") if t else "  ?  "
            print('    {}  {}'.format(h, ln[:110]))
    else:
        print("\n  sin incidentes")

    return 0


if __name__ == "__main__":
    sys.exit(main())
