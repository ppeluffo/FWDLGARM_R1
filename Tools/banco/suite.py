#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
suite.py - el punto de entrada. Importa las áreas y corre el runner.

    ./suite.py -p /dev/ttyUSB0 --version 0.0.75
    ./suite.py -p /dev/ttyUSB0 -a AB          # sólo identidad y configuración
    ./suite.py -p /dev/ttyUSB0 --sin-destructivos

⚠ Cerrar el minicom antes: el puerto es exclusivo.

⚠ Esta suite PISA la configuración del equipo y formatea la microSD. Está
acordado que corre sobre equipos de prueba.
"""

import sys

import runner

# El orden de importación fija el orden de ejecución, y eso importa: las áreas
# están ordenadas por dependencia, de lo que no necesita nada conectado a lo que
# necesita el modem, el servidor y el esclavo Modbus.
import tests_a_identidad  # noqa: F401
import tests_b_config     # noqa: F401

if __name__ == "__main__":
    sys.exit(runner.main())
