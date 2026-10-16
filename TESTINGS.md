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

## Test 2 — El contador en modo CONTINUO, 48 h (en curso)

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

⏳ Resultados: pendiente.
