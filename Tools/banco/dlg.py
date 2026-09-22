#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
dlg.py - el transporte: la conexión serie con el datalogger.

⭐ POR QUÉ UN HILO LECTOR Y NO "MANDAR Y LEER LA RESPUESTA"

Porque el equipo HABLA SOLO. `tkWan`, `tkSys` y `tkCtlPres` imprimen cuando les
toca, y eso cae **en medio** de la respuesta a un comando:

    cmd>status
    version      : FWDLGARM_R1 0.0.74
    tkWan:: ONLINE_DATA              <- esto no es parte del status
    identidad    : HW=SPQ_ARM_R1 ...

Un script ingenuo que lea "hasta el prompt" y compare línea por línea falla por
razones falsas. Y un test que falla sin motivo destruye la confianza en la
batería entera más rápido de lo que la construye.

Acá hay un hilo que vuelca TODO continuamente a un buffer y a un archivo con
timestamps, y las aserciones se hacen sobre ese texto. El log fechado vale por
sí solo, aunque no corra ningún test: hoy las trazas del banco se copian a mano
del minicom.

⛔ EL PUERTO NO SE ADIVINA. En esta máquina `/dev/ttyACM0` es el lector de
huellas, no el datalogger. Va explícito por argumento, y antes de correr nada
se exige ver el prompt `cmd>` para confirmar que del otro lado hay un equipo.
"""

import re
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

import serial

# La consola TERM: USART1 a 9600 8N1, sin control de flujo. Es como la levanta
# el firmware; no cambiar sin cambiar el `.ioc`.
BAUD_TERM = 9600

PROMPT = "cmd>"

# El banner de `tkCmd`: "FWDLGARM_R1 0.0.74 - consola TERM"
RE_BANNER = re.compile(r"(\S+)\s+(\d+\.\d+\.\d+)\s+-\s+consola TERM")


class Timeout(Exception):
    """Lo esperado no apareció en el plazo. Lleva el texto visto, que es lo
    único que permite entender por qué sin volver a correr la prueba."""

    def __init__(self, que, visto):
        super().__init__(f"no aparecio {que!r}")
        self.que = que
        self.visto = visto


class Datalogger:
    def __init__(self, puerto, log_dir="logs", eco=True):
        self.puerto = puerto
        self.eco = eco
        self._ser = serial.Serial(
            port=puerto,
            baudrate=BAUD_TERM,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            rtscts=False,
            xonxoff=False,
        )
        self._buf = ""
        self._lock = threading.Lock()
        self._corriendo = True

        Path(log_dir).mkdir(parents=True, exist_ok=True)
        marca = datetime.now().strftime("%Y%m%d-%H%M%S")
        self._log_path = Path(log_dir) / f"banco-{marca}.log"
        self._log = open(self._log_path, "w", encoding="utf-8", errors="replace")

        self._hilo = threading.Thread(target=self._leer_siempre, daemon=True)
        self._hilo.start()

    # ---------------------------------------------------------------- interno
    def _leer_siempre(self):
        """Vuelca todo lo que llega. Nunca filtra ni interpreta: eso es trabajo
        de los tests, y un transporte que decide qué guardar es un transporte
        que esconde justo la línea que hacía falta."""
        parcial = ""

        while self._corriendo:
            try:
                datos = self._ser.read(4096)
            except Exception:
                break

            if not datos:
                continue

            texto = datos.decode("utf-8", errors="replace")

            with self._lock:
                self._buf += texto

            # Al archivo va con timestamp por línea. El `cmd>` no trae salto,
            # así que lo que quede sin cerrar se arrastra a la vuelta siguiente.
            parcial += texto
            while "\n" in parcial:
                linea, parcial = parcial.split("\n", 1)
                self._escribir(linea.rstrip("\r"))

            if parcial.endswith(PROMPT):
                self._escribir(parcial)
                parcial = ""

    def _escribir(self, linea):
        t = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self._log.write(f"[{t}] {linea}\n")
        self._log.flush()

        if self.eco:
            print(f"  \033[2m│ {linea}\033[0m", flush=True)

    # ------------------------------------------------------------------ texto
    def texto(self):
        with self._lock:
            return self._buf

    def limpiar(self):
        """Descarta lo acumulado. Se llama ANTES de mandar un comando, para que
        lo que se busque después no matchee con una corrida anterior."""
        with self._lock:
            self._buf = ""

    # ------------------------------------------------------------- operaciones
    def enviar(self, cmd):
        """Manda un comando. ⚠ El firmware exige el comando COMPLETO desde el
        2026-09-08: no matchea por prefijo."""
        self._escribir(f">>> {cmd}")
        self._ser.write((cmd + "\r").encode("ascii"))
        self._ser.flush()

    def esperar(self, patron, timeout=10.0, desde=None):
        """Espera un patrón (str o regex compilada) y devuelve el match.
        `desde` permite acotar la búsqueda a lo llegado después de un punto."""
        limite = time.time() + timeout
        inicio = desde if desde is not None else 0

        while time.time() < limite:
            texto = self.texto()[inicio:]

            if hasattr(patron, "search"):
                m = patron.search(texto)
                if m:
                    return m
            elif patron in texto:
                return patron

            time.sleep(0.05)

        raise Timeout(getattr(patron, "pattern", patron), self.texto()[inicio:])

    def cmd(self, comando, timeout=10.0, espera_prompt=True):
        """Manda un comando y devuelve TODO lo que llegó hasta el prompt.

        ⚠ Eso incluye el log de otras tareas que haya caído en el medio, y es a
        propósito: filtrarlo acá escondería justo lo que a veces se quiere ver.
        Los tests que necesitan precisión buscan su patrón, no comparan el bloque
        entero.
        """
        self.limpiar()
        self.enviar(comando)

        if not espera_prompt:
            time.sleep(0.3)
            return self.texto()

        self.esperar(PROMPT, timeout=timeout)
        return self.texto()

    # ------------------------------------------------------------ sincronismo
    def despertar(self, timeout=6.0):
        """Confirma que del otro lado hay un datalogger.

        ⛔ Esto NO es una formalidad: en esta máquina `/dev/ttyACM0` es el lector
        de huellas. Abrir el puerto equivocado no da error — da silencio, que se
        parece demasiado a un equipo colgado.
        """
        self.limpiar()
        self._ser.write(b"\r")
        self._ser.flush()

        try:
            self.esperar(PROMPT, timeout=timeout)
            return True
        except Timeout:
            return False

    def esperar_arranque(self, timeout=25.0):
        """Espera el banner y devuelve (nombre, version).

        ⭐ Leer la versión del banner es lo que evita probar un binario distinto
        del que uno cree: cuando se puso la regla de subirla en cada entrega,
        `FW_VERSION` decía 0.0.8 mientras los tags iban por v0.0.14.
        """
        m = self.esperar(RE_BANNER, timeout=timeout)
        self.esperar(PROMPT, timeout=10.0)
        return m.group(1), m.group(2)

    def reset(self, timeout=25.0):
        """Reinicia el equipo y espera a que vuelva. Devuelve (nombre, version)."""
        self.limpiar()
        self.enviar("reset")
        return self.esperar_arranque(timeout=timeout)

    # ------------------------------------------------------------------ cierre
    def cerrar(self):
        self._corriendo = False
        time.sleep(0.2)

        try:
            self._ser.close()
        finally:
            self._log.close()

    @property
    def log_path(self):
        return self._log_path


def abrir(puerto, log_dir="logs", eco=True):
    """Abre el puerto y verifica que hay un datalogger. Sale con un mensaje
    útil si no, en vez de fallar adentro del primer test."""
    try:
        dlg = Datalogger(puerto, log_dir=log_dir, eco=eco)
    except serial.SerialException as e:
        print(f"\n⛔ no se pudo abrir {puerto}: {e}")
        print("   ¿está el minicom abierto? El puerto es exclusivo: cerralo primero.")
        sys.exit(2)

    if not dlg.despertar():
        print(f"\n⛔ {puerto} se abrió pero nadie contesta el prompt 'cmd>'.")
        print("   Verificá que es el puerto del datalogger y que está alimentado.")
        print("   (en esta máquina /dev/ttyACM0 es el lector de huellas)")
        dlg.cerrar()
        sys.exit(2)

    return dlg
