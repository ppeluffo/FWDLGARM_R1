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

⛔ **No flashees mientras corre la suite.** Un download reinicia el equipo, y los
tests que verifican "no se reseteó" lo reportan como falso positivo del watchdog
— y ahí uno se pone a buscar un bug que no existe. Ya pasó el 2026-09-23.
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
| **G** | ⭐ transmisión: el servidor vivo, PING, CONF_ALL, DATA, la FSM | |
| **H** | ⭐ watchdog: kill, bridge, prórrogas, el cuelgue y el `WDG=3` | ✋ la microSD y el cuelgue |
| I | régimen dormido, de una sola vía | ✋ desconectar `TERM_SENSE` |

Implementadas **A, B, G y H** (21 tests). C, D, E, F e I siguen el mismo molde.

## El área G asume que el servidor está bien

⭐ **La única fuente de la suite es el log del datalogger.** No se lee el log del
servidor, ni su base, ni se le hacen consultas — entre otras cosas porque el
servidor puede estar en otra máquina, sin acceso.

Y no hace falta: **la respuesta del servidor ya viaja por la consola del
equipo**. `CLASS=PONG`, `CONFIG=OK` y el `OK: N de N` son el servidor diciendo
que sí, contados por el datalogger, que es justo lo que se valida.

Lo que tiene que estar listo **antes** de correr el área G, y es responsabilidad
del operador:

| | |
|---|---|
| La **ingesta corriendo** y alcanzable desde el módulo | en la IP y puerto que tenga el DTU (`lte info` los muestra; se fijan con `lte set server <ip> <puerto>` + `lte save`) |
| El **IMEI dado de alta** en el servidor | si no, `CONF_ALL` devuelve `CONFIG=ERROR` y ninguna configuración cierra nunca |
| Una **SIM con datos** | ⚠ tener señal no es tener conexión: lo que decide es `AT+CIP?` |

Si algo falta, los tests fallan — y está bien que fallen— pero el motivo es el
entorno y no el firmware. Los mensajes de error lo dicen.

⭐ **El test del `CONFIG=OK` corre la sesión dos veces**, y ésa es la parte que
importa: el criterio de aceptación no es que la configuración se aplique, sino
que **en la sesión siguiente el servidor deje de pedir los bloques**. Eso es lo
único que prueba que los strings del hash son idénticos de los dos lados.

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
