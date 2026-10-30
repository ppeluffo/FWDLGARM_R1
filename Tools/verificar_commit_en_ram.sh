#!/bin/bash
# verificar_commit_en_ram.sh -- la red del `fw commit`
#
# ⛔⛔ POR QUE EXISTE: `fw commit` BORRA EL BANCO 1, que es desde donde se
#    ejecuta. Todo el codigo que corra durante esa operacion tiene que estar en
#    RAM; una sola instruccion en flash es un HardFault con el firmware ya
#    borrado, o sea el equipo MUERTO.
#
# ⚠ Y no alcanza con `static inline`: con -O0 --que es como se compila todo el
#   bring-up-- GCC NO INLINEA NADA. Ya paso: dos de las tres funciones quedaron
#   en flash y `prvCommitEnRam()` las llamaba por un veneer. **Compilo sin un
#   solo warning.** Esto es lo que lo caza.
#
#    ./Tools/verificar_commit_en_ram.sh [Debug/FWDLGARM_R1.elf]

set -u
ELF="${1:-Debug/FWDLGARM_R1.elf}"
FN="prvCommitEnRam|prvProgramarDw|prvEsperarFlash"
RAM="^2000"

P=$(echo ~/st/stm32cubeide_2.2.0/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.linux64_*/tools/bin)
NM="$P/arm-none-eabi-nm";      [ -x "$NM" ] || NM=arm-none-eabi-nm
OD="$P/arm-none-eabi-objdump"; [ -x "$OD" ] || OD=arm-none-eabi-objdump

[ -f "$ELF" ] || { echo "ERROR: no existe $ELF"; exit 2; }
fallas=0

echo "== 1. las tres funciones tienen que vivir en RAM =="
tabla=$("$NM" -n "$ELF" | grep -E " (t|T) ($FN)\$")
[ -z "$tabla" ] && { echo "   ERROR: no encontre las funciones en $ELF"; exit 2; }
while read -r dir _ nom; do
    if [[ "$dir" =~ $RAM ]]; then echo "   OK   $nom  0x$dir"
    else                          echo "   ⛔   $nom  0x$dir  <- EN FLASH"; fallas=1; fi
done <<< "$tabla"

# ⭐ Si alguna quedo en flash, no tiene sentido seguir: el rango del
# desensamblado abarcaria medio firmware y el reporte seria ilegible.
if [ $fallas -ne 0 ]; then
    echo
    echo "⛔ NO BAJAR ESTE FIRMWARE: 'fw commit' dejaria el equipo muerto"
    exit 1
fi

# el rango que ocupan, para acotar el desensamblado
ini=$(echo "$tabla" | head -1 | cut -d' ' -f1)
fin=$(echo "$tabla" | tail -1 | cut -d' ' -f1)
cuerpo=$("$OD" -d "$ELF" | awk -v a="$ini" -v b="$fin" \
   '/^[0-9a-f]+ </{d=substr($1,1,8); f=(d>=a && d<=b)} f')

echo "== 2. ninguna llamada puede salir de RAM =="
malas=$(echo "$cuerpo" | grep -E "\bblx?\s" | grep -vE "bl\s+2000" )
if [ -n "$malas" ]; then echo "$malas" | sed 's/^/   ⛔ /'; fallas=1
else                     echo "   OK   todas las llamadas caen en 0x2000xxxx"; fi

echo "== 3. ningun literal puede leerse de flash =="
lits=$(echo "$cuerpo" | grep -oE "; \(?0x[0-9a-f]{8}" | grep -v "0x2000" | sort -u)
if [ -n "$lits" ]; then echo "$lits" | sed 's/^/   ⛔ /'; fallas=1
else                    echo "   OK   sin literales fuera de RAM"; fi

echo
[ $fallas -eq 0 ] && { echo "✅ el commit es autocontenido en RAM"; exit 0; }
echo "⛔ NO BAJAR ESTE FIRMWARE: 'fw commit' dejaria el equipo muerto"; exit 1
