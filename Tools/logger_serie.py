#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Logger de la consola serie del datalogger SPQ_ARM, para correr desatendido
durante el test de 48 h del contador de pulsos.

  python3 logger_serie.py                      # /dev/ttyUSB0, 48 h
  python3 logger_serie.py --puerto /dev/ttyUSB1 --horas 48
  python3 logger_serie.py --pasivo             # sin mandar NINGUN comando

QUE HACE
--------
  - captura todo lo que el equipo emite, con TIMESTAMP PROPIO por linea
  - manda 'tag on' para que el log quede parseable, y lo REENVIA periodicamente
  - cada 15 min pide 'cnt', 'status' y 'wdg': son los checkpoints
  - sobrevive a que el USB se desconecte y se reconecte
  - rota el archivo cada 50 MB

EL TIMESTAMP PROPIO NO ES REDUNDANTE con el del equipo. El equipo estampa la
hora de su RTC solo en la linea del registro; todo lo demas -- las sesiones del
modem, el latido del contador, los avisos -- sale sin hora. Y el RTC del equipo
y el reloj de esta maquina son DOS relojes distintos: tener los dos permite
detectar si derivan.

POR QUE MANDA COMANDOS, Y POR QUE TAMBIEN SIRVE EN --pasivo
----------------------------------------------------------
El criterio del test es "cierres emitidos == pulsos contados". El firmware
0.0.19+ emite el acumulado SOLO en cada poleo:

    CNT:: total=14402 pendientes=3 cortos=0 imposibles=0

...asi que en modo pasivo el test YA es concluyente. Los comandos de cada 15
min son REDUNDANCIA: si el latido fallara, los checkpoints de 'cnt' lo cubren,
y 'status'/'wdg' agregan stacks, errores de UART y margen del watchdog, que es
lo que convierte esto tambien en el soak de estabilidad.

EL TAG SE PIERDE EN CADA RESET, y por eso se reenvia. 'tag on' es idempotente,
asi que mandarlo de nuevo no cuesta nada -- es mas robusto que detectar el
banner, que depende de un string que podria cambiar. Igual se detecta
'arrancando' para reaccionar rapido a un reset por watchdog.
"""

import argparse
import os
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError:
    sys.exit("falta pyserial:  pip3 install pyserial   (o apt install python3-serial)")

ROTAR_BYTES   = 50 * 1024 * 1024
SEG_CHECKPOINT = 900          # 15 min
CMDS_CHECKPOINT = ("cnt", "status", "wdg")


def ts():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


class Log:
    """Archivo con rotacion. Escribe y hace flush por linea: un corte de luz a
    las 40 h no puede llevarse las ultimas horas en un buffer."""

    def __init__(self, base):
        self.base = Path(base)
        self.n = 0
        self.f = None
        self._abrir()

    def _abrir(self):
        if self.f:
            self.f.close()
        suf = "" if self.n == 0 else f".{self.n}"
        self.ruta = Path(str(self.base) + suf)
        self.f = open(self.ruta, "a", encoding="utf-8", errors="replace")
        print(f"[{ts()}] log -> {self.ruta}", flush=True)

    def linea(self, texto, marca="<<"):
        self.f.write(f"{ts()} {marca} {texto}\n")
        self.f.flush()
        if self.f.tell() > ROTAR_BYTES:
            self.n += 1
            self._abrir()

    def cerrar(self):
        if self.f:
            self.f.close()


def abrir_puerto(puerto, baud):
    """Abre sin tocar DTR/RTS.

    EL ORDEN IMPORTA: dtr/rts se ponen en False ANTES del open(). No esta
    verificado si alguno llega a NRST en este adaptador, y resetear la placa sin
    querer en medio de un test de 48 h lo arruinaria entero.
    """
    s = serial.Serial()
    s.port = puerto
    s.baudrate = baud
    s.timeout = 0.3
    s.dtr = False
    s.rts = False
    s.open()
    return s


def main():
    ap = argparse.ArgumentParser(description="Logger de la consola serie del SPQ_ARM")
    ap.add_argument("--puerto", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--horas", type=float, default=48.0)
    ap.add_argument("--dir", default=".")
    ap.add_argument("--pasivo", action="store_true",
                    help="no manda NINGUN comando (ni 'tag on'). El test sigue "
                         "siendo valido: el latido CNT:: del firmware alcanza")
    args = ap.parse_args()

    d = Path(args.dir)
    d.mkdir(parents=True, exist_ok=True)
    marca = datetime.now().strftime("%Y%m%d_%H%M%S")
    log = Log(d / f"equipo_{marca}.log")

    log.linea("=" * 66, "##")
    log.linea(f"ARRANCA EL LOGGER -- hora local {ts()}", "##")
    log.linea(f"puerto {args.puerto} {args.baud} 8N1 | objetivo {args.horas} h | "
              f"{'PASIVO' if args.pasivo else 'con checkpoints cada 15 min'}", "##")
    log.linea("VERIFICAR que el reloj de esta maquina y el de la BBB concuerden,", "##")
    log.linea("o el cruce de los dos logs miente.", "##")
    log.linea("=" * 66, "##")

    t0 = time.monotonic()
    fin = t0 + args.horas * 3600.0
    prox_check = t0 + 5.0          # el primero enseguida, para tener una base
    s = None
    buf = b""
    n_lineas = 0
    n_reconex = 0
    n_resets = 0

    try:
        while time.monotonic() < fin:
            # --- (re)conexion ---
            if s is None:
                try:
                    s = abrir_puerto(args.puerto, args.baud)
                    log.linea(f"puerto ABIERTO ({args.puerto})", "##")
                except Exception as e:
                    log.linea(f"no se pudo abrir {args.puerto}: {e} -- reintento en 5 s", "##")
                    time.sleep(5)
                    continue

            # --- leer ---
            try:
                d_ = s.read(4096)
            except Exception as e:
                log.linea(f"ERROR DE LECTURA: {e} -- cierro y reconecto", "##")
                try:
                    s.close()
                except Exception:
                    pass
                s = None
                n_reconex += 1
                time.sleep(2)
                continue

            if d_:
                buf += d_
                # Se parte por \n y se deja lo incompleto para la vuelta
                # siguiente: cortar una linea a la mitad rompe el parseo del
                # CNT:: justo en el numero que se vino a leer.
                while b"\n" in buf:
                    cruda, buf = buf.split(b"\n", 1)
                    texto = cruda.decode("utf-8", "replace").rstrip("\r")
                    if texto == "":
                        continue
                    log.linea(texto)
                    n_lineas += 1

                    # Un reset apaga el modo tag: se repone enseguida en vez de
                    # esperar al checkpoint, para no perder 15 min de etiquetas.
                    if (not args.pasivo) and ("arrancando" in texto):
                        n_resets += 1
                        log.linea(f"detectado ARRANQUE del equipo (#{n_resets}): "
                                  "repongo 'tag on'", "##")
                        try:
                            s.write(b"tag on\r")
                            s.flush()
                        except Exception:
                            pass

            # --- checkpoints ---
            if (not args.pasivo) and (time.monotonic() >= prox_check):
                prox_check = time.monotonic() + SEG_CHECKPOINT
                log.linea(f"--- CHECKPOINT t+{(time.monotonic()-t0)/3600:.2f} h "
                          f"({n_lineas} lineas, {n_reconex} reconexiones, "
                          f"{n_resets} arranques) ---", "##")
                for c in ("tag on",) + CMDS_CHECKPOINT:
                    try:
                        log.linea(c, ">>")
                        s.write((c + "\r").encode())
                        s.flush()
                        # Se le da aire a cada comando: 'status' es largo y a
                        # 9600 tarda. Sin esto las respuestas se pisan entre si.
                        time.sleep(4)
                        d2 = s.read(16384)
                        if d2:
                            buf += d2
                    except Exception as e:
                        log.linea(f"ERROR al mandar '{c}': {e}", "##")
                        break

        log.linea(f"FIN: {args.horas} h cumplidas. {n_lineas} lineas, "
                  f"{n_reconex} reconexiones, {n_resets} arranques del equipo.", "##")

    except KeyboardInterrupt:
        log.linea(f"INTERRUMPIDO. {n_lineas} lineas, {n_reconex} reconexiones, "
                  f"{n_resets} arranques.", "##")
    finally:
        if s:
            try:
                s.close()
            except Exception:
                pass
        log.linea("logger cerrado.", "##")
        log.cerrar()


if __name__ == "__main__":
    main()
