# Suite de banco del datalogger

Corre una batería de comandos contra el equipo por la consola TERM, analiza las
respuestas y da un veredicto. La idea es **validar cada versión de firmware de
forma sistemática y formal** en vez de a mano con el minicom.

```bash
./suite.py -p /dev/ttyUSB0 --version 0.0.75
./suite.py -p /dev/ttyUSB0 -a AB            # sólo algunas áreas
./suite.py -p /dev/ttyUSB0 --sin-destructivos
```

⚠ **Cerrá el minicom antes**: el puerto es exclusivo.
⚠ **La suite pisa la configuración y formatea la microSD.** Corre sobre equipos
de prueba, no sobre uno con datos que importen.

## Las áreas

| | Qué | ✋ |
|---|---|---|
| **A** | identidad, banner, los 24 comandos, `wdg`, stacks | |
| **B** | ⭐ configuración y los 5 hashes | |
| C | periféricos: I2C, RTC, ADC, INA, contador, válvula, SD | |
| D | almacenamiento: ventana, volcado, lotes | ✋ sacar la microSD |
| E | Modbus | ✋ conectar el esclavo |
| F | control de presión | ✋ conectarlo |
| G | transmisión: PING, CONF_ALL, DATA, la FSM | ✋ el servidor |
| H | watchdog | ✋ el `lte bridge` largo |
| I | régimen dormido, de una sola vía | ✋ desconectar `TERM_SENSE` |

Hoy están implementadas **A y B**. El resto sigue el mismo molde.

## Cuando el script necesita que hagas algo

Se detiene, te dice qué, y sigue cuando confirmás:

```
──────────────────────────────────────────────────────────────────
✋ INTERVENCIÓN: retirá la microSD del datalogger.
   [Enter] cuando esté listo   ·   [s] saltear este test
──────────────────────────────────────────────────────────────────
```

`s` marca **SKIP**, que se ve distinto de **FAIL**: una corrida sin el esclavo
Modbus conectado no ensucia el reporte con fallas falsas.

## ⛔ Lo que esta suite NO valida

**El consumo, y todo lo que sólo ocurre dormido.** Con la terminal conectada
`TERM_SENSE` toma `pwrLOCK_TERM` y el equipo corre en Sleep a ~3,5 mA, nunca en
Stop 2. El bug del tickless comiéndose bytes del USART y los 82 µA del pull-up
de `SD_DET` **no habrían aparecido acá**. Las mediciones siguen siendo con el
tester y el ST-LINK desenchufado del USB.

El área **I** es la excepción parcial: desconectando sólo el pin de `TERM_SENSE`
—dejando TX y RX— el equipo entra en Stop 2 y **sigue mandando log**, así que se
puede verificar que las líneas salen íntegras (o sea que el candado de TX hace
su trabajo) y medir el intervalo real entre poleos.

⚠ En ese modo el canal es de **una sola vía**: en Stop 2 la USART no puede
recibir —ésa es la razón de ser de `pwrLOCK_TERM`— así que el script sólo
escucha. Un comando enviado ahí se perdería y parecería una falla del firmware.

## `referencia.json`

Los hashes de `CH`, `MH` y `PH` no tienen un valor publicado por el servidor, así
que se comparan **contra la corrida anterior**. El primer run crea el archivo; los
siguientes detectan cualquier cambio.

Un cambio no es necesariamente un bug, pero **tiene que ser deliberado**: si un
hash se mueve sin que nadie lo haya querido, el servidor pide reconfigurar ese
bloque en todas las sesiones — el "tráfico infinito en campo" contra el que
advierte `cfg_hash.h`.
