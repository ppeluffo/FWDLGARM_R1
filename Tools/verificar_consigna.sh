#!/bin/bash
#
# verificar_consigna.sh  -  la DOBLE CONSIGNA, validada sin hardware.
#
# ⭐ NO copia codigo: extrae las funciones del `Application/tasks/tkCtlPres.c`
# REAL en cada corrida, asi el test no se puede desactualizar respecto del
# firmware. Es el mismo criterio con que se validaron la tabla de Pearson del
# hash (sacada de `cfg_hash.c`) y el CRC32 del autoupdate.
#
# Lo que comprueba:
#   1. test_franjas        : los bordes de las dos franjas, la configuracion
#                            invalida, y COBERTURA EXHAUSTIVA de los 1440
#                            minutos para los 2.072.160 pares (diurna,nocturna)
#                            posibles: ni un minuto sin consigna, y exactamente
#                            dos transiciones por dia, cada una en su minuto.
#   2. test_reconciliacion : la maquina de estados: arranque en AMBIGUA, el caso
#                            normal sin envios, el cambio de franja, los 3
#                            reintentos separados 5 min, el agotamiento, la hora
#                            no confiable, la configuracion invalida, y ⭐ EL
#                            CASO DE CAMPO: que la consigna se aplique aunque
#                            el minuto exacto no se muestree nunca.
#
# Uso:  Tools/verificar_consigna.sh
#       Tools/verificar_consigna.sh --autotest   (prueba que DETECTA un bug)
#
set -u

cd "$( dirname "$0" )/.." || exit 1

SRC=Application/tasks/tkCtlPres.c
T=$( mktemp -d )
trap 'rm -rf "$T"' EXIT

# El commit del que sale la implementacion ANTERIOR, para medir el bug de borde.
# Si ya no existe (arbol recien clonado), esa comparacion se omite y el resto
# del test corre igual.
COMMIT_VIEJO=0f0de37

rc=0
echo "== extrayendo de $SRC =="

sed -n '/^static uint16_t prvHhmmAMinutos/,/^}/p'      "$SRC" >  "$T/extraido.inc"
sed -n '/^static cpres_cmd_t prvConsignaDeseada/,/^}/p' "$SRC" >> "$T/extraido.inc"
sed -n '/^static void prvServicioConsigna/,/^}$/p'     "$SRC" >  "$T/servicio.inc"

for f in extraido.inc servicio.inc; do
    n=$( wc -l < "$T/$f" )
    if [ "$n" -lt 10 ]; then
        echo "  ⛔ $f quedo en $n lineas: cambio la firma de alguna funcion?"
        rc=1
    else
        echo "  $f: $n lineas"
    fi
done
[ $rc -ne 0 ] && exit 1

CON_VIEJA=""
if git show "$COMMIT_VIEJO:$SRC" 2>/dev/null \
     | sed -n '/^static cpres_cmd_t prvConsignaQueCorresponde/,/^}/p' > "$T/vieja.inc" \
   && [ -s "$T/vieja.inc" ]; then
    echo "  vieja.inc: desde $COMMIT_VIEJO ($( wc -l < "$T/vieja.inc" ) lineas)"
    CON_VIEJA="-DCON_VERSION_VIEJA"
else
    echo "  ⚠ no se pudo extraer la version anterior de $COMMIT_VIEJO: se omite la comparacion"
fi

# --autotest: rompe el borde a proposito, para probar que el test lo CAZA.
# Un verificador que nunca se vio disparar da falsa seguridad.
if [ "${1:-}" = "--autotest" ]; then
    echo
    echo "== AUTOTEST: se mete el bug de borde ( >= -> > ) a proposito =="
    sed -i 's/usAhora >= usDia/usAhora > usDia/' "$T/extraido.inc"
fi

echo
for t in test_franjas test_reconciliacion; do
    echo "===================== $t ====================="
    extra=""
    [ "$t" = "test_franjas" ] && extra="$CON_VIEJA"
    # shellcheck disable=SC2086
    if ! gcc -Wall -O2 -I"$T" -o "$T/$t" "Tools/tests_consigna/$t.c" $extra; then
        echo "  ⛔ no compila"
        rc=1
        continue
    fi
    "$T/$t" || rc=1
    echo
done

if [ "${1:-}" = "--autotest" ]; then
    if [ $rc -ne 0 ]; then
        echo "✅ AUTOTEST OK: el test DETECTO el bug inyectado."
        exit 0
    fi
    echo "⛔ AUTOTEST FALLO: se inyecto un bug y el test paso igual."
    exit 1
fi

[ $rc -eq 0 ] && echo "✅ la doble consigna pasa los dos tests" \
              || echo "⛔ hay fallos"
exit $rc
