#!/usr/bin/env python3
"""
mkfw_casos.py -- genera los archivos de prueba del autoupdate.

    python3 Tools/mkfw_casos.py FIRMWARE.BIN -o /ruta/a/la/tarjeta

⭐ POR QUE EXISTE: el camino feliz de `fw` se prueba con un archivo bueno, pero
   lo que el mecanismo tiene que garantizar es que **NO escriba la flash con
   basura**. Eso son los casos de FALLA, y sin este script habria que fabricarlos
   a mano con un editor hexadecimal cada vez.

⛔ Y NO se hace tocando `mkfw.py`: aquel saca la version y el hardware de
   `main.h` a proposito, para que no exista una segunda fuente de verdad. Este
   script PARCHEA un archivo ya generado, que es otra cosa: es un instrumento
   de test, como `generador_pulsos.py`.

Los casos, y que camino ejercita cada uno:

    FWOK.BIN      el archivo tal cual        -> "misma version" (si es el que corre)
    FWNUEVO.BIN   version cambiada           -> "hay un firmware nuevo y valido"
    FWIMGMAL.BIN  un byte de la IMAGEN       -> "la imagen esta CORRUPTA"
    FWHDRMAL.BIN  un byte del HEADER         -> "el header esta CORRUPTO"
    FWOTROHW.BIN  otro hardware              -> "es el firmware de OTRO producto"
    FWBASURA.BIN  no es nuestro              -> "no es un firmware de este mecanismo"
    FWCORTO.BIN   el largo no cierra         -> "el largo no cierra"

⚠ Escrito para que corra tambien en PYTHON 3.5 (sin f-strings).
"""

import argparse
import os
import struct
import sys
import zlib

HDR_LEN = 64
VER_OFF, VER_LEN = 8, 16
HW_OFF,  HW_LEN  = 24, 16
LEN_OFF, CRC_OFF, HDRCRC_OFF = 40, 44, 48


def rehacer_hdr_crc(cab):
    """El CRC del header cubre los bytes 0..47 -- con offsetof, no con 'todo menos 16'."""
    crc = zlib.crc32(cab[:HDRCRC_OFF]) & 0xFFFFFFFF
    return cab[:HDRCRC_OFF] + struct.pack("<I", crc) + cab[HDRCRC_OFF + 4:]


def campo(texto, largo):
    b = texto.encode("ascii")
    return b + b"\0" * (largo - len(b))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("origen", help="un FIRMWARE.BIN valido, de mkfw.py")
    ap.add_argument("-o", "--destino", default=".")
    args = ap.parse_args()

    datos = bytearray(open(args.origen, "rb").read())
    if bytes(datos[:4]) != b"SPQF":
        raise SystemExit("ERROR: {} no es un FIRMWARE.BIN".format(args.origen))

    ver = bytes(datos[VER_OFF:VER_OFF + VER_LEN]).split(b"\0")[0].decode()
    hw = bytes(datos[HW_OFF:HW_OFF + HW_LEN]).split(b"\0")[0].decode()
    img_len = struct.unpack("<I", bytes(datos[LEN_OFF:LEN_OFF + 4]))[0]
    print("origen: version '{}', hardware '{}', imagen {} bytes".format(ver, hw, img_len))

    if not os.path.isdir(args.destino):
        os.makedirs(args.destino)

    def guardar(nombre, contenido, que_prueba):
        ruta = os.path.join(args.destino, nombre)
        with open(ruta, "wb") as f:
            f.write(bytes(contenido))
        print("  {:<14} {:>8} B   -> {}".format(nombre, len(contenido), que_prueba))

    # 1. el bueno, tal cual
    guardar("FWOK.BIN", datos, "misma version (si es el que corre)")

    # 2. version distinta. ⭐ Se cambia SOLO el campo de version y se rehace el
    #    CRC del header -- la imagen y su CRC quedan intactos, asi que el
    #    archivo es legitimo salvo por decir otra version.
    d = bytearray(datos)
    nueva = "9.9.99"
    d[VER_OFF:VER_OFF + VER_LEN] = campo(nueva, VER_LEN)
    d[:HDR_LEN] = rehacer_hdr_crc(bytes(d[:HDR_LEN]))
    guardar("FWNUEVO.BIN", d, "version '{}' -> HAY FIRMWARE NUEVO".format(nueva))

    # 3. imagen corrupta: UN byte. El header queda perfecto, asi que la unica
    #    defensa es el CRC de la imagen -- que es justo lo que se quiere probar.
    d = bytearray(datos)
    pos = HDR_LEN + img_len // 2
    d[pos] ^= 0xFF
    d[VER_OFF:VER_OFF + VER_LEN] = campo(nueva, VER_LEN)   # para que no corte antes por "misma version"
    d[:HDR_LEN] = rehacer_hdr_crc(bytes(d[:HDR_LEN]))
    guardar("FWIMGMAL.BIN", d, "1 byte de la imagen cambiado -> IMAGEN CORRUPTA")

    # 4. header corrupto: un byte del largo, SIN rehacer su CRC
    d = bytearray(datos)
    d[LEN_OFF] ^= 0x55
    guardar("FWHDRMAL.BIN", d, "1 byte del header, sin rehacer su CRC -> HEADER CORRUPTO")

    # 5. otro producto
    d = bytearray(datos)
    d[HW_OFF:HW_OFF + HW_LEN] = campo("OTRO_HW_R9", HW_LEN)
    d[VER_OFF:VER_OFF + VER_LEN] = campo(nueva, VER_LEN)
    d[:HDR_LEN] = rehacer_hdr_crc(bytes(d[:HDR_LEN]))
    guardar("FWOTROHW.BIN", d, "hardware 'OTRO_HW_R9' -> OTRO PRODUCTO")

    # 6. ni siquiera es nuestro
    guardar("FWBASURA.BIN", b"esto no es un firmware, es texto plano\n" * 50,
            "texto plano -> MAGIC")

    # 7. el largo del header no cierra con el tamaño del archivo
    d = bytearray(datos)
    d[LEN_OFF:LEN_OFF + 4] = struct.pack("<I", img_len - 8)
    d[VER_OFF:VER_OFF + VER_LEN] = campo(nueva, VER_LEN)
    d[:HDR_LEN] = rehacer_hdr_crc(bytes(d[:HDR_LEN]))
    guardar("FWCORTO.BIN", d, "largo declarado 8 B menor -> LARGO")

    print("\nCopiar los 7 a la raiz de la microSD y correr en la consola:")
    print("   fw FWOK.BIN / FWNUEVO.BIN / FWIMGMAL.BIN / FWHDRMAL.BIN /")
    print("   FWOTROHW.BIN / FWBASURA.BIN / FWCORTO.BIN")


if __name__ == "__main__":
    main()
