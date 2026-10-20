# TESTINGS

Registro de los **tests de banco largos** del firmware FWDLGARM_R1: qué se midió, con qué
instrumento, qué pasó y qué quedó abierto.

> **Por qué existe este archivo y no va todo en `CLAUDE.md`.** Aquel documenta el **diseño** —por qué
> el firmware es como es— y ya pasa las 3.000 líneas. Un test largo produce otra cosa: **números con
> fecha, instrumento y procedimiento**, que hay que poder releer dentro de seis meses para saber si
> una medición nueva contradice a una vieja. Las dos cosas mezcladas hacen que ninguna se encuentre.

Las herramientas viven en `Tools/` y se versionan con el firmware que prueban: el latido `CNT::` de
la consola es su contrato.

---

## Test 1 — El contador de pulsos, 48 h con la BeagleBone (2026-10-13 → 2026-10-15)

| | |
|---|---|
| **Firmware** | `0.0.19`, compilado `Oct 12 2026 16:57:59` |
| **Configuración** | modo `DISCRETO`, `timerpoll` 60 s, `magpp` 0,1 m³/pulso, contador habilitado |
| **Generador** | BBB, `generador_pulsos.py`, cierre de **50 ms**, jitter **±2 %**, perfil de 9 tramos |
| **Logger** | `ulises-sat` (Python 3.5.2), `logger_serie.py`, checkpoints cada 15 min |
| **Archivos** | `pulsos_20261013_090531.csv` (2,1 MB) · `equipo_20261013_090643.log` (2,0 MB) · `generador_20261013_090531.log` |
| **Corrida** | **48,00 h exactas**, 2 vueltas completas del perfil de 23,97 h |

### ⭐⭐ El criterio de aceptación: PASÓ

```
cierres emitidos por la BBB : 33.085
contados por el equipo      : 33.085      <- resta CONSTANTE en -22
descartes por antirrebote   : 0 cortos
```

Los **−22** son el desfase de arranque: el equipo ya tenía 22 pulsos de las pruebas del 12/10 cuando
el generador empezó. ⭐ **Lo que vale es que la resta no se movió ni una cuenta en 48 horas** — al
empezar −22, al terminar −22, deriva **0**.

⭐ **Las dos puntas se confirman entre sí**: en el CSV **ningún** pulso salió con dT < 500 ms (el
mínimo real fue 1764 ms), y el equipo informa `cortos=0`. O sea que el antirrebote no tuvo nada
legítimo que descartar, **y tampoco se comió nada**.

### ⭐⭐ El test se partió en dos tramos, y eso lo convirtió en un bisect

Con `TERM_SENSE` conectado el equipo toma `pwrLOCK_TERM` y baja a **Sleep**, no a Stop 2. El log se
autoseparó —las respuestas a comandos aparecen sólo cuando la USART recibe—:

| Tramo | Desde → hasta | Duración | El micro | Checkpoints |
|---|---|---|---|---|
| **1** | 13/10 09:07 → **14/10 06:52:21** | **21,75 h** | **Stop 2**, despierta por EXTI en cada pulso | 89 **mudos** |
| **2** | 14/10 06:52 → 15/10 09:06 | **26,01 h** | **Sleep**, siempre despierto | 103 **con respuesta** |

⭐ **El conteo es exacto en los dos.** Eso es lo que el corte vino a medir: el despertar por EXTI
desde Stop 2 **no pierde pulsos**, ni siquiera a 200 m³/h donde llegan cada 1,8 s. Lo que estaba
documentado como razonamiento —*"la EXTI latchea el flanco, así que el `__disable_irq()` del tickless
demora la interrupción, no la pierde"*— ahora está **medido sobre 33.085 pulsos**.

⚠ Sin el corte, el hallazgo de abajo no se habría podido atribuir.

### ⛔⛔⛔ EL HALLAZGO: el TICK CORRE 1,14 % LENTO MIENTRAS EL EQUIPO DUERME

No lo buscábamos, y es el resultado más importante del test. Medido con el campo `tick` que imprime
`status`, contra el reloj del logger:

| Tramo | ticks/s reales | vs 512 Hz nominal |
|---|---|---|
| **1 — Stop 2 (dormido)** | **506,160** | ⛔ **−1,141 %** |
| **2 — Sleep (despierto)** | 511,728 | −0,053 % |

Y el caudal reportado, contra el reproceso del algoritmo sobre los mismos dT:

| El micro | diferencia equipo vs reproceso |
|---|---|
| **dormido** | **1,0 a 1,5 %**, sistemático y **siempre de más** |
| **despierto** | **0,005 a 0,08 %** |

⭐⭐ **Es el mismo número, y el signo lo confirma**: el tick atrasa → los dT medidos **en ticks** salen
cortos → `Q = K/dT` sale **alto**, en la misma proporción.

#### ⚠ Esto corrige algo que `CLAUDE.md` daba por inocuo

El atraso del tick está documentado **desde el bring-up** (`port_lptim_tick.c`): el port descarta el
resto de la división al convertir cuentas del LPTIM en ticks, hasta 0,98 ms por despertada, y la nota
predecía *"0,05 % a 1 Hz, ~1,5 % a 30 Hz"*. ⭐ **Medido 1,141 %, dentro de lo predicho.**

⛔ **Pero esa nota dice "no rompe nada — los timestamps los estampa el RTC, no el tick", y eso ya no
alcanza.** El `dT` del caudal **se mide en ticks** (`caudal.c`), así que el atraso sale como **caudal
de más en un dato que se transmite al servidor**. No es un `vTaskDelay()` que se estira: es una
medida física con 1,1 % de sesgo, **y sólo en campo**, que es donde el equipo duerme.

⭐ **El arreglo ya estaba identificado**: arrastrar el resto de una despertada a la siguiente, unas
pocas líneas en `vPortSuppressTicksAndSleep()`. Toca el port, validado desde `v0.0.5`, así que va en
su propio commit y con una variable por vez.

⚠ **Para dimensionarlo antes de decidir**: 1,1 % sobre un caudal es menos que la incertidumbre de un
caudalímetro de campo, así que **no invalida ninguna instalación**. Pero es un sesgo sistemático, no
ruido, y se acumula en los totalizados del servidor.

### ⭐⭐ El reproceso del EMA: el firmware implementa lo que dice

`analizar_test.py` corre ahora el algoritmo de `caudal.c` sobre los dT del CSV.

```
                   reproceso    el equipo     dif
  total                33085        33085      +0        <- ⭐ exacto
  validos              31757        31529    +228
  cortos                   0            0      +0
  imposibles            1328         1569    -241
```

⭐ **El total cuadra exacto**, y donde el tick está sano el caudal coincide a **4 cifras**:

```
  tramo            Qteor   reproceso   el equipo      dif
  regimen_bajo     30.00      30.007      30.005    0.005 %
  pico_manana     150.00     150.056     150.040    0.011 %
  medio            60.00      59.996      59.991    0.009 %
  tarde            80.00      80.011      80.006    0.006 %
```

#### ⭐ La sutileza que decide el conteo de descartes

Cuando un pulso se descarta —corto o imposible— `caudal_pulso_desde_isr()` hace `return` **antes** de
`ulUltimoTick = ulTicks`: **la referencia NO avanza**. El dT del siguiente se mide desde el último
**válido**, así que **no puede haber dos descartes seguidos**.

⚠ Modelarlo mal cambia el resultado en cientos: con la referencia avanzando el reproceso da **2009**
imposibles y sin avanzar **1328**, sobre los mismos datos. Está anotado en el docstring de
`reprocesar()` porque es la clase de detalle que se "simplifica" sin notarlo.

### ⭐ El soak: 48 h sin que nada se mueva

| | |
|---|---|
| ⭐ **Heap** | **2992 bytes libres, el MISMO valor en los 103 checkpoints** — cero fuga, cero fragmentación |
| ⭐ **Stacks** (peor caso de toda la corrida) | `tkCmd` 24,6 % · `tkSys` 24,5 % · `tkCPres` 24,2 % · `tkWan` **13,0 %** · `tkCtl` 6,5 % |
| ⭐ **Watchdog** | margen mínimo **30.140 ms** de 90.000 — exactamente el margen de diseño de `tkWan` (trocea en 60 s contra un plazo de 90), nunca menos |
| ⭐ **Resets** | **0** en 48 h. Los 104 `reset por: BOR/POR PIN` del log son la respuesta de `status` informando el reset **anterior** al test; no hay ni un banner de arranque |
| Ventana | 42 de 1984 (2 %) — transmitió todo, sin acumular |
| Sesiones | ~60 registros confirmados y borrados por hora, las 48 h |

⭐⭐ **Los stacks no se movieron de lo medido en banco**, así que 48 h de operación continua
confirman la decisión del 2026-10-12 de **no agrandarlos**: siguen siendo un instrumento con señal.

### ⛔ Un error de diseño del INSTRUMENTO, no del firmware

El tramo `maximo` corría a **200 m³/h exactos**, que es `CAUDAL_MAX_M3H`: el perfil pasaba
**justo sobre el umbral** del descarte por "físicamente imposible", y con el jitter de ±2 % la mitad
de sus pulsos caía de cada lado. De 4002 pulsos, **2015 salieron con dT < 1800 ms**.

⚠ El totalizador no se afecta —cuenta todos los flancos— pero el caudal de ese tramo quedó sin
sentido: el equipo reportó ~148 m³/h donde el generador emitía 200.

⭐ **Es la misma familia que el ancho de 10 ms del simulador del AVR contra el umbral de 4,8 ms del
filtro: margen cero.** ✅ Corregido: el tramo pasa a **190 m³/h** (`casi_maximo`), con 57 ms de aire
sobre el umbral en el peor caso del jitter.

ℓ **Y de paso dejó medido el umbral efectivo del equipo**: por barrido, los 1569 descartes
corresponden a un umbral de **~1810 ms** (198,9 m³/h) y no a los 1800 teóricos. Los 10 ms de
diferencia son el tick atrasado del tramo dormido.

### ⚠ La BBB no tiene NTP: 938 ppm de deriva

```
deriva entre los dos relojes: 161,8 s en 47,9 h  ->  +938 ppm
```

⭐ **El conteo no se afecta** —compara totales, que no dependen de la hora— y el analizador **mide**
esa deriva con los propios datos, usando el conteo como referencia común. Si la hubiera supuesto
cero, habría reportado un desfase creciente y lo habría leído como pulsos perdidos.

⚠ Lo que sí queda con ±2,7 min de incertidumbre es el *"cuándo"* de una pérdida. Para la próxima
corrida: `sudo date -s` en la BBB antes de arrancar.

### Pendientes que deja este test

| | |
|---|---|
| ⛔ **El resto del tick en `vPortSuppressTicksAndSleep()`** | el hallazgo de arriba. Commit propio, toca el port |
| ⏳ El **barrido de frecuencia** (`--barrido`) | el techo de ~60 Hz del filtro, documentado desde el bring-up y nunca verificado |
| ⚠ Los **errores de UART no salen en `status`** | el soak no los pudo reportar porque el firmware no los imprime. Si se quieren en el próximo, hay que agregarlos |
| ⏳ Caminos de stack **no ejercitados** | `tkCtlPres` ejecutando una consigna real, `tkWan` con un lote de 1984 líneas, `fs sd format` |

---

## Test 2 — El contador en modo CONTINUO, 48 h (2026-10-16 → 2026-10-18)

Mismo banco, con el `PWRMODO` cambiado **desde el servidor** para que el equipo se reconfigure solo.

**Qué agrega sobre el Test 1**, y no es lo que parece:

| | |
|---|---|
| ⚠ Para el **contador** | `CONTINUO` ≈ tramo 2: con `TERM_SENSE` conectado ninguno de los dos duerme en Stop 2. **Lo nuevo no es el sueño** |
| ⭐⭐ Lo nuevo es **el modem transmitiendo sin parar** | la actividad eléctrica más fuerte del equipo: si los pulsos se perdieran por **ruido/EMI**, es el único tramo donde aparecería |
| ⭐⭐ Los **stacks con `tkWan` trabajando de verdad** | sesiones encadenadas cada `timerpoll` en vez de sueltas |
| ⭐ El **cambio de régimen en vivo** | nunca se ejercitó: el `PWRMODO` llega por `CONF_BASE` y el equipo se reconfigura sin que nadie vaya al sitio |

⭐ **La firma que confirma que el cambio entró de verdad, y no sólo que el servidor lo mandó: el `BH`
tiene que CAMBIAR**, porque `[PWRMODO:%d]` entra en ese hash.

```
CONF_ALL  ->  el servidor pide BASE      <- el hash dejo de coincidir
CONF_BASE ->  se aplica y se graba
CONF_ALL  ->  CONFIG=OK                  <- cerro con el modo nuevo
```

Si el `BH` no se mueve, el modo **no entró** y el equipo sigue en `DISCRETO` con el log diciendo que
todo está bien.

### ✅ Resultado: PASÓ (analizado el 2026-10-19, firmware `0.0.19`)

Logs: `Tools/equipo_20261016_123310.log`, `Tools/pulsos_20261016_123344.csv`,
`Tools/generador_20261016_123344.log`. Mismo perfil del Test 1 (dos vueltas de 24 h, `magpp` 0,1,
cierre de 50 ms, jitter ±2 %), con el tramo máximo ya en **190 m³/h**.

```
cierres emitidos por la BBB : 32.884
contados por el equipo      : 32.884     <- resta en 0 de principio a fin
cortos : 0     imposibles : 0     resets en 48 h : 0 (un solo arranque, 47,76 h de tick)
```

| | |
|---|---|
| ⭐⭐ **No se perdió ni un pulso con el modem transmitiendo** | **2.824 sesiones** de datos, una por minuto, durante 48 h. Era el único tramo donde una pérdida por ruido/EMI podía aparecer, y no apareció |
| ⭐ **El EMA** | equipo contra reproceso: **≤ 0,036 %** en los 14 tramos |
| ⭐ **El tick** | **512,009 ticks/s = +0,002 %**: sin Stop 2 el tick es exacto. Confirma que el 1,141 % del Test 1 es **sólo** del sueño |
| ⭐ **La transmisión** | **2.880 registros, 2.880 confirmados** (2.768 × `1 de 1` + 56 × `2 de 2`). La ventana nunca pasó de 2 |
| La sesión | una sola apertura (`OFFLINE` → `ONLINE_CONFIG` con `CONFIG=OK` → `ONLINE_DATA`) y 48 h sin caerse ni reabrir. 32 acuses vacíos de `DATANR` descartados, como corresponde |
| El soak | heap fijo en **2992 B**; stacks máx: `tkCmd` 26,1 %, `tkCPres` 24,2 %, `tkSys` 23,2 %, `tkWan` 12,8 %, `tkCtl` 7,8 % — los mismos números del Test 1 |

ℹ️ El `validos` del equipo da 10 menos que el reproceso **por el instrumento**: el último `status`
fue a las 12:19 y los pulsos siguieron hasta las 12:32. El `total` del último latido coincide exacto.

### ⛔ Lo que este test NO validó: el cambio de régimen en vivo  — ✅ lo cerró el Test 3

El equipo **arrancó ya en `CONTINUO`** (`tkWan arrancando (modo CONTINUO)` en el banner) y hubo una
sola sesión de configuración, que contestó `CONFIG=OK` con **`BH=0xDB`** todo el tiempo. O sea que el
`PWRMODO` se cambió **antes** de arrancar el logger y el mecanismo —el servidor pide `BASE`, se aplica,
el `BH` se mueve— **no quedó registrado**. Para validarlo hay que arrancar en `DISCRETO`, cambiar el
modo en el servidor con el logger corriendo, y ver moverse el `BH`.

✅ **Eso pasó solo el 2026-10-19 y quedó en el registro de eventos** — ver el Test 3: `CONTINUO` → el
servidor pide `BASE` → `RESET` → `ARRANQUE … modo DISCRETO`. No hizo falta montar nada.

### Tres cosas que aparecieron

1. ⛔ **Un error del ANALIZADOR, no del firmware.** Una ventana de `cerca_del_umbral` daba
   **17 millones de m³/h**. La causa: la consola es compartida, y `tkWan` imprimió la respuesta del
   servidor **en medio** del registro de `tkSys`:
   ```
   18/10/26 04:45:39;</log><log>PA=-2.50;…Q0=9.948;</log><log><- …"<html>CLASS=DATA&CLOCK=2610180446</html>"
   ```
   y el `CLOCK=2610180446` se tomó como un campo de caudal. En el log del equipo **no hay ni un
   `Q0` anómalo**. Corregido en `analizar_test.py` (corta en el `<-`); ese tramo pasó a 0,029 %.
   Verificado en Python 3.5 con `verificar_py35.sh`.
2. ⚠ **Esa misma intercalación puede romper la app de la terminal**: un registro llega partido en
   varios `<log>` con la respuesta del servidor en el medio. No toca los datos, pero si la app arma
   el registro juntando fragmentos, lo va a armar mal. La salida limpia es que `tkSys` imprima el
   registro **de una sola escritura**.
3. ℹ️ **56 minutos llegaron con un minuto de atraso** (los `2 de 2`). `tkWan` en `ONLINE_DATA` espera
   `timerpoll` **después** de vaciar (~1 s), así que deriva contra el `vTaskDelayUntil` de `tkSys` y
   cada ~50 vueltas saltea un minuto. **No se pierde nada**; si molesta, se arregla esperando con
   `vTaskDelayUntil` también en `tkWan`.

### Pendientes que deja este test

| | |
|---|---|
| ⛔ **El resto del tick en `vPortSuppressTicksAndSleep()`** | sigue siendo **el único** hallazgo de los dos tests que afecta un dato que viaja al servidor (+1,1 % de caudal dormido). ⏸ **Diferido por Pablo (2026-10-19)**: sale a campo con el sesgo, se ve en el futuro |
| ✅ El cambio de `PWRMODO` en vivo | **CERRADO por el Test 3**: quedó registrado en el log de eventos, con el `BH` moviéndose y el modo nuevo en el `ARRANQUE` siguiente |
| ⏳ El registro en una sola escritura de consola | por la app de la terminal |
| ⚠ Los errores de UART en `status` | siguen sin imprimirse; ningún test los pudo reportar |
| ⏳ El consumo con el modem en `CONTINUO` | no se midió |

---

## Test 3 — El registro de eventos en la microSD, 19 h en banco (2026-10-19 → 2026-10-20)

Primera corrida del registro de eventos (`eventos.{h,c}`, commit `89a4f2e`, firmware `0.0.19`
compilado `Oct 19 2026 14:31:44`). Quedó corriendo toda la noche **sin terminal**, en `DISCRETO` con
`timerpoll` 60 s y `timerdial` 900 s, contra el servidor de testing.

El log: `Tools/EVENTOS.TXT`, **251 líneas / 17.074 bytes**, de `19/10 14:40:36` a `20/10 10:02:15`.
⚠ No es un test del contador: **no había caudalímetro** (`cnt total 0` en los cuatro `RESUMEN`), así
que lo que se validó es el *instrumento*, no la medición.

### ⭐⭐ Los 245 bytes recuperados de la SRAM2 eran LEGÍTIMOS, y la cuenta cierra AL BYTE

Al arrancar, el equipo informó `245 bytes de eventos de ANTES del reset, recuperados de la SRAM2`, y
quedó la duda de si eran líneas reales o basura que la validación de la magia dejó pasar. **Son
exactamente las cuatro líneas que quedaban sin volcar cuando el servidor pidió el reset:**

```
26/10/19 14:40:46 t=20 ENLACE en 10 s: CSQ -57 dBm, 1 PING(s)
26/10/19 14:40:47 t=21 CONF_ALL: el servidor pide reconfigurar: BASE
26/10/19 14:40:51 t=25 el servidor pide RESET: reiniciando
26/10/19 14:40:51 t=25 reset pedido por la consola
```

```
las 4 lineas con sus CRLF  =  245 bytes     <- el numero que informo el equipo, exacto
```

Las cinco anteriores —el `ARRANQUE` y los cuatro `RESUMEN`— ya se habían escrito a la tarjeta, así
que **no quedó nada por explicar**: el buffer en `.ram2_noinit` sobrevivió al reset por software y se
volcó entero en el arranque siguiente. ⭐ Que el número coincida al byte es más fuerte que leer las
líneas y encontrarlas plausibles.

### ⭐⭐ Y de paso cerró el pendiente que dejó el Test 2: el cambio de régimen EN VIVO

El Test 2 no pudo validarlo porque el equipo ya había arrancado en `CONTINUO`. Esas mismas cuatro
líneas son la secuencia completa, y ocurrió sola:

```
ARRANQUE … modo CONTINUO | timerpoll 60 s, timerdial 3600 s     <- como estaba
CONF_ALL: el servidor pide reconfigurar: BASE                    <- el hash dejo de coincidir
el servidor pide RESET: reiniciando
ARRANQUE … modo DISCRETO | timerpoll 60 s, timerdial 900 s       <- ENTRO
```

⭐ **El `PWRMODO` se cambió desde el servidor sin que nadie fuera al equipo, y el registro de eventos
lo deja demostrado en cuatro líneas.** Es exactamente el caso de uso para el que se escribió: en
campo esa secuencia es invisible, porque lo único que llega al servidor son los datos.

### ✅ Resultado: PASÓ

| Criterio | Resultado |
|---|---|
| ⭐ **El log sobrevive al reset** | los 245 bytes, al byte |
| ⭐ **19,3 h sin una sola falla** | **77 enlaces, 76 sesiones, `sesiones fallidas 0`** |
| **1153 registros transmitidos** | ventana nunca pasó de **10 de 1984**; `pisados 0` |
| ⭐ **El acumulado del `RESUMEN` cierra exacto** | informa `36 vaciados ok, 545 registros` y las sesiones desde ese arranque suman **36 y 545** — y se reinicia con el reset, como debe |
| **Heap invariable** | 2992 B en los cuatro `RESUMEN` |
| Sin descartes | `cortos 0, imposibles 0, sd postergadas 0, rtu descartados 0`, sin `[!] PERDIDOS` |
| Sesiones | 28-39 s, media **31,8 s**; CSQ de −57 a −81 dBm, media −70 |
| Rotación y volcado | el archivo quedó en 17 KB contra el techo de 1 MB; ningún evento perdido |

**Los stacks, por primera vez con `tkCtlPres` consumiendo de verdad** (palabras libres, arranque →
`RESUMEN` de medianoche, 9 h):

```
cpres  485 -> 389  (-96, el que mas se movio)      sys  793 -> 726  (-67)
wan   1927 -> 1780 (-147)                          ctl  359 -> 353  (-6)      cmd  870 -> 870
```

⚠ Siguen holgados —`cpres` en 389 de 512 es **76 % libre**— y coinciden con el peor caso ya medido
(124 palabras usadas). No se mueven los márgenes: ver la nota de `CLAUDE.md` sobre por qué los *high
water mark* son un instrumento.

### ⛔⛔ EL HALLAZGO: el reloj saltó 15 MINUTOS, y el log no lo dice

No se buscaba. Sale de cruzar el timestamp del RTC contra el campo **`t=`** de cada línea, que es el
tick y es independiente del RTC:

```
20 00:06:07  t=33594   el RTC RETROCEDE   -74 s   mientras el tick avanza  +17 s
20 00:21:31  t=34510   el RTC avanza      -83 s menos que el tick
20 09:00:01  t=66213   el RTC avanza      +59 s   mientras el tick avanza +939 s   <- 880 s atrasado
20 09:15:07  t=66231   el RTC avanza     +905 s   mientras el tick avanza  +17 s   <- y los recupera
```

Los otros **71 intervalos entre sesiones son limpios** (el RTC avanza +9 s más que el tick, que es la
deriva conocida), así que no es un corrimiento progresivo: son **dos saltos puntuales**, y el grande
casi se cancela consigo mismo (−880 / +897).

⛔ **Y el hueco real es que el log no registra ninguna corrección de reloj.** Hubo que deducir el
salto cruzando dos columnas. `wan_rtc_sincronizar()` **ya calcula e imprime la deriva en ppm** cuando
ajusta — si esa línea fuera un evento, el log se explicaría solo.

⚠ **Las dos hipótesis, y ninguna está verificada:**

| | |
|---|---|
| **(a)** el `CLOCK=` del servidor metió una hora mala en la sesión de 08:59 y la corrigió en la siguiente | explica los dos signos y que se cancelen |
| **(b)** el MCP79410 se detuvo ~880 s | pero su cristal `Y1` **adelanta** (+332 ppm, sin condensadores de carga), no atrasa |

⭐ La (a) es la más probable, y **las separa una sola cosa: loguear la corrección como evento.** En
campo esto se vería como muestras con la fecha corrida y nada que apunte a la causa — y el servidor
indexa por esa fecha.

### ⭐ El tick: reconfirmado de forma independiente, y NO es una constante

Sobre los 71 intervalos limpios, con el RTC como referencia:

| | ticks/s reales | vs 512 Hz |
|---|---|---|
| **Esta corrida** (DISCRETO, **sin caudalímetro**) | **507,256** | **−0,927 %** |
| Test 1, Stop 2 (con caudalímetro a ~0,2 Hz) | 506,160 | −1,141 % |
| Test 1, Sleep (terminal conectada) | 511,728 | −0,053 % |

⭐ **La diferencia es coherente con que acá no hubiera pulsos**: cada despertada descarta el resto de
la división al convertir cuentas del LPTIM en ticks, así que **el atraso depende del patrón de
despertadas, no es un número fijo del firmware**. Dicho de otro modo: el sesgo que va a ver un equipo
en campo depende del caudal que mida. ⏸ El arreglo sigue **diferido** por decisión de Pablo.

### ⛔ Un falso hallazgo mío, y vale como lección de método

Reporté que `get_fattime()` estampaba las fechas **3 h corridas**, porque el `mtime` de la copia en
`Tools/EVENTOS.TXT` (`07:02:14`) daba exactamente 3 h menos que su última línea (`10:02:15`), y el
segundo cuadraba con la cuantización a 2 s de FAT.

⛔ **Es falso, y lo desmienten dos cosas:** el código de `get_fattime()` no tiene ningún ajuste de
huso —lee el mismo RTC que el log y empaqueta los campos sin tocarlos— y los **`PULSOS*.CSV` de la
otra tarjeta llevan la hora local correcta**: `PULSOS00.CSV` dice `2026-10-13 09:05:26`, que es la
hora exacta en que arrancó el Test 1.

⭐ O sea que el corrimiento es de **cómo llegó el archivo a `Tools/`**, no del firmware. Van varias en
este proyecto: **un timestamp de archivo no es un instrumento del equipo**, y la verificación que
cerró el caso fue mirar el código y un segundo archivo, no razonar sobre el primero.

### Pendientes que deja este test

| | |
|---|---|
| ✅ **Loguear la corrección del reloj** | **HECHO el 2026-10-20**: `RELOJ puesto en hora…` con el salto, y `RELOJ deriva: … ppm`. Era el único hueco que este test encontró, y lo encontró en la primera corrida |
| ⏳ **`wdg colgar`** | no hay ni un `[X] WATCHDOG` ni un `ARRANQUE … IWDG` en el log: el camino del watchdog en el registro sigue sin ejercitarse |
| ⏳ El contador en el log | `cnt total 0`: sin caudalímetro, esa parte del `RESUMEN` no se ejercitó |
| ℓ `reset pedido por la consola` | lo imprime también cuando el reset lo pidió **el servidor** (la línea anterior lo dice). Mensaje fijo; manda a buscar al lado equivocado |
| ℹ️ `bt12v` 10,17..10,66 V | el montaje de banco de ese riel, ya conocido — no hay nada que investigar |
