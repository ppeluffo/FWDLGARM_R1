#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BeagleBone Black: generador de pulsos de caudalimetro para el test de 48 h del
contador CNT0 del datalogger SPQ_ARM (firmware FWDLGARM_R1).

  sudo python3 generador_pulsos.py              # perfil de 48 h
  sudo python3 generador_pulsos.py --horas 2    # version corta, para ensayar
  sudo python3 generador_pulsos.py --barrido    # barrido de frecuencia (aparte)
  sudo python3 generador_pulsos.py --laptop     # sin GPIO, para probar el script

QUE PRUEBA, Y CUAL ES EL CRITERIO DE ACEPTACION
-----------------------------------------------
Uno solo, y todo lo demas se deriva de el:

    los cierres que emitio la BBB  ==  los pulsos que conto el datalogger

El equipo lo informa solo en cada poleo, sin que nadie tipee nada:

    CNT:: total=14402 pendientes=3 cortos=0 imposibles=0

Este script produce el OTRO lado de esa igualdad. Los dos logs se cruzan por
timestamp despues del test.

EL RELE (Panasonic TX-S) NO ES LA RESTRICCION, y esta verificado:

    vida mecanica       min. 50 x 10^6 ope.   <- 33.000 en 48 h = 0,07 %
    carga minima        10 uA / 10 mV DC      <- la real son 4,9 mA / 12 V
    contacto            Ag+Au clad            <- el bueno para carga baja
    operate / release   max. 5 ms c/u

=> PULSE_WIDTH_S NO PUEDE SER 10 ms. El simulador del AVR usaba ese valor, y
   con un operate de hasta 5 ms el contacto quedaria cerrado apenas ~5 ms,
   contra el umbral de 4,8 ms del filtro RC de la placa: MARGEN CERO, justo la
   zona donde el firmware cuenta o no cuenta de forma erratica. Con 50 ms el
   cierre efectivo son ~45 ms, o sea 9x el umbral.

EL RELE INVIERTE LA LOGICA, y por eso la polaridad queda bien:

    pin en BAJO  -> rele suelto     -> contacto ABIERTO  (reposo de campo)
    pin en ALTO  -> rele activado   -> contacto CERRADO  (el pulso)

Con el contacto abierto en reposo, R40 (el pull-up de 10 K del colector) NO
drena: a 50 ms cada 12 s el ciclo de trabajo es del 0,4 %, o sea ~1,3 uA
promedio contra los 312 uA que costaria el contacto cerrado permanente.

EL PRIMER PULSO DE CADA TRAMO ES DE TRANSICION: su columna q_teorico ya es la
del tramo nuevo, y su dt_real tambien respeta el periodo nuevo (ver el comentario
de tren()), pero el caudal que el equipo calcule en ese pulso es el del cambio.
Al analizar hay que usar SIEMPRE q_real --derivado del dt_real MEDIDO-- y no
q_teorico: son iguales salvo en las transiciones y cuando el jitter pega.

REQUISITOS:  apt install python3-libgpiod gpiod

⛔ COMPATIBLE CON PYTHON 3.5 -- NO USAR f-STRINGS.
   El tercer equipo (ulises-sat) corre Python 3.5.2 (Ubuntu 16.04), y las
   f-strings son 3.6+. Un SyntaxError de f-string se detecta al PARSEAR el
   archivo, asi que falla ANTES de ejecutar una sola linea -- un chequeo de
   version adentro del script no se veria nunca. Formatear con .format().

   Verificar antes de copiarlo:  python3 -c "import ast;ast.parse(open('X').read())"
   Buscar f-strings:             grep -nE "f\"|f'" X
   (el `%f` de los formatos de fecha da falso positivo)
"""

import argparse
import atexit
import csv
import os
import random
import signal
import sys
import time
from datetime import datetime
from pathlib import Path

# ==================== CONFIG ==============================================
# Los dos pines que manejan los dos reles. Mismos que el simulador del AVR
# (simulador_claude_R004.py), verificados ahi:
#   P9_12 -> gpiochip0 linea 28     P9_15 -> gpiochip0 linea 16
# OJO: el mapeo pin -> (chip,linea) depende de como enumera el kernel de ESTA
# BBB. Verificar con `gpioinfo | grep -E 'P9_12|P9_15'`.
GPIO_PINS = [
    ("gpiochip0", 28),          # rele 1
    ("gpiochip0", 16),          # rele 2
]
GPIO_CONSUMER = "generador_pulsos"

PULSE_WIDTH_S = 0.050           # 50 ms: ver la cabecera. NO bajar de ~20 ms.
MAGPP_M3      = 0.1             # m3 por pulso. Mismo que los escenarios del AVR.
JITTER_PCT    = 2.0            # +/- % del periodo, no acumulativo

RELE_VIDA_MECANICA = 50_000_000  # del datasheet TX-S

# El perfil de UN DIA, que se repite. (caudal_m3h, segundos, etiqueta)
# caudal 0 = silencio.
#
# dT_zero = magpp * 3600000 / CAUDAL_MIN_M3H = 0.1*3600000/3 = 120 s, asi que
# un silencio tiene que pasar de 120 s para disparar el decay a cero. Los de
# 300 s lo pasan con margen y dejan ver ademas el REARRANQUE, que es un camino
# propio (alpha = 1 en el primer intervalo).
PERFIL_DIA = [
    (0,      180, "silencio_inicial"),   # arranque desde cero
    (30,    6*3600, "regimen_bajo"),     # noche: regimen estable, el EMA converge
    (150,   4*3600, "pico_manana"),      # ESCALON x5: sin clamp deberia tomar ~5 pulsos
    (60,    4*3600, "medio"),
    (200,   1*3600, "maximo"),           # el tope: periodo 1,8 s
    (10,    3*3600, "cerca_del_umbral"), # Q bajo, cerca de CAUDAL_MIN_M3H = 3
    (0,      300, "silencio_decay"),     # > dT_zero: decay a cero Y rearranque
    (80,    5*3600, "tarde"),
    (5,      3000, "bajo_umbral"),       # por encima de 3 pero cerca
]

# El perfil de la PRUEBA CORTA, para validar la cadena entera antes de largar
# las 48 h: ~55 min y recorre los mismos caminos que el perfil largo.
#
# NO se usa --escala para esto: la escala divide TODO, incluidos los silencios, y
# un silencio de 6 s no dispara el decay a cero (hace falta > dT_zero = 120 s).
# O sea que una prueba escalada dejaria sin ejercitar justo el camino que mas
# conviene ver antes de dejar el equipo 48 h solo.
PERFIL_PRUEBA = [
    (0,      150, "silencio_inicial"),   # > dT_zero: arranque limpio desde cero
    (30,     600, "regimen"),            # ~50 pulsos: el EMA converge
    (150,    480, "escalon_arriba"),     # ~200 pulsos: seguimiento sin clamp
    (200,    300, "maximo"),             # ~166 pulsos: periodo 1,8 s
    (0,      150, "silencio_decay"),     # decay a cero Y rearranque
    (60,     600, "medio"),              # ~100 pulsos
    (10,     480, "bajo"),               # ~13 pulsos: Q bajo, cerca del umbral
    (80,     540, "final"),              # ~120 pulsos
]

# El barrido de frecuencia, que va APARTE del test de 48 h: busca el LIMITE del
# filtro RC (techo documentado ~60 Hz, nunca verificado), no la ausencia de
# perdidas. Son ~5.000 operaciones, nada para 50 M.
BARRIDO_HZ   = [5, 10, 20, 30, 45, 60, 80]
BARRIDO_SEG  = 30

# ==================== estado global =======================================
_gpio_chips = {}
_gpio_lines = []
_laptop = False
_seq = 0
_relay_ops = 0
_t0_wall = None


def ts():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def log(msg):
    linea = '[{}] {}'.format(ts(), msg)
    print(linea, flush=True)
    if _flog:
        _flog.write(linea + "\n")
        _flog.flush()


_flog = None

# ==================== GPIO ================================================
def gpio_init():
    global _gpio_chips, _gpio_lines
    if _laptop:
        log("MODO LAPTOP: sin GPIO, no se toca ningun rele")
        return
    import gpiod                      # libgpiod 1.x (Debian 12 bookworm)
    for chip_name, line_off in GPIO_PINS:
        if chip_name not in _gpio_chips:
            _gpio_chips[chip_name] = gpiod.Chip(chip_name)
        line = _gpio_chips[chip_name].get_line(line_off)
        line.request(
            consumer=GPIO_CONSUMER,
            type=gpiod.LINE_REQ_DIR_OUT,
            default_vals=[0],         # reposo BAJO = rele suelto = contacto ABIERTO
        )
        _gpio_lines.append(line)
        log('GPIO salida: {} linea {} (reposo BAJO = contacto abierto)'.format(chip_name, line_off))
    atexit.register(gpio_cleanup)


def gpio_cleanup():
    """Deja los reles SUELTOS (contacto abierto). Importa: un rele que quede
    pegado deja R40 drenando 312 uA y el contador en bajo permanente."""
    global _gpio_chips, _gpio_lines
    if _laptop:
        return
    for line in _gpio_lines:
        try:
            line.set_value(0)
            line.release()
        except Exception:
            pass
    _gpio_lines = []
    for chip in _gpio_chips.values():
        try:
            chip.close()
        except Exception:
            pass
    _gpio_chips = {}


def dormir_hasta(deadline):
    """Espera hasta un instante monotonico absoluto.

    NO se usa time.sleep(periodo) a secas: eso acumula el tiempo de ejecucion de
    cada vuelta y al cabo de 48 h la deriva seria grande. Con deadline absoluto
    el error no se acumula.
    """
    resto = deadline - time.monotonic()
    if resto > 0:
        time.sleep(resto)


def pulso():
    """Un cierre del contacto. Devuelve (t_monotonic, t_wall) del CIERRE."""
    global _relay_ops
    t_mono = time.monotonic()
    t_wall = time.time()
    if not _laptop:
        for line in _gpio_lines:
            line.set_value(1)
    dormir_hasta(t_mono + PULSE_WIDTH_S)
    if not _laptop:
        for line in _gpio_lines:
            line.set_value(0)
    _relay_ops += 1
    return t_mono, t_wall


# ==================== tren de pulsos ======================================
def tren(w, etiqueta, caudal_m3h, duracion_s, jitter_pct=JITTER_PCT):
    """Emite pulsos al caudal dado durante duracion_s. caudal<=0 -> silencio."""
    global _seq, _t_ult_mono

    if caudal_m3h <= 0:
        log('  {}: SILENCIO {:.0f} s{}'.format(
            etiqueta, duracion_s,
            ' (> dT_zero = 120 s: dispara el decay a cero)'
            if duracion_s > 120 else ''))
        time.sleep(duracion_s)
        return 0

    periodo = 3600.0 * MAGPP_M3 / caudal_m3h
    if periodo <= PULSE_WIDTH_S * 1.5:
        log('  [!] {}: periodo {:.0f} ms demasiado corto para un pulso '
            'de {:.0f} ms. SALTEADO.'.format(
                etiqueta, periodo * 1000, PULSE_WIDTH_S * 1000))
        return 0

    n_est = int(duracion_s / periodo)
    log('  {}: Q={} m3/h, periodo {:.3f} s, {:.2f} h, ~{} pulsos'.format(etiqueta, caudal_m3h, periodo, duracion_s / 3600, n_est))

    fin = time.monotonic() + duracion_s

    # EL PRIMER PULSO DEL TRAMO RESPETA EL PERIODO DESDE EL ULTIMO CIERRE.
    #
    # Antes era `siguiente = time.monotonic()`, o sea que al cambiar de tramo el
    # ritmo se reiniciaba y el primer pulso salia JUSTO despues del ultimo del
    # tramo anterior: dT de decenas de ms. Y eso tiene una consecuencia que
    # arruina el test -- el equipo los descarta por el ANTI-REBOTE de 500 ms,
    # con toda razon, asi que contaria menos pulsos que los emitidos y el
    # analisis reportaria perdidas que no existen. 22 de 46 pulsos salian asi en
    # el ensayo; en 48 h serian ~18 (2 vueltas x 9 tramos).
    #
    # El max() cubre los dos casos: en un cambio de tramo normal espera el
    # periodo nuevo, y despues de un silencio largo --donde el instante ya
    # paso-- el pulso sale enseguida, que es el REARRANQUE y es lo correcto.
    siguiente = (max(_t_ult_mono + periodo, time.monotonic())
                 if _t_ult_mono is not None else time.monotonic())
    n = 0

    while time.monotonic() < fin:
        # LA ESPERA VA ANTES DEL PULSO, no despues.
        #
        # Con el dormir_hasta() al final del cuerpo, el primer pulso del tramo
        # salia ENSEGUIDA sin importar el instante calculado arriba -- que es
        # justo el bug que esto vino a arreglar. Se vio porque el analizador
        # reporto 22 de 46 pulsos con dT < 500 ms despues del primer "arreglo".
        dormir_hasta(siguiente)

        if time.monotonic() >= fin:
            break

        t_mono, t_wall = pulso()
        _seq += 1
        n += 1

        # El dT REAL medido, no el teorico. Es la pieza que hace posible el
        # analisis: un sleep() de Linux no es exacto, y sin este numero una
        # diferencia de caudal es indistinguible entre "el firmware calcula
        # mal" y "la BBB no emitio cuando creia".
        dt_ms = (t_mono - _t_ult_mono) * 1000.0 if _t_ult_mono is not None else 0.0
        _t_ult_mono = t_mono

        q_real = (3600000.0 * MAGPP_M3 / dt_ms) if dt_ms > 0 else 0.0

        w.writerow([
            datetime.fromtimestamp(t_wall).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3],
            _seq, etiqueta, '{:.3f}'.format(caudal_m3h), '{:.1f}'.format(dt_ms), '{:.3f}'.format(q_real),
            '{:.0f}'.format(PULSE_WIDTH_S * 1000),
        ])
        w_flush()

        p = periodo
        if jitter_pct > 0:
            p *= 1.0 + random.uniform(-jitter_pct, jitter_pct) / 100.0
        siguiente += p

    return n


_t_ult_mono = None
_fcsv = None


def w_flush():
    """Flush por pulso: si la BBB se corta, el CSV ya tiene todo lo emitido.

    Por eso NO hace falta un archivo de acumulado aparte -- el ground truth se
    recupera leyendo la ultima linea del CSV, que es una sola fuente de verdad.
    Es append, no reescritura del mismo sector: amable con la SD.
    """
    if _fcsv:
        _fcsv.flush()
        os.fsync(_fcsv.fileno())


def resumen(etapa=""):
    dur = time.time() - _t0_wall
    pct = 100.0 * _relay_ops / RELE_VIDA_MECANICA
    log('RESUMEN{}: corrida {:.2f} h | emitidos {} | operaciones del rele '
        '{} ({:.4f} % de su vida mecanica)'.format(
            ' ' + etapa if etapa else '', dur / 3600, _seq, _relay_ops, pct))


# ==================== main ================================================
def main():
    global _laptop, _flog, _fcsv, _t0_wall, _seq, PULSE_WIDTH_S

    ap = argparse.ArgumentParser(description="Generador de pulsos para el test del contador")
    ap.add_argument("--horas", type=float, default=48.0, help="duracion total (default 48)")
    ap.add_argument("--prueba", action="store_true",
                    help="perfil CORTO de ~55 min: valida la cadena entera "
                         "(generador + logger + analizador) antes de largar las 48 h")
    ap.add_argument("--barrido", action="store_true",
                    help="barrido de frecuencia para buscar el techo del filtro")
    ap.add_argument("--laptop", action="store_true", help="sin GPIO (probar el script)")
    ap.add_argument("--escala", type=float, default=1.0,
                    help="divide las duraciones del perfil. --escala 1000 corre "
                         "las 48 h en ~3 min y ejercita TODOS los tramos: es como "
                         "se valida el script antes de largar el test de verdad")
    ap.add_argument("--dir", default=".", help="donde escribir los logs")
    args = ap.parse_args()

    _laptop = args.laptop
    d = Path(args.dir)
    d.mkdir(parents=True, exist_ok=True)
    marca = datetime.now().strftime("%Y%m%d_%H%M%S")
    pcsv = d / 'pulsos_{}.csv'.format(marca)
    plog = d / 'generador_{}.log'.format(marca)

    _flog = open(plog, "a", encoding="utf-8")
    _t0_wall = time.time()

    # La linea de alineacion: los dos logs tienen que arrancar con la hora, o
    # cruzarlos despues es adivinar.
    log("=" * 70)
    log('ARRANCA EL GENERADOR DE PULSOS -- hora local {}'.format(ts()))
    log('  magpp = {} m3/pulso | ancho del cierre = {:.0f} ms | jitter +/-{} %'.format(MAGPP_M3, PULSE_WIDTH_S * 1000, JITTER_PCT))
    log('  CSV: {}'.format(pcsv))
    log("  VERIFICAR que el reloj de ESTA maquina y el del logger concuerden,")
    log("  o el cruce de los dos logs miente.")
    log("=" * 70)

    gpio_init()
    signal.signal(signal.SIGTERM, lambda *a: sys.exit(0))

    _fcsv = open(pcsv, "w", newline="", encoding="utf-8")
    w = csv.writer(_fcsv)
    w.writerow(["timestamp", "seq", "tramo", "q_teorico_m3h", "dt_real_ms",
                "q_real_m3h", "ancho_ms"])
    w_flush()

    try:
        if args.barrido:
            log("BARRIDO DE FRECUENCIA: busca el TECHO del filtro RC (~60 Hz")
            log("documentado, nunca verificado). No es el test de 48 h.")
            for hz in BARRIDO_HZ:
                periodo = 1.0 / hz
                if periodo <= PULSE_WIDTH_S * 1.5:
                    ancho = max(0.004, periodo / 3.0)
                    log('  {} Hz: periodo {:.1f} ms -> ancho reducido a '
                        '{:.1f} ms (el de 50 ms no entra)'.format(
                            hz, periodo * 1000, ancho * 1000))
                    PULSE_WIDTH_S = ancho
                n0 = _seq
                fin = time.monotonic() + BARRIDO_SEG
                siguiente = time.monotonic()
                while time.monotonic() < fin:
                    pulso()
                    _seq += 1
                    siguiente += periodo
                    dormir_hasta(siguiente)
                log("  {:3d} Hz -> EMITIDOS {} en {} s. Comparar contra el 'CNT:: total' del equipo.".format(hz, _seq - n0, BARRIDO_SEG))
                time.sleep(5)
            resumen("barrido")
            return

        if args.escala != 1.0:
            log('[!] ESCALA {}x: las duraciones se dividen. Esto NO es'.format(args.escala))
            log("    el test, es el ensayo del script.")

        perfil = PERFIL_PRUEBA if args.prueba else PERFIL_DIA
        if args.prueba:
            log("PERFIL DE PRUEBA (~55 min): es el ensayo de la cadena completa,")
            log("no el test. Recorre los mismos caminos que el perfil de 48 h.")

        total_dia = sum(x[1] for x in perfil) / args.escala
        log('Perfil: {:.2f} h, {} tramos'.format(total_dia / 3600, len(perfil)))
        log('Objetivo: {} h'.format(args.horas))

        fin_total = time.monotonic() + args.horas * 3600.0 / args.escala
        ult_resumen = time.monotonic()
        vuelta = 0

        while time.monotonic() < fin_total:
            vuelta += 1
            log('--- vuelta {} del perfil ---'.format(vuelta))
            for caudal, dur, etiqueta in perfil:
                if time.monotonic() >= fin_total:
                    break
                # Recortar el ultimo tramo para no pasarse de las horas pedidas.
                dur = min(dur / args.escala, fin_total - time.monotonic())
                if dur <= 0:
                    break
                tren(w, etiqueta, caudal, dur)
                if time.monotonic() - ult_resumen > 900:      # cada 15 min
                    resumen()
                    ult_resumen = time.monotonic()

        resumen("FINAL")
        log("TEST COMPLETO. Entregar este CSV junto con el log del logger.")

    except KeyboardInterrupt:
        log("INTERRUMPIDO por el usuario")
        resumen("interrumpido")
    finally:
        gpio_cleanup()
        if _fcsv:
            _fcsv.close()
        log("reles SUELTOS (contacto abierto). Fin.")
        if _flog:
            _flog.close()


if __name__ == "__main__":
    main()
