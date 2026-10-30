#!/usr/bin/env python3
"""
mkfw.py -- arma el FIRMWARE.BIN que el datalogger lee de la microSD.

    python3 Tools/mkfw.py Debug/FWDLGARM_R1.elf [-o FIRMWARE.BIN]

Toma el .elf, le saca la imagen cruda con objcopy, y le PREPENDE un header de
64 bytes con la version, el hardware, el largo y el CRC32.

⭐ LA VERSION Y EL HARDWARE SALEN DE `Core/Inc/main.h`, NO DE ARGUMENTOS.
   Pasarlos a mano crearia una segunda fuente de verdad que se puede
   desincronizar del binario -- y un firmware que dice ser una version que no
   es, es exactamente lo que este mecanismo tiene que hacer imposible.

⛔ POR QUE NO SE USA EL .elf DIRECTAMENTE: es un contenedor con headers,
   secciones, simbolos y debug (288 KB en Debug contra 176 KB de imagen real).
   Cargarlo pediria parsear sus program headers y copiar los segmentos LOAD a
   sus direcciones. El .bin es la imagen cruda: se copia tal cual.

El header, 64 bytes, todo little-endian:

    off  largo  campo
      0      4  magic      "SPQF"
      4      2  hdr_ver    version del FORMATO (hoy 1)
      6      2  hdr_len    64
      8     16  fw_ver     "0.0.19", terminado en NUL
     24     16  fw_hw      "SPQ_ARM_R1", terminado en NUL
     40      4  img_len    bytes de la imagen (sin este header)
     44      4  img_crc32  CRC-32 (zlib) de la imagen
     48      4  hdr_crc32  CRC-32 de los bytes 0..47
     52     12  reservado (ceros)

⭐ `fw_hw` NO es decorativo: evita que el firmware de otro producto entre en
   este equipo. Es el mismo criterio del IMEI falso -- hacer el error
   imposible en vez de confiar en que nadie se equivoque.

⚠ Escrito para que corra tambien en PYTHON 3.5 (sin f-strings), por
  consistencia con los otros scripts de Tools/. Ver verificar_py35.sh.
"""

import argparse
import os
import re
import struct
import subprocess
import sys
import zlib

MAGIC      = b"SPQF"
HDR_VER    = 1
HDR_LEN    = 64
VER_LEN    = 16
HW_LEN     = 16

# El banco de flash son 512 KB y el header va adentro del archivo, no de la
# imagen -- pero la imagen sola tampoco puede pasarse del banco.
BANCO_BYTES = 512 * 1024


def version_y_hw_del_main_h(ruta):
    """Lee FW_VERSION y FW_HW del main.h. Es la UNICA fuente de verdad."""
    texto = open(ruta, "r", encoding="utf-8", errors="replace").read()
    out = {}
    for campo in ("FW_VERSION", "FW_HW"):
        m = re.search(r'^\s*#define\s+' + campo + r'\s+"([^"]*)"', texto, re.M)
        if not m:
            raise SystemExit("ERROR: no encuentro {} en {}".format(campo, ruta))
        out[campo] = m.group(1)
    return out["FW_VERSION"], out["FW_HW"]


def campo_fijo(texto, largo, nombre):
    b = texto.encode("ascii")
    if len(b) >= largo:          # >= porque hace falta lugar para el NUL
        raise SystemExit("ERROR: {} '{}' no entra en {} bytes".format(
            nombre, texto, largo))
    return b + b"\0" * (largo - len(b))


def objcopy(elf, destino):
    """Saca la imagen cruda del .elf. Usa el objcopy de CubeIDE si esta."""
    candidatos = []
    base = os.path.expanduser("~/st/stm32cubeide_2.2.0/plugins")
    if os.path.isdir(base):
        for d in sorted(os.listdir(base)):
            if "gnu-tools-for-stm32" in d:
                p = os.path.join(base, d, "tools", "bin", "arm-none-eabi-objcopy")
                if os.path.isfile(p):
                    candidatos.append(p)
    candidatos.append("arm-none-eabi-objcopy")

    for oc in candidatos:
        try:
            subprocess.check_call([oc, "-O", "binary", elf, destino])
            return oc
        except (OSError, subprocess.CalledProcessError):
            continue
    raise SystemExit("ERROR: no pude correr arm-none-eabi-objcopy")


def main():
    ap = argparse.ArgumentParser(description="arma el FIRMWARE.BIN para la microSD")
    ap.add_argument("elf", help="el .elf compilado")
    ap.add_argument("-o", "--salida", default="FIRMWARE.BIN")
    ap.add_argument("--main-h", default=None,
                    help="Core/Inc/main.h (por omision, junto al proyecto del .elf)")
    args = ap.parse_args()

    if not os.path.isfile(args.elf):
        raise SystemExit("ERROR: no existe {}".format(args.elf))

    main_h = args.main_h
    if main_h is None:
        # el .elf vive en Debug/ o Release/, y main.h en ../Core/Inc/
        raiz = os.path.dirname(os.path.dirname(os.path.abspath(args.elf)))
        main_h = os.path.join(raiz, "Core", "Inc", "main.h")
    if not os.path.isfile(main_h):
        raise SystemExit("ERROR: no existe {}".format(main_h))

    fw_ver, fw_hw = version_y_hw_del_main_h(main_h)

    tmp = args.salida + ".img"
    oc = objcopy(args.elf, tmp)
    imagen = open(tmp, "rb").read()
    os.remove(tmp)

    if len(imagen) == 0:
        raise SystemExit("ERROR: la imagen quedo vacia")
    if len(imagen) > BANCO_BYTES:
        raise SystemExit("ERROR: la imagen son {} bytes y el banco {} ({} de mas)".format(
            len(imagen), BANCO_BYTES, len(imagen) - BANCO_BYTES))
    # ⚠ La imagen tiene que ser multiplo de 8: el L4 programa de a doble palabra.
    #   Se rellena con 0xFF, que es el valor de la flash borrada.
    resto = len(imagen) % 8
    if resto:
        imagen += b"\xFF" * (8 - resto)

    img_crc = zlib.crc32(imagen) & 0xFFFFFFFF

    cab = struct.pack("<4sHH16s16sII",
                      MAGIC, HDR_VER, HDR_LEN,
                      campo_fijo(fw_ver, VER_LEN, "FW_VERSION"),
                      campo_fijo(fw_hw,  HW_LEN,  "FW_HW"),
                      len(imagen), img_crc)
    assert len(cab) == 48, len(cab)
    hdr_crc = zlib.crc32(cab) & 0xFFFFFFFF
    cab += struct.pack("<I", hdr_crc) + b"\0" * 12
    assert len(cab) == HDR_LEN, len(cab)

    with open(args.salida, "wb") as f:
        f.write(cab)
        f.write(imagen)

    print("objcopy      : {}".format(oc))
    print("version      : {}   (de {})".format(fw_ver, os.path.relpath(main_h)))
    print("hardware     : {}".format(fw_hw))
    print("imagen       : {} bytes ({:.1f} % del banco de 512 KB)".format(
        len(imagen), 100.0 * len(imagen) / BANCO_BYTES))
    print("CRC32 imagen : 0x{:08X}".format(img_crc))
    print("CRC32 header : 0x{:08X}".format(hdr_crc))
    print("salida       : {} ({} bytes)".format(args.salida, HDR_LEN + len(imagen)))
    print()
    print("Copiar ese archivo a la microSD como FIRMWARE.BIN")


if __name__ == "__main__":
    main()
