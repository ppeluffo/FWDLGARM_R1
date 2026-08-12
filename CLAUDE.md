# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> El proyecto, su código y su documentación están **en español**. Escribir comentarios, docs y
> respuestas en español.

> **Rutas.** Este archivo vive dentro del repo `FWDLGARM_R1`, pero describe todo el árbol del
> proyecto. **Salvo que se diga otra cosa, las rutas son relativas a `SPQ_ARM/`**, dos niveles más
> arriba: `Hardware/R001/` es `../../Hardware/R001/` desde acá. Las que empiezan con `Core/`,
> `Drivers/` o `Debug/` sí son de este repo.

## Sobre qué árbol se trabaja (leer primero)

Bajo `Firmware/` hay **tres** árboles de firmware. Sólo uno está vivo:

| Directorio | Qué es | Se toca |
|---|---|---|
| **`Firmware/FWDLGARM_R1/`** | **El firmware en desarrollo.** STM32CubeIDE + STM32L496RGT6, alineado con la placa `Hardware/R001/`. Iniciado 2026-08-05. | **Sí — sólo acá** |
| `Firmware/FWDLGZ/` | Prototipo de bring-up STM32 (FreeRTOS V11.1.0 + tick por LPTIM1). Repo git propio → `github.com/ppeluffo/FWDLGZ.git`. | No, salvo pedido expreso |
| `Firmware/FWDLGZ_V1/` | Port previo al **ATSAM4LS8BA** (Makefile + isla ASF). Histórico. | No, salvo pedido expreso |

Los dos últimos son **pruebas y prototipos**: material de consulta cuando Pablo lo indique, no
código a mantener. Su mapa de pines **contradice** al de la placa real (ver más abajo).

`FWDLGARM_R1` **no es todavía un repo git** (no tiene `.git`).

## El proyecto

Firmware del datalogger **SPQ** sobre **STM32L496RGT6** (Cortex-M4 **con FPU**, LQFP64), placa
propia **R001**. Linaje: firmware AVR `FWDLGX 3.0.0` (AVR128DA64) → port a ATSAM4LS8BA (`FWDLGZ_V1`)
→ **STM32L4**. El criterio de diseño permanente es **ultra bajo consumo**: es un datalogger a
batería que duerme casi todo el tiempo.

### Estado actual (2026-08-12)

**La placa está poblada sólo parcialmente.** Hoy hay montados: la **fuente**, el **micro**, la
**interfaz de programación SWD** (PA13/PA14), **LEDs en PB9 y PA2**, el **cristal de 32.768 kHz en
PC14/PC15** con sus condensadores de carga a GND, el **conector de la terminal** (PB6/PB7 más
`TERM_SENSE` en PB5), y el **bus I2C2** (PB13/PB14, pull-up de 10 kΩ) con la **EEPROM M24M01** y el
**RTC MCP79410 con su pila**. Nada más: no hay modem LTE, ni RS485, ni microSD, ni front-end
analógico.

Esto define el alcance de lo que se puede validar en banco: **clock, LSE, LED, SWD, la consola y el
bus I2C**. El resto del mapa de pines de `interfases_pines.csv` es el diseño completo de R001, no
hardware presente — no tiene sentido escribir drivers contra periféricos que todavía no están
montados.

El proyecto **se borró y se rehízo de cero el 2026-08-10** (ver control de versiones), y desde
entonces avanzó en seis etapas, todas validadas en banco y etiquetadas en git:

| Tag | Estado |
|---|---|
| `v0.0.1` | Clock MSI a 60 MHz, LED parpadeando |
| `v0.0.2` | FreeRTOS con API nativa, tarea `tkCtl` |
| `v0.0.3` | **Cristal externo: LSE + RTC**, `Error_Handler()` con destellos |
| `v0.0.4` | **Tick del kernel por LPTIM1 desde el LSE**, 512 Hz exactos |
| `v0.0.5` | **Tickless con Stop 2** — de 3,2 mA a 0,215 mA de placa |
| `v0.0.6` | **Consola TERM (TX + RX) y `TERM_SENSE`** — el micro dormido queda en **~5 µA** |
| `v0.0.7` | **Bus I2C2, EEPROM M24M01 y RTC MCP79410** — los tres validados con datos reales |

- `SystemClock_Config()`: **MSI (range 6 = 4 MHz) → PLL `PLLM=1`, `PLLN=30`, `/2` → 60 MHz**,
  `FLASH_LATENCY_3`, voltage scale 1, AHB/APB1/APB2 sin divisor. El **SYSCLK sigue viniendo del MSI**;
  el cristal alimenta al RTC, no al reloj de sistema.
- **LSE andando** (`v0.0.3`): `RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI` con
  `LSEState = RCC_LSE_ON`, y el RTC activado como consumidor (`Mcu.IP3=RTC`,
  `RTCClockSelection = RCC_RTCCLKSOURCE_LSE` en `HAL_RTC_MspInit()`). Ver más abajo por qué activar
  el RTC era la pieza que faltaba.
- **`__HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW)`** — el drive más bajo, o sea el de menor consumo,
  acorde al criterio del equipo. **Pero es también el de menor margen de arranque**: anda en banco a
  temperatura ambiente con este cristal. **Falta verificarlo en frío** antes de darlo por bueno en
  campo; si falla, subir un escalón cuesta algunos µA.
- **FreeRTOS** con port `GCC/ARM_CM4F`, heap_4, timebase de la HAL en TIM6, interface CMSIS-RTOS v2.
  **La aplicación se escribe con la API nativa** (`xTaskCreateStatic`, `vTaskDelay`), no con el
  wrapper: ver la sección del roadmap.
- **Tick del kernel por LPTIM1** (`v0.0.4`), en `Application/FRTOS/port_lptim_tick.c`: LSE 32768 Hz →
  prescaler /32 → contador de 1024 Hz → `ARR = 1` → **512 Hz exactos, sin deriva**. **El SysTick ya no
  se arranca nunca** (el `SysTick_Handler()` de `cmsis_os2.c` queda muerto); el timebase de la HAL
  sigue en TIM6. Ver la sección de `configTICK_RATE_HZ` para por qué 512 y no 1024 ni 1000.
  ⚠ **Efecto secundario que costó medio día el 2026-08-11:** como el SysTick queda apagado,
  `error_delay_ms()` usa **siempre** su lazo tosco calibrado por `SystemCoreClock`. Y si
  `Error_Handler()` se dispara desde adentro de `SystemClock_Config()`, esa variable todavía vale
  **4 MHz en vez de 60**: los destellos salen **quince veces más rápidos** y el patrón de 2
  parpadeos se convierte en un parpadeo tan veloz que a ojo **parece un LED prendido fijo**. Se
  confundió con un cuelgue mudo más de una vez. Resuelto llamando a `SystemCoreClockUpdate()` al
  entrar a `Error_Handler()`, y —mejor todavía— haciendo que además **lo diga por la consola**: la
  USART se levanta ahora en `USER CODE BEGIN Init`, antes de `SystemClock_Config()`, corriendo con
  el MSI a 4 MHz (a 9600 el divisor da 417, 0,08 % de error). Así hay diagnóstico por texto incluso
  cuando el que falla es el reloj.
- **Tickless con Stop 2** (`v0.0.5`), `vPortSuppressTicksAndSleep()` en el mismo archivo:
  `configUSE_TICKLESS_IDLE = 2`, techo de 64 s por sueño, piso de 3 ticks. Al despertar rellama a
  `SystemClock_Config()` (Stop apaga el PLL) y le informa al kernel el tiempo dormido con
  `vTaskStepTick()`. **`HAL_GetTick()` queda atrasado lo que haya durado el sueño**, porque TIM6 se
  suspende antes de dormir: sirve para los timeouts relativos del HAL, **no como hora**.
  Ver la sección de bajo consumo y la advertencia sobre el SWD.
- **Consola TERM** (`v0.0.6`), USART1 a **9600** por PB6/PB7: `Application/tasks/tkCmd.c` sobre
  FRTOS-IO. TX por interrupción con semáforo y candado de energía; RX por interrupción a un stream
  buffer, con la tarea **bloqueada en el kernel** (nada de poleo — ver el checklist de portación).
  Comandos: `help`, `status`, `sense`, `reset`, `reboot`. ⚠ **El parser matchea por PREFIJO**, así
  que `r` ejecuta `reset` y `s` ejecuta `status`; hay que tipear `res`/`reb` y `st`/`se`.
- **Tareas:** `tkCtl` (`Application/tasks/tkCtl.c`), prioridad `tskIDLE_PRIORITY+1`, stack de 384
  palabras, memoria **estática** (no toca el heap). Destella el LED y polea `TERM_SENSE` **cada 1 s**
  (`TKCTL_PERIOD_MS`). `tkCmd` (`Application/tasks/tkCmd.c`), también estática, 512 palabras de las
  que usa 137. `defaultTask` existe sólo porque CubeMX no deja vaciar la lista de tareas, y se
  elimina con `vTaskDelete(NULL)` apenas arranca el scheduler.
- `Error_Handler()` tiene **patrón de destellos de diagnóstico** en el LED: **2 = no arrancó un
  oscilador de baja velocidad** (con el cristal: cristal, condensadores o drive muy bajo),
  **5 = cualquier otra falla**. Desde `v0.0.6` **además lo dice por texto**, con `error_print()` en
  `main.c`: HAL por poleo, que es lo único que anda antes del scheduler y con las interrupciones
  cortadas. El LED quedó como respaldo para cuando no hay nadie mirando la consola.
  Detalles de implementación más abajo.
- `LED_PORT`/`LED_PIN`/`LED2_*` están en `main.h` (bloque *Private defines*) como **alias** de los
  símbolos que genera CubeMX, para que los vean todos los `.c` sin duplicar la definición del pin.
- **Pendiente de hardware:** los condensadores de carga son de **10 pF**, que corresponden a un
  cristal de `CL` ≈ 7-9 pF. Si el montado es de los comunes de **12,5 pF**, el RTC va a correr
  rápido —del orden de +50 a +100 ppm, unos **9 s/día**— lo cual importa en un datalogger que estampa
  la hora. Verificar el `CL` contra el BOM; si corresponde, cambiarlos por 18-22 pF.

### ⚠ El programador: lo que hay que saber antes de tocar nada

Un día entero se fue en esto, y los síntomas son lo bastante desconcertantes como para que sin estas
notas se vuelva a investigar desde cero. **El dongle es un ST-LINK/V2 standalone** (`0483:3748`,
firmware `V2J48S7`, genuino y reciente — no hace falta actualizarlo).

**1. Los fallos intermitentes de flasheo eran la TENSIÓN de alimentación.** Se veían como
`Error finishing flash operation` en CubeIDE o `failed to download Sector[0]` en CubeProgrammer, y
aparecieron al crecer el binario. El riel estaba en **~3,45-3,59 V**, contra un máximo absoluto de
**3,6 V** del STM32L496. Con el riel corregido a **3,28 V** el flasheo pasó a ser confiable.

> Firma del problema: **falla la escritura antes que el borrado** (la escritura usa la bomba de carga
> interna y es más exigente), y si la tensión sigue subiendo empieza a fallar también el borrado. Las
> transacciones cortas —device ID, option bytes— nunca fallan porque no tocan la flash.
>
> **Chequeo de un segundo:** el campo `Voltage` que imprime `STM32_Programmer_CLI -c port=SWD` al
> conectar. Si no coincide con el tester, o si deriva entre lecturas consecutivas, el problema es la
> alimentación. Se investigaron y descartaron antes: frecuencia SWD, option bytes, firmware del
> ST-LINK y cableado. Ninguno era.

**2. Programando desde la GUI, el firmware arranca solo — no hace falta ciclo de alimentación.**
Verificado el 2026-08-10 tras el download desde las herramientas de ST.

> Hubo un tramo en que **sí** hacía falta cortar y reponer la alimentación, y costó entender por qué.
> No era una característica del dongle: era **el estado en que lo dejaban las herramientas de línea
> de comandos**, sobre todo OpenOCD (punto 3). Una vez destrabada la línea de NRST con un `-hardRst`
> de CubeProgrammer, el arranque automático volvió a funcionar.
>
> Si el síntoma reaparece —se programa OK pero el micro no arranca, o el LED queda fijo— **la causa
> es NRST trabado, no el firmware.** Se destraba con
> `STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -hardRst`, o desenchufando el ST-LINK del **USB**.

**3. 🚫 NO usar OpenOCD con este dongle.** Al hacer `shutdown` deja **NRST asertado**, y como el
ST-LINK sigue alimentado por USB mantiene el micro en reset: la placa no arranca **ni cortándole la
alimentación**, y el LED queda fijo. Es un pozo que cuesta mucho reconocer, porque parece firmware
roto o placa quemada. Para destrabarlo: un `-hardRst` de CubeProgrammer, o **desenchufar el ST-LINK
del USB** (desconectar sólo el cable a la placa no alcanza).

**4. ⚠ La frecuencia SWD tiene que estar en 950 kHz. En `Auto` (4000 kHz) falla el borrado.**
Encontrado el **2026-08-11**, con el riel ya bueno en 3,27 V — así que **no es el problema del punto 1**,
es otro distinto con síntomas parecidos.

Se veía como `Error: failed to erase memory` al bajar desde CubeIDE. Con la misma placa y el mismo
cable, `STM32_Programmer_CLI ... freq=950` leía option bytes, `FLASH_SR` y memoria sin un error. La
diferencia era sólo la frecuencia: el GDB server usa 4000 kHz cuando la launch config dice
`frequency = "0"`, que es el default y significa *Auto*.

> **Se arregla en:** *Run → Debug Configurations… → pestaña **Debugger** → Frequency (kHz)* → `950`.
> En la GUI de CubeProgrammer, el mismo campo en el panel del ST-LINK.
>
> Misma firma que el problema de tensión y por eso confunde: **las transacciones cortas nunca fallan,
> las largas sí**. Leer un registro es una transacción; borrar la flash son miles seguidas.

**5. "El LED queda fijo" tiene DOS causas, y se distinguen con un comando.** Una es el NRST trabado
(punto 2). La otra es que **el micro esté en blanco**: si el borrado anduvo pero la escritura no, no
hay firmware que haga nada y el LED queda en el estado en que quedó el pin. Parece un cuelgue de
firmware y **no lo es** — el 2026-08-11 se depuró durante un rato un "cuelgue" que era esto.

```bash
$P/STM32_Programmer_CLI -c port=SWD freq=950 mode=UR -r32 0x08000000 4
```

En `0x08000000` vive el **stack pointer inicial**. Si dice `20040000` o parecido, hay firmware. Si
dice **`FFFFFFFF`, el chip está vacío** y no hay ningún cuelgue que depurar. Vale la pena hacerlo
antes de instrumentar nada.

Otros dos registros útiles en el mismo viaje, y cómo leerlos:

| Registro | Dirección | Qué decir de él |
|---|---|---|
| `FLASH_SR` | `0x40022010` | `0` = sin flags pegados (`WRPERR`, `PGSERR`, `PROGERR`) ni ocupado. Si no es 0, el controlador de flash quedó en falla. |
| `RCC_CSR`  | `0x40021094` | Bits 31-25, las banderas de reset (son **acumulativas** hasta que se limpian con `RMVF`, así que ver varias juntas es normal). Los bits `[11:8]` son el `MSISRANGE`: acá tienen que dar `6` (4 MHz), que es lo que espera `SystemClock_Config()`. |

### ✅ El LSE y CubeMX: asignar los pines NO alcanza (resuelto en `v0.0.3`)

**La trampa costó una vuelta entera en este proyecto, así que conviene leerla antes de configurar
cualquier otro periférico que cuelgue del LSE** (LPTIM1 para el tick, por ejemplo).

Asignar PC14/PC15 como `OSC32_IN`/`OSC32_OUT` y poner el *RTC/LCD Source Mux* en LSE es **necesario
pero no suficiente**. CubeMX genera el encendido del LSE sólo si **algún periférico lo consume de
verdad**, y "de verdad" significa que el periférico esté **activado**, o sea que figure en `Mcu.IPx`.
Un oscilador sin destino se considera no usado y no se emite una sola línea para él.

Lo que se vio acá, exactamente: con los pines asignados, `RCC.RTCClockSelection=RCC_RTCCLKSOURCE_LSE`
y `RCC.RTCFreq_Value=32768` en el `.ioc`, pero el RTC **sin activar**, `SystemClock_Config()` seguía
trayendo únicamente `RCC_OSCILLATORTYPE_MSI`. Mismo síntoma en el prototipo `FWDLGZ`, que tenía
LPTIM1 asignado pero con el mux en PCLK: el LSE nunca se encendía.

**La pieza que faltaba:** *Pinout & Configuration → Timers → **RTC** → "Activate Clock Source"*.
Con eso el RTC entra en `Mcu.IPx` y la cadena se completa sola.

**Cómo verificar que quedó bien** (no fiarse de `LSE_VALUE` en `RCC.IPParameters`: sólo aparece si se
cambia el valor por defecto, así que su ausencia no prueba nada):

- **El chequeo que vale**: que en `SystemClock_Config()` aparezcan `RCC_OSCILLATORTYPE_LSE` y
  `LSEState = RCC_LSE_ON`. Los campos del `.ioc` pueden mentir; el `.c` generado no.
- En el `.ioc`: el consumidor tiene que figurar en `Mcu.IPx` / `Mcu.IPNb`. **No alcanza con que su
  `<Perif>Freq_Value` valga 32768** — eso es el estado del árbol de clocks, no evidencia de que haya
  un consumidor activado.
- En el MspInit del periférico: el mux en LSE (`RCC_RTCCLKSOURCE_LSE`, `RCC_LPTIM1CLKSOURCE_LSE`).

Estado que quedó tras hacerlo bien, como referencia:

```
.ioc                  Mcu.IP3=RTC
SystemClock_Config()  RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI, LSEState = RCC_LSE_ON
                      __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW)
HAL_RTC_MspInit()     RTCClockSelection = RCC_RTCCLKSOURCE_LSE
```

Por esta misma trampa `port_lptim_tick.c` del prototipo `FWDLGZ` termina haciendo a mano
`HAL_PWR_EnableBkUpAccess()` + `__HAL_RCC_LSE_CONFIG(RCC_LSE_ON)` + `__HAL_RCC_LPTIM1_CONFIG(...LSE)`.
**Es un parche sobre una configuración incompleta, no el camino recomendado**: activando el
periférico en CubeMX no hace falta nada de eso.

Y no olvidar **RCC → Parameter Settings → LSE Drive Capability**: elegirlo a propósito (consumo vs.
margen de arranque), no dejar el default. Acá quedó en `LOW` — ver la advertencia del estado actual.

### Notas de bring-up del LSE

El cristal de 32.768 kHz es la base de todo el bajo consumo (tick por LPTIM1 en modo Stop, RTC,
calendario). **Ya está validado** (`v0.0.3`), pero estas notas siguen valiendo para diagnosticar si
alguna vez deja de arrancar:

- **Hardware montado:** cristal de 32.768 kHz entre los pines 3 y 4 del LQFP64 —`PC14-OSC32_IN` y
  `PC15-OSC32_OUT`— con un condensador de **10 pF a GND en cada pin**.
- **Si el LSE no arranca, el síntoma son 2 destellos** en el LED (`Error_Handler()`), no un cuelgue
  mudo. Sospechar, en orden: el cristal, los condensadores de carga, y el `LSE Drive Capability`
  —que está en `LOW`, el de menor margen.
- **Dominio de backup:** el LSE vive detrás del bit `DBP` de `PWR->CR1`. Si se usa
  `HAL_RCC_OscConfig()` (la vía que genera CubeMX), la HAL lo destraba sola. Sólo hace falta
  `HAL_PWR_EnableBkUpAccess()` explícito si se toca el LSE **por registros**, como hace
  `port_lptim_tick.c`.
- El arranque de un cristal de 32 kHz es lento (cientos de ms). La HAL espera `LSERDY` con timeout,
  así que una falla cae en `Error_Handler()` en lugar de colgarse.
- **La carga tiene que corresponder al `CL` del cristal:** `CL_efectiva = (C1·C2)/(C1+C2) + C_parásita`.
  Con 10 pF y 10 pF da ≈ **8 pF**, correcto para un cristal de `CL` 7-9 pF pero **demasiado poco para
  uno de 12,5 pF**, que es el valor más común. Cargar de menos hace oscilar **rápido**. Ver el
  pendiente de hardware en el estado actual.
- Para medirlo sin cargar el cristal con una punta: sacarlo por **MCO** o por **LSCO**, no pinchando
  PC14/PC15.

## Metodología: bring-up incremental

**La placa se puebla de a un periférico, y el firmware acompaña ese avance.** Cada etapa se valida en
banco antes de pasar a la siguiente. El objetivo es que cuando algo falle, el sospechoso sea el
último bloque agregado y no un sistema entero sin estrenar.

Consecuencias para el trabajo diario:

- **No escribir código para hardware que no está montado.** Si el periférico no está soldado, no se
  puede validar, y código sin validar acumula deuda que después aparece toda junta.
- **Cada etapa termina en un punto conocido-bueno.** Hay que poder decir "acá andaba" y volver.
  Sin eso, un bring-up incremental pierde su principal ventaja (ver la nota de control de versiones
  más abajo).
- **Una variable por vez.** Si se agrega hardware nuevo *y* se refactoriza el firmware en el mismo
  paso, se pierde la capacidad de aislar la causa.
- **Primero la infraestructura de diagnóstico.** La consola TERM (USART1, PB6/PB7) conviene tenerla
  temprano: una vez que hay `printf` y comandos, todo lo que venga después se depura interactivamente
  en lugar de a ciegas con el LED y el debugger.

Orden seguido, con cada etapa validada en banco y etiquetada en git:

1. ✅ **Clock + LED, bare-metal** (`v0.0.1`) — MSI → PLL → 60 MHz, LED en PB9.
2. ✅ **FreeRTOS desde CubeMX** (`v0.0.2`) — API nativa en la aplicación, tick del kernel en el
   SysTick y timebase de la HAL en TIM6. Trampas resueltas más abajo.
3. ✅ **LSE + RTC** (`v0.0.3`) — el cristal arranca y alimenta al RTC. Se adelantó respecto del plan
   original, que lo tenía para el final: el `Error_Handler()` con destellos se repuso antes, para no
   quedar a ciegas si el cristal no arrancaba.
4. **Tick por LPTIM1** — se adelantó respecto del plan original (venía después de TERM), porque el
   LSE ya estaba resuelto y era el único requisito. Partido en dos para no mover dos variables juntas:
   - **4a** ✅ **validado en banco** (`v0.0.4`) — el tick del kernel deja el SysTick y pasa al LPTIM1
     alimentado por el LSE, **sin** tickless (`configUSE_TICKLESS_IDLE = 0`). Código en
     `Application/FRTOS/port_lptim_tick.c`. El LED de `tkCtl` siguió destellando a su ritmo, que era el
     criterio de aceptación: el tick sale del cristal y los tiempos no cambiaron.
   - **4b** ✅ **validado en banco** (`v0.0.5`) — tickless de verdad: `configUSE_TICKLESS_IDLE = 2` y
     `vPortSuppressTicksAndSleep()` con Stop 2. Ver la sección de bajo consumo más abajo.
5. ✅ **TERM / USART1** (`v0.0.6`) — consola, `printf` y `TERM_SENSE`. Es el que más cambió el modo
   de trabajo: a partir de acá se depura interactivamente en vez de contar destellos. Comandos hoy:
   `help`, `status`, `sense`, `reset`, `reboot`. Lo que costó el día **no fue firmware** —un cable de
   serie malo y la frecuencia SWD en Auto—; ambos quedaron documentados más arriba.
6. ✅ **I2C** (`v0.0.7`) — bus I2C2 por interrupción, EEPROM **M24M01** (128 KB) y RTC externo
   **MCP79410**. Los tres validados con datos reales, no sólo con ACKs: la EEPROM con escrituras que
   cruzan bordes de página y de bloque (`ee test`), y el RTC conservando la hora tras un minuto sin
   alimentación. Falta el **monitor de corriente INA**, que todavía no está poblado.
7. RS485/Modbus → **microSD/SPI** → entradas analógicas → modem LTE. La microSD ya tiene relevamiento
   y decisiones pendientes anotadas: ver *microSD + FatFs: diseño pendiente*.
8. ✅ **Bajo consumo** (`v0.0.6`) — cerrado por los dos lados: el firmware con el tickless y el
   hardware con la fuente, que bajó de 210 a 60 µA de quiescent. Ver abajo.
9. **Validación en Release** — obligatoria antes de campo. Ver abajo.

**Pendientes conocidos, todos anotados y ninguno bloqueante:** el comando `reset` cuelga la placa
(`reboot` anda, así que no es la reinicialización del firmware); el parser de comandos matchea por
**prefijo**, así que un carácter de ruido puede ejecutar un comando destructivo; y `BOR_LEV` sigue en
el default más bajo (~1,7 V), que para un equipo a batería conviene decidir a propósito.

### ✅ Quién es el dueño de la hora: el MCP79410 (decidido el 2026-08-12)

**El dato que lo decidió, medido en banco:** se fijó la hora en el MCP79410, se dejó la placa
**un minuto entero sin alimentación** y al volver **el reloj había seguido corriendo**. Un minuto no
lo aguanta ningún capacitor: la pila de respaldo está poblada y funciona. El `PWRFAIL` del chip
además registró el corte con marca de tiempo de caída y de retorno.

El RTC interno no puede hacer eso: **`VBAT` no está poblado en R001**, así que pierde la hora en cada
corte. Por eso el esquema es:

| | Rol |
|---|---|
| **MCP79410** | **Autoritativo.** Sobrevive al corte de alimentación. Es de donde sale la hora de verdad. |
| **RTC interno del STM32** | **Copia de trabajo.** Se carga desde el externo al arrancar; los timestamps salen de registros, sin pagar una transacción I2C cada vez. Se resincroniza cada tanto para corregir deriva. |

#### ⚠ `PWRFAIL` es un latch, no un registro rodante

Encontrado por Pablo el 2026-08-12, cortando la alimentación dos veces seguidas: la segunda vez las
marcas de tiempo **seguían mostrando el primer corte**. No es un bug del driver, es el chip: el
MCP79410 graba el primer corte y se congela, y **mientras `PWRFAIL` siga en 1 los cortes posteriores
no pisan las marcas**. Recién vuelve a grabar cuando alguien baja el bit.

Consecuencia para la aplicación, y hay que tenerla en cuenta cuando exista el registro de eventos:
**al arrancar hay que leer las marcas, guardarlas, y recién ahí limpiar `PWRFAIL`.** Los dos errores
posibles duelen: si no se limpia nunca, en campo se ve para siempre el primer corte de la vida del
equipo y ninguno de los que importan; si se limpia sin leer antes, se tira la evidencia.
`drv_rtc_init()` **no limpia a propósito**.

⏳ **Falta implementar la sincronización**, y con ella se arregla lo que quedó pendiente: el bloque
`Check_RTC_BKUP` que genera CubeMX está **vacío**, así que `MX_RTC_Init()` reinicializa la hora en
cada arranque. Al haber una fuente autoritativa afuera, eso deja de importar: se pisa con lo que
diga el MCP79410.

⚠ **NO desactivar el RTC interno en CubeMX.** No cuesta nada —ya corre del LSE, que está encendido
igual— y es el consumidor que destrabó el encendido del LSE en `v0.0.3` (ver la sección de la trampa
de CubeMX). Hoy `LPTIM1` también lo consume, así que en teoría el LSE sobreviviría, pero no vale la
pena volver a poner un pie en ese pozo para ahorrar cero.

### Compilar en Release: por qué todavía no, y cuándo sí

Todo el bring-up se hace en **Debug** (`-O0 -g3 -DDEBUG`), a propósito: con `-O0` el debugger dice la
verdad —el paso a paso sigue las líneas y las variables se pueden mirar—, mientras que con `-Os` GCC
inlinea, reordena y elimina variables, y la mitad aparece como *optimized out*. En una etapa cuyo
método es validar de a un periférico, eso cuesta más de lo que ahorra. Y el espacio no aprieta:
**47 KB de 1024 KB de flash**.

**Pero es una decisión con fecha de vencimiento**, por tres razones:

1. **Energía.** Con `-O0` cada variable pasa por memoria, sin asignación a registros: el código corre
   2 a 3 veces más lento. La corriente mientras está despierto es la misma, pero **la duración de cada
   ventana despierta se triplica**. Hoy es poco en absoluto porque el micro duerme el 98 % del tiempo,
   pero crece con cada bloque que haga trabajo real (poleo Modbus, escrituras a la SD, sesiones del
   modem).
2. **Stack.** `-O0` usa bastante más stack, así que el cambio va en la dirección segura — pero **los
   *high water mark* medidos en Debug no son los que van a valer en Release.** No ajustar los tamaños
   de stack al límite con los números equivocados.
3. **Los bugs que sólo existen optimizados.** Un `volatile` faltante, una barrera ausente, una carrera
   entre ISR y tarea: con `-O0` andan, con `-Os` se rompen, porque recién ahí el compilador se toma
   las libertades que el estándar le permite. **Hay que encontrarlos en el banco, no en el campo.**
   Lo escrito hasta ahora lo contempla (`bTerminalPresente` y `ulCandados` son `volatile`, el camino
   del tickless tiene `__DSB()`/`__ISB()`, el contador de `error_delay_ms()` es `volatile`), pero
   **el código que se porte de FWDLGX hay que revisarlo con este criterio**: viene de un compilador
   AVR más conservador.

**Práctica recomendada:** compilar en Release **cada tanto durante el desarrollo**, aunque no se
flashee. Que compile y linkee ya descarta bastante, y cuando aparezca un bug que sólo existe con
`-Os` va a estar cerca del cambio que lo causó, en vez de aparecer seis módulos después.

**Antes de campo:** un Release validado en banco, con las mediciones de **consumo** y de **stack
high water mark** rehechas sobre ese binario.

Dato útil: **`configASSERT` NO depende de `-DDEBUG`** (`FreeRTOSConfig.h:158`), así que en Release se
conservan las verificaciones de FreeRTOS, incluidas las de prioridad de interrupciones. Y
`USE_FULL_ASSERT` está apagado en ambas configuraciones, con lo cual el `assert_param()` de la HAL no
hace nada en ninguna de las dos: ahí no hay diferencia entre Debug y Release.

### Bajo consumo: estado ✅

**Cerrado en `v0.0.6`.** El tickless anda, la fuente se arregló, y los dos números que quedaron son
los que corresponden.

| Medición (placa entera, riel de 3,28 V) | Corriente |
|---|---|
| Antes del tickless (`v0.0.4`) | 3,2 mA |
| **Dormido, terminal desconectada** | **65 µA** |
| La fuente sola, en vacío | 60 µA |
| **Despierto, terminal conectada** | **3,5 mA** (+1,5 mA del conector y el adaptador) |
| Con tickless, **ST-LINK conectado al USB** | +145 µA |

- **El micro dormido consume ~5 µA**, la resta de las dos primeras filas. El número se midió **dos
  veces con pisos distintos** —contra los 210 µA de quiescent de la fuente vieja y contra los 60 de
  la nueva— y dio lo mismo las dos veces, así que ya no es "por debajo del piso del instrumento"
  sino una medición. Coincide con lo esperable de Stop 2 con LSE + RTC + LPTIM1 vivos, más el LED al
  2 % de duty.
- **Los 3,5 mA con la terminal enchufada NO son una falla: son el diseño.** `TERM_SENSE` toma
  `pwrLOCK_TERM` y el port baja de Stop 2 a **Sleep**, que deja el PLL a 60 MHz para que la USART
  pueda recibir. Es el precio de tener consola, y sólo se paga mientras alguien está mirando. Si
  alguna vez importa —una terminal olvidada enchufada en campo— la palanca es bajar el reloj
  mientras dure la sesión, no sacar el candado.
- **El ST-LINK aporta ~145 µA.** Las mediciones de bajo consumo se hacen con el dongle
  **desenchufado del USB** — no alcanza con desconectar el cable a la placa.
- Lo que queda por delante no es optimizar esto: es que cada periférico nuevo (modem, microSD, ADC)
  entre con su candado y su corte de alimentación, sin arruinar estos 5 µA.

#### ⚠ Con el tickless andando, el SWD se pone difícil

El micro pasa **más del 98 % del tiempo en Stop 2, donde el SWD está muerto.** Un intento de conectar
en `Hot plug` va a fallar con "no target found" **aunque el firmware esté perfecto**. No es la placa.

Hay que tomar el micro en el reset: en CubeProgrammer, *Reset mode* → **`Hardware reset`** o
**`Core reset`** (no `Hot plug`); por CLI, `mode=UR`. La launch config de CubeIDE ya usa
`connect_under_reset`, así que desde el IDE anda sola. Ver también la sección del programador: si
además queda NRST trabado, el síntoma se parece pero la causa es otra.

#### FreeRTOS desde CubeMX: lo que ya se aprendió (paso 2)

Se hizo funcionar el 2026-08-10 antes de rehacer el proyecto. Al repetirlo, esto ya está resuelto:

- **Hay que mover el timebase de la HAL a otro timer.** FreeRTOS se queda con el SysTick y la HAL lo
  necesita para `HAL_IncTick()`. En *SYS → Timebase Source* se elige **TIM6**; CubeMX genera
  `Core/Src/stm32l4xx_hal_timebase_tim.c` y saca el `SysTick_Handler()` de `stm32l4xx_it.c`. **Sin
  este paso el primer `HAL_Delay()` cuelga.**
- **El port correcto es `GCC/ARM_CM4F`** (este micro tiene FPU y se compila `-mfloat-abi=hard`).
  CubeMX lo elige solo, pero conviene confirmarlo: era *la* diferencia grande contra el SAM4L.
- La configuración que anduvo: interface **CMSIS-RTOS v2**, **heap_4 de 20000 bytes**, tick 1000 Hz,
  asignación estática y dinámica, `configUSE_NEWLIB_REENTRANT=1`. El binario pasó de ~6 KB a ~20 KB
  de `text` y de 1,5 KB a ~25 KB de `bss` (casi todo el heap).
- Nota: la línea AVR/SAM4L y el prototipo `FWDLGZ` usan la **API nativa** de FreeRTOS
  (`xTaskCreate`, `xQueueSend`). Elegir CMSIS-RTOS v2 agrega una capa de traducción para leer ese
  código heredado.
- **La capa CMSIS no es puramente decorativa: `cmsis_os2.c` aporta dos cosas que el proyecto
  necesita**, y si algún día se saca hay que reponerlas a mano:
  `vApplicationGetIdleTaskMemory()` / `vApplicationGetTimerTaskMemory()` —obligatorias porque
  `configSUPPORT_STATIC_ALLOCATION=1`— y el propio **`SysTick_Handler()`** (el `FreeRTOSConfig.h`
  mapea `SVC_Handler` y `PendSV_Handler`, pero **no** `SysTick_Handler`). El prototipo `FWDLGZ` tiene
  resueltos los callbacks de memoria en `Application/bsp/bsp.c`.

#### ⚠ `configTICK_RATE_HZ = 512`: `portTICK_PERIOD_MS` está ENVENENADO

Decidido el 2026-08-10 al encarar el tickless. **El tick pasa de 1000 a 512 Hz** para que salga
exacto del cristal de 32.768 kHz.

**Por qué 512 y no 1000.** 1000 no divide a 32768, así que ningún número entero de cuentas da 1 ms:
con 33 cuentas el tick real es 992,97 Hz y con 32 es 1024 Hz. Ese error no es jitter, es **sesgo
permanente que se acumula**: con 33 cuentas, +0,71 %, o sea **~10 minutos de deriva por día** (es el
caso del prototipo `FWDLGZ`, cuyo tick real quedó en ~993 Hz). Con 512 el tick sale **exacto**: LSE
32768 / prescaler 32 = 1024 Hz de contador, y 2 cuentas por tick.

**Por qué 512 y no 1024**, que era la primera elección: **CubeMX no admite `TICK_RATE_HZ` mayor a
1000** — si se tipea 1024, al guardar lo revierte a 1000 sin avisar. Se podía forzar con un
`#undef`/`#define` en el bloque `USER CODE BEGIN Defines` de `FreeRTOSConfig.h` (que está después de
la definición generada y sobrevive la regeneración), pero eso deja el `.ioc` diciendo 1000 y el
binario corriendo a 1024 — exactamente el tipo de desincronización entre fuente de verdad y realidad
que ya costó un día con el LSE. Se descartó.

**El costo de 512 es la resolución del tick: 1,95 ms.** No afecta a nada de este firmware: los
períodos son de segundos a minutos. Lo que necesita más precisión —el t3.5 de Modbus RTU, 1,75 ms—
**no se puede temporizar con el tick a ninguna frecuencia** (tampoco a 1000 Hz) y va por el registro
`RTOR` del USART o por un timer dedicado.

**La trampa, y por qué hay veneno.** El port calcula:

```c
/* portmacro.h:74 */
#define portTICK_PERIOD_MS  ( ( TickType_t ) 1000 / configTICK_RATE_HZ )
```

Con 512, `1000 / 512` en aritmética entera es **1**, no 1,95. El patrón clásico del código heredado
de la línea AVR/FWDLGX —`vTaskDelay( 500 / portTICK_PERIOD_MS )`— **compila perfecto y espera el
doble**: 500 ticks = 976 ms. Silencioso. (Con 1024 hubiera dado 0 y el compilador lo cazaba por
división por cero; con 512 esa red de seguridad no existe.)

Por eso `main.h`, en `USER CODE BEGIN EM`, lo redefine a un identificador inexistente:

```c
#undef  portTICK_PERIOD_MS
#define portTICK_PERIOD_MS  USAR_pdMS_TO_TICKS_NO_portTICK_PERIOD_MS
```

Cualquier uso falla en compilación diciendo qué hacer. **No es un error del archivo: es a propósito.**

**Regla: siempre `pdMS_TO_TICKS()`.** Es exacto para milisegundos **múltiplo de 125** (porque
`512/1000 = 64/125`): 125, 250, 500, 1000, 2000, 5000 dan resultado exacto. Los que no lo son se
truncan hacia abajo — la espera sale **un poco más corta, nunca más larga**, con error acotado a
menos de un tick (~1,95 ms) y **sin acumularse**. Para lo que sea crítico, conviene **pensar los
períodos en ticks** y dejar `pdMS_TO_TICKS()` para lo que tolera el redondeo.

### Control de versiones — resuelto, y por qué se hizo

`FWDLGARM_R1` **ya es un repo git**, con un tag por cada etapa validada en banco (`v0.0.1` … `v0.0.4`).
**Etiquetar cada etapa validada no es opcional acá**: es lo que le da sentido al bring-up incremental,
poder decir "hasta acá andaba" y volver.

El motivo es concreto. El 2026-08-10 el proyecto se borró entero para rehacerlo desde cero **sin
respaldo**: se perdieron el `Error_Handler()` con patrón de destellos, las funciones auxiliares
`led_config()` / `error_delay_ms()` y la etapa con FreeRTOS ya validada. Nada era recuperable y hubo
que rehacerlo. De ahí el `.gitignore` heredado de `FWDLGZ` (ignora `Debug/`, `Release/`,
`.metadata/`, `*.launch`, `*.ioc.bak`).

## Build & flash

El proyecto es **STM32CubeIDE 2.2.0** (`/opt/st/stm32cubeide_2.2.0/stm32cubeide`), con el workspace
Eclipse en `Firmware/` (`Firmware/.metadata/`); ambos proyectos STM32 están importados ahí.

El build normal es desde el IDE. `Debug/` contiene los makefiles **generados** por CDT
(toolchain *GNU Tools for STM32 14.3.rel1*), así que `make` dentro de `Debug/` reproduce el mismo
build — pero:

- **Hay que invocar `make all`, no `make` a secas.** Los `subdir.mk` que el makefile incluye definen
  el target `clean` **antes** de que aparezca `all:` (línea 61), así que el default goal de make
  termina siendo `clean`: un `make` pelado **borra el build en vez de compilarlo**. CubeIDE siempre
  invoca `make all`; a mano hay que acordarse.
- **Hay que usar el `arm-none-eabi-gcc` de CubeIDE, no el de `/usr/bin`.** Los `subdir.mk` pasan
  `-fcyclomatic-complexity`, que es una extensión del GCC de ST y el del sistema rechaza con
  `unrecognized command-line option`. La toolchain está en
  `/opt/st/stm32cubeide_2.2.0/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.linux64_*/tools/bin`
  — ponerla al frente del `PATH`.
- **`Debug/` es generado, no se edita a mano**: CubeIDE lo regenera y pisa los cambios. Para agregar
  fuentes o includes hay que hacerlo en las propiedades del proyecto (`.cproject`), no en los `.mk`.
- **Una fuente nueva no entra al build sola.** Las listas de archivos viven en los `subdir.mk` **y**
  en `Debug/objects.list` (que es lo que consume el link, vía `@objects.list`). Un `.c` nuevo en
  `Core/Src/` —o un driver de la HAL que CubeMX acaba de copiar a `Drivers/`— no se compila hasta que
  el IDE regenere esos archivos: **Project → Refresh (F5) y después Build**. El síntoma de olvidarlo
  es un `undefined reference` en el link a funciones que sí existen en el árbol.
- **`Debug/makefile` no es relocalizable**: referencia el linker script por **ruta absoluta**
  (`/home/pablo/Spymovil/.../FWDLGARM_R1/STM32L496RGTX_FLASH.ld`). Mover el árbol rompe el build
  hasta regenerarlo desde el IDE.

### Flasheo — lo hace Pablo

**Flashea Pablo, no Claude**, desde la GUI de **STM32CubeIDE** o de **STM32CubeProgrammer**. No
lanzar comandos de programación por cuenta propia; si hace falta un binario nuevo, compilarlo y
avisar. Con ese flujo **el firmware arranca solo al terminar el download**, sin ciclo de
alimentación.

El archivo a cargar en CubeProgrammer es el **`.elf`** (`Debug/FWDLGARM_R1.elf`): lleva las
direcciones adentro, así que no hay que tipear `0x08000000` como sí haría falta con un `.bin`. El
proyecto no genera `.hex` (se activaría en *Properties → C/C++ Build → Settings → MCU/MPU Post build
outputs*), pero no aporta nada sobre el `.elf`.

**`openocd` está instalado en el sistema pero NO debe usarse** — ver la advertencia sobre NRST más
arriba. La launch config `FWDLGARM_R1 Debug.launch` usa el ST-LINK GDB server de CubeIDE
(`reset_strategy = connect_under_reset`, gdbserver en 61234).

Para **diagnóstico** —no para flashear— sirve la CLI de CubeProgrammer, que viene dentro de CubeIDE:

```bash
P=/opt/st/stm32cubeide_2.2.0/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_*/tools/bin
$P/STM32_Programmer_CLI -c port=SWD freq=950 mode=HOTPLUG        # Voltage, device ID, firmware del ST-LINK
$P/STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -ob displ       # option bytes: RDP, WRP, PCROP
$P/STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 0x40021094 1   # RCC_CSR: qué tipo de reset hubo
```

`mode=HOTPLUG` conecta sin resetear. Ojo: **una lectura por SWD puede frenar el firmware que está
corriendo** — si el LED deja de parpadear después de un sondeo, es eso y no una falla.

No hay suite de tests: es firmware bare-metal, se valida en banco.

### Flags y mapa de memoria

```
-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard   # ← FPU por hardware
-std=gnu11 -O0 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L496xx
--specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -lc -lm
```

**El FPU es la diferencia grande contra el SAM4L** (que era soft-float): el port de FreeRTOS que
corresponde acá es **ARM_CM4F**, no ARM_CM3.

| Región | Origen | Tamaño |
|---|---|---|
| FLASH | `0x08000000` | 1024 KB |
| RAM   | `0x20000000` | 256 KB |
| RAM2  | `0x10000000` | 64 KB |

`_Min_Heap_Size = 0x200`, `_Min_Stack_Size = 0x400` (`STM32L496RGTX_FLASH.ld`).

### Regla de oro con CubeMX

Flujo de trabajo: la **configuración** (pines, clocks, periféricos) se hace en **STM32CubeMX** a
través del `.ioc`; el **desarrollo** es en **STM32CubeIDE, en C**.

`FWDLGARM_R1.ioc` es la **fuente de verdad** de `Core/Src/*.c`, `Core/Inc/*.h` y `Core/Startup/`.
Regenerar desde CubeMX **pisa todo lo que esté fuera** de los bloques
`/* USER CODE BEGIN X */ … /* USER CODE END X */`. Código propio: dentro de esos bloques, o —mejor—
en un árbol aparte (p. ej. `Application/`, como hizo el prototipo `FWDLGZ`) agregado al proyecto.
Cambiar un pin o un periférico se hace **en el `.ioc`**, no editando el `.c` generado.

## Pinout: los pines los dicta Pablo

> **⚠ Regla vigente (2026-08-10): no deducir pines del CSV.** Lo único montado y usable hoy es el
> **LED**. A medida que se vayan poblando periféricos, **Pablo indica explícitamente en qué pines
> está conectado cada uno**. Al configurar un periférico nuevo en el `.ioc`, preguntar los pines si
> no los dio; no tomarlos de la tabla de abajo ni proponerlos por cuenta propia.

La tabla siguiente es el **diseño completo de R001**, útil como referencia general, pero **no es
confirmación de que algo esté montado ni cableado**. `Hardware/interfases_pines.csv` es su origen;
`Firmware/FWDLGZ/PINOUT.md` es una **propuesta obsoleta** del prototipo y **contradice** a la placa
— no usarlo.

| Función | Pines (R001) |
|---|---|
| SWD | PA13 SWDIO, PA14 SWCLK |
| TERM (consola) | PB6 TX, PB7 RX → **USART1** |
| LTE (modem) | PA0 TX, PA1 RX → **UART4** |
| RS485 (modbus) | PB10 TX, PB11 RX → **USART3** |
| I2C | PB13 SCL, PB14 SDA → **I2C2** (poblado; pull-up de 10 kΩ) |
| SPI (microSD) | PA15 NSS, PC10 SCK, PC11 MISO, PC12 MOSI |
| Contador de pulsos CNT0 | PA12 (EXTI) |
| Analógicas | PC5, PB0 |
| LED, LED2 | PB9, PA2 (en el `.ioc`; no figuran en el CSV) |

Puntos a resolver contra el esquemático `Hardware/R001/` y el datasheet **DS11585**
(`Datasheets/STMicroelectronics/stm32l496ae.pdf`) antes de configurar el `.ioc`:

- **PB13/PB14 son `I2C2`**, no `I2C1`; **PC10–PC12 + PA15 son `SPI3`**, no `SPI2`. El CSV nombra la
  interfaz genéricamente; la instancia del periférico queda determinada por el pin. (Derivado del
  mapa de AF — confirmar en el datasheet.)
- El CSV lista **`PA7 = NRST`**. En LQFP64 el reset es un pin dedicado; verificar qué señal es
  realmente PA7 en el esquemático.
- **`PA12 = "EXTINT0"`** es el nombre lógico del contador 0; la línea EXTI física es **EXTI12**.
- PB0 y PC5 como entradas de ADC1: confirmar los canales exactos y la referencia.

## Arquitectura objetivo

Datalogger clásico por capas sobre FreeRTOS. **Principio HAL: sólo la capa de drivers toca el
hardware; la aplicación es agnóstica** (misma separación kernel/port que FreeRTOS). Si aparece
acceso a registros o a pines en una capa superior, se empuja hacia abajo, al driver.

Bloques funcionales que el firmware debe terminar teniendo (heredados de la línea AVR/SAM4L):
consola de comandos por TERM, modem LTE, RS485/Modbus, contadores de pulsos, entradas analógicas
(4-20 mA), RTC, microSD/FatFs, configuración persistente en flash y gestión de energía.

### El árbol `Application/`

El código propio vive **fuera de `Core/`**, para que una regeneración de CubeMX no lo toque nunca:

```
Application/
├── pwr/        pwr_lock.{h,c}          candados de energía (ver abajo)
├── drivers/    drv_uart.{h,c}          UART sobre la HAL, tabla de instancias
│               drv_term_sense.{h,c}    TERM_SENSE, poleado por tkCtl (no EXTI)
│               drv_i2c.{h,c}           bus I2C2 por interrupción + candado de bus
│               drv_eeprom.{h,c}        M24M01, dirección plana de 17 bits
│               drv_rtc79410.{h,c}      RTC externo MCP79410 (BCD, pila, PWRFAIL)
├── FRTOS/      port_lptim_tick.c       overrides del port: tick por LPTIM1 + tickless
├── FRTOS-IO/   frtos-io.{h,c}          fd table + frtos_open/read/write/ioctl + xprintf
│               frtos_cmd.{h,c}         ciclo de comandos
└── tasks/      tkCtl.{h,c}             control: destello del LED + poleo de TERM_SENSE
                tkCmd.{h,c}             consola
```

**Convención de tareas:** cada una vive en `Application/tasks/` con su propio header, que declara su
prioridad, el tamaño de stack y su memoria estática (`extern`); las definiciones van en el `.c`.
`main.h` y `main.c` no llevan nada de eso. Los archivos NO se prefijan con el nombre del proyecto:
`tkCtl.c`, no `FWDLGARM_R1_tkCtl.c`.

**`Application/` no se compila sola**: el `.cproject` lista `Core`, `Middlewares` y `Drivers` como
*source path*. Al clonar o rehacer el proyecto hay que agregarla en *Project → Properties → C/C++
General → Paths and Symbols → **Source Location** → Add Folder*, y sus cinco subdirectorios en la
pestaña **Includes** (con el combo **Configuration** en `[ All configurations ]`, o anda en Debug y
falla en Release meses después).

Son **dos pasos independientes** y fallan distinto: sin los *Includes* no compila
(`No such file or directory`); sin el *Source Location* **compila pero no linkea**, con
`undefined reference` a funciones que sí están en el árbol. Ese segundo síntoma es el confuso.

> ⚠ **Si los archivos se crearon desde afuera del IDE —por consola, git checkout, o Claude— hay que
> hacer *Refresh* (F5) sobre el proyecto ANTES de agregar el source folder.** Eclipse mantiene su
> propio índice de recursos y no ve lo que aparece en el disco por detrás: en el diálogo *Add Folder…*
> la carpeta directamente no figura, y el paso parece haberse hecho cuando en realidad no quedó nada.
> Ya pasó una vez y costó un rato entenderlo.

**FRTOS-IO** es la misma API de FWDLGX —`frtos_open/read/write/ioctl` despachando por
`file_descriptor_t`, mismos códigos de `ioctl_*`, mismo `int16_t` con `-1` en error— pero el despacho
es por **tabla con vtable** en vez de un `switch` con una función por instancia
(`frtos_write_uart0..4`). En el AVR las cinco copias eran inevitables porque los registros de cada
USART eran constantes de compilación; acá cada UART es un `UART_HandleTypeDef`, así que alcanza una
fila por instancia. Los fd cuyo hardware no está poblado (`fdWAN`, `fdRS485A`, `fdI2C`, `fdNVM`)
figuran en la tabla con `NULL`: compilan y devuelven `-1`.

**Candados de energía (`pwr_lock.h`).** Los drivers avisan "mientras trabajo, no entres en Stop".
Es un bitmask, no un contador, así que tomar dos veces el mismo candado es idempotente. Mientras haya
uno tomado, `vPortSuppressTicksAndSleep()` baja de **Stop 2 a Sleep**: el micro se sigue durmiendo
entre interrupciones pero los relojes quedan vivos. Sin esto, entrar en Stop a mitad de una
transmisión la corta. Lo usan hoy TERM_SENSE y el TX de la consola; van a necesitarlo el modem, la
microSD y el ADC. Medido en `v0.0.6`: con `pwrLOCK_TERM` tomado la placa consume **3,5 mA**, contra
**65 µA** sin candados. El mecanismo funciona en las dos direcciones.

### ✅ TERM_SENSE se polea, no va por EXTI (decidido el 2026-08-12)

La primera versión ponía PB5 en EXTI por ambos flancos, razonando que polear despertaría al micro.
**El razonamiento era falso y conviene entender por qué, porque el mismo error se puede repetir con
cualquier entrada digital que venga.** El porqué largo está en `Application/drivers/drv_term_sense.h`;
el resumen:

- **`tkCtl` ya despierta cada segundo** para destellar el LED. Leer un pin en esa vuelta no agrega ni
  una despertada. El poleo sale caro cuando **obliga** a despertar; acá no obliga a nada.
- **La EXTI sí costaba.** El conector rebota: se midieron **hasta 82 interrupciones por un solo
  enchufe**, y cada flanco despierta al micro de Stop 2 y le hace rehacer `SystemClock_Config()`.
- **Muestrear un nivel una vez por segundo es antirrebote perfecto y gratis.** Con EXTI habría que
  agregar un filtro.
- El datalogger trabaja **desatendido**; la terminal es la excepción. Detectarla en milisegundos era
  resolver un problema que no existe.

La regla general que queda: **EXTI para lo que tiene que despertar al micro, poleo desde `tkCtl` para
lo que sólo tiene que estar al día.** Y cuando entre el watchdog, decidir de una vez el período de
`tkCtl`, el kick y el poleo juntos — los tres comparten esa vuelta, así que subir sólo uno no ahorra
nada.

### ⚠ La EEPROM es una M24M01 de 128 KB, no una M24M02 de 256

Descubierto el **2026-08-12** con `i2c scan`, y contradice el comentario heredado de
`FWDLGX.X/SRC/ULIBS/eeprom.h`. La firma que lo delató: el chip contesta en las direcciones de 7 bits
**`50` y `51`**, y su *Identification Page* en **`58` y `59`** (mismo chip, código de dispositivo
`1011` en vez de `1010`). Una M24M02 **tendría** que contestar en `50..53`: los bits A17/A16 los
decodifica adentro y no son patas que se puedan atar. Dos bloques y no cuatro = 1 Mbit.

Consecuencias que ya están resueltas en `drv_eeprom.{h,c}`, pero que hay que tener presentes:

- **La dirección es de 17 bits y el bit A16 viaja en el byte de dispositivo** (`0xA0` los primeros
  64 KB, `0xA2` los segundos). El driver expone una dirección plana `0x00000..0x1FFFF` y esconde eso.
- **`eeprom.c` de FWDLGX usa `uint16_t` y habla siempre a `0xA0`**: el firmware del AVR usaba **sólo
  64 KB**, la mitad del chip. No era un error allá, pero **su direccionamiento no se puede portar
  tal cual**.
- ⚠ **No escribir nunca en `58`/`59`.** La página de identificación se bloquea en sólo-lectura de
  forma **permanente e irreversible**, y el bloqueo se dispara con una escritura. Leerla es gratis.

Y las dos trampas de escribir en cualquier EEPROM I2C, ambas mudas, ambas resueltas en el driver y
ejercitadas por el comando `ee test`:

1. **La escritura de página no desborda: DA LA VUELTA.** Pasarse del borde de 256 bytes no sigue en
   la página siguiente, pisa el principio de *esa misma* página. Sin error y sin aviso.
2. **Después de escribir, el chip queda sordo ~5 ms** y NACKea todo, incluso su propia dirección. Ese
   NACK **no es un error**: es *acknowledge polling*, hay que reintentar. Sin esto, escribir dos
   páginas seguidas falla siempre en la segunda.

### Portar FWDLGX a ARM: el checklist

**El criterio es adaptar, no envolver**: el código que entra tiene que quedar diciendo la verdad
sobre el micro en el que corre. Nada de macros de compatibilidad que simulen mecanismos del AVR.
Estas son las trampas que ya aparecieron, todas encontradas al portar TERM:

| Idioma AVR | Qué hacer en ARM | Por qué |
|---|---|---|
| `PSTR("…")`, `xprintf_P`, `pgm_read_byte` | **borrarlos** — `sed -i 's/xprintf_P/xprintf/g; s/PSTR(\("[^"]*"\))/\1/g'` | El AVR es Harvard y un literal iba a RAM salvo que se lo marcara. El Cortex-M4 tiene espacio unificado: los literales quedan en `.rodata`, en flash, y se leen con un `ldr` común. No hay nada que marcar ni dos variantes de `printf` posibles. |
| Buffers y structs **definidos en un `.h`** | declarar `extern` en el header, definir una vez en el `.c` | Patrón de una sola unidad de compilación. GCC 14 usa `-fno-common`: da *multiple definition* apenas el header entre en dos `.c`. |
| `strlcpy` / `strlcat` | implementar local | Son de BSD; newlib no las trae. |
| Poleo con `vTaskDelay(1)` en el lazo de lectura | bloquear en una primitiva del kernel (stream buffer, cola, semáforo) | Despertaría al micro **512 veces por segundo** y anula el tickless. Es el cambio más importante de todos. |
| TX por poleo esperando el registro | interrupción + semáforo, y **tomar un candado de energía** | Pollear una línea a 115200 son ~5,5 ms de CPU girando; y sin el candado, el Stop 2 corta la trama. |
| `vTaskDelay( ms / portTICK_PERIOD_MS )` | `pdMS_TO_TICKS( ms )` | `portTICK_PERIOD_MS` está **envenenado a propósito** en `main.h`; ver la sección del tick. |
| `cli()` / `sei()`, `<avr/io.h>`, `<avr/interrupt.h>` | `taskENTER_CRITICAL()` / HAL, y el acceso a registros **baja al driver** | Principio HAL: sólo la capa de drivers toca el hardware. |
| Acceso a registros desde un header de capa alta | empujarlo al driver | El `frtos-io.h` viejo tenía macros que escribían el USART del AVR. |

### microSD + FatFs: diseño pendiente (paso 6 del roadmap)

Relevado el **2026-08-11**, antes de escribir una línea. **No es código a escribir todavía**: la
microSD no está poblada, y la regla del bring-up incremental es no escribir contra hardware ausente.
Lo que sigue es lo que ya está averiguado y lo que **queda por decidir**, para que cuando toque el
paso 6 se llegue con las decisiones tomadas y no discutiéndolas con el soldador en la mano.

#### Lo que CubeMX da y lo que no

CubeMX ofrece FatFs en *Middleware and Software Packs*. Lo que trae `STM32Cube_FW_L4 V1.18.2` es
**FatFs de ChaN R0.12c** (`Middlewares/Third_Party/FatFs/src/ff.h:22` → rev `68300`) — casi con
seguridad el mismo proyecto que hay detrás de la librería externa que se usó en la rama de FWDLGX.

**Pero CubeMX da sólo la mitad de arriba.** Los `diskio` que ST provee en
`FatFs/src/drivers/` son para SDMMC (BSP v1/v2), SDRAM, SRAM, USB host y PPP; **no hay ninguno de SD
por SPI**, y los cuatro ejemplos de FatFs del paquete L4 son todos `FatFs_uSD` por SDMMC. Como en
R001 la microSD va por **SPI3**, en CubeMX hay que elegir el modo **"User-defined"**: genera
`FATFS/Target/user_diskio.c` con `USER_initialize/status/read/write/ioctl` **vacías**.

| Capa | Quién la pone |
|---|---|
| `f_open/f_read/f_write/f_lseek`, FAT, directorios | CubeMX, gratis |
| `diskio` — esqueleto y registro del driver | CubeMX genera el molde |
| **Driver de tarjeta SD sobre SPI**: CMD0, CMD8, ACMD41, CMD58, CMD17/24, tokens, CRC7/CRC16, espera de `busy` | **nuestro, entero** |

O sea: **no hay que rehacer el filesystem, hay que rehacer el driver de tarjeta** — justo lo que
aportaba la librería externa del AVR. Su lógica se porta casi tal cual, porque el protocolo SD-SPI
es idéntico byte a byte; lo que cambia es el transporte (registros del AVR → `HAL_SPI_*`, **no por
poleo** — ver el checklist de portación).

#### FreeRTOS: compatible, con tres asteriscos

- **`option/syscall.c` está escrito contra CMSIS-RTOS** (`osMutexNew`, `osMutexAcquire`). Compila
  —`cmsis_os2.c` está en el proyecto por los callbacks de memoria estática— pero quedaría siendo la
  única parte de la aplicación hablando CMSIS. Reescribir sus cinco funciones con
  `xSemaphoreCreateMutexStatic()` son ~40 líneas. **Recomendación: `_FS_REENTRANT = 0`** y que una
  sola tarea sea dueña de la SD: la serialización sale del diseño, no de un mutex.
- **`_FS_TIMEOUT = 1000` está en TICKS, no en ms.** Con el tick a 512 Hz son 1,95 s. Misma trampa
  que `portTICK_PERIOD_MS`.
- **`_USE_LFN = 3` (el default del template) llama a `pvPortMalloc`**, y un buffer LFN son ~600 bytes
  por operación contra un heap de **3000 bytes** (`FreeRTOSConfig.h:71`). Va a **`_USE_LFN = 0`**:
  nombres 8.3, que es lo que quiere un datalogger (`20260811.DAT` entra perfecto). De yapa, con LFN
  apagado no se compilan `ccsbcs.c` ni la tabla de code page.
- **RAM:** con `_FS_TINY = 0` un `FATFS` son ~560 bytes y un `FIL` ~550 (lleva el buffer de sector de
  512 adentro). **Eso no entra en el stack de una tarea** (tkCmd tiene 512 palabras = 2 KB): o van
  estáticos, o **`_FS_TINY = 1`**, que baja el `FIL` a ~40 bytes compartiendo el buffer del volumen.
  Para quien escribe un archivo por vez, `_FS_TINY = 1` es la elección correcta.

#### ⏳ Lo que hay que decidir — y por qué importa más que todo lo anterior

**1. ¿R001 corta la alimentación de la microSD?** Un load switch o un P-MOS por GPIO. Una microSD
consume **0,2 a 1 mA en idle** y **50 a 100 mA en pico de escritura**: contra los ~5 µA del micro
dormido son tres órdenes de magnitud, así que **la tarjeta domina el balance energético y anula el
tickless si queda alimentada**. Peor: una tarjeta "quieta" puede seguir haciendo garbage collection
interno cientos de ms después de un write. **Verificar contra el esquemático `Hardware/R001/`.**

Si el corte existe, arrastra tres consecuencias:

- Hay que **desmontar y remontar** en cada ciclo (`f_mount(NULL, …)` + re-init), porque al cortar la
  tarjeta pierde estado. El re-init —74 clocks, CMD0, ACMD41 hasta salir de idle— cuesta de decenas
  a cientos de ms.
- Por eso la política **no puede ser "escribir cada muestra"**: hay que acumular en RAM y volcar de a
  bloques. Cuántas muestras se toleran perder define el tamaño del buffer.
- Al cortar, los pines de SPI3 tienen que quedar **bajos o en Hi-Z**. Si quedan altos **alimentan la
  tarjeta por los diodos de protección** y el corte no corta nada. Clásico, y cuesta verlo con el
  tester.

**2. ¿La microSD es el almacenamiento primario o es exportación/respaldo?** FAT es frágil ante corte
de alimentación a mitad de un write: no se pierde el último registro, se puede perder **la FAT
entera**. Un equipo a batería con un modem LTE que hunde el riel en el pico de TX es exactamente el
caso malo.

- *Primaria* → conviene **log crudo circular por sectores**, con FAT sólo para exportar. Robusto ante
  corte, pero la tarjeta deja de ser legible directamente en una PC.
- *Exportación*, con el dato primario en la NVM → **FatFs directo**, `f_sync()` por bloque, y si se
  corrompe se reformatea sin drama.

Mirar cómo estaba resuelto en FWDLGX antes de elegir.

**3. Menores, pero conviene fijarlos:** `_VOLUMES = 1`; `_USE_MKFS` sólo si se quiere un comando
`format` en la consola (cuesta ~2 KB de flash); `_FS_NORTC = 0` y **enganchar `get_fattime()` al
RTC** para que los archivos tengan fecha de verdad.

#### Encaje con FRTOS-IO

**La SD NO entra en la tabla de `file_descriptor_t`.** FatFs ya *es* una API de archivos; meterla
detrás de `frtos_read/write/ioctl` sería envolver una abstracción en otra sin ganar nada. La
aplicación llama `f_open/f_write` directo, y el driver SD es dueño exclusivo del SPI3. `fdNVM` sí
encaja en la tabla, porque ahí sí hay un stream de bytes. El candado `pwrLOCK_SD` ya existe en
`pwr_lock.h` esperando a este driver.

### Qué vale la pena rescatar del prototipo `FWDLGZ` (sólo si Pablo lo pide)

Ya resuelto ahí, contra este mismo micro:

- **Tick de FreeRTOS por LPTIM1 desde el LSE** (`Application/FRTOS/src/port_lptim_tick.c`): sobrescribe
  la weak `vPortSetupTimerInterrupt()` del port CM4F, deja el SysTick para `HAL_IncTick()` y pone el
  tick del kernel en un timer que **sigue vivo en modo Stop** — base del tickless de bajo consumo.
  Ojo con la precisión: 32768/1000 no es entero (el tick real queda en ~993 Hz).
- `FreeRTOSConfig.h` ARM-izado: `configTICK_RATE_HZ=1000`, `configMAX_PRIORITIES=4`, heap_4 de 16 KB,
  asignación estática y dinámica, `SVC_Handler`/`PendSV_Handler` mapeados al port pero **`SysTick_Handler` no**.
- `Application/bsp/bsp.c`: arranque del scheduler y los callbacks de memoria estática
  (`vApplicationGetIdleTaskMemory` / `vApplicationGetTimerTaskMemory`) que exige
  `configSUPPORT_STATIC_ALLOCATION=1`.

Su mapa de pines, en cambio, **no sirve**: es previo a la placa R001.

## Layout del repositorio

- `Firmware/` — raíz del workspace de CubeIDE. Contiene los tres árboles de firmware
  (`FWDLGARM_R1/` ← el activo) y `FreeRTOSv202604.00-LTS.zip`.
- `Hardware/R001/` — esquemáticos y PCB Altium de la placa (`spq_arm_*_R001.SCHDOC`, `.PCBDOC`,
  `Schematic Prints.pdf`) + `interfases_pines.csv`, el mapa de pines vigente.
- `Datasheets/` — datasheets y app notes, incluido el del STM32L496 (DS11585).
- `Tools/` — herramientas del ciclo SAM4L anterior (DFP `.atpack`, instalador XC32). Sin uso hoy.
- `Promps/` — notas de sesiones previas.

## Toolchain

- `arm-none-eabi-gcc` del sistema (`/usr/bin`) y el que trae CubeIDE (GNU Tools for STM32 14.3.rel1).
- STM32CubeIDE 2.2.0 en `/opt/st/stm32cubeide_2.2.0/`.
- Firmware package: **STM32Cube FW_L4 V1.18.2**; CubeMX 6.18.0.
- `gh` **no** está instalado — usar git plano; para push, `GIT_TERMINAL_PROMPT=0 git push`.
