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

### Estado actual (2026-09-04)

> **⚠ Hay DOS placas, y hay que saber de cuál se habla.** La **placa nueva** —fabricada por Pablo,
> estrenada el 2026-09-02— lleva la **fuente del modem rediseñada** (TPS22810 → LMR33630 en cascada),
> y al **2026-09-04 está poblada entera**. Sobre ella se validó el firmware completo, y el riel de
> 3,8 V del modem quedó ajustado y andando. La **placa original** (la del TPS62130 muerto) sigue en
> el banco y sirve de instrumento de comparación — con dos placas, la sana separa "el firmware está
> mal" de "la placa está mal" en un minuto.
>
> ⚠ **Lo que no cierra en la placa nueva es el reposo: 354 µA contra 40 µA de la original**, con el
> mismo firmware. Pablo lo dejó afuera a propósito para seguir con el firmware; está anotado en la
> sección del modem con lo que ya se sabe para acotar la búsqueda.

**Lo que sigue describe el poblado de la placa ORIGINAL**, que fue el que marcó el orden del
bring-up. Hoy hay montados: la **fuente**, el **micro**, la
**interfaz de programación SWD** (PA13/PA14), **LED en PB9**, el **cristal de 32.768 kHz en
PC14/PC15** con sus condensadores de carga a GND, el **conector de la terminal** (PB6/PB7 más
`TERM_SENSE` en PB5), el **bus I2C2** (PB13/PB14, pull-up de 10 kΩ) con la **EEPROM M24M01**, el
**RTC MCP79410 con su pila** y el **INA3221** que mide los lazos de 4-20 mA, el **RS485** con su
transceiver **SP3485** y los tres rieles conmutados, la **fuente lineal de los sensores 4-20 mA**
(`EN_PWR_SENS420`, PB12), la **microSD** con su alimentación conmutada y la **medida de los rieles**
(los dos load switches con sus divisores y sus seguidores TLV8802) y el **contador de pulsos** (opto,
filtro RC y 74AUP1G17) y la **electroválvula TOYI** con su load switch (soldada, confirmado por Pablo
el 2026-08-18). **Falta poblar un solo módulo: el modem LTE** —que en la placa nueva ya tiene su
fuente andando; falta el módulo en sí, un **WH-LTE-7S1-E**—.

Esto define el alcance de lo que se puede validar en banco: **clock, LSE, LED, SWD, la consola, el
bus I2C, el RS485, la medida de 4-20 mA, la microSD, los rieles por ADC, el contador de pulsos y la
electroválvula**. El resto del mapa de pines de
`interfases_pines.csv` es el diseño completo de R001, no hardware presente — no tiene sentido
escribir drivers contra periféricos que todavía no están montados.

El proyecto **se borró y se rehízo de cero el 2026-08-10** (ver control de versiones), y desde
entonces avanzó en trece etapas, todas validadas en banco y etiquetadas en git:

| Tag | Estado |
|---|---|
| `v0.0.1` | Clock MSI a 60 MHz, LED parpadeando |
| `v0.0.2` | FreeRTOS con API nativa, tarea `tkCtl` |
| `v0.0.3` | **Cristal externo: LSE + RTC**, `Error_Handler()` con destellos |
| `v0.0.4` | **Tick del kernel por LPTIM1 desde el LSE**, 512 Hz exactos |
| `v0.0.5` | **Tickless con Stop 2** — de 3,2 mA a 0,215 mA de placa |
| `v0.0.6` | **Consola TERM (TX + RX) y `TERM_SENSE`** — el micro dormido queda en **~5 µA** |
| `v0.0.7` | **Bus I2C2, EEPROM M24M01 y RTC MCP79410** — los tres validados con datos reales |
| `v0.0.8` | **RS485 (SP3485) con DE por hardware** y los 3 rieles conmutados. De paso, el tickless dejó de comerse bytes |
| `v0.0.9` | **INA3221: medida de 4-20 mA** y el riel de la fuente lineal de sensores. Validado contra un calibrador de 0 a 20 mA |
| `v0.0.10` | **microSD por SPI3, etapa 1** (la tarjeta, sin FatFs): energía conmutada, sectores leídos y escritos, y el reposo intacto |
| `v0.0.11` | **Medida de los rieles por ADC1** — 12 V por divisor y 3,3 V por `VREFINT`. Validado contra el tester, y el reposo sin moverse |
| `v0.0.12` | **Contador de pulsos CNT0 por EXTI** — el antirrebote lo hace todo el hardware. La falla que costó el bring-up era del optoacoplador, no del firmware |
| `v0.0.13` | **Electroválvula TOYI** — el servo abre y cierra, y el reposo quedó en los mismos 6 µA. Con esto **todo el hardware poblado tiene driver** |

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
  cristal de `CL` ≈ 7-9 pF. Si el montado es de los comunes de **12,5 pF**, este oscilador va a
  correr rápido —del orden de +50 a +100 ppm—. Verificar el `CL` contra el BOM; si corresponde,
  cambiarlos por 18-22 pF.
  ⛔ **CORRECCIÓN (2026-09-21): esta nota decía que eso afectaba a "la hora que estampa el
  datalogger", y es FALSO.** Hay **dos cristales de 32.768 kHz** en R001 y es fácil confundirlos:
  éste es el **LSE del micro** (PC14/PC15), que alimenta el tick del kernel y el RTC interno —una
  copia de trabajo que ni siquiera se sincroniza todavía—. **La hora de las muestras la lleva el
  MCP79410 con SU propio cristal, `Y1`, en X1/X2 de ese chip.** O sea que este pendiente afecta a los
  `vTaskDelay()` y al `timerpoll`, donde 100 ppm son despreciables; la deriva de la hora se mide y se
  corrige aparte (ver la fase 2, `wan_rtc_sincronizar()`).

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

**6. Y la causa más tonta de todas, que costó una tarde el 2026-09-02: el CONECTOR del cable a la
placa.** Placa nueva, sin estrenar. Los síntomas fueron, en este orden: `failed to erase memory` con
la tensión perfecta, después `Unable to get core ID` **en los tres modos de conexión**, y el LED del
dongle parpadeando rojo/verde sin parar. Todo eso mientras `Voltage` informaba 3,24 V correctos —
porque **`VAPP` se mide en el conector y no dice nada de si `SWDIO`/`SWCLK` llegan al micro**.

Lo que hay que quedarse de acá no es "revisá el cable", es el **método**, porque los tres puntos
anteriores mandaron a buscar en el lugar equivocado:

- **Un cambio de comportamiento en el tiempo descarta el diseño.** La placa conectó bien a la mañana
  —se le leyeron el device ID, los option bytes y 64 KB de flash dos veces— y dejó de conectar a la
  tarde. Eso no lo hace un circuito: lo hace un contacto. Es la misma regla que cerró lo del TPS62130.
- **Con dos placas, la sana es un instrumento.** Bajar el *mismo binario* a la placa vieja separó en
  un minuto "el firmware está mal" de "la placa está mal". Cuando el mismo binario falla en las dos,
  el sospechoso es el firmware o el dongle; cuando falla en una sola, es esa placa.
- ⚠ **Subir la tensión "por las dudas" va en la dirección equivocada.** Pasó acá: el riel se llevó de
  3,25 a 3,35 V buscando que el borrado anduviera. El máximo absoluto son **3,6 V** y el punto 1 de
  esta misma sección dice que el problema era la tensión **alta**. El valor bueno es **3,28 V**.

**7. La asimetría "leer anda, borrar no" tiene una explicación física y conviene tenerla a mano.** El
borrado es lo único que hace correr la **bomba de carga interna** de la flash durante mucho tiempo
seguido: es la operación más exigente eléctricamente que sabe hacer el chip. Por eso una alimentación
marginal, o un pin `VDD`/`VDDA`/`VBAT` mal soldado, se manifiesta primero ahí y deja las lecturas
intactas. Si en cambio **tampoco hay `core ID`**, ya no es la bomba de carga: el SWD no está llegando
al micro y hay que ir a la continuidad de PA13/PA14 y a las patas de alimentación.

### 🔧 `PRUEBA_MINIMA`: el firmware de una línea para estrenar una placa

En `main.c` (bloque *Private define*, `USER CODE BEGIN PD`) vive:

```c
#define PRUEBA_MINIMA           1     /* 0 = operación normal */
```

En 1, `main()` se desvía en `USER CODE BEGIN Init` a `prvPruebaMinima()`, que **no retorna**: no llega
a correr **ninguna** de las inicializaciones generadas por CubeMX ni el scheduler. Se escribió el
**2026-09-02** para estrenar la placa nueva y **conviene no borrarlo**: la próxima placa lo va a
necesitar igual.

Lo que deliberadamente **no** usa: el cristal de 32.768 kHz (el reloj sale entero del MSI, con una
copia de `SystemClock_Config()` sin las tres líneas del LSE), el RTC, el LPTIM1, FreeRTOS, y ningún
periférico externo. **El `.ioc` queda intacto** — el cristal sigue configurado, simplemente no se usa.

**El LED cuenta hasta dónde llegó**, y ésa es toda la gracia:

| Lo que se ve en PB9 | Qué significa |
|---|---|
| 10 destellos rápidos → pausa → latido lento | Todo bien: micro, PLL y consola |
| 10 destellos rápidos → pausa → **quieto** | El micro corre; se cuelga configurando el reloj |
| **Nada** | No llegó el firmware, o el micro no arranca (NRST trabado, o no se programó) |

⚠ **Los destellos NO usan `HAL_Delay()`**, sino el lazo por sondeo de `error_delay_ms()`: nada de
TIM6, `uwTick` ni interrupciones. Y la etapa 1 corre **antes de tocar el reloj**, con el MSI a 4 MHz
tal cual quedó el micro tras el reset. Es a propósito — el destello tiene que depender de lo mínimo
posible, porque lo que contesta es "¿el micro corre?", y eso es cierto aunque el PLL no arranque. Por
la misma razón `prvClockSinCristal()` **no llama a `Error_Handler()`**: si el PLL falla devuelve 0, el
LED destella igual y la consola lo dice.

La consola (9600 8N1) imprime un banner por poleo, así que **es de una sola vía**: no responde a lo
que se tipee. Los comandos vienen recién con el firmware completo.

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
   alimentación.
7. ✅ **RS485** (`v0.0.8`) — `USART3` con el **DE manejado por hardware** (PB1 = `USART3_RTS_DE`), tres
   rieles conmutados y lectura por **tramas delimitadas por silencio**, que es ya la delimitación de
   Modbus RTU. Falta la capa **Modbus** y los mapas de registros de los módulos.
8. ✅ **Entradas de 4-20 mA** (`v0.0.9`) — **INA3221** en el I2C2 (dirección 7 bits `41`) y la fuente
   lineal de los sensores en `EN_PWR_SENS420` (PB12). Validado contra un calibrador: ver abajo.
9. ✅ **microSD, etapa 1: la tarjeta** (`v0.0.10`) — SPI3 con el CS por software, energía conmutada
   por `EN_PWR_SD` y presencia por `SD_DET`. Protocolo SD-SPI completo hasta leer y escribir
   sectores. ⏸ **La etapa 2 —FatFs encima— la difirió Pablo el 2026-08-14 a las capas de
   aplicación**, junto con Modbus y el registro de muestras. El hardware de la SD está cerrado; lo
   que falta es política de datos, no bring-up. **No adelantarla.**
10. ✅ **Medida de los rieles por ADC1** (`v0.0.11`) — `Application/drivers/drv_adc.{h,c}` y el comando
   `vin`. El riel de **12 V** va por PB0 (`ADC1_IN15`) con divisor 56K/10K y load switch en PC4; el de
   **3,3 V sale de `VREFINT`**, sin hardware — su circuito existe en la placa pero **no se usa**, por
   lo explicado más abajo. Validado en banco el **2026-08-17**: las dos medidas coinciden con el
   tester y **el reposo no se movió**, que era lo que confirmaba que el ADC queda en deep power-down.
11. ✅ **Contador de pulsos CNT0** (`v0.0.12`) — `Application/drivers/drv_pulsos.{h,c}` y el comando
   `cnt`. EXTI por flanco de bajada en PA12; **todo el antirrebote lo hace el hardware**. Validado en
   banco el **2026-08-18**, después de que Pablo corrigiera el optoacoplador: durante el bring-up el
   colector no bajaba de 2,53 V y el firmware no tenía nada que ver. Ver la sección propia más abajo.
12. ✅ **Electroválvula TOYI** (`v0.0.13`) — `Application/drivers/drv_valvula.{h,c}` y el comando `ev`.
   Es un **servo**, no una biestable: `EN_EV_TOYI` (PA6) la alimenta y `CTL_EV_TOYI` (PA7) elige el
   sentido. Validada en banco el **2026-08-18**: abre, cierra, y **el reposo quedó en los mismos
   6 µA**. Ver la sección propia más abajo.
13. ✅ **Modem LTE, etapa 1: la fuente** — validada en banco el **2026-09-04**, riel en **3,8 V**.
   `Application/drivers/drv_lte.{h,c}` y el comando `lte`. Es la fuente **rediseñada** (TPS22810 →
   LMR33630 en cascada) sobre la **placa nueva**; la vieja mató dos TPS62130. Con eso `EN_LTE_3V8`
   (PA4) quedó sin función y el driver se simplificó a `drv_lte_power()`.
   ⚠ **El reposo NO cumplió el criterio**: 354 µA con la placa poblada entera, contra 40 µA en la
   original. Pablo lo dejó afuera por ahora; ver la sección propia.
14. ✅ **Modem LTE, etapa 2: la UART4** — **validada en banco el 2026-09-04**: el módulo contesta AT.
   UART4 a **115200** por PA0/PA1, con `lte esc` / `lte at` / `lte bridge`. El módulo es un
   **WH-LTE-7S1-E** (Cat-1, USR IOT). Ver la sección propia — sobre todo la **secuencia de escape de
   tres tiempos**. **Con esto todo el hardware de R001 tiene driver y se termina el bring-up.**

15. ✅ **Bajo consumo** (`v0.0.6`) — cerrado por los dos lados: el firmware con el tickless y el
   hardware con la fuente, que bajó de 210 a 60 µA de quiescent. Ver abajo. ⚠ **Reabierto en la
   placa nueva**: 354 µA en reposo con todo poblado, ver la sección del modem.
16. **Pulido final, con el hardware ya terminado.** Acá van las cosas que no habilitan nada nuevo y
   que conviene hacer una sola vez, al final, en vez de rehacerlas cada vez que entra un periférico:
   la **sincronización del RTC interno desde el MCP79410**, el período definitivo de `tkCtl` junto
   con el watchdog y el poleo de `TERM_SENSE` (los tres comparten la misma vuelta), el
   comportamiento del parser de comandos, y `BOR_LEV`.
17. **Validación en Release** — obligatoria antes de campo. Ver abajo.

**Pendientes conocidos:** `BOR_LEV` sigue en el default más bajo (~1,7 V), que para un equipo a
batería conviene decidir a propósito.

✅ **Resueltos el 2026-09-08:** el parser dejó de matchear por **prefijo** (ver la fase 2), y el
pendiente del `reset` se cerró **con el diagnóstico invertido**: `reset` **anda** y era `reboot` el
que colgaba. O sea que **el problema nunca fue la línea de NRST**. `reboot` se eliminó — el detalle
está en el comentario que quedó en `tkCmd.c`, en su lugar.

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

#### ✅ Cómo se sabe si la hora es confiable: la firma en la SRAM

**El respaldo por pila es intermitente**, medido el 2026-08-12: de cuatro cortes de alimentación
seguidos, el MCP79410 aguantó tres y falló uno, volviendo a su fecha en blanco (`2001-01-01`). No es
firmware —se comportó igual las cuatro veces— ni una pila agotada, que fallaría siempre; el perfil es
de **contacto intermitente en el porta pila**. Queda como pendiente de hardware.

Pero el episodio destapó algo más importante que la pila: **el firmware tiene que poder decir "esta
hora no es confiable"**. En campo esto va a pasar, y un datalogger que estampa `2001-01-01` en las
muestras sin marcarlas es peor que uno que no estampa nada — los datos malos se mezclan con los
buenos y después no hay forma de separarlos.

Adivinar por la fecha es frágil (¿qué año es "demasiado viejo"?). El mecanismo implementado es una
**firma de 5 bytes en la SRAM del propio MCP79410**, y funciona porque **la SRAM y el contador de
tiempo se alimentan de la misma pila**:

```
firma intacta  <=>  el respaldo sostuvo  <=>  el reloj nunca se detuvo
firma perdida  <=>  el respaldo falló    <=>  el chip arrancó frío
```

**No hay caso intermedio, y ahí está toda la gracia: no es una heurística, es una equivalencia
física.** Si algún día la hora quedara en un lado y la firma en otro con alimentaciones distintas, el
mecanismo dejaría de valer y habría que repensarlo.

Detalles que importan:

- La firma la escribe **`drv_rtc_escribir()`**, y **después** de poner la hora: fijar la hora es
  justo el momento en que alguien afirma que es correcta. El orden no es casual — al revés, si
  fallara la puesta en hora el equipo quedaría jurando que una hora incorrecta es confiable, que es
  el único desenlace realmente malo.
- `drv_rtc_validez()` chequea además que el oscilador esté corriendo: con la firma intacta pero
  `ST` en 0 la hora está congelada, y devolver "válida" sería mentir.
- Los primeros `DRV_RTC_SRAM_USUARIO` bytes de la SRAM son de la firma y `drv_rtc_sram_escribir()`
  **rechaza escribirlos**. La aplicación empieza después.
- El comando **`rtc invalid`** borra la firma para poder ejercitar el camino de arranque en frío
  **sin sacar la pila**.

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

⏳ **La sincronización queda POSTERGADA a propósito** (decidido por Pablo el 2026-08-12): se
implementa **en la etapa final de pulido del firmware, cuando el hardware esté terminado**, no ahora.
El motivo es de método: mientras se sigan poblando periféricos, el trabajo que rinde es bring-up, y
esto es una optimización —evitar una transacción I2C por timestamp— que no habilita nada que hoy
falte. **No adelantarla sin que Pablo lo pida.**

Cuando toque, dos cosas que ya están resueltas y no hay que volver a pensar:

- **Copiar sólo si `drv_rtc_validez()` devuelve `rtcHORA_VALIDA`.** Sin ese chequeo se copia basura
  con toda confianza, que es exactamente el problema que la firma vino a resolver.
- Con eso se cierra solo el pendiente del `Check_RTC_BKUP` **vacío** que genera CubeMX, por el cual
  `MX_RTC_Init()` reinicializa la hora en cada arranque: al haber una fuente autoritativa afuera, se
  pisa con lo que diga el MCP79410 y deja de importar.

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
en `Normal` o en `Hot plug` va a fallar con **`Error: No STM32 target found`** aunque el firmware esté
perfecto. No es la placa.

**⚠ El síntoma que confunde: se VE el reset y aun así falla.** Le pasó a Pablo el **2026-08-18**. En
`Normal`, CubeProgrammer **pulsa NRST** —de ahí el reset que se ve— y recién después intenta
conectarse; para entonces el firmware ya arrancó, llegó al scheduler y se durmió. La carrera está
perdida antes de empezar.

**El campo que lo arregla es `Mode`, no `Reset mode`,** y es fácil confundirlos porque están uno al
lado del otro en el panel del ST-LINK:

| Campo | Qué dice | Cuál va |
|---|---|---|
| **`Mode`** | **cuándo** se conecta | **`Under reset`** — mantiene NRST asertado mientras establece la conexión, así que toma al micro antes de que ejecute una sola instrucción |
| `Reset mode` | qué tipo de reset | `Hardware reset` |
| `Frequency` | — | `950 kHz`, nunca `Auto` (ver el punto 4 de la sección del programador) |

Por CLI es `mode=UR`. La launch config de CubeIDE ya usa `connect_under_reset`, así que **desde el IDE
esto no se ve nunca**. Ver también la sección del programador: si además queda NRST trabado, el
síntoma se parece pero la causa es otra.

De yapa, al conectar CubeProgrammer informa `Debug in Low Power mode enabled`: pone los bits de
`DBGMCU_CR` para que el debug sobreviva a los modos de bajo consumo. O sea que **el problema es sólo
el primer enganche** — una vez adentro, ya no se le escapa aunque el firmware duerma.

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
| LTE (modem **WH-LTE-7S1-E**) | PA0 `LTE_TXD`, PA1 `LTE_RXD` → **UART4**. Energía: **PC13 `EN_LTE_DCIN`** (TPS22810, EN=1 prende) — es el único interruptor, la fuente es en **cascada**. **PA5 `LTE_PWR`** → transistor → power switch (⚠ invertido: PA5=1 aprieta). ~~PA4 `EN_LTE_3V8`~~ quedó **sin función** con la fuente nueva |
| RS485 (modbus) | PB10 TX, PB11 RX, **PB1 `USART3_RTS_DE`** → **USART3**, 9600 8N1, transceiver **SP3485** |
| Rieles conmutados (TPS22819, EN=1 prende, pull-down de 100 K) | PC6 `EN_PWR_RS485`, PC7 `EN_PWR_QMBUS`, PB15 `EN_PWR_CPRES` |
| Fuente lineal de los sensores 4-20 mA | **PB12 `EN_PWR_SENS420`** (EN=1 prende) |
| Medida del riel de 12 V (TPS22810, EN=1 prende, pull-down) | **PC4 `EN_SENS12V`**, divisor 56K/10K → seguidor TLV8802 → **PB0 `ADC1_IN15`** |
| Medida del riel de 3,3 V | **existe pero NO se usa** (`EN_SENS3V3` PB2, divisor 56K/56K, PC5): el riel sale de `VREFINT` |
| I2C | PB13 SCL, PB14 SDA → **I2C2** (poblado; pull-up de 10 kΩ) |
| microSD | PA15 `SD_SS`, PC10 `SD_SCK`, PC11 `SD_MISO`, PC12 `SD_MOSI` → **SPI3** (NSS por software), **PD2 `SD_DET`** (a GND con tarjeta, pull-up interno), **PB3 `EN_PWR_SD`** ⚠ **0 = prende** (SI2301 canal P, pull-up de 100 K) |
| Contador de pulsos CNT0 | **PA12 `CNT0`**, EXTI por flanco de **bajada**, **sin pull interno**. Contacto seco → opto (open-collector con pull-up de 10K a 3V3) → RC de 4K7 / 1 µF → **74AUP1G17** (Schmitt, no inversor) |
| Electroválvula TOYI (servo) | **PA6 `EN_EV_TOYI`** (TPS22810, EN=1 alimenta al servo), **PA7 `CTL_EV_TOYI`** (1 = abrir, 0 = cerrar). Sin realimentación de posición; el riel del servo lo elige un jumper: 12 V o 3,3 V |
| Analógicas | PC5, PB0 |
| LED | **PB9** (en el `.ioc`; no figura en el CSV). ⛔ **PA2 ya NO es LED2**: iba a un pin de SIM del módulo y por eso el modem no leía su tarjeta. Quedó sin asignar el 2026-09-09 |

Puntos a resolver contra el esquemático `Hardware/R001/` y el datasheet **DS11585**
(`Datasheets/STMicroelectronics/stm32l496ae.pdf`) antes de configurar el `.ioc`:

- **PB13/PB14 son `I2C2`**, no `I2C1`; **PC10–PC12 + PA15 son `SPI3`**, no `SPI2`. El CSV nombra la
  interfaz genéricamente; la instancia del periférico queda determinada por el pin. (Derivado del
  mapa de AF — confirmar en el datasheet.)
- ✅ **Resuelto (2026-08-18): el CSV lista `PA7 = NRST` y es un error del CSV.** En LQFP64 el reset es
  un pin dedicado, y Pablo confirmó que PA7 es **`CTL_EV_TOYI`**, la dirección de la electroválvula.
  Un recordatorio más de que la fuente de verdad de los pines es Pablo, no el CSV.
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
├── config/     cfg_hash.{h,c}          Pearson + tabla: el hash del protocolo
│               cfg_utils.{h,c}         strlcpy y str2bool
│               cfg_nvm.{h,c}           layout de la EEPROM, save/load, checksums
│               cfg_base.{h,c}          timerpoll, timerdial, pwrmodo
│               cfg_ainputs.{h,c}       3 canales de 4-20 mA + calibracion
│               cfg_counter.{h,c}       contador de pulsos
│               cfg_modbus.{h,c}        local + 5 canales remotos
│               cfg_consigna.{h,c}      doble consigna de presion
├── pwr/        pwr_lock.{h,c}          candados de energía (ver abajo)
├── drivers/    drv_uart.{h,c}          UART sobre la HAL, tabla de instancias
│               drv_term_sense.{h,c}    TERM_SENSE, poleado por tkCtl (no EXTI)
│               drv_i2c.{h,c}           bus I2C2 por interrupción + candado de bus
│               drv_eeprom.{h,c}        M24M01, dirección plana de 17 bits
│               drv_rtc79410.{h,c}      RTC externo MCP79410 (BCD, pila, PWRFAIL)
│               drv_rs485.{h,c}         rieles conmutados y pines del SP3485
│               drv_ina3221.{h,c}       medida de 4-20 mA + riel de la fuente lineal
│               drv_sd.{h,c}            tarjeta microSD por SPI (sectores, sin FatFs)
│               drv_adc.{h,c}           rieles de 12 V (divisor) y 3,3 V (VREFINT)
│               drv_pulsos.{h,c}        contador de pulsos CNT0 por EXTI
│               drv_valvula.{h,c}       electroválvula TOYI (servo, sin realimentación)
│               drv_lte.{h,c}           modem LTE: energia, power switch y UART4
├── FRTOS/      port_lptim_tick.c       overrides del port: tick por LPTIM1 + tickless
├── FRTOS-IO/   frtos-io.{h,c}          fd table + frtos_open/read/write/ioctl + xprintf
│               frtos_cmd.{h,c}         ciclo de comandos
└── tasks/      tkCtl.{h,c}             control: destello del LED + poleo de TERM_SENSE
                tkCmd.{h,c}             consola
                tkSys.{h,c}             el poleo: arma el dataRcd y lo guarda
                tkWan.{h,c}             la FSM de la sesion con el servidor
                wan_frame.{h,c}         el CONTRATO: arma y parsea los frames
                fs_datos.{h,c}          la VENTANA: FS circular sobre la EEPROM
                fs_sd.{h,c}             los LOTES: la microSD como extension
```

**Convención de tareas:** cada una vive en `Application/tasks/` con su propio header, que declara su
prioridad, el tamaño de stack y su memoria estática (`extern`); las definiciones van en el `.c`.
`main.h` y `main.c` no llevan nada de eso. Los archivos NO se prefijan con el nombre del proyecto:
`tkCtl.c`, no `FWDLGARM_R1_tkCtl.c`.

**`Application/` no se compila sola**: el `.cproject` lista `Core`, `Middlewares` y `Drivers` como
*source path*. Al clonar o rehacer el proyecto hay que agregarla en *Project → Properties → C/C++
General → Paths and Symbols → **Source Location** → Add Folder*, y sus seis subdirectorios en la
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
fila por instancia — `fdTERM`, `fdWAN`, `fdRS485A` y `fdI2C` ya están enganchados. El único que sigue
con `NULL` es **`fdNVM`**: compila y devuelve `-1`.

**Candados de energía (`pwr_lock.h`).** Son la política de energía del equipo, definida por Pablo:

| Situación | Qué hace el sistema |
|---|---|
| **Terminal conectada** (`TERM_SENSE`, poleado por `tkCtl`) | **Nunca** entra en ningún modo de sueño |
| **Poleo Modbus o sesión de modem** | La tarea levanta su candado antes y lo baja al terminar |
| **El resto del tiempo** | **Stop 2** — y es el ~95 %, donde vive la autonomía |

Es un bitmask, no un contador: tomar dos veces el mismo candado es idempotente, y si el modem y el
Modbus se solapan cada uno tiene su bit sin pisarle la bandera al otro. Mientras haya **al menos uno**
tomado, `vPortSuppressTicksAndSleep()` **vuelve enseguida sin hacer nada** y el idle gira.

⚠ **Esto NO es una optimización de consumo: es de CORRECTITUD.** Ver la sección siguiente.

Medido en `v0.0.6`: con `pwrLOCK_TERM` tomado la placa consumía **3,5 mA** durmiendo en Sleep, contra
**65 µA** sin candados. Girando será más —falta medirlo— y está anotado como pendiente.

#### ⚠ El tickless se comía bytes del USART, y no era un problema de energía

Encontrado el **2026-08-12** persiguiendo "la flecha arriba no anda en la consola". La medición que lo
cerró, con el comando `keys`:

| Prueba | Bytes capturados | Errores de UART |
|---|---|---|
| Tecla `ESC` ×3 — **un byte suelto** cada vez | `1B 1B 1B` | **0** |
| Flecha ×3 — **tres bytes pegados** cada vez | `1B 1B 1B` | **3** |
| **Flecha ×3, ya con el arreglo** | `1B 5B 41` ×3 — **los 9** | **0** |

O sea que de cada secuencia `ESC [ A` sobrevivía sólo el primer byte, y con el arreglo llegan las tres
completas.

**El mecanismo:** `vPortSuppressTicksAndSleep()` hacía `__disable_irq()` —que enmascara por `PRIMASK`
**todas** las interrupciones sin importar la prioridad, así que subir la del USART no habría servido—
y recién las habilitaba al final, después de parar y rearmar el LPTIM1 **dos veces**. Cada espera del
flag `ARROK` son 3 ciclos de LSE, ~92 µs. Esa ventana se acerca a los **1042 µs** que dura un byte a
9600: el segundo byte se queda en `RDR` sin que nadie lo lea y el tercero lo pisa. Overrun.

**Con tecleo humano no se ve nunca** —las teclas van a 100 ms— pero **Modbus es todo ráfagas de bytes
pegados**, así que esto habría roto el RS485 de la peor manera posible: intermitente y dependiente del
momento.

Dos cosas que conviene no olvidar:

- **El peligro nunca fue el `WFI`**, que despierta con cualquier interrupción en unos ciclos. Fue el
  `__disable_irq()` que el tickless ponía alrededor.
- **No "arreglarlo" acortando la ventana.** La haría menos probable, que en un bug dependiente del
  momento es peor que no tocarlo: pasa el banco y falla en campo.

⏳ **Pendiente anotado:** con el candado tomado el idle **gira**, que es la opción segura. Reemplazarlo
por un **`__WFI()` pelado** no pierde un solo byte y bajaría el consumo activo a un tercio; se hace
cuando se pueda medir. Con 15 s de poleo cada 5 minutos el ciclo de trabajo es del 5 %, así que hoy no
aprieta.

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

### ✅ Las entradas de 4-20 mA: el INA3221 (`v0.0.9`)

Monitor de corriente de 3 canales en el I2C2, dirección de 7 bits **`41`** (A0 a VS) = `0x82` en 8
bits — el mismo chip y la misma dirección que FWDLGX (`DEVADDRESS_INA1`, y por eso aquel código
forzaba `ina_id = 1` en todas sus funciones). Driver en `Application/drivers/drv_ina3221.{h,c}`,
comando `ina` en la consola.

**Validado el 2026-08-14 contra un calibrador**, aplicando 4, 8, 12, 16 y 20 mA. El ajuste por
mínimos cuadrados da `I_leída = 0,99770 · I_real + 0,002`, con **todos los residuos por debajo de
media cuenta** (1 cuenta = 40 µV / 7,32 Ω = 5,46 µA). O sea: la linealidad es perfecta dentro de la
resolución del chip, y lo único que hay es un **error de ganancia de −0,23 %**, que equivale a que el
shunt real sea de 7,337 Ω en vez de 7,32 — dentro de la tolerancia de una resistencia del 0,5 %.

⚠ **Ese −0,23 % NO se corrige en el driver.** Es exactamente lo que absorbe la calibración de dos
puntos que ya existía en FWDLGX (`imin`/`imax` contra `mmin`/`mmax`), y meterle un factor acá lo
escondería donde nadie lo va a buscar el día que cambie una placa.

**Las cuatro cosas que hay que tener presentes:**

- **El consumo manda el diseño del driver.** El INA3221 consume **~350 µA midiendo y ~2 µA en
  power-down**, contra los ~5 µA del micro dormido: dejarlo encendido multiplica por setenta el
  consumo de reposo del equipo. Por eso el estado de reposo es **dormido**, `drv_ina_init()` lo deja
  así, y `drv_ina_medir()` lo duerme **también en el camino de error** — si no, un fallo aislado del
  I2C dejaría al chip consumiendo para siempre sin que nada lo delate salvo la autonomía.
- **Una medida tarda ~1,4 s** (500 ms de asentamiento de la fuente lineal + 845 ms de barrido: 128
  promedios × 3 canales × (1,1 + 1,1) ms). Es a propósito: 128 promedios es lo que hace que una
  lectura de 4-20 mA sea estable. **Durante esa ventana el micro puede dormir en Stop 2 sin problema**
  —el INA convierte solo y el riel es un GPIO, que sobrevive al Stop—, así que este driver **no toma
  ningún candado de energía propio**: el único es el `pwrLOCK_I2C` de cada transacción.
- **El signo importa, y FWDLGX lo perdía.** El registro de shunt es complemento a dos de 13 bits
  alineado en `[15:3]`; `ainputs.c` hacía `>> 3` sobre un `uint16_t`, así que una medida negativa
  —lazo abierto, sensor al revés— salía como un número enorme y positivo. Acá el corrimiento va con
  extensión de signo. Con 4-20 mA sanos no se nota nunca; con un sensor desconectado, sí.
- **El fondo de escala está justo.** 20 mA sobre 7,32 Ω son 146 mV contra los ±163,8 mV que admite el
  chip: una corriente de falla por encima de ~22 mA **satura** en vez de leerse alta.

**Qué canal es qué entrada no lo decide el driver.** Habla de `inaCH1..inaCH3`, que son los del chip.
FWDLGX mapeaba al revés (entrada 0 → CH3, 1 → CH2, 2 → CH1) y usaba el *bus voltage* de CH1 para la
batería; ese mapeo depende del cableado y es de la capa de aplicación.

**No hay offset que corregir, y conviene saber por qué se llegó a pensar que sí.** La primera tanda
de medidas mostró **−0,098 mA** (18 cuentas) en el punto de "0 mA", fuera de la recta de arriba por
0,1 mA — mucho más que el offset típico del chip. Resultó que ese punto era **el lazo desconectado**,
no 0 mA inyectados: repetido con el calibrador dando 0,000 mA de verdad, la lectura es correcta
(2026-08-14).

⚠ **Ojo con la tentación de usar ese −0,098 como detector de sensor ausente.** Con el lazo abierto la
entrada del INA queda flotando, así que ese número no es una especificación sino lo que se leyó una
vez: puede cambiar con la temperatura, la humedad o la placa. **El criterio correcto para "sensor
desconectado" es el del propio lazo de 4-20 mA**: por debajo de ~3,5 mA no hay transmisor sano
—NAMUR NE43 pone el umbral de falla en 3,6 mA—, y eso vale igual si la entrada lee −0,098, 0,000 o
+0,3. Es de la capa de aplicación, junto con la calibración.

### ✅ La microSD, etapa 1: la tarjeta (`v0.0.10`)

SPI3 con el **CS por software** (PA15), energía conmutada por **`EN_PWR_SD` (PB3)** y presencia por
**`SD_DET` (PD2)**. Driver en `Application/drivers/drv_sd.{h,c}`, comando `sd` en la consola.
**Todavía sin FatFs**: esto mueve sectores de 512 bytes, y la etapa 2 va encima.

Validado el **2026-08-14** con una SDHC de 4 GB: inicializa, el `C_SIZE` de la CSD da 7 610 368
sectores —verificado a mano contra lo que informó el firmware—, el CID sale en ASCII legible, el
sector 0 es un MBR con una partición FAT32 (tipo `0x0B`) **que empieza en el sector 8192**, y un
sector escrito y releído coincide en los 512 bytes.

**⚠ `EN_PWR_SD` es de lógica INVERTIDA, al revés que los rieles del RS485.** Maneja el gate de un
**SI2301, MOSFET de canal P**, con un pull-up de 100 kΩ: **`0` prende, `1` o Hi-Z apaga**. El pull-up
es lo que garantiza que la tarjeta arranque apagada, antes de que el firmware llegue a configurar
nada. En CubeMX eso obliga a poner el *Output Level* en **High**, porque el default es Low y acá Low
significa prender.

**El reloj va en dos tiempos:** 234 kHz para inicializar, como exige la norma (100-400 kHz), y
7,5 MHz después. Si alguna vez una tarjeta inicializa bien pero lee de forma errática, **bajar ese
segundo prescaler es lo primero que hay que probar**.

#### ⚠ La lección que costó los 89 µA: un pull-up interno contra un contacto cerrado

Vale para **cualquier entrada digital que venga después**, empezando por el contador de pulsos.

`SD_DET` es un contacto que va a GND cuando hay tarjeta, y el hardware no trae pull-up externo. Se
puso el interno del STM32 — que **son de 30 a 50 kΩ**. Con la tarjeta insertada el contacto cierra y
ese pull-up queda drenando para siempre:

```
3,3 V / 40 kΩ ~= 82 µA, las 24 horas, sólo por tener la tarjeta puesta
```

Medido: el reposo pasaba de **6 µA a 95 µA con sólo insertar la tarjeta**, sin encenderla. Contra los
~5 µA del micro dormido eso multiplica por quince el consumo del equipo.

**Lo que hace que esto sea peligroso es que funcionalmente todo andaba perfecto.** El pull-up era
necesario —sin él el pin flotaba y la detección mentía— y no había ningún síntoma salvo la corriente.
En campo se habría visto como "la batería dura menos de lo previsto", meses después, sin nada que
apunte al culpable.

**La solución la propuso Pablo, y además simplificó el driver:** la detección **sólo sirve justo
antes de usar la tarjeta, y para usarla hay que prenderla igual**. Así que el pull-up vive y muere
con el riel, dentro de `prvPinesBus()`, junto con los pines del SPI. Mientras la tarjeta está
encendida esos 82 µA son ruido al lado de los 0,2-1 mA que consume ella. El orden correcto queda:

```
drv_sd_power( true )  ->  drv_sd_presente()  ->  drv_sd_arrancar()
```

Y `drv_sd_presente()` **devuelve false con el riel apagado**, porque ahí el pin está en alta
impedancia y cualquier otra respuesta sería inventada. Por eso el comando `sd` dice
`sin saber (riel apagado)` en vez de `vacia`.

Dos cosas generales que quedan de acá:

- **La medición de consumo es parte del criterio de aceptación de cada etapa**, no un chequeo final.
  Este bug no se encuentra de ninguna otra forma.
- **Antes de habilitar un pull-up interno, preguntarse qué pasa cuando el contacto está cerrado.**
  Si va a estarlo la mayor parte del tiempo, las opciones son conmutarlo con el periférico —como
  acá—, o pedir uno externo de 1 MΩ, que costaría 3 µA.

### ✅ Los rieles por ADC1 (`v0.0.11`)

`Application/drivers/drv_adc.{h,c}`, comando `vin`. **Validado en banco el 2026-08-17**: las dos
medidas coinciden con el tester y el reposo quedó igual que antes, que es lo único que confirma que el
ADC vuelve a *deep power-down* entre medidas.

**⚠ El riel de 3,3 V NO se puede medir con un ADC referenciado a él mismo.** R001 trae el circuito
—divisor 56K/56K, load switch en PB2, seguidor TLV8802, PC5— pero la cuenta se cancela sola:

```
ADC = (V3V3 / 2) / VREF+ x 4095 = (V3V3 / 2) / V3V3 x 4095 = 2047, SIEMPRE
```

Da 2047 con el riel en 3,3 V, en 3,0 o en 3,6: **la medida no contiene información sobre lo que se
quiere medir**. La solución es `VREFINT`, la referencia interna de ~1,212 V, que es un valor absoluto
**calibrado de fábrica por chip** (`VREFINT_CAL`, medida a VDDA = 3,0 V):

```
VDDA_real = 3000 mV x VREFINT_CAL / VREFINT_leído
```

Y sale **mejor** que el circuito: el divisor acumula la tolerancia de dos resistencias más el offset
del operacional; `VREFINT` sólo tiene deriva térmica. Vale porque **en R001 VDDA del micro ES el riel
de 3,3 V** (confirmado por Pablo el 2026-08-14). Pablo evaluó sacar el circuito de la placa; como ya
estaba soldado, se dejó con **`EN_SENS3V3` (PB2) configurado y apagado a propósito**, y PC5 sin
asignar en CubeMX —los pines no configurados quedan en analógico tras el reset, que es el de menor
fuga—.

**Sin `VREFINT` la medida de 12 V también estaría mal**, y de forma menos evidente porque devuelve un
número plausible: convertir contra un 3,3 V nominal supuesto traslada directo cualquier desvío del
riel. Por eso `drv_adc_v12_mv()` lee **siempre los dos canales**, VREFINT primero.

**Dos cosas del hardware que condicionan el driver:**

- **El TLV8802 es un operacional *nanopower*:** ~320 nA y apenas ~6 kHz de ancho de banda. Eso le deja
  impedancia de salida alta a la frecuencia con la que el ADC carga su capacitor de muestreo, así que
  el muestreo va en **640,5 ciclos** (~43 µs a 15 MHz) y el riel necesita **10 ms de asentamiento**
  antes de creerle a una medida.
- **El divisor de 12 V consume 182 µA** (12 V / 66 kΩ) mientras está conectado, y el de 3,3 unos
  29 µA. Por eso existen los load switches y por eso el driver los deja apagados salvo durante la
  ventana de medición. ⚠ Los **TPS22810 son activo ALTO**, al revés que el SI2301 de la microSD: los
  dos criterios conviven en la misma placa.

**El ADC no queda habilitado entre medidas:** reposa en *deep power-down* con su regulador apagado.
Al despertar hay que reponer el regulador, esperar sus 20 µs y **restaurar el factor de calibración**,
que ese modo no conserva — sin lo último la medida sale igual, pero con el error que la calibración
venía a corregir. Es un bug callado y por eso está explícito en `prvAdcDespertar()`.

#### ⚠ Medir el nodo del divisor con el tester: qué significa un "riel + 0,6 V"

Anotado el **2026-08-17**, midiendo el punto medio del divisor de 12 V con el tester: dio **3,7 V**,
cuando con 56K/10K y 12,2 V de entrada **tiene** que dar `12,2 x 10 / 66 = 1,85 V`.

**3,7 V no es un número cualquiera: es 3,1 + 0,6, un diodo por encima del riel que alimenta al
TLV8802.** Esa es la firma de un **diodo de protección de entrada del operacional conduciendo hacia su
propio VDD**: el nodo no está ahí porque el divisor lo ponga, está *clavado* ahí porque el clamp no lo
deja subir más. Para que pase, el divisor tendría que estar entregando bastante más que eso — con 56K
arriba, la de abajo tendría que ser de ≥24K; el error típico es tenerlas invertidas.

Y no sería sólo una lectura mala: mete corriente desde los 12 V hacia el riel de 3,3 V a través del
operacional, y le pone al TLV8802 una entrada por encima de su máximo absoluto.

**Cómo se descarta en un segundo, y por qué acá no era eso:** si el nodo estuviera realmente en 3,7 V
durante la conversión, el seguidor saturaría contra su riel y **`vin` informaría ~20 V, no 12,2**.
Como la medida sale bien, en el instante en que el ADC convierte el nodo está en 1,85 V. Los 3,7 V
salieron de otra condición —el nodo queda flotando con `EN_SENS12V` apagado, y ahí el tester lee
cualquier cosa—. La verificación definitiva, si alguna vez vuelve a aparecer, es con la placa **sin
alimentación** y el óhmetro: 10K a GND y 56K contra la salida del TPS22810.

#### La trampa de CubeMX: el "ADC Clock Mux" queda en `PLLSAI1R` y está bien

Costó un rato el 2026-08-15. CubeMX marca error en *Clock Configuration* y ofrece resolverlo solo:
**no hay que dejarlo**, porque lo que hace es **encender PLLSAI1**. Eso son un PLL más consumiendo y
—peor— un PLL más que arrancar **en cada despertada del tickless**, porque el Stop 2 los apaga y
`vPortSuppressTicksAndSleep()` rellama a `SystemClock_Config()`.

El ADC del STM32L4 tiene **dos caminos de reloj en el silicio**, y sólo uno está en juego:

| Camino | Lo controla | Fuente |
|---|---|---|
| Asíncrono | `RCC_CCIPR.ADCSEL` ← el *"ADC Clock Mux"* del árbol | PLLSAI1R / PLLSAI2R / SYSCLK |
| **Síncrono** | `ADC_CCR.CKMODE` ← el *"Clock Prescaler"* del ADC | **HCLK** |

Con `CKMODE` distinto de cero el hardware **ignora `ADCSEL` por completo**. Por eso el mux queda gris
y mostrando `PLLSAI1R`: es un residuo visual, no está en el camino. La configuración correcta es
`Clock Prescaler = Synchronous clock mode divided by 4` → HCLK/4 = 15 MHz.

**Cómo se verifica de verdad** (los campos del `.ioc` pueden mentir; el `.c` generado no):

```bash
grep -c 'PLLSAI1' Core/Src/main.c        # 0 = el PLL no se enciende
grep 'ClockPrescaler' Core/Src/main.c    # ADC_CLOCK_SYNC_PCLK_DIV4
```

Y que **no** haya ninguna llamada a `HAL_RCCEx_PeriphCLKConfig()` con `RCC_PERIPHCLK_ADC`: eso
confirma que `ADCSEL` ni se toca. Si el error de la GUI impide generar, subir el `N` de PLLSAI1 a 16
—con MSI a 4 MHz da un VCO de 64 MHz, el mínimo válido— alcanza para acallar la validación sin
encender nada.

### ✅ El contador de pulsos CNT0 (`v0.0.12`)

`Application/drivers/drv_pulsos.{h,c}`, comando `cnt`. **Validado en banco el 2026-08-18.** El
firmware quedó como se había escrito: no hubo un solo cambio entre "no anda" y "anda", porque lo que
fallaba era el optoacoplador (ver más abajo).

**Toda la parte difícil de contar pulsos está resuelta en el hardware**, y por eso el driver es corto.
El esquemático es `Circuito del contador de pulsos.png` ("CAUDALIMETRO PULSOS"):

```
12VRAIL --R39 2K2--\                        3V3RAIL
                    JP13 --+--- R41 10K ---+   |
3V3RAIL --R38 330--/       |               |  R40 10K
                           |  U14          |   |
                        ánodo |\   colector +---+--- R42 4K7 --+--- U13 --> CNT0 (PA12)
                              | \                              |   74AUP1G17
   JP12 (contacto) -----------+cátodo   emisor --> GND        C17 1uF     (Schmitt,
                           |                                   |           NO inversor)
                        C18 10pF                              GND
                           |
                          GND
```

**`JP13` elige de qué riel se alimenta el LED del opto**, y es la decisión de energía del circuito
(ver más abajo). `R41` es el pull-up del contacto: mantiene el cátodo al mismo potencial que el ánodo
mientras el contacto está abierto, así el LED queda apagado y el nodo definido. `C18` de 10 pF es
filtro de RF/ESD en el borne de entrada, **no** antirrebote: con 10K da 0,1 µs.

**Polaridad:** contacto cerrado → conduce el opto → el nodo cae → el 1G17 **no invierte**, así que
**PA12 queda en BAJO mientras el contacto está cerrado**. Se cuenta el **flanco de bajada**, que es el
cierre.

**El filtro no es simétrico**, y eso fija el techo de frecuencia: descarga por 4K7 (τ = 4,7 ms) y carga
por 10K + 4K7 (τ = 14,7 ms). Con los umbrales del 74AUP1G17 a 3,3 V (VT+ ≈ 1,8 V, VT− ≈ 1,2 V):

| | |
|---|---|
| Cierre más corto que se detecta | ~4,8 ms |
| Apertura más corta que se detecta | ~11,6 ms |
| Período mínimo | ~16 ms → **techo absoluto ~60 Hz** |

O sea **antirrebote de hardware de 5 a 12 ms** —de sobra para un reed, que rebota menos de 2 ms— y
**hasta unos 30 Hz con margen**, que es lo que da un caudalímetro (confirmado por Pablo el
2026-08-17). Por encima de eso el filtro se come pulsos y no hay firmware que lo arregle. **No agregar
antirrebote por software:** sólo daría otra forma de perder pulsos.

**Tres cosas que conviene no tener que volver a averiguar:**

- **El pin va sin pull interno.** La salida del 1G17 es CMOS push-pull. Un pull interno pelearía
  contra él y costaría 82 µA cada vez que el driver empuja al lado contrario: un **pull-up** mientras
  el contacto está cerrado, y un **pull-down las 24 horas**, porque el reposo es el nivel alto. Por eso
  el comando `cnt` imprime `PUPDR` — para cazar una regeneración de CubeMX que lo meta de vuelta.
  Lo que sí consume, y es de hardware, son los **330 µA** de `R40` mientras el contacto está cerrado;
  como en reposo está abierto, eso se paga sólo durante el pulso. **Pero el LED del opto consume mucho
  más — ver abajo.**
- **EXTI y no un timer, y no por comodidad.** Los LPTIM son los únicos que siguen contando en Stop 2,
  pero **PA12 no es entrada de ningún LPTIM**. PA12 sí es `TIM1_ETR`, pero TIM1 cuelga de APB2, que en
  Stop 2 no tiene reloj. Así que cada pulso despierta al micro; a la frecuencia de un caudalímetro es
  despreciable.
- **Ningún pulso se pierde en el `__disable_irq()` del tickless**, a diferencia de lo que le pasaba a
  los bytes del USART: la EXTI **latchea** el flanco, así que la interrupción queda demorada, no
  perdida. Se perdería sólo si llegaran dos pulsos dentro de esa ventana de ~100 µs.

**⚠ `JP13` es una decisión de energía, y el costo por cierre es de milliamperes, no de microamperes.**
Con el contacto **cerrado** el LED del opto conduce, y la corriente la fija la resistencia de la rama
elegida:

| `JP13` | Corriente del LED | Potencia del cierre | Equivalente en el riel de 3,3 V |
|---|---|---|---|
| **12 V** (`R39` 2K2) | (12 − 1,2)/2K2 = **4,9 mA** | 59 mW | **~18 mA** si el riel de 12 V sale de un elevador |
| **3,3 V** (`R38` 330) | (3,3 − 1,2)/330 = **6,4 mA** | 21 mW | 6,4 mA |

O sea que la posición de 3,3 V cuesta **un tercio de la energía** aunque su corriente sea mayor. A
cambio, la de 12 V le da al contacto seco una tensión de mojado mucho mejor, que es lo que rompe la
película de óxido de un reed y hace que el contacto sea confiable después de meses parado. **La
elección es de Pablo y es de campo, no de firmware** — el driver anda igual en las dos posiciones,
porque el tiempo lo fijan `R40`, `R42` y `C17`, del otro lado del opto.

Lo que sí hay que hacer es la cuenta con el ciclo de trabajo real del caudalímetro: si el reed queda
cerrado el 5 % del tiempo, la rama de 12 V son **~900 µA de promedio**, contra los ~5 µA del micro
dormido. Si la cuenta apretara, la palanca es subir `R39`: un opto con CTR ≥ 50 % satura de sobra con
1 mA, porque del otro lado sólo tiene que hundir los 330 µA de `R40`.

#### ⚠ El opto no saturaba, y la cuenta que lo dice en dos renglones

Durante el bring-up (2026-08-17) el contador no contaba nada, y el síntoma parecía de firmware: con el
contacto cerrado el colector del opto **bajaba de 3,25 V a 2,53 V y ahí se quedaba**. El 74AUP1G17
hacía lo correcto —2,53 V es un ALTO perfectamente válido contra su `VT−` de ~1,2 V—, así que en PA12
no había flanco y no había nada que el driver pudiera hacer.

**Lo que cierra el diagnóstico es el CTR**, y se saca con dos lecturas de tester:

```
I_LED      = (12 - 1,15) / 2K2 = 4,93 mA     <- el ánodo en 1,15 V prueba que el LED conduce
I_colector = (3,25 - 2,53) / 10K = 72 µA     <- lo que realmente entrega el fototransistor
CTR        = 72 µA / 4,93 mA ~= 1,5 %        <- un opto sano da entre 20 % y 600 %
```

Para hundir el nodo por debajo de 0,4 V harían falta ~285 µA. O sea que **el lado de entrada estaba
bien y el que fallaba era el propio opto**: 1,5 % es el orden del **beta inverso** de un transistor,
que es del 1 al 10 % del directo — la firma de un fototransistor trabajando al revés. **Pablo lo
corrigió en la placa el 2026-08-18 y el circuito quedó andando sin tocar una línea de firmware.**

La lección general, que vale para cualquier señal digital que entre por un opto: **medir el ánodo y el
colector y sacar el CTR** separa en un minuto "el contacto no cierra" de "el opto no satura" de "el
firmware no cuenta". Sin esa cuenta los tres se parecen.

**⚠ Y una que sí queda anotada como pendiente: cada despertada prematura atrasa el tick.** El port del
tickless descarta el resto de la división al convertir cuentas del LPTIM en ticks —está dicho en
`port_lptim_tick.c`— y son hasta 0,98 ms por despertada, ~0,49 ms en promedio. Con el LPTIM
despertando solo eso no ocurre nunca; **con el contador de pulsos ocurre en cada pulso**:

| Frecuencia de pulsos | Atraso del tick |
|---|---|
| 1 Hz | 0,05 % |
| 30 Hz | **~1,5 %** |

No rompe nada —los timestamps los estampa el RTC, no el tick— pero estira todos los `vTaskDelay()` en
esa proporción. **El arreglo es arrastrar el resto de una despertada a la siguiente**, unas pocas
líneas en `vPortSuppressTicksAndSleep()`. Va aparte, en su propio commit: toca el port, que está
validado desde `v0.0.5`, y acá se cambia una variable por vez.

### ✅ La electroválvula TOYI (`v0.0.13`)

`Application/drivers/drv_valvula.{h,c}`, comando `ev`. **Validada en banco el 2026-08-18**: el servo
abre y cierra con la secuencia de abajo, y **el reposo quedó en los mismos 6 µA** de antes, que era lo
único que podía delatar un load switch a medio apagar o un pin en el estado equivocado.

⏳ **Lo que falta medir es la corriente del movimiento**: la de arranque, la de régimen y —la que
importa— la de **atascamiento**. Es la que decide de qué riel puede comer el servo (ver el jumper más
abajo) y va a ser el consumo más grande del equipo. No es bring-up: es un dato para el dimensionado.

**No es una biestable: es un servo**, un motor eléctrico con dos señales y **ninguna realimentación de
posición** (datos de Pablo, 2026-08-18):

| Pin | Señal | Qué hace |
|---|---|---|
| **PA6** | `EN_EV_TOYI` | TPS22810, **EN=1 alimenta** al servo. Es *el* interruptor de energía |
| **PA7** | `CTL_EV_TOYI` | dirección: **1 = abrir, 0 = cerrar** |

Mientras está alimentado el servo se mueve hacia donde diga `CTL` y al llegar al tope se queda ahí. El
firmware sólo le da tiempo —**5 s**— y después le corta la energía:

```
1. CTL a la dirección deseada        <- PRIMERO la dirección
2. EN = 1                            <- recién ahí se energiza
3. esperar 5 s                       <- el recorrido, con la tarea BLOQUEADA
4. EN = 0
5. CTL = 0                           <- DESPUÉS de cortar
```

**El orden de 1 y 2 no es cosmético.** Energizar antes de fijar la dirección arranca el motor hacia
donde hubiera quedado `CTL` la vez anterior, y recién después lo hace corregir: un arranque en falso
por movimiento. Y el paso 5 va después del 4 por lo de siempre —dejar una señal en alto contra un
integrado sin alimentar es back-powering, la misma trampa del SPI de la microSD— con la ventaja de
que `CTL = 0` es además el estado de "cerrar", que es el seguro.

**⚠ El estado es una creencia, no una medición.** Nada en la placa dice si la válvula quedó abierta o
cerrada: lo que informa el driver es **el último comando que ejecutó**. Al arrancar la incógnita es
total, y el driver **asume ABIERTA** —el estado peligroso— para que la corrección tenga sentido: si de
verdad estaba abierta se cierra, y si ya estaba cerrada el comando es inofensivo porque el motor
empuja contra el tope. Al revés —asumir cerrada— dejaría una válvula abierta que el firmware cree
cerrada, que es el único desenlace realmente malo. `drv_valvula_estado_asumido()` distingue las dos
situaciones, igual que la firma del MCP79410 distingue una hora confiable de una inventada.

⏳ **Pero el movimiento de arranque NO lo manda el firmware de hoy** (decidido por Pablo el
2026-08-18). Estuvo escrito en `tkCmd` un rato y se sacó: **en qué condiciones conviene mover una
válvula al energizar el equipo es política de la capa de aplicación**, junto con el registro de
muestras y el watchdog. Mientras eso no esté definido, un cierre automático significaría 5 s de motor
en cada reset —incluidos los diez seguidos de una sesión de flasheo y los espurios que meta el
watchdog cuando exista—. La válvula se mueve sólo cuando alguien lo pide. **No adelantarlo.**

**Energía.** En reposo no consume nada: el load switch cortado deja al servo sin alimentar y los dos
pines quedan en 0 contra sus pull-down. Lo que se paga son los 5 s de motor, que van a ser **la
corriente más grande del equipo** y hay que medirlos. Durante esa ventana **el micro duerme en
Stop 2** —los GPIO conservan su estado—, así que el driver **no toma ningún candado de energía**, por
el mismo razonamiento que el INA3221 con su ventana de 1,4 s.

**⚠ Un jumper elige de qué riel come el servo: 12 V o 3,3 V.** El firmware anda igual en las dos
posiciones —lo único que ve es el `EN` del TPS22810— pero **cambia el modo de falla**:

| Jumper | Qué implica |
|---|---|
| **3,3 V** | El motor comparte el riel con el micro. El pico de arranque, y sobre todo un **atascamiento** del servo, se le descuentan a la misma fuente que alimenta al STM32; si la caída alcanza, lo resetea **a mitad de movimiento**, que es justo el estado indefinido que el driver no puede detectar |
| **12 V** | El motor queda aguas arriba del regulador, que absorbe el pico |

Es la misma clase de decisión que `JP13` en el contador de pulsos: no la resuelve el firmware. Lo que
hay que medir en banco es **la corriente de arranque y la de atascamiento**, no sólo la de régimen.

**Qué se portó de FWDLGX** (`ULIBS/toyi_valves.{c,h}`): la secuencia, que era correcta, y poco más. Lo
que cambió:

- el tiempo pasa de **10 s a 5 s** (dato nuevo de Pablo);
- `vTaskDelay( 10000 / portTICK_PERIOD_MS )` → `pdMS_TO_TICKS()`, obligatorio acá (ver la sección del
  tick);
- los accesos a `PORTC.OUT` bajan al driver, y el `t_valve_status valve_status` que aquel header
  **definía** —no declaraba— pasa a ser estático del `.c` con acceso por función: con `-fno-common`
  eso daba *multiple definition* apenas el header entrara en dos `.c`;
- aparece el **mutex**: dos movimientos solapados serían `CTL` cambiando con el motor energizado, o
  sea el servo invirtiendo el sentido a mitad de camino. El segundo en llegar recibe `false` en vez de
  encolarse, porque encolar movimientos de una válvula no significa nada.

### 🔨 El modem LTE — etapa 1 ✅ validada, etapa 2 (UART4) escrita y sin probar

`Application/drivers/drv_lte.{h,c}`, comando `lte`. **Las dos etapas quedaron validadas en banco el
2026-09-04**: la fuente con el riel ajustado a 3,8 V, y la UART4 hablando con el módulo. La sesión
completa que lo cerró:

```
cmd>lte on
energia del modem ENCENDIDA: EN_LTE_DCIN = 1 (PWRKEY sin tocar)
cmd>lte esc
secuencia de escape: +++ / a / a / +ok ...
MODO COMANDO. Ya acepta 'lte at AT+CSQ'
cmd>lte at AT
'AT' -> 9 bytes:
AT
OK
```

**Con esto todo el hardware de R001 tiene driver**, y el bring-up se termina.

Dos cosas que dice esa traza y conviene no perder:

- ✅ **El módulo arranca solo al recibir energía**: no se tocó `lte key` en ningún momento. O sea que
  **el `PWRKEY` queda para APAGAR y reiniciar, no para encender**, y eso simplifica la política de
  sesiones — una sesión abre con `drv_lte_power(true)` y nada más.
- **El eco está activo** (`AT+E`): los 9 bytes son `AT\r` de eco más `\r\nOK\r\n`. Hay que contar con
  él al parsear respuestas, o apagarlo.

⚠ **El módulo es un WH-LTE-7S1-E**, no el SIM7080G que se venía suponiendo. Ver la sección propia.

| Pin | Señal | Qué maneja |
|---|---|---|
| **PC13** | `EN_LTE_DCIN` | TPS22810, **EN=1 prende**: **toda** la energía del modem (topología en cascada) |
| **PA5** | `LTE_PWR` | transistor → el power switch del módulo (**invertido**) |
| **PA0** | `LTE_TXD` | `UART4_TX` — sale del micro |
| **PA1** | `LTE_RXD` | `UART4_RX` |
| ~~PA4~~ | ~~`EN_LTE_3V8`~~ | **sin función**: el `EN` del LMR33630 va atado a `VIN` |

**⚠ `LTE_PWR` está invertido por el transistor.** PA5 en 1 lo hace conducir y hunde el colector: el
módulo ve un **BAJO**, que es su nivel activo. El reposo es **PA5 = 0**, y no sólo por el nivel
lógico — con PA5 en 1 el transistor drena permanentemente la corriente del pull-up. Por eso el driver
expone `drv_lte_pwrkey( bApretado )` y no "poner PA5 en alto": si algún día cambia el circuito, lo
que hay que corregir es una línea y no todos los llamadores.

⏳ **La política de encendido NO vive acá** (Pablo, 2026-08-18): cuánto dura el pulso, cuántos
reintentos y cuándo se abre una sesión son de la capa de aplicación. El driver da las dos
herramientas para averiguar esos números en banco: `lte key on|off` pone el nivel a mano y
`lte key <ms>` hace un pulso cronometrado.

#### ✅ UART4 en el `.ioc` (Pablo, 2026-09-04)

Quedó así, y el árbol **compila limpio, sin un warning**:

```
Mcu.IP9=UART4                    NVIC.UART4_IRQn = true, prioridad 5
PA0 -> UART4_TX, label LTE_TXD   huart4: 115200 8N1, sin control de flujo
PA1 -> UART4_RX, label LTE_RXD   PA4 quedó SIN ASIGNAR (analógico, menor fuga)
```

Recordar la trampa de siempre después de regenerar: **Project → Refresh (F5) y Build**, porque una
fuente nueva no entra sola a `objects.list`.

#### La etapa 2: qué hace el firmware hoy

| Comando | Qué hace |
|---|---|
| `lte` | estado de los pines, errores de la UART4 y candados tomados |
| `lte on \| off` | la energía. Al prender toma `pwrLOCK_WAN` y limpia el buffer de RX |
| `lte key on\|off` | el nivel del power switch, a mano |
| `lte key <ms>` | un pulso cronometrado — así se averigua cuánto necesita el módulo |
| **`lte esc`** | **la secuencia de escape a modo comando** — sin esto el módulo no entiende AT |
| `lte at <cmd>` | manda `<cmd>` + CR y muestra la respuesta, delimitada por silencio |
| `lte tx <texto>` | manda el texto crudo, sin CR, y escucha |
| `lte rx <ms>` | sólo escucha |
| `lte bridge` | **puente terminal ↔ modem**, se sale con Ctrl-D |

**El puente es la herramienta que hace utilizable el bring-up**: `at` y `tx` toman una sola palabra
—el parser corta en los espacios— así que cualquier comando AT con espacios va por ahí. ⚠ Las dos
puntas corren a velocidades distintas, así que en una ráfaga larga la consola a 9600 pierde texto;
para leer sin perder nada están `at` y `rx`, que juntan primero y después imprimen.

La UART entró **sin una línea de código nuevo en `drv_uart`**, que es para lo que existe la tabla: una
fila con `&huart4`, su buffer de 512 bytes y su candado. `fdWAN` dejó de ser `NULL` en la tabla de
FRTOS-IO, así que `xfprintf( fdWAN, … )` ya funciona.

#### ⚠️ La topología de la placa VIEJA: las dos fuentes son INDEPENDIENTES

> ⛔ **Vale sólo para la placa actual, la del TPS62130 muerto.** El rediseño cerrado el 2026-08-20 pasa
> a **cascada** —el convertidor cuelga del load switch— y deja esta sección obsoleta. Se conserva
> porque es la placa que hay hoy en el banco y la que describe el `drv_lte.{h,c}` de este commit.
> Ver "El diseño nuevo" más abajo.

**`DCIN` no tiene nada que ver con el 3V8.** La entrada del TPS62130 son **los 12 V crudos de la
batería**, no el riel conmutado por el TPS22810. Son dos ramas paralelas colgadas del mismo origen:

```
bateria 12 V --+-- TPS22810 (EN_LTE_DCIN, PC13) --> DCIN del modem
               |
               +-- TPS62130 (EN_LTE_3V8,  PA4 ) --> riel de 3,8 V del modem
```

Tres consecuencias, y la segunda importa más de lo que parece:

1. **`lte 3v8 on` levanta el riel aunque DCIN esté apagado.** Los dos comandos son ortogonales.
2. ⚠ **El TPS62130 consume de la batería las 24 horas, aunque su `EN` esté en 0.** La hoja de datos da
   un shutdown current de **1,5 µA típico pero 25 µA máximo**. Contra un reposo de placa de 6 µA eso
   es entre "se nota" y "cuadruplica el consumo del equipo entero". **Hay que medirlo en esta placa**,
   no confiar en el típico: es el único número de esta etapa que puede obligar a cambiar el hardware.
3. El orden de `drv_lte_rieles()` —DCIN primero, 3V8 después, y al apagar al revés— **ya no es una
   necesidad eléctrica, es una convención**. Queda así hasta que la etapa 2 diga qué orden quiere el
   módulo entre su `VBAT` y su `DCIN`; si pide otro, se cambia sin consecuencias.

#### ⛔ El riel de 3V8 se RE-DISEÑA: se murieron DOS TPS62130 (2026-08-19)

**El chip no está fallando por diseño: se está muriendo.** Dos unidades en el banco. El diagnóstico
final del segundo fue inequívoco: `FB = 0,022 V` —o sea el lazo pidiendo todo lo que el chip pueda
dar— con `VOUT = 0`. Un regulador vivo y habilitado responde a eso subiendo la salida hasta llegar o
hasta su clamp de 7,4 V. Ese no respondía.

**Antes de eso se descartó, midiendo, todo lo demás**, y no hay que rehacerlo:

- **El montaje**: pad térmico a AGND/PGND, `AVIN`↔`PVIN`, `PVIN`↔12 V y `VOS`↔salida, las cuatro en
  0 Ω con la placa apagada. En un VQFN el pad **no es un disipador, es el retorno de masa**, y era el
  sospechoso número uno.
- **El divisor**: con `Vo = 1,9 / FB = 0,4` la relación daba 4,75, exactamente la posición de 3,8 V.
- **`SS/TR`**: 4,58 V flotando, muy por encima de la referencia.
- **La fuente de laboratorio**: sin límite de corriente.

**⚠ La firma que hay que reconocer**: el chip anda en vacío y se descompone con carga, con resultados
**distintos cada vez**. Cuando un circuito deja de ser repetible, el sospechoso ya no es el diseño.

**Por qué se murieron.** El máximo absoluto de `AVIN`/`PVIN` es **20 V** sobre un riel de 12: quedan
8 V de margen. Se evaluaron dos causas y quedó una:

1. ❌ **Enchufar la fuente de laboratorio con la placa conectada.** Era la hipótesis principal —cables
   inductivos contra un cerámico de entrada forman un tanque que repica a casi el doble— pero **se
   debilitó al saber que `12VRAIL` ya trae 1000 µF + 22 µF en la entrada de la placa** (Pablo,
   2026-08-20). Ese electrolítico amortigua el tanque. Aun así, **la regla de banco vale igual: no
   conectar ni desconectar los cables de la fuente con la salida habilitada**; se prende y se apaga
   con el interruptor.
2. ✅ **Girar el potenciómetro con el circuito encendido — el sospechoso que queda.** Cada pérdida de
   contacto del cursor deja `FB` abierto, y ahí *"the device clamps the output voltage at the VOS pin
   internally to approximately 7.4 V"* — contra un **máximo absoluto de 7 V en `VOS`**. La protección
   misma trabaja fuera de rango, y los dos chips murieron en sesiones de ajuste.

⚠ **Pero la conclusión NO es "nunca un pote": es que importa en qué pata está.** En aquel circuito
estaba **arriba** (`VOS`→`FB`), y ahí un cursor abierto manda el lazo a fondo. **Abajo** (`FB`→GND),
un cursor abierto deja `FB` siguiendo a `VOUT` y el lazo regula a la referencia —1,0 V en el
LMR33630—, que es inofensivo. El diseño nuevo lo tiene abajo a propósito.

#### ✅ El diseño nuevo: TPS22810 → LMR33630ADDA (esquemático cerrado el 2026-08-20)

Esquemático en **`Nueva fuente 3V8.png`** (la revisión previa, con los errores, quedó en
`Fuente de 3V8.png`). **Todavía no existe la placa**: hay que fabricarla y poblarla.

```
12VRAIL --[ TPS22810, EN_LTE_DCIN PC13 ]--> 12V_LTE_RAIL --+-- JP1 --> DCIN del modem (+ C1 470 µF)
                                                           |
                                                           +-- LMR33630ADDA --> JP2 --> 3V8RAIL
```

⚠ **La topología deja de ser dos ramas paralelas y pasa a ser CASCADA.** Esto **invalida la sección
"Las dos fuentes son INDEPENDIENTES"** de más arriba y su equivalente en `drv_lte.h`, que describen la
placa vieja. Ver el impacto en firmware al final.

**`JP1` y `JP2` son excluyentes**: el modem se alimenta **o** por `DCIN` a 12 V **o** por `3V8RAIL`,
nunca por los dos. Existen para poder intercambiar módulos sin cambiar componentes.

##### Las decisiones, y por qué

- **`EN` del LMR33630 va DIRECTO a `VIN`**, sin pasar por el micro. La hoja de datos lo bendice
  (*"Can be connected directly to VIN; **Do not float**"*) y resuelve dos problemas de un saque:
  1. ⛔ **El máximo absoluto de `EN` es `VIN + 0,3 V`.** Con `EN` en PA4 (3,3 V) y `VIN` colgando del
     load switch, `drv_lte_3v8(true)` con DCIN apagado ponía `EN` **3,3 V por encima de `VIN`** — una
     violación que el firmware podía provocar, y que el header viejo incluso invitaba a cometer al
     decir que los comandos eran "ortogonales".
  2. `EN` ya no flota durante el reset del micro, cuando PA4 está en alta impedancia.
  Se pierde el control independiente del 3V8, que con los jumpers excluyentes **no le sirve a nadie**:
  con `JP2` siempre se quiere el convertidor prendido, y con `JP1` queda al vicio costando su `IQ` sin
  conmutar (24 µA típico, 34 máximo) **sólo durante las sesiones**.
- **`PG` queda al aire y `R8` sale del BOM.** Se evaluó rutearlo a PA4 para que el firmware *sepa* en
  vez de creer, y **Pablo lo descartó con razón**: la señal no cambiaría ninguna decisión del equipo
  —si el modem no contesta en tres intentos la sesión falla igual— y el diagnóstico lo hace un técnico
  con un tester. Sólo compraba una línea de log más precisa.
  ⚠ **Si algún día se retoma: el pull-up va a `3V8RAIL`, nunca a `3V3RAIL` ni a `VCC`.** `VCC` son 5 V
  sobre un PA4 que no los tolera; y `3V3RAIL` es el único de los tres que **sobrevive a `VIN`**, así
  que violaría la misma llamada al pie que `EN` y drenaría ~26 µA permanentes por el clamp de `PG`
  —la trampa del pull-up de `SD_DET` otra vez—. `VOUT` y `VCC` no pueden exceder a `VIN` por
  construcción, y por eso son los dos que nombra la hoja de datos.
- **`L1 = 6,8 µH`** (era 22 µH). El WEBENCH devolvió 22 µH porque se le pidió un diseño de **1 A**,
  pero la hoja de datos advierte que *"for applications with much smaller maximum load than the
  maximum available from the device, **the maximum device current should be used**"*, y pone un
  **máximo** de inductancia: *"the minimum inductor ripple current must be no less than about **10% of
  the device maximum rated current**"* — con menos ripple el control por modo corriente deja de
  medir bien. Para 12→3,8 V a 400 kHz eso da un techo de **21,6 µH**: 22 µH es margen cero, y con la
  tolerancia del inductor (±20 %) y de `fSW` (340-460 kHz) queda afuera. La ventana es 2,7 a 21,6 µH
  y el valor de la tabla 9-2 de TI es 6,8. **`Isat` ≥ 4,1 A (`ILIMIT` máx), ideal ≥ 5,05 A (`ISC`
  máx)** — elegido **Bourns `SRP6540-6R8M`** (5,5 A, 49,5 mΩ, 4 mm de alto, ~US$ 0,58); alternativa
  sin discusión, Coilcraft `XAL6060-682MEC` (9,2 A, 20,8 mΩ, pero 6,1 mm y ~US$ 2). El DCR no decide
  acá: a 0,5 A la diferencia entre los dos son **7 mW sobre 1,9 W**.
- **`C29 = 1 µF` cerámico en `VIN` del TPS22810.** No existía. `12VRAIL` ya trae 1000 µF + 22 µF en la
  entrada de la placa —lo cual **sí** cumple la regla `CIN > CL` de la hoja de datos, que si no se
  viola trae *"current flow through the body diode from VOUT to VIN"*— pero **están lejos**, y a
  10 nH/cm de pista esa inductancia los desconecta de los transitorios rápidos. Bulk y cerámico local
  hacen trabajos distintos.
- **`C2` (`CT`) de 10 nF a 27 nF.** `SR = 46,62/CT`; con 10 nF la rampa es de 2,1 ms y cargar 480 µF
  pide **2,24 A**, por encima de los 2 A del SOT23-6. Con 27 nF: 5,5 ms y **0,84 A**.
- **`C1` (470 µF) pasa del lado de `DCIN`, después de `JP1`.** Así el componente queda siempre
  montado —que era el objetivo, poder intercambiar modems sin tocar el BOM— pero **el load switch sólo
  lo carga en la configuración `DCIN`**; con `JP2` carga apenas los 10 µF de `C28`. De yapa el
  capacitor queda directamente en la pata del modem, con el jumper aguas arriba y fuera del camino de
  los pulsos.
- **La realimentación**: `R10 = 100 kΩ` (el valor que recomienda TI) y `R11` un **pote de 100 kΩ con
  el cursor atado al extremo de GND** — así un cursor abierto degrada a una resistencia definida en
  vez de a un circuito abierto intermitente. Con `VREF = 1,0 V`, `Vout = 1 + 100k/R11`; como la
  resistencia efectiva es `R_cursor ∥ 100 k`, el recorrido útil es 0 a 50 kΩ y **los 3,8 V caen a
  ~56 % del giro (`R11` = 35,7 kΩ), con ~12 mV por grado**.
  ⚠ **El pote NO tiene tope hacia arriba**: girando de más `Vout` se va hacia `VIN`, y el `VBAT` del
  SIM7080G admite 4,8 V. **Se ajusta SIEMPRE con `JP2` abierto**, midiendo en `C27`; con el modem
  desconectado no hay nada en la placa que se pueda romper (`C27` es de 16 V y el chip admite `VOUT`
  hasta 24 V). Se propuso una fija de 33 kΩ en serie para poner el techo en 4,03 V por construcción y
  **Pablo la descartó**: es un prototipo, el ajuste es con `JP2` abierto, y **en producción el pote se
  reemplaza por una fija** — que va a caer cerca de **35,7 kΩ**, que existe en E96.
- **`C27` = 100 µF cerámico 16 V, más un 100 nF en paralelo.** El valor sobra: la ecuación 6 con
  `ΔIOUT` = 0,5 A y `ΔVOUT` ≤ 250 mV pide ~13 µF. El 100 nF va porque TI lo pide aparte, *"reducing
  voltage spikes on the output caused by inductor and board parasitics"*. Repartirlo en 4 × 22 µF es
  **opcional**: el pico del modem lo absorben los 2 × 100 µF que SIMCom exige en el propio `VBAT`, del
  otro lado de `JP2`. ⚠ Techo de TI: *"maximum total output capacitance… 10 times the design value, or
  1000 µF, whichever is smaller"* — con los del módulo se llega a ~300 µF y ya no sobra tanto.
  Conviene que el cable entre `JP2` y el modem sea corto: `C27`, la inductancia del cable y los 200 µF
  del módulo forman un tanque de decenas de kHz, cerca del ancho de banda del lazo.
- **`C28` = 10 µF y `C6` = 220 nF** en `VIN` del LMR33630 son exactamente lo que pide la hoja de datos.

⏳ **Pendiente de BOM**: confirmar tensiones de los cerámicos — `C29` 50 V, `C28` **≥ 25 V, ideal
50 V** (*"rated for at least the maximum input voltage; preferably twice"*), `C6` 50 V X7R, `C4`
≥ 10 V. `C27` ya confirmado en 16 V.
⏳ **Opcional, cuando se toque la entrada de la placa**: un **TVS (SMAJ18A) junto a los 1000 µF**.
Bajó de prioridad al descartarse el repique como causa, pero **el TPS22810 tiene el mismo máximo
absoluto de 20 V que el chip que murió** y es lo más expuesto de la placa: cuelga de la batería las
24 horas. **Un load switch no es un dispositivo de protección, es un interruptor.**

##### ✅ La placa nueva existe (2026-09-02), y el micro ya está validado sobre ella

Pablo la fabricó y la pobló **parcialmente y a propósito**: tiene el **micro**, el **LED**, la
**terminal serial**, el **conector de programación** y **la fuente del modem**. Nada más. Es la misma
metodología de siempre: se estrena el mínimo que permite decir "esto anda" antes de mirar lo que se
vino a probar.

Validado en banco ese mismo día, en tres pasos:

| Paso | Estado |
|---|---|
| Se programa, y el micro corre | ✅ con `PRUEBA_MINIMA` — ver la sección del programador |
| La consola TERM | ✅ el banner sale limpio, o sea que el PLL arrancó y el divisor está bien |
| **El firmware completo** (`PRUEBA_MINIMA = 0`) | ✅ **anda correcto** — con esto quedan validados el **cristal/LSE**, el RTC, el LPTIM1, FreeRTOS y el tickless sobre la placa nueva |

⏳ **Lo que NO se probó todavía es justamente la fuente del modem.** El plan de banco, en este orden:
`JP1` y `JP2` **abiertos** → `lte dcin on` → medir en **`C27`** y ajustar el pote a **3,8 V**
(~12 mV por grado, y **no tiene tope hacia arriba**) → el mismo nodo en **VCA** → arrancar con carga →
**el reposo, que tiene que seguir en 6 µA** → recién ahí `JP2` y el modem.

##### ✅ La fuente ANDA (2026-09-04) y el firmware ya está limpio

Pablo la ajustó y quedó en **3,8 V**. Con eso se cerró la etapa 1 y se hizo la limpieza que estaba
anotada desde el 2026-08-20:

1. ✅ **`EN_LTE_3V8` (PA4) desapareció del driver**: `drv_lte_3v8()` y `drv_lte_rieles()` ya no
   existen, y `DRV_LTE_MS_ENTRE_RIELES` tampoco. Queda **`drv_lte_power()`**, que es el único
   interruptor. En la consola, `lte 3v8` se fue y `lte dcin on|off` pasó a ser **`lte on|off`**.
   ⏳ **Falta el `.ioc`**: PA4 sigue asignado como `EN_LTE_3V8`. Conviene sacarlo cuando se toque
   CubeMX —un pin no asignado queda en analógico tras el reset, que es el de menor fuga—.
2. ✅ **`drv_lte.h` dejó de decir que las dos fuentes son independientes**, y explica por qué el
   diseño viejo dejaba que el firmware violara el máximo absoluto de `EN` (`VIN + 0,3 V`).
3. ⚠ **Cortar la energía ahora mata también la alimentación del módulo.** Sigue vigente: la política
   de sesiones tiene que apagar el módulo por su power switch o por AT **antes** de bajar
   `EN_LTE_DCIN`. El driver deja hacerlo en cualquier orden a propósito — el orden correcto es de la
   capa de aplicación.
4. El `QOD` con `R1` = 330 Ω tarda **~0,5 s** en descargar los 470 µF en configuración `DCIN`
   (τ = 155 ms) y ~3 ms en configuración 3V8. Importa si la aplicación hace un ciclo de energía para
   resetear el módulo: cortar y reponer enseguida no resetea nada.
5. **Nota para la capa de aplicación**: llenar `C1` son 5,64 mC, y si el origen no aportara nada
   `12VRAIL` caería a 8,2 V por milisegundos. No es brownout —la fuente de 3V3 sigue trabajando— pero
   **no hay que medir `vin` ni los 4-20 mA en el instante de encender el modem**.

#### ⚠ PC13 no es un GPIO cualquiera

Está en el dominio de backup, alimentado a través del power switch. El **DS11585** es explícito:

> *PC13, PC14 and PC15 are supplied through the power switch. Since the switch only sinks a limited
> amount of current (3 mA), the use of GPIOs PC13 to PC15 in output mode is limited: the speed should
> not exceed 2 MHz with a maximum load of 30 pF. These GPIOs must not be used as current sources.*

Para el `EN` de un TPS22810 está perfecto —entrada de alta impedancia, ~1 µA, señal continua— pero **el
día que alguien quiera colgar otra cosa de PC13, la respuesta puede ser distinta**.

Y hay un segundo detalle, bastante menos obvio:

> *After a Backup domain power-up, PC13, PC14 and PC15 operate as GPIOs. Their function then depends
> on the content of the RTC registers, **which are not reset by the system reset**.*

O sea que si alguna vez se habilita la salida del RTC —`RTC_OUT`, la calibración, `TAMP1`, `WKUP2`—
**PC13 deja de ser GPIO y pasa a manejarlo el RTC**, y esa configuración **sobrevive al reset del
sistema**. El síntoma sería un modem que se prende o se apaga solo sin que ninguna línea de código lo
toque, y no habría forma de encontrarlo mirando el firmware. Hoy está limpio
(`hrtc.Init.OutPut = RTC_OUTPUT_DISABLE`, `main.c:529`); **no tocarlo al reconfigurar el RTC.**

#### ⚠ El módulo es un WH-LTE-7S1-E, NO un SIM7080G (cambio del 2026-09-04)

Decisión de Pablo: *"Por ahora NO vamos a usar el modem SIMCOM. Seguimos usando el WH-LTE-7S1-E."*
El SIM7080G se evaluó mientras se rediseñaba la fuente y quedó afuera; **lo que sigue valiendo de
aquel análisis es sólo lo de la fuente** —los 3,8 V, el pico de 0,5 A y el desacople— porque eso fue
lo que dimensionó el LMR33630 y la placa ya está fabricada así.

Es un **LTE Cat-1 DTU de USR IOT**: serie a LTE. Del lado del micro es una UART y nada más — se lo
configura con comandos AT y después transporta bytes en modo transparente (TCP/UDP/HTTPD/SMS, hasta 4
sockets). Eso simplifica mucho la etapa 2 respecto de un módulo crudo: **no hay pila de red que
manejar desde el firmware**.

Documentación en **`Datasheets/Componentes/PUSR/`**: manual de hardware, manual de usuario, el juego
de comandos AT y un `Comandos AT modem USR.txt` propio.

| Parámetro | Valor |
|---|---|
| Alimentación | **5-16 V por `DCIN`** (pines 13/14, típico 12 V) **o 3,4-4,2 V por `VCAP`** (pin 16, recomendado 3,8 V) |
| **UART** | **TTL-3,0 V**, `AT+comando`. Baudrates de 1200 a 921600 — **acá va a 115200** |
| `PWRKEY` (pin 10) | *"Power pin, **pull up by default**"*, nivel activo **BAJO** |
| Bandas | LTE FDD B1/B3/B7/B8/B20/B28 + GSM 900/1800 |
| Indicadores | `WORK` (pin 9, parpadea 1 s), `NET`, `LINKA`, `LINKB` — salidas de 3,0 V, **no cableadas en R001** |

**⚠ Tres cosas del manual que hay que tener presentes:**

1. ⚠ **La UART del módulo es de 3,0 V, no de 3,3.** Textual: *"When the I/O of the user's MCU is not
   3.0V, **level matching is needed**"*, con un circuito de referencia a transistores. Los dos
   sentidos no son igual de riesgosos: **modem → micro anda sin adaptación** (los 3,0 V superan
   cómodo el `VIH` del STM32, que es 0,7 × 3,3 = 2,31 V), pero **micro → modem le entra 0,3 V por
   encima de su dominio**. Muchos módulos lo toleran; el fabricante pide adaptación. **Verificarlo en
   la placa** — si el módulo no contesta o contesta basura, ése es el primer sospechoso, antes que el
   firmware.
2. ✅ **`JP1`/`JP2` excluyentes no es una convención de R001: el módulo lo exige.** Sobre `VCAP`:
   *"**Can not use with DCIN simultaneously**"*. La placa implementa exactamente eso.
3. ⚠ **El desacople de la rama 3V8 queda corto.** Para `DCIN` el fabricante pide 220 µF y la placa
   pone `C1` = 470 µF, de sobra. Para `VCAP` su circuito de referencia son **470 + 220 + 22 µF** más
   los cerámicos, y del lado de `JP2` sólo hay `C27` = 100 µF + 100 nF. Es un **Cat-1**, que transmite
   con picos bastante más grandes que el Cat-M1 que se había supuesto. Si en configuración 3V8 el
   módulo se resetea o se cae de la red al transmitir, **el sospechoso es el bulk, no el firmware**.

#### ⚠ El modem arranca en modo TRANSPARENTE: `+++` solo no lo pasa a AT

Encontrado el **2026-09-04**, en la primera prueba con el módulo. Pablo probó `lte tx +++AT` y no
pasó nada — y es el error natural, porque en cualquier otro módulo `+++` alcanza. **Acá la secuencia
es de TRES tiempos**, y el módulo pide una confirmación en el medio (traza real en
`Datasheets/Componentes/PUSR/Comandos AT modem USR.txt`):

```
micro -> "+++"     (SIN CR: es una contraseña, no un comando)
modem -> "a"
micro -> "a"       (sin CR)
modem -> "+ok"     <- recién a partir de acá acepta AT
```

**El segundo tramo es lo que hace que a mano no se pueda**: entre la `a` que contesta el módulo y la
`a` que hay que devolverle no da el tiempo de tipear un comando. Por eso está implementado como
**`drv_lte_escape()` / `lte esc`** y no como una receta. Para volver a modo transparente, `AT+ENTM`.

La `+++` es en realidad la **contraseña de comando**, configurable con `AT+CMDPW` (1 a 10 bytes) y
`+++` de fábrica. ⚠ **Si alguien la cambia, el síntoma es indistinguible de un TX roto.**

⏳ Los tiempos **no están en el manual**; las constantes de `drv_lte.h` son un punto de partida
generoso, a ajustar en banco.

**El valor de retorno es un diagnóstico y en el bring-up vale más que el éxito**, porque separa el
único par de hipótesis que desde afuera se ven iguales:

| Resultado | Qué prueba |
|---|---|
| `lteESC_SIN_A` | **No dice nada del TX**: puede no estar llegando, o el módulo puede no estar en transparente, o la contraseña ser otra |
| `lteESC_SIN_OK` | ⭐ **PRUEBA que el TX funciona** — el módulo contestó a algo que le mandamos. La falla está en el segundo tramo, no en el enlace |
| `lteESC_OK` | Modo comando |

Es el mismo criterio que el CTR del optoacoplador: buscar la **medición que cierra el diagnóstico**
en vez de acumular síntomas.

⏳ **Lo que el manual NO dice: cuánto dura el pulso del `PWRKEY`.** Sí dice que *"POWER_KEY and RESET
have the same function to control the power on and off"*, y para `RESET` da **0,5 s**. O sea que 0,5 s
es **por dónde empezar a probar, no un dato del `PWRKEY`** — por eso `lte key <ms>` recibe la duración
por argumento y no hay constante que la fije. Tampoco está cuánto tarda en contestar un AT desde que
se lo alimenta; eso va a fijar el timeout de arranque de una sesión y hay que medirlo.

⚠ **La regla de no cortarle la alimentación a un módulo que está corriendo vale igual**, aunque acá
no haya un párrafo del fabricante que la respalde: es la forma conocida de corromperle la flash
interna a cualquiera de estos módulos. `drv_lte_power( false )` **no es una operación inocente**, y la
política de sesiones tiene que apagar primero por el power switch o por AT.

#### El consumo de reposo: 354 µA, y queda para después

Medido por Pablo el **2026-09-04**, con la placa nueva ya poblada entera:

| Configuración | Reposo |
|---|---|
| Placa nueva, sólo micro + LED + terminal | **14 µA** |
| **Placa nueva, todos los componentes** | **354 µA** |
| Placa original, todos los componentes, mismo firmware | **40 µA** |

O sea que **el criterio de aceptación de la etapa no se cumplió**: se esperaba que el reposo no se
moviera de los ~6 µA de `v0.0.13`, y la placa nueva completa consume ~9 veces lo que la vieja. Los
340 µA aparecen al poblar el resto de los componentes, no con el micro solo.

⏸ **Pablo decidió dejarlo afuera por ahora** y seguir con el firmware. Queda anotado como pendiente
de hardware, y hay que retomarlo antes de campo: 354 µA contra 40 µA es un factor de 9 en la
autonomía del equipo.

Cuando se retome, lo que ya se sabe acota la búsqueda:

- **No es el firmware**: el mismo binario da 40 µA en la placa original.
- **No es el micro ni la terminal**: con eso solo la placa nueva da 14 µA, *mejor* que la vieja.
- **Es algo que se agregó al poblarla.** El sospechoso natural es el que ya mordió una vez: un
  **pull-up interno contra un contacto cerrado**, como los 82 µA de `SD_DET` (ver la sección de la
  microSD). El método que corresponde es el de siempre — **despoblar o levantar de a un componente y
  medir**, o alimentar los rieles de a uno.
- ⚠ **No es la fuente del modem**: con el convertidor detrás del TPS22810, lo único permanente son
  los **500 nA** del load switch. Eso era el sospechoso número uno del diseño viejo y el rediseño lo
  eliminó.

El driver del modem **sí toma candado de energía**, y a diferencia de lo que decía la etapa 1, es por
**correctitud y no por consumo**: mientras el módulo está alimentado puede hablar sin que nadie le
pregunte, y a 115200 la ventana de `__disable_irq()` del tickless (~100 µs) es más larga que un byte
entero (87 µs). Ver la sección del tickless comiéndose bytes del USART, que es el mismo mecanismo a
9600. El costo es que con el modem prendido la placa no baja de Sleep y consume ~3,5 mA — al lado de
lo que come el modem, es ruido.

## 🔨 FASE 2: la aplicación

Cerrada la fase 1 (`v0.0.14`, todo el hardware con driver), empieza el port de la **aplicación** desde
**FWDLGX 2.0.12** (AVR128DA64), que es el firmware **en producción**. El árbol vive en
`~/Spymovil/Dataloggers/FWDLGX.X/` — `SRC/TASKS/` y `SRC/XLIBS/` son las partes que interesan.

### ⭐ El frame es el contrato

Pablo puso una sola condición dura: *"Es importante mantener el formato de los frames de datos ya que
el servidor debe poder entendernos"*. **El servidor no se toca**, así que el protocolo no es una
decisión de diseño: es un dato. Está relevado completo en la memoria del proyecto
(`fwdlgx-protocolo-frames.md`); el resumen:

- Transporte **HTTP GET** con query string (`AT+WKMOD=HTTPD`), respuestas **envueltas en HTML**.
- Prefijo de todo frame: `ID=<imei>&HW=..&TYPE=..&VER=..&CLASS=..`. En el equipo nuevo:
  **`HW="SPQ_ARM_R1"`, `TYPE="FWDLGARM"`, `VER=` la versión del firmware** (Pablo, 2026-09-07;
  el `TYPE` corregido el 2026-09-11: **va sin la revisión de placa**, igual que `FWDLGX` en el AVR).
- Frame de datos: `CLASS=DATA` + `DATE`/`TIME` + un campo por canal **habilitado**, con **el nombre que
  dice la configuración** + `V0` (válvula) + `bt3v3` + `bt12v`. Precisiones distintas y deliberadas:
  analógicas `%0.2f`, contador y modbus `%0.3f`.
- Secuencia: `PING`→`PONG`, `CONF_ALL` con **seis hashes** (uno por bloque), los `CONF_*` que el
  servidor pida, y recién ahí los datos.

### Paso 2: `dataRcd` y `tkSys`

`Application/tasks/tkSys.{h,c}`, comando `poll`. La tarea despierta cada `timerpoll` segundos, polea
todo lo configurado y deja un `dataRcd_t` — **mismos campos y mismo orden que el `dataRcd_s` del
AVR**, porque de ahí sale el frame.

**El comando `poll` llama a la MISMA función que la tarea** (`tkSys_poll()`), no a una copia de
prueba: si fueran dos caminos, lo que se valida a mano dejaría de ser lo que hace el equipo solo.

**`vTaskDelayUntil` y no `vTaskDelay`**: el período se cuenta desde el despertar anterior, así que lo
que tarde el poleo no se acumula. Con `vTaskDelay` los registros se irían corriendo de los minutos
redondos a lo largo del día.

#### ⚠ El registro está incompleto a propósito, y lo DICE

Dos campos todavía no se pueden medir. En vez de rellenar con ceros hay un **bitmask `usInvalidos`**,
y la consola imprime `SIN_DATO`:

| Campo | Por qué |
|---|---|
| ~~Las 3 analógicas~~ | ✅ **resuelto el 2026-09-09**: Pablo cambió el INA3221 y el poleo ya las lee |
| El contador | Ver el paso 2b, abajo |

**Un canal que no se pudo medir y otro que midió cero se ven idénticos en un float.** En un
datalogger eso es lo peor que puede pasar: un dato plausible y falso se mezcla con los buenos y
después no hay forma de separarlos. Es el mismo criterio que la firma en la SRAM del MCP79410 para la
hora, o el `estado_asumido` de la válvula.

⏳ **Qué hace el frame con un campo inválido se decide en el paso 3**: lo natural es no emitirlo, pero
eso lo tiene que aceptar el servidor.

#### ⚠ Dos hallazgos del banco (2026-09-08)

**1. La hora puede ser inválida aunque el chip conteste.** El primer poleo estampó `01/01/01
00:42:37` y el registro lo daba por bueno: el MCP79410 había arrancado frío y devolvía su fecha en
blanco con toda naturalidad. `tkSys_poll()` no estaba consultando **`drv_rtc_validez()`**, que existe
justamente para eso y **no es una heurística sobre la fecha** —mira la firma en la SRAM del chip, que
se alimenta de la misma pila que el contador de tiempo—. Corregido: la hora se conserva en el
registro para diagnóstico, pero marcada `dataINVALIDO_RTC`, y la consola lo dice con un
`(HORA NO CONFIABLE)` pegado al timestamp.

Es exactamente el escenario contra el que se diseñó la firma, y quedó demostrado que sin engancharla
el equipo estampa `2001-01-01` sin que nada lo delate.

**2. El nombre del canal ES la clave del campo en el frame, y no había nada que impidiera
repetirlo.** En banco aparecieron dos campos `CAU0=` en la misma línea: el contador y el canal Modbus
0 estaban los dos habilitados y los dos se llamaban `CAU0`. En el frame eso emite dos campos iguales
y **el servidor se queda con uno de los dos** — un dato se pierde en silencio.

Y no es un descuido raro: es la configuración natural cuando el caudal puede venir **por pulsos o por
Modbus**, que son alternativas y no cosas simultáneas. El propio FWDLGX trae los dos ejemplos con ese
mismo nombre. Ahora `cfg_nvm_chequear_nombres()` los detecta y los informa en `config` y en
`config save`. **Avisa, no impide**: qué canales habilitar es decisión del operador, y un equipo que
se niega a guardar en medio de una instalación es peor que uno que advierte.

#### ✅ Las analógicas entraron al poleo (2026-09-09)

Pablo **cambió el INA3221** —el viejo no contestaba en el I2C— y con eso se destrabó lo único que
faltaba del paso 2. Tres cosas del enganche que no son obvias:

**⚠ 1. El canal del INA NO es el número de la entrada: el mapeo está invertido.**

| Entrada | Canal del chip |
|---|---|
| `a0` | `inaCH3` |
| `a1` | `inaCH2` |
| `a2` | `inaCH1` |

Sale de `ainputs_read_channel_raw()` de FWDLGX, que hace exactamente ese switch. **Depende del
cableado de la placa, no del chip**, y por eso la tabla vive en `tkSys.c` y no adentro del driver
—que habla de `inaCH1..3`, que son los del integrado—. ✅ **Confirmado por Pablo el 2026-09-09**: en
R001 la asignación es la misma que en el AVR. No "corregirla" por parecer al revés.

**2. El `sensors_pwr_settle_time` es un asentamiento EXTRA, no un reemplazo.** Existe para los
sensores lentos —los de ultrasonido de Dica— que necesitan mucho más que los 500 ms por omisión. Se
aplica **encendiendo el riel desde `tkSys` y esperando**; `drv_ina_medir()` ve que ya está encendido y
no vuelve a pagar su propio asentamiento, que es justo para lo que ese camino existe en el driver. Así
el tiempo configurable sale **sin tocar código validado**.

**3. La conversión reproduce la del AVR, incluidos sus dos casos de borde**, porque de ahí sale el
número que viaja en el frame (`cfg_ainputs_convertir()`). ✅ **Validada en banco el 2026-09-09**
contra corriente inyectada:

- **`|magnitud| < 0,01` se fuerza a 0,0.** No es cosmético: sin eso, un cero medido con un pelo de
  ruido negativo se imprime **`-0.00`** con dos decimales, y del lado del servidor eso es un valor
  distinto de `0.00`. El comentario del AVR dice lo mismo.
- **`imax == imin` devuelve -999.0**, el centinela que el AVR usa para "la configuración no permite
  convertir". Es **otro** número que el **-9999** de `wan_frame.h`, que significa "no se pudo medir":
  se conservan los dos porque el servidor ya conoce el primero.

⚠ **Una falla del I2C invalida los TRES canales, no uno.** `drv_ina_medir()` informa un solo
resultado para el barrido completo, así que no hay forma de saber cuál se leyó bien; marcar sólo
alguno sería inventar.

Y una diferencia deliberada con el AVR que ya estaba documentada: **el corrimiento del registro de
shunt va con extensión de signo**. Aquel hacía `>> 3` sobre un `uint16_t`, así que una corriente
negativa —lazo abierto, sensor al revés— salía como un número enorme y positivo.

#### ⏳ Paso 2b: el caudal, y por qué va aparte

Al portar el contador apareció algo que cambia el alcance: **el `modo_medida` NO cambia lo que se
transmite.** `dr->contador` lleva **siempre** `cnt.caudal`; `PULSOS` sólo cambia cómo se imprime en la
consola (entero en vez de 3 decimales). O sea que no hay una versión "simple" del contador que
transmita pulsos: **el dato que viaja es el caudal, o no hay dato**.

Y ese caudal, en el AVR, sale de un **EMA calculado en la ISR por cada pulso**, con:

- decay a cero por silencio prolongado (`dT_zero_ms`, derivado de `magpp`),
- clamp de slew-rate al ±10 % del caudal anterior,
- arranque con alpha variable durante los primeros pulsos,
- promediado de los EMA acumulados entre poleos,
- descarte por debajo de `CAUDAL_MIN_M3H`.

Son ~600 líneas afinadas en campo, con un simulador de pulsos en Python y escenarios E1…E10
(`FWDLGX.X/SRC/DOCS/`). **Y toca la ISR de `drv_pulsos`**, que hoy sólo cuenta y no lleva timestamps
por pulso. Es un paso propio, con su diseño y su validación contra ese simulador — no un detalle del
paso 2.

#### ✅ De paso: la sintaxis de `config counter` estaba mal

Al leer `counter_config_channel()` del AVR apareció que el orden real es
**`<enable> <name> <magpp> <modo> <qmax> <alpha>`** —el modo va **tercero**— y que **`alpha` es
configurable**. Estaba puesto con el modo al final y sin alpha. Importa por dos razones: Pablo pidió
la misma sintaxis que el AVR para no reeducar a los técnicos, y **`alpha` viaja en el hash**: sin
poder fijarlo quedaría siempre en 0.25 y el hash no coincidiría con el de un equipo afinado distinto.

### Paso 3: el frame

`Application/tasks/wan_frame.{h,c}`, comando `frame`. Es la serialización de un `dataRcd_t` en el
formato que espera el servidor, portada de `wan_load_dr_in_txbuffer()`. **Sin modem, sin red.**

⭐ **Validado contra el AVR sin hardware**: se compiló la implementación nueva para el host junto con
una transcripción literal de la del AVR y se compararon los frames en tres configuraciones —la de los
ejemplos del servidor, la misma con `DATANR`, y los **9 canales habilitados**—. **Idénticos los
tres.** El de 9 canales da 212 bytes, lo que confirma de paso por qué el AVR desbordaba: allá el
buffer era de 255 y con nombres largos no alcanza. Acá son 512.

```
ID=000000000000000&HW=SPQ_ARM_R1&TYPE=FWDLGARM&VER=0.0.21&CLASS=DATA&DATE=260908&TIME=140533
&pA=3.14&pB=7.50&CAU0=12.345&PRE1=4.200&V0=1&bt3v3=3.281&bt12v=12.150
```

#### ⚠ El frame NO se imprime con `xprintf`

`xprintf` formatea en un buffer estático de **160 bytes** (`XPRINTF_BUFFER_SIZE`) y **el frame es más
largo**: 174 en el primer intento de banco, hasta ~350 con los 9 canales y nombres largos. Pasarlo por
ahí lo **truncaba en silencio**, y el síntoma confundía: se veía un frame cortado a mitad de campo
mientras el contador informaba el largo correcto, así que la sospecha caía sobre el frame en vez de
sobre la impresión.

Va con `frtos_write( fdTERM, … )` directo: el frame ya es una cadena terminada, no necesita formateo.
**Vale para cualquier cosa larga que se quiera sacar por consola.**

#### ⚠ La firma del RTC certifica CONTINUIDAD, no CORRECCIÓN

Encontrado en banco el **2026-09-08**: el equipo informó `01/01/01` **con la firma intacta y el
oscilador corriendo**, o sea que `drv_rtc_validez()` devolvió `rtcHORA_VALIDA` — correctamente, según
lo que ese mecanismo puede saber.

Y es una limitación de fondo, no un bug: **la firma se escribe al fijar la hora**, así que certifica
que el reloj no se detuvo desde entonces. Si alguna vez se fijó una hora equivocada, la firma la
certifica igual y el reloj viene contando sin interrupciones desde 2001.

El refuerzo que se agregó **no es una heurística arbitraria** del tipo "el año parece viejo": **una
muestra no puede ser anterior a la compilación del firmware que la tomó**. Si el año del RTC es menor
que el año de `__DATE__`, la hora es *imposible*, no improbable — y eso no depende de ninguna
constante que envejezca.

```c
#define TKSYS_ANIO_COMPILACION  ( ( __DATE__[9] - '0' ) * 10 + ( __DATE__[10] - '0' ) )
```

Los dos chequeos se complementan: la firma detecta el arranque frío (que el año no delata, porque el
chip podría arrancar con una fecha plausible), y el año detecta la hora mal fijada (que la firma no
puede ver).

#### El centinela -9999 (decisión de Pablo, 2026-09-08)

Un campo que no se pudo medir viaja como **-9999**, no se omite. El razonamiento es de Pablo:
*"si no lo transmitimos el servidor no lo detecta"*. Un campo **ausente** se confunde con un canal
deshabilitado y pasa desapercibido; un **-9999** salta a la vista, y como las magnitudes que mide el
equipo son positivas, no puede confundirse con una medida real.

Es el mismo criterio que el `SIN_DATO` de la consola, la firma del MCP79410 y el `estado_asumido` de
la válvula: **hacer visible lo que no se sabe**.

⚠ **La hora es la excepción**: no es un campo numérico, así que si el reloj arrancó frío viaja su
fecha tal cual (`DATE=010101`), que del lado del servidor es igual de detectable.

#### ⏳ El IMEI es falso en esta etapa

`wan_imei()` devuelve **15 ceros**. Es provisorio y acordado con Pablo: esta etapa no incluye el
modem. Lo definitivo, para el paso 5: **tkWAN prende el modem al arrancar, pregunta `AT+IMEI?` y lo
cachea**; queda fijado aunque el modem se apague después, así que el frame nunca necesita el modem
encendido para armarse. Sólo hay que cambiar lo que devuelve esa función.

⚠ **Los 15 ceros son a propósito y no un placeholder cualquiera**: es sintácticamente un IMEI —no
rompe el parseo del servidor— pero **ningún equipo real lo tiene**, así que un frame de prueba que
llegara por error a producción sería rechazado como equipo desconocido en vez de mezclarse con los
datos de un datalogger que existe.

### Paso 4: el almacén de registros

`Application/tasks/fs_datos.{h,c}`, comando `fs`. Buffer circular sobre la EEPROM, portado de
`fileSystem.c`. **1984 registros de 64 bytes** en `0x01000..0x1FFFF` — casi el doble que el AVR, que
usaba la EEPROM entera porque su configuración vivía en la NVM interna del micro.

Con `timerpoll` de 5 minutos son **casi 7 días** de autonomía sin transmitir.

#### ⚠ La FAT va en la SRAM del MCP79410, y es por VIDA ÚTIL

Los datos se reparten sobre 1984 posiciones, así que cada celda se reescribe una vez por semana —unos
500 ciclos en 10 años sobre un chip que aguanta millones—. Pero **la FAT se actualiza en cada
registro**: en la EEPROM serían más de un millón de escrituras sobre las mismas celdas. La SRAM del
RTC no tiene límite de ciclos y ya está respaldada por la pila. Es lo que hace el AVR
(`RTC_write( FAT_ADDRESS, … )`) y encaja con el área que `drv_rtc79410` reserva después de la firma.

⚠ **El riesgo que eso trae no es teórico en este equipo**: si se pierde la pila se pierde la FAT, y
con ella la referencia a todos los datos. **El porta pila falla de forma intermitente** —de cuatro
cortes aguantó tres—. Pablo decidió mantener el diseño del AVR; la defensa que sí se puso es barata:
la FAT **se valida al leerla** —checksum *y* coherencia de punteros— y si no cierra se formatea
avisando, en vez de operar con punteros basura y pisar la configuración, que vive justo antes en la
misma EEPROM. Los registros llevan tag `0xC5`, así que el día que haga falta se puede reconstruir.

#### ⚠ El checksum se calcula con `offsetof`, NO con `sizeof - 1`

Bug encontrado en banco el **2026-09-09**, y vale como patrón porque el síntoma acusaba al
componente equivocado.

`fs_fat_t` son cuatro `uint16_t` y un `uint8_t` de checksum: **9 bytes de contenido, pero `sizeof`
daba 10** porque el compilador alinea el final a 2. O sea que el checksum vivía en el byte 8 y el
**byte 9 era relleno**. El código calculaba sobre `sizeof - 1` = 9 bytes, y eso **incluye al propio
checksum en el rango**: al grabar entraba su valor viejo y al releer el nuevo, así que **nunca
coincidían**.

El efecto era exacto y silencioso: **la FAT se declaraba inválida en cada arranque**, el equipo
formateaba y perdía todos los registros guardados. Y el mensaje decía
`(se perdio el respaldo del RTC?)`, **culpando a la pila del MCP79410, que estaba perfecta**.

⭐ **Lo cazó Pablo con el argumento correcto**: *"el RTC no se perdió así que no parece haber habido
un problema de la batería"*. Un diagnóstico que acusa al componente equivocado cuesta más que no
tener diagnóstico — la hora había sobrevivido al corte, así que la SRAM también.

**La regla**: el checksum cubre **todo lo que hay antes del campo checksum**, y eso se escribe
`offsetof( tipo, ucChecksum )`. `sizeof - 1` parece lo mismo y sólo coincide cuando el campo es
literalmente el último byte — o sea cuando no hay relleno al final, que es una propiedad del
compilador y no del diseño.

⚠ **En `cfg_nvm.c` el mismo patrón NO es un bug pero sí es frágil**: ahí se escribe y se lee en
`pucData[ usSize - 1 ]`, que también es relleno, pero **el mismo byte en los dos lados**, así que
cierra. El precio es que el campo `ucChecksum` de esas structs no se usa y **el checksum real vive en
el padding**: si alguien agrega un campo y el relleno cambia de tamaño, se mueve de lugar y las
configuraciones guardadas dejan de validar. No se tocó para no invalidar lo ya grabado, pero es lo
primero a corregir si alguna vez hay que cambiar esas structs.

#### Circular de verdad: el nuevo pisa al más viejo

⚠ **Acá se cambió a propósito el comportamiento del AVR** (decisión de Pablo, 2026-09-08). Aquel dice
"ringbuffer" en el comentario pero **no lo es**: al llenarse rechaza el registro nuevo
(`ERROR: FS full`), o sea que conserva lo viejo y **pierde lo que está pasando**. Con el modem sin
señal una semana, el equipo dejaría de registrar justo cuando más importa.

Acá el nuevo pisa al más viejo, y **se avisa la primera vez**: que se empiece a pisar es información
de campo — dice que el equipo lleva demasiado tiempo sin poder transmitir.

#### Leer y borrar son operaciones separadas

`fs_datos_peek()` mira sin consumir, `fs_datos_pop()` descarta. Un registro **se borra recién cuando
el servidor confirmó que lo recibió**; si fueran una sola operación, cada sesión cortada se llevaría
los datos puestos.

`pop()` y `format()` **no borran la EEPROM**: alcanza con mover el puntero. Borrar costaría una
escritura por registro —desgaste y tiempo— para no ganar nada.

#### ⚠ `packed` en el registro, y el margen que quedó

El `_Static_assert` cazó que el registro **no entraba en 64 bytes** por el relleno que ARM inserta
para alinear los `float`. Se resolvió con `__attribute__((packed))`, pero **el motivo de fondo no es
el tamaño**: sin él, el layout en memoria persistente dependería de opciones de compilación. Un
cambio de flags o de versión del compilador movería los offsets y **los registros viejos se leerían
corridos, con checksum válido** —porque el checksum se calcula sobre los mismos bytes—. Datos
plausibles y falsos. En el AVR el asunto no existía: es de 8 bits y alinea a byte.

⏳ **Queda ajustado: 62 de 64 bytes.** Agregar un campo a `dataRcd_t` no entra, y el
`_Static_assert` lo va a decir. Cuando pase, la decisión es entre subir el registro a 72 —1763
registros en vez de 1984, o sea 6,1 días en vez de 6,9— o sacar algo. **Conviene decidirlo antes de
que haya equipos con datos guardados**, porque cambiar el tamaño invalida lo grabado.

### Paso 4b: la microSD como extensión, con la EEPROM de ventana

Diseño propuesto por Pablo y acordado el **2026-09-08**. Reemplaza la idea previa de "EEPROM
primaria + SD histórico redundante".

```
cada muestra  ->  EEPROM (la VENTANA)  --se llena-->  un archivo en la microSD
                       |                                      |
                       +--- al transmitir: primero la EEPROM, después los archivos
```

**Por qué es el diseño correcto, y no una comodidad**: el problema real de la SD es el costo de cada
acceso —encender, re-inicializar la tarjeta, montar, escribir, desmontar, apagar— contra los 5 ms de
una escritura I2C. Poleando cada minuto, escribir la SD en cada muestra serían **1440 ciclos por
día**. Con la ventana, **la SD se toca una vez cada 33 horas** (1984 registros a 1/min), en una
operación grande y previsible. Es la diferencia entre usar FatFs de a ratos y tenerlo en el camino
crítico de cada ciclo.

Y el reparto queda limpio:

- **La EEPROM absorbe cada muestra**: barata, siempre presente, y **sin estructura que corromper** —un
  corte de alimentación pierde a lo sumo un registro, no la tabla de asignación entera como haría FAT.
- **La SD sólo recibe volcados espaciados**, que es cuando el riesgo de FatFs es manejable.
- **Degrada bien**: sin tarjeta, el equipo funciona exactamente como hoy.

Casos de uso que lo motivaron (Pablo): un equipo **logueando sin transmitir dos semanas** —20.160
muestras, ~10 volcados, unos pocos MB— y un modo **debug con la consola espejada a un archivo**. Lo
segundo sí escribe seguido, pero es una sesión supervisada, no operación de campo.

#### Las tres decisiones (Pablo, 2026-09-08)

**1. En la SD van los FRAMES DE TEXTO ya armados**, no registros binarios. Cuesta ~3× más espacio,
que en una SD es irrelevante, y a cambio: los archivos son legibles en una PC, **desaparece el
problema de "configuración nueva con datos viejos"** —el frame guardado ya tiene los nombres con los
que se midió— y al transmitir no hay que rearmar nada.

**2. Los archivos se numeran con un CONTADOR SECUENCIAL**, no con la fecha. Si el RTC arrancó frío,
los nombres por fecha colisionarían. El contador va junto a la FAT en la SRAM del RTC; la fecha ya
viaja adentro de cada frame.

**3. Al transmitir: primero la EEPROM, después los archivos de la SD.** No es el orden cronológico, y
está bien: *"los frames tienen fecha que se usa para indexar la base de datos del servidor, no
importa como lleguen"*. Los datos que entren durante la sesión quedan en la ventana para la próxima.

⭐ Y tiene una ventaja que no se buscaba: **vaciar la EEPROM primero libera la ventana al principio de
la sesión**, justo antes de la parte larga (los archivos). Eso reduce la chance de que la ventana se
llene en medio de la transmisión, que era el caso incómodo del orden cronológico.

#### ✅ FatFs, generado por Pablo el 2026-09-08

*Middleware → FATFS → **User-defined*** (no SDMMC: la SD va por SPI). Quedó con `_USE_LFN = 0`,
`_FS_TINY = 1`, `_VOLUMES = 1`, `_FS_REENTRANT = 0`, `_FS_NORTC = 0` y `_USE_MKFS = 1`.

⚠ En la GUI la opción de la fecha **no se llama "Disabled"** sino **`Dynamic timestamp`**
(= `_FS_NORTC = 0`); `Fixed timestamp` pondría la misma fecha en todos los archivos.

`FATFS/Target/user_diskio.c` quedó enganchado a `drv_sd`, con dos decisiones:

- ⚠ **`USER_initialize()` NO enciende la tarjeta**: sólo informa si está lista. El encendido lo hace
  `fs_sd`, que es quien sabe cuándo vale la pena pagarlo y cuándo apagar. Si encendiera desde ahí,
  FatFs prendería la SD sola en cada `f_mount` y nadie sabría cuándo apagarla — justo el control que
  el diseño de ventana quiere conservar.
- **`GET_BLOCK_SIZE` devuelve 1**, que no es la verdad —una SD borra de a bloques de decenas de KB—
  pero es el valor **seguro**: hace que `f_mkfs` alinee de forma conservadora en vez de asumir una
  alineación que la tarjeta no tiene. El valor real sale de `ERASE_BLK_LEN` en la CSD, que `drv_sd`
  no expone. Sólo afecta al formateo.

**`get_fattime()`** (en `fs_sd.c`) lee del MCP79410, y **si la hora no es confiable devuelve 0** —que
FatFs interpreta como "sin fecha"— en vez de estampar 2001-01-01 en el directorio. Un archivo sin
fecha se nota; uno fechado en 2001 se copia a un informe sin que nadie lo mire dos veces.

⚠ Recordar que **`_FS_TIMEOUT` está en TICKS, no en ms**: con el tick a 512 Hz, el default de 1000
son 1,95 s. Hoy no importa porque `_FS_REENTRANT = 0`.

#### ✅ La tarjeta se formatea DESDE EL DATALOGGER (`fs sd format borrar`)

Criterio de Pablo, **2026-09-09**: *"la idea es que las tarjetas microSD las trabajemos solo en el
datalogger y no tengamos que formatearlas antes en un PC"*. Un técnico que cambia una tarjeta en
campo no tiene una PC al lado.

Y no es un lujo: **las tarjetas nuevas de más de 32 GB vienen en exFAT**, que esta configuración no
lee (`_FS_EXFAT = 0`) y rechaza con `FR_NO_FILESYSTEM`. Es exactamente lo que apareció en banco —
`FRESULT=13` — con la tarjeta funcionando perfectamente a nivel de sectores.

Tres decisiones del formateo:

- **`FM_FAT | FM_FAT32`, NO `FM_ANY`.** `FM_ANY` incluye `FM_EXFAT`, y dejaría la tarjeta formateada
  en algo que **el propio equipo no puede montar después**. Que FatFs elija entre FAT16 y FAT32 según
  la capacidad está bien; salirse de ahí, no.
- **Con tabla de particiones** (sin `FM_SFD`), que es como vienen las SD de fábrica y lo que espera
  cualquier lector de tarjetas.
- **Se monta al terminar para verificar.** Formatear y no comprobarlo dejaría al equipo diciendo
  "listo" sobre una tarjeta que va a rechazar el primer volcado — y eso se descubriría **33 horas
  después**, con los datos ya en riesgo.

⛔ **La palabra `borrar` es obligatoria y va a propósito**: `fs sd format` se parece demasiado a
`fs format` —que sólo vacía la ventana— como para que un tipeo apurado se lleve los lotes de una
instalación. El parser ya exige el comando completo; esto es la segunda red.

⚠ **Tarda**: escribe las dos copias de la FAT **sector por sector**, porque `drv_sd` no expone
escritura múltiple. En una tarjeta grande son varios segundos con la tarea bloqueada. Si alguna vez
molesta, ahí está el lugar donde mirar (CMD25, multi-block write).

#### El volcado va al UMBRAL del 90 %, no al llenarse

Es la diferencia entre un margen y un borde. Si se esperara a `count == length`, un volcado fallido
—tarjeta ausente, error de escritura— dejaría al equipo pisando registros desde el intento siguiente.
Con el umbral al 90 % quedan ~198 registros de aire para reintentar: más de 3 horas a una muestra por
minuto.

Y **el orden dentro del volcado es la única garantía real**: se escribe el archivo, se **confirma el
`f_close()`**, y recién ahí se vacía la ventana. Si se vaciara antes, un corte en el medio se
llevaría los datos de los dos lados a la vez. Si algo falla, la ventana **queda intacta**.

### ✅ Pasos 4 y 4b validados en banco (2026-09-09)

| Criterio | Resultado |
|---|---|
| ⭐ **La FAT sobrevive al corte** | `FS:: 2 registros guardados de 1984` al arrancar — antes decía "FAT invalida" |
| Formateo desde el equipo | `fs sd format borrar` → **FAT32, 3.804.608 KB libres** |
| Volcado | `SD:: 2 registros volcados a LOTE0001.DAT`, y la ventana quedó en 0 |
| Numeración secuencial | `LOTE0001` … `LOTE0003`, con el contador en la SRAM del RTC |
| Fecha de los archivos | `2026-09-09 12:08` con hora confiable, **sin fecha** cuando no lo era |
| ⭐ **Sin tarjeta** | `no hay tarjeta (SD_DET en alto)` y **la ventana quedó intacta**: `guardados: 1` antes y después |

⭐ **La última es la que valida el diseño**: la microSD es una **extensión**, no una dependencia. Si
ahí se hubieran perdido datos, todo el esquema de ventana estaría mal planteado.

Los **306 bytes de un lote de 2 registros** son 153 por línea, o sea un frame más el CRLF: el
contenido es el que se va a transmitir, sin rearmar nada.

ℹ️ **De paso quedó probado el mecanismo de la hora, por los dos lados**: `LOTE0001` se creó con el
RTC en arranque frío y quedó **sin fecha** (`1980-00-00`, que es `get_fattime()` devolviendo 0);
`LOTE0002` y `LOTE0003`, con la hora ya fijada, quedaron con la fecha real. ⚠ El porta pila **volvió
a fallar** durante las pruebas — sigue siendo el pendiente de hardware de siempre.

### ✅ Paso 5a: el módulo se configura desde la consola, y el PING anda (2026-09-09)

```
lte on
lte esc                            <- entra en modo comando
lte info                           <- lee TODO del modulo, y cachea el IMEI
lte set server 192.168.0.20 5000
lte save                           <- graba y REINICIA el modulo
lte exit                           <- vuelve a transparente
lte ping
-> ID=860909055244702&HW=SPQ_ARM_R1&TYPE=FWDLGARM&VER=0.0.36&CLASS=PING
<- "<html>CLASS=PONG</html>"
```

#### ⚠ El estado del módulo es EXPLÍCITO: los comandos no entran solos en modo AT

Criterio de Pablo (2026-09-09), copiado del AVR: *"con un comando lo pongo en modo AT y con otro lo
saco. Luego tengo comandos que ASUMIENDO que está en modo AT le mandan la configuración o leen. Estos
comandos NO intentan ponerlo. Si no está, fallan."*

`lte esc` entra, `lte exit` sale, y `info`/`set`/`save` sólo hablan. `lte ping` es al revés: **asume
modo TRANSPARENTE**, porque en modo comando el módulo no transmite.

**Y eso elimina un bug de raíz.** La primera versión intentaba entrar "por las dudas" antes de cada
comando, y mandaba `+++` cuando el módulo **ya estaba** en modo AT. Ese `+++` va **sin CR** —es una
contraseña, no un comando— así que quedaba colgado en el buffer del módulo y el `AT` siguiente se le
concatenaba: leía `+++AT`, contestaba `ERROR`, y desde afuera se veía como *"no se pudo entrar en modo
comando"* **estando adentro**. Un estado explícito no puede tener ese problema.

#### La configuración del módulo NO se duplica en el datalogger

**La IP, el puerto y la URL viven en el módulo** (criterio de Pablo). El equipo sólo escribe el
payload; el GET entero lo arma el DTU. Por eso `lte info` **pregunta cada vez** en vez de mostrar una
copia local que podría estar desactualizada.

Los comandos envueltos existen para que **el técnico no tenga que saber AT** —mismo criterio que
`modem set server` en FWDLGX—: `lte set server <ip> <puerto>`, `lte set apn`, `lte set url`,
`lte set httpd`. Emiten los mismos AT que el AVR (`AT+HTPSV=ip,puerto`, `AT+APN=apn,,,0`, …). Y cada
uno **verifica el `OK`**: el módulo contesta igual ante un parámetro mal formado, y un "listo" sobre
una configuración que no entró es peor que un error.

⚠ **Nada queda grabado hasta `lte save`** (`AT+S`), que además **reinicia el módulo** y lo deja en
transparente.

#### ⭐ Transmitir es sólo escribir el payload

El módulo delimita la trama **por silencio** en la serie (su `ftime`), así que no hay terminador que
mandar. Es lo mismo que hace `MODEM_txmit()` en el AVR.

#### ⛔ Los pines 21 y 22 del módulo NO se rutean al micro: son las señales de su SIM

**Causa raíz del "el equipo no transmite nada", encontrada por Pablo el 2026-09-09.** Es un error de
hardware de R001 y **hay que corregirlo en la próxima revisión de la placa**.

El WH-LTE-7S1-E **trae su propio socket de SIM** y externaliza esas señales por los pines 20-23
—`VSIM`, `SIM_DAT`, `SIM_CLK`, `SIM_RST`— *"users can also design according to the needs with the SIM
pins"*, o sea **por si alguien quiere poner la tarjeta afuera**. No son pines de propósito general.

En R001 el 21 (`SIM_DAT`) y el 22 (`SIM_CLK`) estaban ruteados al micro, y eso **cargaba el bus de la
SIM interna**: el módulo no podía leer su propia tarjeta. Liberados, funcionó de inmediato.

⚠ **La regla que queda**: si el módulo trae la SIM adentro, esos cuatro pines se dejan **al aire**.
Sólo se cablean si la SIM va a vivir en la placa portadora — y en ese caso van al socket, nunca al
micro.

#### ⚠ Tener señal NO es tener conexión: lo que decide es `AT+CIP?`

El equipo no transmitía **un solo frame** y el servidor no registraba nada. La comparación que lo
cerró fue de Pablo: **puso el mismo módulo en un datalogger AVR y ahí sí transmitía**, o sea que el
módulo, la red y el servidor estaban bien.

La causa estaba a la vista y era dos escalones más abajo de donde yo miraba: **`+ICCID:` vacío**. El
módulo no leía su SIM —por los pines 21/22 ruteados al micro, ver arriba— así que no había registro de
datos (`AT+CIP?` → `+CME ERROR:50`) y se tragaba el payload sin enviarlo. **El `CSQ 31` era lo que
despistaba: es señal de radio, no conexión.**

Por eso `lte info` consulta ahora también `AT+ICCID?` y `AT+CIP?`, y **cierra con un veredicto** en
vez de dejar nueve respuestas para interpretar. El orden del aviso va de la **causa al efecto**: sin
SIM no hay red, y sin red no hay IP — avisar de la IP cuando el problema es la SIM manda a buscar al
lugar equivocado.

⏳ **Para el paso 5b**: el log del AVR contra este mismo servidor devolvió
`CLASS=CONF_ALL&CONFIG=ERROR` — *"El servidor no reconoce al datalogger"*. Para el `PING` no importa,
pero **antes de probar los frames de configuración hay que dar de alta ese IMEI en el servidor**.

### Paso 5b-1: el frame `CONF_ALL`

Comando **`lte conf`**. Manda un hash por bloque y el servidor contesta cuáles quiere reconfigurar.
**Por ahora sólo informa**: aplicar la configuración es el 5b-2, y verla antes de escribir el parseo
dice qué manda este servidor de verdad y con qué formato.

#### Van CINCO hashes, no seis — y el servidor va a pedir `FLOWC` siempre

El AVR manda además `FH`, el de *flowcontrol*, que en este equipo no existe. Pablo lo autorizó
(2026-09-11): *"puede no mandar el FH, pero el servidor tomará uno por defecto y mandará en la
respuesta que debe pedir reconfigurar el flowcontrol. Luego si el datalogger no lo hace, no pasa
nada"*.

⚠ **La consecuencia de diseño**: `CONF_ALL` **nunca va a responder `CONFIG=OK`**, así que la FSM
tiene que pasar a transmitir datos igual. Si esperara ese `OK`, el equipo no mandaría una sola
muestra.

Por suerte la estructura del AVR ya lo tolera y hay que copiarla tal cual: **sólo `conf_base` aborta
si falla**; los demás bloques, si no se configuran, simplemente no suman y `wan_state_online_config()`
termina en `WAN_ONLINE_DATA` igual.

`FLOWC` **se parsea aunque no se use**: verlo en la consola explica por qué la configuración nunca
cierra. Sin eso parecería un error.

#### ⚠ El `CSQ` del frame no es el `rssi`, y dos valores del `rssi` no son medidas

Lo tenía documentado el AVR y **es lo que nos despistó el 2026-09-09**:

```c
/* csq queda con el |dBm| de la senal ( dBm = -113 + 2*rssi ).
 *  - rssi == 99 : "desconocido / no detectable" (3GPP).
 *  - rssi >= 31 : centinela que devuelve el modem ANTES de campar en red
 *    0515-0516 ("+CSQ: 31,0" -> luego "+CME ERROR:50" en AT+CIP?).       */
```

O sea que **`CSQ: 31,0` nunca fue "señal excelente"**: es el centinela de *todavía no registrado*, y
le sigue el `+CME ERROR:50` de `AT+CIP?` — exactamente la secuencia que vimos. El AVR se había comido
el mismo despiste en mayo de 2026.

En el frame viaja **`|dBm| = 113 − 2·rssi`** (por eso el AVR manda `73` con `20,99`), y `lte info`
ahora **interpreta** el valor en vez de sólo mostrarlo.

#### `UID` y `WDG`: distintos del AVR, y está bien

| Campo | AVR | Acá | Confirmado por Pablo (2026-09-11) |
|---|---|---|---|
| `UID` | 32 chars (128 bits) | **24 chars** — el STM32L4 tiene 96 bits | *"el servidor no valida el largo y de hecho no lo está usando"* |
| `WDG` | bits crudos de su registro de reset | **código enumerado** (`3 = IWDG`, `5 = BOR`, …) | *"es sólo informativo… tampoco sería problema que mande un código enumerado"* |

El equipo se identifica por el **IMEI**; los otros dos son diagnóstico.

#### Si el servidor no nos reconoce, el equipo se espacia solo

Ante `CONFIG=ERROR` (no conoce al datalogger) o `FAIL` (no conoce el frame), el AVR **se reconfigura**:
`PWR_DISCRETO` con `timerdial` y `timerpoll` en 3600. Es la política correcta —insistir cada minuto
contra un servidor que no te va a contestar sólo gasta batería y tráfico— y hay que portarla.

⏳ **Pero todavía NO se hace**: que un comando de banco cambie la configuración del equipo por lo bajo
sería peor que el problema. Entra con la FSM, en el 5c.

### ✅ Paso 5b-2: los `CONF_*` — parsear lo que manda el servidor y aplicarlo

`lte conf` dejó de ser un informe: después del `CONF_ALL`, por cada bloque que el servidor pidió le
manda su hash, **aplica la configuración que conteste** y graba **una sola vez al final**. Es la
secuencia completa, la misma que va a correr sola la FSM del 5c.

Los formatos —relevados de `wan_process_rsp_config*()` del AVR— están en `wan_frame.h`. Lo que hay
que saber de este paso:

#### ⚠ `CONF_BASE` lleva la identidad del equipo y los otros cuatro NO

Es una asimetría del AVR que no tiene explicación aparente y que hay que reproducir igual, porque es
el contrato:

```
CLASS=CONF_BASE&UID=..&ICCID=..&CSQ=..&WDG=..&HASH=0x..     <- con identidad
CLASS=CONF_AINPUTS&HASH=0x..                                <- sólo el hash
```

#### ⛔ El servidor manda MENOS campos de los que parsea el AVR, y falta uno es NORMAL

Encontrado en banco el **2026-09-11**, en la primera corrida contra el servidor real. La respuesta fue:

```
CLASS=CONF_COUNTERS&C0=FALSE,X,1.0,CAUDAL          <- CUATRO campos, no seis
```

O sea que el comentario del AVR —que muestra cuatro— describe **lo que manda el servidor**, y su
código parsea seis porque **tolera que falten**. La primera versión los exigía todos y descartó el
bloque entero.

**La regla, y es la del AVR en los tres bloques** (`counter_config_channel`, `ainputs_config_channel`,
`modbus_config_channel`): **el único campo obligatorio es el NOMBRE; cualquier otro en `NULL` quiere
decir "dejá ese campo como está"**. Los setters de `Application/config/` la implementan ahora igual,
validando los que vienen **contra los que ya están** y escribiendo sólo si el conjunto completo
cierra — así un campo suelto no puede dejar la configuración en un estado que ninguna validación
aprobó.

#### ⛔ Un campo que entra en el hash y NO viene en la respuesta no cierra nunca

Es el modo de falla que apareció el **2026-09-11**, y conviene entenderlo como regla general porque
se repite con cualquier campo:

```
el campo entra en el hash  +  el servidor no lo manda
    -> el equipo se queda con SU valor
    -> el hash sigue distinto del suyo
    -> pide reconfigurar ese bloque en TODAS las sesiones
```

Es exactamente el "tráfico infinito en campo" contra el que advierte `cfg_hash.h`, pero por una causa
que no está en el formato: está en **qué campos viajan**.

Los dos que lo dispararon:

| Bloque | El campo | Estado |
|---|---|---|
| `ainputs` | **`PST`** (`[PST:%03d]`) | ✅ **cerrado** — el formato SÍ lo incluye (`…&PST=15&A0=…`); la respuesta que llegó en banco no lo traía. El equipo ya lo parsea y lo aplica. |
| `counter` | **`QMAX` y `ALPHA`** (`[C0:…,%.2f,%.2f]`) | ⏳ la respuesta corta en el modo (`C0=FALSE,X,1.0,CAUDAL`). Falta el cálculo del servidor para confirmarlo igual que ainputs. |

**Cuánto pesa el `PST`**: no es un ajuste fino, **cambia el hash entero**, así que con el default en
0 no hay ninguna chance de coincidir con un servidor que tiene 15.

##### ⭐ El hash de `ainputs` quedó verificado CONTRA EL SERVIDOR (2026-09-11)

Pablo pasó el `get_ainputs_hash_from_config()` del servidor, y se corrió su lógica **literal** en
Python —con la tabla de Pearson sacada del propio `cfg_hash.c`— contra el C del equipo compilado para
el host. Los strings y los hashes son **idénticos**:

```
[PST:015]
[A0:FALSE,PPR,4,20,0.00,10.00,0.00]
[A1:FALSE,X,4,20,0.00,25.00,0.00]
[A2:FALSE,X,4,20,0.00,60.00,0.00]      ->  AH = 0xD7  en los dos lados
```

| `PST` | servidor | equipo |
|---|---|---|
| 0 | `0x3F` | `0x3F` |
| 10 | `0xC1` | `0xC1` |
| **15** | **`0xD7`** | **`0xD7`** |
| 20 | `0x8D` | `0x8D` |

⭐ **Esto es más fuerte que la validación contra el AVR**: aquella probaba que copiamos bien el
firmware viejo; ésta prueba que coincidimos con **la otra punta del contrato**, que es lo que
realmente importa.

⚠ **La única diferencia estructural que queda** —y hoy no se puede disparar, pero conviene saberla—:
el equipo **trunca a 64 bytes** (`CFG_HASH_BUFFER_SIZE`, heredado del AVR) y **el servidor no**. Con
`CFG_PARAMNAME_LENGTH = 12` el string más largo posible de un canal son **50 bytes**, así que hay
margen; el día que alguien agrande el nombre o agregue un campo, ese margen es lo primero a
recalcular.

#### ⭐ `config hash`: los STRINGS, que es lo único comparable

Para eso se agregó. Imprime, bloque por bloque, **el string sobre el que se calcula el Pearson**:

```
cmd>config hash
AINPUTS  [PST:005]
         [A0:FALSE,PPR,4,20,0.00,10.00,0.00]
         …
BH=0x66 AH=0x74 …
```

Dos cosas por las que está hecho así:

- **Lo imprime `cfg_hash_string()`**, o sea **lo que entra al Pearson**, no una reimpresión aparte.
  Una copia podría diferir justo en el decimal que uno vino a mirar, y el diagnóstico mentiría sobre
  lo único que interesa.
- El **valor** del hash no dice en qué carácter está la diferencia; el string, sí. Con el bloque
  impreso a un lado y la configuración del servidor al otro, la comparación es a ojo.

Campos que el servidor manda y **el equipo ignora a propósito** porque no están en el hash ni en el
AVR: `SAMPLES` y `ALMLEVEL` en `CONF_BASE`. Ignorarlos es correcto — si entraran en el hash, el AVR
en producción tampoco cerraría.

#### El parseo no usa `strsep`, y es mejor que el del AVR en un punto concreto

`strsep()` y `strlcpy()` son de BSD y newlib no las trae, así que hay una sola función,
`prvCampo( rta, "CLAVE=", nro_de_token, … )`, que parsea **en el lugar**. La diferencia que importa:
el AVR copiaba **64 bytes** desde el campo a un buffer global y tokenizaba ahí, de modo que **un
campo largo se truncaba y los últimos tokens salían `NULL`** — un canal modbus con nombres largos
perdía el `pow10` en silencio. Acá cada token se acota por separado.

⚠ **Eso NO toca el contrato.** El contrato es lo que se **manda** —el hash sobre el string
formateado, con su buffer de 64 bytes, que sigue igual—; cómo se lee la respuesta es asunto nuestro.

Los delimitadores sí son los del AVR, `&,;:=><`, y los `<>` están porque **la respuesta viene
envuelta en HTML**: sin ellos el último valor se llevaría puesto el `</html>`. De yapa se corta
también en cualquier carácter de control, que el AVR no hacía: un CRLF al final dejaba el `\r` pegado
al valor y el setter lo rechazaba por una razón que desde afuera no se entiende.

#### Tres reglas del aplicado

- **Un campo que el servidor no mandó se deja como está.** No hay valor por omisión: si la clave no
  aparece, no hay nada que aplicar.
- **Un canal se aplica entero o no se aplica.** Se leen los 7 tokens de una analógica (9 de un
  modbus, 6 del contador) **antes** de tocar nada: una calibración a medio configurar es una
  calibración inventada, y encima pasaría el checksum.
- **Se verifica que la respuesta sea de ESE bloque** antes de aplicarla — el `wan_check_response()`
  del AVR. Con el módulo en transparente, una respuesta demorada del frame anterior llega igual, y
  aplicar la configuración de un bloque leyendo la respuesta de otro escribiría basura con toda
  naturalidad.

#### ⚠ Lo que este paso deliberadamente NO hace

- **No aborta la secuencia si un bloque falla.** Sigue con los demás, igual que el AVR (en
  `wan_state_online_config()` **sólo `conf_base` aborta**). Es lo que permite que el `FLOWC` que el
  servidor va a pedir siempre —porque no mandamos su hash— no deje al equipo sin transmitir una sola
  muestra.
- **No actúa el `CONFIG=ERROR`.** La política de "si el servidor no nos reconoce, pasar a `DISCRETO`
  con los timers en 1 h" sigue siendo del 5c: que un comando de banco cambie el modo de operación del
  equipo por lo bajo sería peor que el problema.
- **No adopta un `PWRMODO` que no sabe ejecutar.** `PWR_RTU` y `PWR_SILENT` existen en el AVR y acá
  están fuera de alcance (Pablo, 2026-09-07): el setter los rechaza, se avisa, y el equipo queda con
  el modo que tenía.

#### Grabar una sola vez, al final

`wan_conf_aplicar()` toca **sólo la RAM**; el `cfg_nvm_save_all()` lo hace el llamador, y únicamente
si algún bloque cambió algo. Grabar bloque por bloque serían cinco escrituras de las que cuatro
podrían quedar a medias si la sesión se corta. De paso se corre `cfg_nvm_chequear_nombres()`, porque
la configuración que manda el servidor puede traer dos canales con el mismo nombre igual que la que
tipea un técnico.

### ✅ Paso 5b VALIDADO EN BANCO (2026-09-11): el contrato del hash CIERRA

⭐ **El criterio de aceptación no era que la configuración se aplicara: era que en la sesión
SIGUIENTE el servidor dejara de pedir los bloques.** Eso es lo único que prueba que nuestros strings
del hash son idénticos a los suyos. Y pasó:

```
cmd>lte conf                      <- primera sesion: aplica los cinco bloques
…
CFG:: configuracion grabada

cmd>lte conf                      <- segunda sesion
-> …&BH=0x8D&AH=0xD7&CH=0xFA&MH=0xBB&PH=0x28
<- "<html>CLASS=CONF_ALL&FLOWC</html>"
el servidor pide reconfigurar: FLOWC
no cambio nada: no se graba la EEPROM
```

| Criterio | Resultado |
|---|---|
| `CONF_BASE` | ✅ los cinco campos (`PWRON=0630`, con el cero adelante, incluido) |
| `CONF_AINPUTS` | ✅ `PST` + los 3 canales |
| `CONF_COUNTERS` | ✅ los 6 campos |
| `CONF_MODBUS` | ✅ `ENABLE`, `LOCALADDR` y los 5 canales |
| `CONF_CONSIGNA` | ✅ |
| Grabado único al final | ✅ una sola vez, y **la segunda sesión no graba nada** |
| ⭐ **Los cinco hashes** | ✅ **el servidor los acepta**: sólo queda `FLOWC`, que se ignora a propósito |

⭐ **`AH=0xD7` era el valor PREDICHO** por el test del host antes de tocar el equipo. La cadena
completa —predicción en el host, aplicación en el equipo, aceptación del servidor— cerró sin
sorpresas.

Los hashes de referencia de esta configuración, que sirven para verificar de un vistazo que nada se
movió: **`BH=0x8D AH=0xD7 CH=0xFA MH=0xBB PH=0x28`**.

⚠ **Lo que hizo falta fue tocar el SERVIDOR, no el firmware**: mandar el `PST` en la respuesta de
`CONF_AINPUTS` y los seis campos en la de `CONF_COUNTERS`. El firmware ya los parseaba.

### 🔨 Paso 5c: los frames de datos y el vaciado

Comando **`lte data`**: transmite los registros de la ventana y los borra **recién cuando el servidor
confirmó**. ⚠ Asume modo TRANSPARENTE, igual que `lte ping` y `lte conf`.

La estructura es la del AVR (`wan_send_from_memory`): bloques de **10** frames `CLASS=DATANR` —que no
esperan respuesta— y el que cierra el bloque va como `CLASS=DATA`, que sí espera. Confirmado ése, se
dan por buenos los diez.

#### ⛔ El borrado NO es el del AVR, y ésa es la diferencia que importa

El AVR usa `FS_readRcd()`, que **consume el registro antes de transmitirlo**: si la sesión se corta,
esos datos ya se perdieron. Acá se lee con `fs_datos_peek( dr, offset )` y se llama `fs_datos_pop( n )`
**sólo tras la confirmación** — que es exactamente para lo que esas dos funciones se separaron en el
paso 4.

Si la sesión se corta no se pierde nada; a lo sumo se retransmiten hasta 10 registros, y como el
servidor los indexa por la fecha que viaja **adentro** del frame, un duplicado es inofensivo. Es el
mismo razonamiento que permite mandar los lotes de la SD después de la ventana sin respetar el orden
cronológico.

**El `count` se congela al entrar**, como en el AVR: los registros que `tkSys` grabe durante el
vaciado quedan para el ciclo siguiente. Sin eso, con un `timerpoll` corto el vaciado no termina nunca.

#### ⚠ La pausa entre frames es OBLIGATORIA, y el AVR la tiene por accidente

En modo transparente el módulo **no tiene terminador**: arma el GET con lo que recibió cuando la serie
se queda callada `ftime` milisegundos. O sea que ese número fija cuánto hay que esperar entre dos
frames seguidos — si se manda más rápido, **los dos se le juntan en un solo GET** y del otro lado
llega un frame corrupto.

⛔ **El AVR no tiene ninguna espera ahí.** Le funciona porque imprime el frame por la consola a 9600
antes de mandarlo, y eso son **~150 ms de pausa accidental**. Depender de eso es depender de que el
log esté encendido y de la velocidad de la terminal: en campo, con el log apagado, los frames se
pegarían. Acá la espera es explícita: `LTE_DATA_MS_ENTRE_FRAMES`, **500 ms**.

✅ **Validado en banco el 2026-09-18**: se transmitieron 10 frames seguidos y **el servidor los
recibió todos**. Era el riesgo principal del paso y quedó descartado.

##### ⛔ El comando NO se llama `AT+FTIME`, y hay DOS criterios de empaquetado

`AT+FTIME?` devuelve **`+CME ERROR:58`** (no soportado) — encontrado en banco el 2026-09-18. Los
comandos reales son dos, y el módulo cierra la trama con **el que se cumpla primero**:

| Comando | Qué mide | Rango | Default |
|---|---|---|---|
| **`AT+UARTFT`** | el silencio que cierra la trama | 10..500 ms | **50 ms** |
| **`AT+UARTFL`** | el largo que también la cierra | 5..4096 B | 1024 B |

✅ **Leídos del módulo el 2026-09-21**: `UARTFT` está en **250 ms** y `UARTFL` en **1024**. O sea que
el módulo **no** está en el default de 50 ms —alguien lo configuró en 250— y los **500 ms** de pausa
quedan con **2× de margen** sobre el silencio que hace falta. El criterio por largo no se dispara:
los frames son de ~150 bytes contra 1024.

⭐ Eso es justamente por lo que `lte info` los consulta en vez de confiar en el default del manual: el
número que importa es el que tiene el módulo, no el de la hoja.

⚠ **`UARTFL` importa aunque hoy no apriete**: con frames de ~150 bytes nunca se llega a 1024, pero
**si alguien lo bajara por debajo del largo de un frame, el módulo lo partiría en dos GET** — y eso
del lado del servidor se vería como frames corruptos sin que el log del equipo muestre nada raro.

#### ⛔ El servidor contesta a TODOS los GET — también a los `DATANR`

**Es el hallazgo del 2026-09-21, y sólo se pudo cerrar con el log del servidor al lado.**

El síntoma era que la respuesta al `CLASS=DATA` llegaba como **`<html></html>`**, sin un solo
`CLASS=`. Parecía que el servidor había recibido el frame y lo había rechazado por su contenido — la
sospecha natural era la **fecha**, porque esos registros viajaban con `DATE=010101`.

⛔ **Era falso, y el log lo desmiente por dos lados:**

```
raw_response->CLASS=DATA&CLOCK=2609211441      <- el servidor SI respondio bien
[procesar_frame] D_DATALINE={'DATE': '010101', ...}
  -> save_dataline -> enqueue_dataline          <- y guarda los de 2001 sin chistar
```

**La causa real es una carrera.** El `NR` de `DATANR` es una convención de la capa de aplicación
—dice que el equipo no va a *esperar* la respuesta, no que el servidor no la mande—: por HTTP
**siempre** hay respuesta, y para un `DATANR` es un cuerpo **vacío**, que envuelto llega como
`<html></html>`. Esas respuestas viajan por LTE con cientos de ms de latencia, así que **llegan
cuando ya estamos mandando el frame siguiente**:

```
14:41:46,455   el servidor procesa el DATANR #9   -> responde vacio
14:41:47,445   el servidor procesa el DATA        -> responde CLASS=DATA&CLOCK=...
```

Con la pausa de 500 ms entre frames, el `drv_lte_flush()` de antes del `DATA` corre **justo antes**
de que llegue la respuesta del `DATANR` anterior: la limpiamos cuando todavía venía en camino, y
después leímos **ésa** en vez de la nuestra.

⭐ **El AVR no tiene este problema, y por qué importa entenderlo**: aquel **acumula** todo lo que
llega en un buffer y busca el patrón con `strstr` (`wan_check_response`), así que una respuesta
demorada queda delante y no estorba. Nosotros leemos **una trama delimitada por silencio** y la
evaluamos sola — que es mejor para todo lo demás, pero necesita tolerar las que no son de este frame.

**La corrección** (`prvEsperarRespuestaConClase()`): se lee en lazo hasta encontrar una respuesta que
traiga `CLASS=`, **descartando las vacías** y diciendo cuántas se descartaron. Descartar es correcto
y no un parche: una respuesta sin `CLASS=` **no lleva información** —es el acuse vacío de un
`DATANR`— así que perderla no pierde nada; lo que no se puede es tomarla por la respuesta de otro
frame.

⚠ **La lección de método, que ya van tres en este proyecto**: el síntoma señalaba al lugar
equivocado —la fecha— y ninguna cantidad de mirar el log del equipo lo habría desmentido. Lo cerró
**la otra punta**, igual que el módulo puesto en un AVR cerró lo de los pines 21/22 y el
`get_ainputs_hash_from_config()` cerró lo del `PST`.

#### ⭐ El módulo tiene reloj NTP: `lte clock`

`AT+CCLK?` devuelve la hora que el módulo sincroniza contra la red (`+CCLK: "20/06/19,20:05:19+32"`,
con el huso en cuartos de hora; el período de recalibración es `AT+NTPTM`, en minutos).

Es **una fuente de hora independiente del servidor Y de la pila del MCP79410**, o sea la salida de
fondo al porta pila intermitente: el equipo puede ponerse en hora **al abrir la sesión, antes de
medir**, en vez de esperar la respuesta a un frame de datos.

`lte clock` la muestra junto a la del equipo; `lte clock set` la aplica — y como `drv_rtc_escribir()`
escribe la firma de la SRAM, eso **saca al chip del arranque en frío sin que nadie vaya al sitio**.

##### ✅ El módulo entrega hora LOCAL, no UTC (banco, 2026-09-21)

Era la duda que bloqueaba automatizarlo —si el equipo estampara UTC donde el AVR estampa local, todos
los registros quedarían **corridos 3 horas** contra los de FWDLGX, plausibles y mal—. La respuesta
está en el propio string:

```
+CCLK: "26/09/21,11:04:08-12"      <- -12 cuartos = UTC-3, y la hora YA viene con el huso aplicado
```

Si diera UTC diría `14:04`. O sea que **el módulo aplica el huso solo** y coincide con lo que estampa
el AVR.

##### ⛔ Y de paso apareció que el MCP79410 ADELANTA

La misma corrida mostró:

```
hora del modulo : 11:04:08        <- coincide con el reloj de la PC
hora del equipo : 11:09:53        <- 5 min 45 s ADELANTADO, y marcado "confiable"
```

Las dos lecturas salen del mismo comando con milisegundos entre una y otra, así que **los 345 s son
discrepancia real**, y Pablo confirmó contra el reloj de la PC que **el bueno es el módulo**.

Para dimensionarlo: si esos 345 s se acumularon en 12 días son **+332 ppm (+28 s/día)**, más de
quince veces la tolerancia de ±20 ppm de un cristal de reloj.

##### ⛔ HAY DOS CRISTALES DE 32.768 kHz, y el de la hora NO es el del micro

Esto casi manda a cambiar el componente equivocado, y es la clase de error que este proyecto ya pagó
caro con el `CSQ 31` y con la FAT que acusaba a la pila:

| Cristal | Dónde | Qué alimenta |
|---|---|---|
| el del **micro** | PC14/PC15, con sus condensadores de **10 pF** | el **LSE**: tick del kernel y RTC interno |
| **`Y1`** | **X1/X2 del MCP79410** | ⭐ **la hora que se estampa en cada muestra** |

El "pendiente de hardware" que venía anotado desde el bring-up habla del **primero**, y decía que
afectaba a la hora del datalogger — **es falso**: ese oscilador mueve los `vTaskDelay()` y el
`timerpoll`, donde 100 ppm son despreciables. **La deriva que medimos es la de `Y1`.**

⚠ **Y en el esquemático `Y1` va directo a X1/X2 sin condensadores de carga a la vista**
(`SCH_spq_arm_logica_R001.pdf`). Si el cristal pide carga y no la tiene, la efectiva queda muy por
debajo de la nominal — y **cargar de menos adelanta, tanto más cuanto menos carga haya**. Eso
encajaría con los +332 ppm medidos, que son demasiados para explicarse sólo por una carga
*levemente* baja.

⏳ **Lo que hay que verificar en la placa**: qué `CL` pide el cristal `Y1` montado, y si lleva o no
condensadores. El firmware ya no manda a mirar PC14/PC15 — el mensaje de la deriva nombra `Y1`
explícitamente.

#### ℹ️ El `bt12v` bajo en banco NO es del firmware (Pablo, 2026-09-21)

En las corridas de estos días el frame informa `bt12v` entre **6,8 y 7,4 V** en vez de ~12. **Pablo
avisó que es un problema de su montaje de banco para medir ese riel, no del equipo**, y que lo va a
resolver.

Queda anotado para que **no vuelva a aparecer como sospechoso** al leer una traza: esos números son
esperables mientras dure, y no hay nada que investigar del lado del `drv_adc` ni del divisor de la
placa.

#### ⭐ La corrección MIDE la deriva, y por eso no vive en el comando de consola

Hay **dos** fuentes de hora y llegan en momentos distintos — las dos pasan por
`wan_rtc_sincronizar()`:

| Fuente | Cuándo llega | Qué resuelve |
|---|---|---|
| el **`CLOCK=` del servidor** | en la respuesta a un frame de datos | la **deriva** en operación normal — es lo que hace el AVR |
| **`AT+CCLK?`** del módulo (NTP) | al abrir la sesión, **antes de medir** | el **arranque en frío**: el `CLOCK` llega tarde, cuando los registros ya se grabaron con fecha 2001 |

⛔ **Y ahí está el problema que obligó a poner la medición en ese punto y no en `lte clock`**: el
servidor corrige la hora en **cada sesión**, así que el reloj siempre se ve bien y **la deriva del
cristal queda tapada para siempre**. Nunca nos enteraríamos de que hay que cambiar un componente de
la placa.

Poniendo la medición donde se aplica la corrección, **cada ajuste informa cuántos ppm se desvió**:

```
DERIVA DEL RTC desde la ultima sincronizacion:
  desde        : 09/09/26 12:08
  transcurrido : 288 h
  desvio       : +345 s (ADELANTADO)
  ==> +332 ppm  (+28 s/dia)
  [!] fuera de la tolerancia tipica de un cristal (+-20 ppm).
      El cristal es Y1, el del MCP79410 (X1/X2 de ese chip),
      NO el de PC14/PC15 del micro.
      ADELANTA -> le falta CARGA capacitiva: verificar que
      Y1 tenga sus condensadores y que correspondan a su CL.
```

La marca de la última sincronización va a la **SRAM del MCP79410** (dirección 32, ver el mapa que
ahora está en `drv_rtc79410.h`), con su checksum calculado con **`offsetof`** — el mismo patrón que
en el paso 4 hacía que la FAT se declarara inválida en cada arranque.

⚠ **Con menos de una hora transcurrida no se calculan ppm**: la resolución del RTC es 1 s, así que
sobre poco tiempo el error relativo se come el resultado. Se informa el desvío crudo y nada más.

La aritmética de fechas corre marzo al mes 0 para que el día bisiesto quede al final del año y no
haya un caso especial en el medio; `y/4` es **exacto** entre 2000 y 2099, que es todo lo que puede
representar un RTC con el año en dos dígitos. Validada sin hardware: **9 casos**, incluidos los dos
cruces de bisiesto (2028 sí, 2027 no) y el `2001-01-01` que el AVR daría como diferencia cero.

Se aplica el mismo chequeo que `tkSys`: **un año anterior al de compilación se rechaza** (el módulo
todavía no sincronizó con la red). `TKSYS_ANIO_COMPILACION` pasó a `tkSys.h` porque ahora lo usan los
dos lugares con idéntico criterio.

Validado sin hardware: **9 casos** de parseo, incluido el ejemplo literal del manual y los cuatro que
tienen que rechazarse.

#### La respuesta a `DATA` trae órdenes: por ahora `CLOCK` y `RESET`

El AVR atiende además `VOPEN`/`VCLOSE` y `EXT_V0/V1_*`, que **van con el paso 7** (acordado con Pablo,
2026-09-11): mueven válvulas, y esa política vive allá.

`RESET` **no se ejecuta dentro del parseo**: se informa y lo hace el llamador, **después del
`pop()`**. Reiniciar antes dejaría los registros confirmados sin borrar y el equipo los retransmitiría
enteros al volver.

#### ⛔ `CLOCK=`: el AVR sólo compara la HORA DEL DÍA, y acá eso es un agujero

`CLOCK=YYMMDDhhmm` pone en hora el equipo, y **no es un adorno**: es cómo se pone en hora solo en
campo. Como `drv_rtc_escribir()` escribe además la firma de la SRAM, **saca al MCP79410 de un arranque
en frío sin que nadie vaya al sitio** — con el porta pila fallando de forma intermitente, ése es el
caso que más va a aparecer.

**El umbral de 90 segundos se conserva** (el AVR lo fecha en 2021-12-14): sin él, con `timerpoll`
corto el reloj se reajusta en cada poleo y la hora se mueve todo el tiempo.

⛔ **Pero la cuenta del AVR es `hour*3600 + min*60 + sec` de los dos lados, y sólo eso.** Un equipo que
arrancó frío en `2001-01-01 10:30` contra un servidor en `2026-09-11 10:30` da diferencia **cero**:
no ajustaría nunca y el equipo quedaría estampando 2001 para siempre. Es precisamente el escenario
que este equipo tiene abierto.

Por eso acá se ajusta **siempre**, antes de mirar los 90 s, en dos casos más:

- **la firma del RTC dice que la hora no es confiable** (arranque en frío), o
- **la FECHA difiere** — y ahí no hay nada que dosificar: una fecha distinta no es deriva del
  cristal, es que uno de los dos está equivocado.

El día de la semana **no se toma del servidor** aunque el AVR lo haga (y lea un byte de más para
conseguirlo): `drv_rtc_escribir()` lo calcula con Sakamoto, que es un dato derivado de la fecha.

Validado sin hardware con stubs del RTC: **11 casos**, incluidos los cuatro strings malformados —que
nunca escriben— y el `2001-01-01` con la misma hora del día, que ahora sí ajusta.

#### 🔨 La segunda mitad: los lotes de la microSD

Van **después** de la ventana (criterio de Pablo, 2026-09-08). `prvLteLotes()` toma el lote más
viejo, lo transmite línea por línea con la misma ventana de confirmación, y **lo borra sólo si entró
entero**.

⚠ **Si la ventana no se pudo vaciar, ni se encienden los lotes**: el enlace está mal y van a fallar
igual, así que no tiene sentido prender la microSD para descubrirlo.

##### ⭐ En la microSD va SÓLO la parte de datos; el prefijo se construye al transmitir

Decisión de Pablo del **2026-09-21**, a partir de una sola pregunta suya: *"¿qué datos escribimos en
la microSD? ¿sólo los datos o los frames?"*. Se guardaba el **frame entero**, y eso metía campos de
**transporte** dentro de un archivo de **datos**:

```
ID=860909055244702&HW=SPQ_ARM_R1&TYPE=FWDLGARM&VER=0.0.53&CLASS=DATA&DATE=260911&TIME=110138&…
└────────────────── transporte: se construye al transmitir ─────────┘└──────── el dato ────────┘
```

⛔ **El que duele es el `ID`: es el IMEI del módulo.** Un lote que quede pendiente y se transmita
después de **cambiar el módulo LTE saldría con el IMEI viejo**, y el servidor lo atribuiría a otro
equipo o lo rechazaría. No es hipotético — el módulo de este banco ya se movió a un AVR para una
prueba.

Y el `CLASS` es una **decisión de transmisión**, no del dato: el mismo registro va como `DATANR` o
como `DATA` según cierre bloque o no. Guardarlo obligaba a reescribirlo al transmitir —con un
`memmove`, porque los dos largos difieren—, y esa función desapareció con este cambio.

**Lo que se guarda ahora** (`wan_frame_datos()`) y **lo que se arma al transmitir**
(`wan_frame_prefijo()` + `&` + la línea):

| | bytes |
|---|---|
| antes, el frame entero por línea | 121 |
| ahora, sólo los datos | **52** |
| ahorro en un lote de 1984 líneas | **134 KB** |

⭐ **El frame que viaja no cambia en un solo byte** — verificado sin hardware: 4 casos, comparando el
resultado contra el string exacto de antes. El contrato con el servidor queda intacto.

⭐ Y se conserva **la razón principal** por la que en la SD van frames de texto y no registros
binarios: los **nombres de los canales** siguen guardados con los que se midió, así que no reaparece
el problema de "configuración nueva con datos viejos".

⚠ **Los lotes del formato viejo se siguen transmitiendo**: si la línea ya empieza con `ID=`, se manda
tal cual. Sin eso, una tarjeta con lotes anteriores al cambio saldría con el prefijo duplicado.
Cuando no queden lotes viejos en ninguna tarjeta, esa rama se puede sacar.

##### Tres decisiones de implementación

- **Se lee una línea ADELANTADA.** El frame que cierra el lote tiene que ir como `CLASS=DATA` para
  que el servidor confirme, y no hay forma de saber que una línea es la última hasta intentar leer la
  siguiente. De ahí los dos buffers.
- **El archivo queda ABIERTO y la tarjeta encendida** durante todo el envío —hasta 1984 líneas y
  varios minutos—. El argumento es de proporción: mientras se transmite, **el modem consume decenas
  de mA** contra los 0,2-1 mA de la microSD; remontar cada pocas líneas serían ~200 ciclos de montaje
  por lote para ahorrar ruido. Leer el lote entero a RAM tampoco es opción: **~300 KB** contra los
  256 KB del micro.
- ⚠ **NO se imprime cada frame**, a diferencia de la ventana. Un lote son ~1984 líneas de 153 bytes,
  y sacarlas por la consola a 9600 son **más de cinco minutos por lote de puro log**. Se informa por
  bloque confirmado.

##### El borrado pide DOS condiciones

```c
bBorrar = bCompleto && ( usSinConfirmar == 0U ) && ( ulLineas > 0UL );
```

`bCompleto` dice que no se cortó; `usSinConfirmar == 0` que el último bloque se dio por bueno. Si
falta cualquiera, **el lote queda** y se retransmite entero la próxima vez — con duplicados de lo que
ya había llegado, que son inofensivos porque el servidor indexa por la fecha de cada frame. Es lo
acordado: un puntero de línea persistente sería un estado más que se puede corromper, para evitar
algo que no hace daño.

⚠ **Un `RESET` del servidor en medio de un lote NO lo borra**: se cierra sin borrar y se reinicia, así
que al volver se retransmite entero. Es lo correcto — no se confirmó todo.

### ✅ Paso 5c VALIDADO EN BANCO (2026-09-21): ventana y lotes

```
OK: 1 de 1 confirmados y borrados; quedan 0
--- LOTE0001.DAT ---
   (se descartaron 1 acuses vacios de DATANR anteriores)
  2 lineas confirmadas
SD:: LOTE0001.DAT transmitido y BORRADO
…
4 lote(s) transmitidos y borrados
```

Y `fs sd list` quedó vacío.

⭐ **Los dos formatos de lote convivieron en la misma corrida**: `LOTE0001/2/3` eran del formato
viejo —se ve en el `fs sd ver`: `ID=…&TYPE=FWDLGARM_R1&VER=0.0.30`— y salieron tal cual; `LOTE9041`
es del nuevo y se armó al transmitir. Los cuatro confirmados y borrados.

De paso quedó validado **el caso de uso principal de `lte clock`**: el equipo estaba en arranque frío
(`01/01/01 00:00:41 NO CONFIABLE`) y salió de ahí sin que nadie fuera al sitio.

#### ⛔ Se transmitió una sesión entera con el IMEI FALSO, y nada lo dijo

En esa misma corrida los frames salieron con **`ID=000000000000000`**. El IMEI se cachea recién
cuando alguien corre `lte info` —que hace `AT+IMEI?`— y esa sesión había empezado con `lte clock
set`. **El servidor los aceptó igual**, así que quedaron en la base atribuidos a un equipo que no
existe, y del lado del datalogger no hubo ni una señal.

Es exactamente contra lo que advertía el comentario de `wan_frame.h` al elegir los 15 ceros: *"ningún
equipo real lo tiene, así que un frame de prueba que llegara por error a producción sería rechazado
como equipo desconocido"*. La parte de "sería rechazado" resultó optimista — **este servidor no lo
rechaza**.

`wan_imei_es_falso()` + `prvAvisarImeiFalso()` en los tres comandos que transmiten (`ping`, `conf`,
`data`). ⚠ **Avisa pero no impide**, que es el criterio de este firmware: en banco a veces se quiere
transmitir sin haber leído el IMEI, y un comando que se niega en medio de una prueba es peor que uno
que advierte.

⏳ **Con la FSM (5d) esto pasa a ser una red de seguridad**: ahí el IMEI se lee al abrir la sesión,
que es cuando corresponde.

#### ℹ️ De paso: el contador de lotes saltó a `LOTE9041`

Tras el arranque frío, la SRAM del MCP79410 quedó con basura y `prvContadorLeer()` sólo filtra
`0xFFFFFFFF`. No rompe nada —el nombre se calcula con `% 10000` y los que ya existen se saltean— pero
los nombres quedan raros.

⏳ **Lo coherente sería reiniciarlo en el mismo momento en que se detecta que la FAT no es válida**:
si la pila falló, todo el estado de la SRAM es sospechoso, no sólo la FAT. Queda anotado; no se tocó
todavía para no mover dos cosas a la vez.

### ✅ Paso 5d: `tkWan`, la máquina de estados — el equipo transmite SOLO

`Application/tasks/tkWan.{h,c}`, portada de `FWDLGX_tkWAN.c`. **Validada en banco el 2026-09-21** (`0.0.58`): la primera
corrida encontró un bug, y con él corregido el equipo hizo la vuelta entera solo. Con esto el equipo deja de depender de que alguien tipee `lte data`: abre la
sesión, se configura, transmite y se apaga por su cuenta.

Los cuatro estados son los del AVR, y **cualquier fallo vuelve a `APAGADO`**:

| Estado | Qué hace |
|---|---|
| `APAGADO` | modem sin energía. Acá se decide **cuánto** esperar (es `u_get_sleep_time()`) |
| `OFFLINE` | enciende, entra en modo AT, lee identidad y red, pone el reloj en hora, sale de AT y prueba con un `PING` |
| `ONLINE_CONFIG` | `CONF_ALL` y los `CONF_*` que el servidor pida |
| `ONLINE_DATA` | vacía la ventana de la EEPROM y después los lotes de la microSD |

Volver al principio ante cualquier fallo no es pereza: **un fallo a mitad de sesión deja al módulo en
un estado que desde afuera no se puede saber** —puede haber quedado en modo AT, con una respuesta a
medio llegar en el buffer—. Apagarlo y empezar de nuevo es lo único que garantiza un punto de partida
conocido.

#### ⭐ La sesión de la FSM es la MISMA que hacen los comandos, no una copia

Todo el 5a-5c se validó primero como comandos de consola, uno por uno contra el servidor real. Al
escribir la FSM ese código **se movió a `tkWan.c`** —875 líneas desde `tkCmd.c`— y los comandos
`lte ping` / `lte conf` / `lte data` pasaron a llamar a `wan_sesion_ping()`, `wan_sesion_config()` y
`wan_sesion_datos()`.

Es el mismo criterio que `poll` con `tkSys_poll()`: si fueran dos caminos, **lo que se valida a mano
dejaría de ser lo que hace el equipo solo**, y el banco perdería su valor como evidencia.

⚠ Al mover 875 líneas entre unidades de compilación, `-fsyntax-only` **no alcanza**: el síntoma de un
símbolo que quedó del lado equivocado es un `undefined reference`, que sólo aparece al **linkear**.
Por eso la verificación fue compilar los 42 objetos y linkear el `.elf` entero.

#### ⭐ El reintento de las lecturas AT ES el poll de registro a la red

Es la parte no obvia del `OFFLINE`, y está dicha en el AVR: `ICCID` e `IMEI` salen enseguida porque
son del propio módulo, pero **`CSQ` y `CIP` dependen de que se haya registrado**, y eso tarda.
Reintentar con espera *es* esperar el registro.

Y si tras los intentos no hay IP, se vuelve a `APAGADO` sin intentar el `PING`: gastar un ciclo de
ping que va a fallar igual sólo cuesta batería. Es la misma lectura que cerró el diagnóstico del
2026-09-09 — **tener señal no es tener conexión**, lo que decide es `AT+CIP?`.

#### ⛔ Detectar la caída no sirve si nadie actúa: `wan_sesion_datos()` devuelve `bool`

Lo trajo Pablo al repasar la FSM (2026-09-21): *"Si cae lo detectamos porque DATA se va por
timeout"*. Cierto, y ahí estaba el hueco: la primera versión **detectaba** el timeout pero la FSM no
hacía nada con él — se quedaba en `ONLINE_DATA` reintentando cada `timerpoll` **con el modem
encendido contra un servidor mudo**, que es el peor consumo posible del equipo.

Ahora el vaciado informa si el enlace se cayó y la FSM vuelve a `APAGADO`, que es lo único que
reestablece: apaga, prende, reentra en modo AT, reespera el registro y rehace el `PING`.

⚠ **Lo que NO cambió es el borrado**: un vaciado interrumpido deja los registros sin confirmar en la
ventana, igual que antes. Eso ya estaba resuelto en el 5c y es por lo que `peek()` y `pop()` están
separados.

#### ⛔ Primera corrida: el `AT+ENTM` NUNCA SE MANDÓ (banco, 2026-09-21)

La FSM arrancó sola y llegó hasta el `PING`, y ahí se quedó dando vueltas:

```
tkWan:: OFFLINE
tkWan:: IMEI 860909055244702, senal 53 dBm negativos
-> ID=860909055244702&HW=SPQ_ARM_R1&TYPE=FWDLGARM&VER=0.0.57&CLASS=PING
<- ID=860909055244702&HW=SPQ_ARM_R1&TYPE=FWDLGARM&VER=0.0.57&CLASS=PING
+CME ERROR:58
tkWan:: sin PONG: apago y reintento despues
```

**La causa es de una línea, y el `(void)` la tapaba:**

```c
( void ) drv_lte_at( "AT+ENTM", NULL, 0U, 0U );     /* vuelta a transparente */
```

`drv_lte_at()` **rechaza `pcRta == NULL` devolviendo −1 antes de escribir un byte**. O sea que el
comando no salió nunca y el módulo siguió en **modo AT** — donde no transmite nada.

##### ⭐ La firma que lo identifica en un vistazo: EL ECO

Vale la pena aprenderla, porque el síntoma manda a buscar a la red y el problema está en el firmware:

| Lo que se ve | Qué significa |
|---|---|
| **la respuesta es el frame IDÉNTICO al que se mandó** | el módulo **ecoa** (`AT+E`), y eso **sólo pasa en modo comando**: en transparente los bytes se van a la red |
| **`+CME ERROR:58`** | *comando no soportado* — `ID=…&CLASS=PING` no es un AT válido. Es el mismo código que dio `AT+FTIME?` |

⚠ **`+CME ERROR:58` NO es "no registrado en la red"; ése es el `50`** (el que devuelve `AT+CIP?`
cuando no hay atache — ver el diagnóstico del 2026-09-09). Confundirlos manda a mirar la cobertura
cuando el problema es de modo.

Y hay una prueba adicional en la misma traza: `wan_sesion_identificar()` había devuelto **true**, o
sea que `AT+CIP?` contestó con IP. El módulo **estaba** atacheado.

**Corregido en `prvSalirDeModoAt()`**, que manda el `AT+ENTM` con buffer, **exige el `OK`** y aborta
la sesión si no lo ve. Mandar un frame sin haber salido de modo AT no puede funcionar nunca, así que
seguir adelante sólo gasta los cinco intentos de PING.

#### ⭐ El PING se reintenta, y los reintentos SON la espera del atache

Criterio de Pablo (2026-09-21): *"En el AVR se reintentan 3 PINGS con un espacio entre ellos. Esto
hace que los primeros puedan fallar pero luego el modem se atachea a la red y el ultimo conecta"*.

Portado de `wan_process_frame_ping()`, que usa **`PING_TRYES = 5`** — se dejaron los cinco del AVR.

⚠ **No hay espera entre intentos, y no hace falta: el espaciado lo da el propio timeout.** Cada
intento se queda hasta `LTE_PING_TIMEOUT_MS` (15 s) esperando el PONG, así que cinco son **75 s de
ventana**. El AVR hace lo mismo con su lazo de 15 esperas de 1 s adentro, y su comentario lo dice:
*"intento durante 2 minutos mandando un ping cada 10s"*.

**Esto NO reemplaza al chequeo de `AT+CIP?`**, y las dos esperas son distintas aunque miren el mismo
fenómeno: el `CIP` evita gastar el ciclo entero —incluido encender el modem— cuando el módulo ni
siquiera tiene IP; los PINGs cubren el tramo en que ya la tiene pero la red todavía no lo deja salir.
El AVR también tiene las dos.

#### ⚠ Un fallo impone una espera larga, aunque el modo sea CONTINUO

`WAN_SEG_TRAS_FALLO`, **120 s**. En la corrida de arriba se vio el problema en vivo: en `CONTINUO` la
espera de `APAGADO` es de **1 segundo**, así que el equipo se pasó la tarde prendiendo y apagando el
modem sin ninguna chance de converger.

Y no es sólo consumo: **cada ciclo le corta la alimentación a un módulo que estaba arrancando**, que
es exactamente lo que puede corromperle la flash interna (ver la advertencia de `drv_lte_power()`).

Los cinco caminos de error pasan ahora por `prvFalloVolverAApagado()`, que marca el fallo además de
volver al estado. En `DISCRETO` y `MIXTO` no cambia nada: ahí la espera normal ya es larga.

#### Qué hace cada modo, y dónde se decide

Todo en `prvSegundosApagado()`, que es `u_get_sleep_time()` del AVR:

| Modo | Espera en `APAGADO` |
|---|---|
| `CONTINUO`, **`RTU`** | **0**: no se apaga |
| `DISCRETO` | `timerdial` |
| `MIXTO` | 0 dentro de la ventana `PWRON..PWROFF`; si está afuera, **hasta que empiece** |
| **`SILENT`** | ⛔ no llega acá: el modem **no se enciende nunca** |

⚠ **`RTU` no tiene caso propio en el AVR**: cae en el `default` y su `base_print` lo llama *"(RTU)
continuo"*. Acá se hace explícito en vez de depender de un default — que un modo de operación dependa
de dónde cae un `switch` es exactamente el tipo de cosa que se rompe callada al agregar el modo
siguiente.

**La ventana de `MIXTO` cruza la medianoche cuando `PWRON > PWROFF`**, y ese caso está resuelto: la
comparación se hace con `||` en vez de `&&`. Y **sin hora válida `MIXTO` elige CONTINUO**, o sea
transmitir de más: un equipo que no reporta es indistinguible de uno roto.

#### ⭐ Con `tkWan` andando, `RTU` deja de descartar SIEMPRE

`wan_hay_enlace()` es lo que le faltaba a `tkSys` desde el 2026-09-12: en `RTU` una muestra se
transmite si hay enlace y **se descarta si no lo hay**. Hasta que existió esta tarea no había a quién
preguntarle, así que descartaba todo.

#### En `CONTINUO` no se reabre la sesión entera en cada vuelta

Tras vaciar, `ONLINE_DATA` **vuelve a sí mismo** esperando `timerpoll`, no a `OFFLINE`. Rehacer modo
AT, lecturas y `PING` en cada vuelta serían ~50 s de setup para mandar un frame.

#### La espera es troceada, y por dos razones distintas

`prvEsperar()` corta en trozos de 60 s:

1. ⏳ **El watchdog (paso 8) necesita un kick periódico**, y una espera de una hora entera no le daría
   lugar a ninguno. El lugar del kick ya está marcado en el lazo.
2. Con el tick a 512 Hz, `pdMS_TO_TICKS( segundos * 1000 )` **desborda un `uint32_t` a partir de
   ~2,3 h**. Troceando, cada espera es chica y el problema no existe. **El AVR tiene exactamente el
   mismo comentario**, con su propio número.

#### ✅ `kill wan`: el operador trabaja el módulo a mano, y después RESETEA

Criterio de Pablo (2026-09-21), copiado del AVR: *"Cuando un operador quiere trabajar con un módulo,
primero 'mata' la tarea que lo usa para no pisarse."*

⭐ **Y el `kill` del AVR saca además a la tarea del watchdog** (`WD_stop_task()`) — ése es el detalle
que lo hace correcto. Suspender una tarea sin desregistrarla haría que el watchdog la diera por
colgada y **reseteara el equipo justo mientras el operador está trabajando**, que es el síntoma más
desconcertante posible. Acá está previsto para cuando el watchdog exista.

⚠ **No hay comando para revivir una tarea, y es a propósito** (Pablo): *"la idea es que luego que un
operador entro en modo comando para pruebas o diagnóstico, al salir resete el datalogger para que
entre en modo de funcionamiento 'limpio'"*. Reanudar una tarea que quedó a mitad de una sesión la
dejaría creyendo cosas que ya no son ciertas.

La muerte es **cooperativa**: `wan_pedir_kill()` levanta una bandera y la tarea se suspende **en su
propio lazo**, después de apagar el modem y soltar el candado de energía. Un `vTaskSuspend()` desde
afuera lo dejaría encendido para siempre.

#### ⚠ Los stacks se agrandaron a propósito, y NO se ajustan por los números de Debug

A pedido de Pablo (2026-09-21): *"Dado que tenemos memoria RAM de sobra, si te parece asignale a cada
tarea y al heap un poco mas asi no quedan justos"*.

| Tarea | Stack (palabras) |
|---|---|
| `tkCtl` | 512 |
| `tkSys` | 1024 |
| `tkCmd` | 1024 |
| **`tkWan`** | **2048** — arma frames y habla con FatFs |

La RAM pasó de 35 a **44 KB de 256**. Apretar los stacks no compra nada y cuesta caro: un desborde en
FreeRTOS **no da un error, corrompe la memoria de al lado**, y el síntoma aparece en cualquier otro
lugar, horas después.

⚠ **Y hay una razón concreta para no ajustarlos por los *high water mark* de hoy: todo el bring-up
corre en Debug con `-O0`, que usa bastante MÁS stack que `-Os`.** Los números de Debug son
conservadores para Release —el cambio va en la dirección segura— pero no son los que van a valer.
`status` imprime el mínimo libre de las cuatro tareas; hay que volver a mirarlos sobre un binario
Release antes de campo.

⏳ **Pendiente para Pablo: `configTOTAL_HEAP_SIZE` sigue en 3000 bytes** y se configura desde CubeMX
(sugerido **16384**). No lo toco desde el `.c` porque desincronizaría el `.ioc`, que es la fuente de
verdad. Hoy no aprieta —las cuatro tareas son estáticas y no tocan el heap— pero FatFs y cualquier
cosa que entre después sí lo usan.

### ✅ Paso 5d VALIDADO EN BANCO (2026-09-21): el equipo transmite SOLO

⭐ **El criterio de aceptación era una vuelta entera sin que nadie tipeara nada**, y se cumplió con
`0.0.58`:

```
tkWan arrancando (modo CONTINUO)
tkWan:: APAGADO
tkWan:: OFFLINE
tkWan:: IMEI 860909055244702, senal 67 dBm negativos
-> …&CLASS=PING
<- "<html>CLASS=PONG</html>"

tkWan:: ONLINE_CONFIG
-> …&CLASS=CONF_ALL&UID=…&ICCID=…&CSQ=…&WDG=…&BH=…&AH=…&CH=…&MH=…&PH=…
<- "<html>CLASS=CONF_ALL&FLOWC</html>"
el servidor pide reconfigurar: FLOWC
  (FLOWC se pide SIEMPRE porque no mandamos su hash: se ignora)
no cambio nada: no se graba la EEPROM

tkWan:: ONLINE_DATA
vaciando la ventana: 48 registros
…
OK: 48 de 48 confirmados y borrados; quedan 0
transmitidos 48, quedan 1 en la ventana
```

| Criterio | Resultado |
|---|---|
| ⭐ **La vuelta entera, sola** | ✅ `APAGADO → OFFLINE → ONLINE_CONFIG → ONLINE_DATA` sin intervención |
| El `PING` | ✅ **al primer intento** — los 5 reintentos no hicieron falta esta vez |
| ⭐ **El hash sigue cerrando dentro de la FSM** | ✅ el servidor pide **sólo `FLOWC`**, igual que a mano en el 5b |
| El vaciado | ✅ **48 de 48** en 5 bloques de 10 |
| Los acuses rezagados | ✅ 3-4 descartados por bloque — el mecanismo del 5c anda igual acá |
| El `count` congelado | ✅ `transmitidos 48, quedan 1`: el que `tkSys` grabó durante el vaciado |
| El progreso `OK: N de M` | ✅ pedido de Pablo el 2026-09-18, validado de paso |
| En `CONTINUO` no reabre la sesión | ✅ queda en `ONLINE_DATA` esperando `timerpoll` |

#### Los números de RAM, con el equipo corriendo

```
heap libre   : 2992 bytes
  tkCmd :  888 de 1024        tkCtl :  497 de 512
  tkSys :  814 de 1024        tkWan : 1784 de 2048   (estado: ONLINE_DATA)
```

**`tkWan` usó 264 palabras de las 2048**, o sea el 13 %, y es la que más trabaja: arma frames, habla
con FatFs y con el modem. Los stacks quedaron holgados **a propósito** (ver arriba), y como Debug con
`-O0` usa **más** stack que Release, estos números son el techo.

⏳ **Corrección del pendiente del heap**: decía que había que subir `configTOTAL_HEAP_SIZE` de 3000 y
**el dato real lo desmiente como urgencia** — quedan **2992 de 3000 libres**, o sea que se usaron
**8 bytes**: las cuatro tareas son estáticas, FatFs está con `_USE_LFN = 0` y nada más pide memoria.
Sigue siendo razonable subirlo cuando se toque CubeMX, pero no bloquea nada.

#### ⚠ Un frame que se ve CORTADO puede ser la terminal, no el firmware

Pasó en esta misma corrida y costó una revisión del código: el `CONF_ALL` aparecía terminando justo
después del `ICCID`, sin los cinco hashes. **Era el ajuste de línea del minicom** (Pablo,
2026-09-21): sin *line wrap*, lo que excede el ancho de la terminal no se muestra.

⭐ **Lo que lo descartó sin tocar nada fue la respuesta del servidor**: contestó pidiendo **sólo
`FLOWC`**, y eso únicamente puede pasar si los cinco hashes llegaron y coincidieron. Es otra vez el
mismo método —**la otra punta es la que cierra el diagnóstico**— aplicado al revés: acá sirvió para
*descartar* un bug en vez de encontrarlo.

⚠ No confundirlo con el truncamiento **real** que sí existe y está documentado más arriba: `xprintf`
formatea en 160 bytes y corta de verdad. La diferencia es dónde mirar: si el frame viaja bien, es la
terminal; si el servidor lo rechaza, es el buffer.

#### ⏳ Lo que esta corrida NO probó

Todo lo anterior es el camino feliz en modo `CONTINUO`. Quedan sin ejercitar:

- **`kill wan`** y el trabajo manual del módulo después.
- **Los lotes de la microSD dentro de la FSM** — la ventana nunca llegó al 90 %, así que no había
  lotes que mandar. El camino está validado a mano en el 5c.
- **Los modos `DISCRETO`, `MIXTO`, `RTU` y `SILENT`** en la FSM (`BH` de referencia: `0xDF` y `0xD4`).
- **El camino de fallo con el backoff de 120 s**, que entró en esta misma versión.

## ✅ Paso 6: Modbus RTU

Pablo lo puso **antes que las consignas** (2026-09-21), y es el orden correcto por una razón que
apareció al relevar: **la consigna del control de presión ES Modbus** —habla por FC03/FC06 con el
esclavo `0x64`, registro 1—, así que el paso 7 no se puede hacer sin esto.

Partido en dos:

- **6a** — el **motor**: la transacción, los codecs y el comando de diagnóstico. Se valida contra
  cualquier esclavo, sin saber todavía el mapa del caudalímetro.
- **6b** — el **enganche al poleo**: los 5 canales configurados → `dr->modbus[]` en `tkSys`, con el
  riel y su tiempo de arranque. Ahí sí hacen falta los mapas reales.

La **configuración ya estaba** desde el paso 1 (`cfg_modbus`, 5 canales, y su hash `MH` validado
contra el servidor), el riel `EN_PWR_QMBUS` está en `drv_rs485` desde `v0.0.8`, y el candado de
energía lo toma `drv_rs485_power()` al prender el bus. Lo que faltaba era sólo el motor.

### ⭐ Se parte en dos capas porque el AVR tiene DOS caminos para hablar Modbus

Es el cambio de diseño del paso, y no es una manía de arquitectura. En FWDLGX conviven:

1. `modbus_io()`, atado a la struct de configuración de un canal.
2. **`cpres.c`, que arma las tramas byte a byte a mano** —`tx_buffer[0] = 0x64; tx_buffer[1] = 0x03;
   …`, con su propio llamado a `modbus_CRC16()`— porque lo único que quiere es leer el registro 1 de
   un esclavo fijo, y para eso el camino 1 no le sirve.

**Lo duplicado es justo lo que arma el CRC y valida la respuesta**, así que cada bug de esa parte
está en dos lugares y hay que acordarse de arreglar los dos. Acá:

| Capa | Dónde | Qué sabe |
|---|---|---|
| `drv_modbus.{h,c}` | `drivers/` | armar el ADU, transmitir, validar la respuesta. **Devuelve bytes crudos** |
| `modbus.{h,c}` | `tasks/` | codec, tipo y divisor: **de bytes a un float** |

Así el poleo usa la de arriba, **la consigna del paso 7 usa la de abajo sin armar una sola trama a
mano**, y hay un único sitio donde se arma un ADU. Es el mismo criterio que `wan_sesion_*`.

### ⛔ Lo que el AVR NO valida, y por qué cada una devuelve un número plausible y falso

Aquel da por buena cualquier respuesta con **largo ≥ 3 y CRC correcto**. Eso deja pasar tres cosas:

1. **La respuesta de OTRO esclavo.** Con dos dispositivos en el bus, uno que conteste tarde se toma
   como respuesta del que se está poleando. **Es el mismo mecanismo que los acuses rezagados de los
   `DATANR`**, que costó una tarde el 2026-09-18.
2. **La respuesta a OTRA función.**
3. ⛔ **Una EXCEPCIÓN Modbus.** El esclavo contesta `fcode | 0x80` más un código de error; eso tiene
   **CRC válido y largo 5**, así que pasa todos los chequeos del AVR y **el código de excepción se
   decodifica como si fuera el dato**. Pedir un registro inexistente devuelve un `2` perfectamente
   creíble. Es del mismo tipo que el signo que perdía el INA3221.

Y una cuarta en la escritura: **el AVR no mira el eco del FC06** (*"No se analiza la respuesta ya que
es echo"*), así que **una escritura rechazada se ve idéntica a una exitosa**. Importa justo donde más
duele: la consigna escribe un comando de válvula y después espera a que el equipo diga que terminó;
si la escritura no entró, lo que se espera no va a pasar nunca.

⭐ **El resultado dice QUÉ falló, no sólo que falló** (`mb_result_t`), y en el banco eso vale más que
el éxito — el mismo criterio que el `lteESC_SIN_A` / `lteESC_SIN_OK` del modem:

| | A dónde manda a mirar |
|---|---|
| `mbSIN_RESPUESTA` | el cableado y la dirección del esclavo |
| `mbCRC` | el ruido y la velocidad |
| **`mbEXCEPCION`** | ⭐ **el enlace está PERFECTO**: el problema es el registro que se pidió |

Con `false` a secas, los tres se ven iguales.

### ⭐ La recepción bloquea en el kernel: el AVR poleaba cada 50 ms

`modbus_rcvd_ADU()` mira el contador del buffer **cada 50 ms hasta un segundo**. Es exactamente lo
que prohíbe el checklist de portación: despertaría al micro 20 veces por segundo y **anularía el
tickless**.

Acá se usa `drv_rs485_read_frame()`, que bloquea en el kernel y corta por **silencio en la línea** —
que es, literalmente, la delimitación que define Modbus RTU. Sale más simple *y* más correcto, y
estaba disponible desde `v0.0.8`.

⚠ El silencio va en **6 ms**: el t3.5 a 9600 son 3,65 ms, pero **el piso de resolución es un tick,
1,95 ms**, y 6 ms son 3 ticks limpios. A 19200 esto quedaría al límite y habría que ir al registro
`RTOR` del USART.

### Funciones: 03, 04 y 06, y por qué no el juego completo

Pablo preguntó si convenía implementarlo entero (2026-09-21). **No**, y además de la regla de
siempre —no escribir código que no se puede validar— hay un argumento concreto:

⛔ **`cfg_modbus_set()` ya acepta `fcode` 3 o 4, pero el AVR sólo implementa el 3.** El 4 cae en
*"FNCODE no implementado"*, así que **se puede guardar una configuración que el equipo no sabe
ejecutar** y el canal falla recién al polear. El propio comentario del AVR dice *"Los canales pueden
ser holding_registers (0x03) o input_registers (0x04)"*: la intención estaba, la implementación no.

Y **la 04 es muy común en caudalímetros**, que suelen exponer las medidas como *input registers*.
Implementarla es gratis: misma trama salvo el byte del fcode.

| 03 | Read Holding Registers | el poleo |
| 04 | Read Input Registers | el poleo — **cierra el hueco de la config** |
| 06 | Write Single Register | la consigna del paso 7 |

El resto —01, 02, 05, 15 y el 16, que el AVR tiene comentado con sus encoders ya escritos— queda
afuera. Cuando aparezca un dispositivo que pida otra función, se agrega **con ese dispositivo en el
banco**.

### ⭐ Los cuatro codecs son una TABLA, no cuatro funciones

El AVR tiene cuatro funciones de ~20 líneas con asignaciones byte a byte. Son una permutación, y
**el nombre del codec es la permutación**: `C3210` manda el byte 0 del payload a `raw[3]`, el 1 a
`raw[2]`, y así.

```c
raw[ pucDest[ codec ][ i ] ] = payload[ i ];
```

⚠ **Pero las tablas de 16 y de 32 bits son distintas y no se puede deducir una de la otra.** Con 16
bits sólo hay dos permutaciones posibles, así que los cuatro codecs colapsan de a pares, y el AVR
elige `C3210 == C1032` (invertido) y `C2301 == C0123` (directo). **Eso es contrato con los
dispositivos que ya están instalados**: "arreglarlo" para que el 16 siga la lógica del 32 haría que
un caudalímetro configurado con `C3210` empiece a leer al revés.

#### ⭐ Validado sin hardware, contra el AVR y contra el bus

Dos comprobaciones, las dos con el código **extraído de los archivos reales**, no con copias:

- **Los codecs**: la tabla contra una transcripción literal de los cuatro `pv_decoder_f3_c*()`, en
  **60 casos** (4 codecs × 5 tipos × 3 payloads). **Cero diferencias.** De yapa, `42 C8 00 00` con
  `C3210` da `100.00`, que es lo que tiene que dar.
- ⭐ **El CRC16 contra TRAMAS REALES**: las seis capturas de un bus que quedaron documentadas en
  `cpres.c` (`64 03 00 01 00 01 DC 3F`, `64 06 00 01 00 05 11 FC`, …). Los seis CRC coinciden. Eso
  es mejor que compararlo contra otra implementación: valida contra lo que de verdad viajó por un
  cable.

### Los otros tres cambios (acordados con Pablo, 2026-09-21)

- **3 intentos por canal.** El AVR **no reintenta en el poleo** —una transacción y, si falla, NaN—
  pero sí reintenta 3 veces en `cpres.c`, que es el mismo bus. ⚠ El costo es tiempo con el riel
  prendido: 5 canales × 3 × 1 s de timeout son **hasta 15 s** en el peor caso.
  ⚠ **Dos fallas no se reintentan**: una **excepción** es el esclavo diciendo que ese registro no
  existe —va a contestar lo mismo las tres veces— y un parámetro inválido ni siquiera se transmitió.
- **`pow(10, n)` → tabla de 10 floats.** Era una llamada de doble precisión para elegir entre diez
  números conocidos.
- **`nro_regs` se acota en el SETTER**, contra `DRV_MODBUS_MAX_REGS` (16) y no contra el 125 del
  protocolo: el techo real es el buffer de recepción. El AVR acepta cualquier valor y después trunca
  la trama al recibir — o sea que la configuración se guarda bien y **el canal falla en campo**.

### ⛔ El error devuelve `false`, no un NaN

`modbus_leer_canal()` devuelve `bool`. En el AVR el error se codifica como `0xFFFFFFFF`, o sea un
**NaN flotante**, que después viaja en el `dataRcd`. Eso tiene dos problemas: impreso con `%.3f` sale
como `nan` —el servidor recibe una palabra donde espera un número— y **un NaN se propaga en silencio**
por cualquier cuenta que lo toque.

Con un `bool` el llamador está obligado a decidir, que es lo que este firmware hace en todos lados: el
centinela `-9999`, el `usInvalidos` del registro, la firma del RTC. ⏳ **Qué pone el frame lo define
el 6b**; lo natural es `-9999` y el `usInvalidos` que ya existe.

### El comando `modbus`

```
modbus                            estado del bus y de los 5 canales
modbus on | off                   los dos rieles: el SP3485 y el modulo
modbus debug on | off             traza hexadecimal de lo que sale y entra
modbus ch <0..4>                  lee UN canal configurado
modbus poll                       lee TODOS los habilitados
modbus read <sla> <reg> <nregs> <fcode> <tipo> <codec> <p10>
modbus write <sla> <reg> <valor>  funcion 06
```

⭐ **`read` arma un canal temporal y lo lee con `modbus_leer_canal()`**, o sea el mismo camino que el
poleo: codecs, tipos, divisor y reintentos. Un atajo propio podría comportarse distinto justo en lo
que se vino a probar.

`modbus on` prende **los dos** rieles y espera los 5 s de arranque del módulo. No duplica al comando
`rs485` —llama a la misma `drv_rs485_power()`— y evita el olvido más común del banco, que es prender
uno solo y no entender por qué nadie contesta.

ℹ️ **De paso**: el AVR prende `EN_PWR_QMBUS` **antes** de medir las analógicas, así el caudalímetro
arranca durante el barrido de 1,4 s del INA3221. El tiempo total es el mismo pero **no se paga**. El
poleo del 6b debería repetir el truco.

### ✅ Paso 6a VALIDADO EN BANCO (2026-09-21), contra un esclavo real

```
cmd>modbus read 9 4118 2 3 U32 C3210 0
MB TX (8):[09][03][10][16][00][02][20][47]
MB RX (9):[09][03][04][00][00][02][FE][F3][13]
766.000

cmd>modbus read 9 4118 2 3 U32 C1032 0       <- el MISMO registro, otro codec
50200576.000

cmd>modbus read 9 4592 2 3 U32 C3210 0       <- un registro que no existe
MB RX: nada en 1000 ms    (x3)
ERROR: SIN RESPUESTA (timeout)
```

⭐ **La validación que cierra el paso: la tabla de codecs PREDIJO los dos valores.** Corriendo el
código real sobre el payload `00 00 02 FE` que devolvió el esclavo, `C3210` da **766** y `C1032` da
**50200576** — exactamente lo que informó el equipo. Y el CRC calculado sobre las dos tramas de esa
sesión coincide con el que viajó por el cable.

| Criterio | Resultado |
|---|---|
| La transacción completa | ✅ el esclavo acepta el pedido y la respuesta se decodifica bien |
| ⭐ **El CRC contra un bus real** | ✅ pedido (`20 47`) y respuesta (`F3 13`), los dos |
| ⭐ **El codec cambia el valor como predice la tabla** | ✅ 766 y 50200576 sobre el mismo registro |
| Los 3 reintentos | ✅ tres TX antes de darse por vencido |
| El timeout | ✅ 1 s por intento |
| Validación de parámetros | ✅ `C1023` (un dedazo por `C1032`) rechazado con la lista de válidos |

#### ⚠ No todos los esclavos contestan con EXCEPCIÓN: algunos CALLAN

Pedir el registro `4592`, que en ese dispositivo no existe, no dio `mbEXCEPCION` sino **silencio**.
Es comportamiento del esclavo y no del firmware —la norma permite las dos cosas— pero conviene
saberlo por dos razones:

- ⏳ **El camino de excepción sigue sin ejercitarse.** Está escrito y es lo que evita que el código
  de error se decodifique como dato, pero hasta que un dispositivo lo dispare, no está probado.
- ⚠ **Y en el diagnóstico de campo, `SIN RESPUESTA` es ambiguo**: puede ser el cableado, la
  dirección del esclavo, la velocidad… **o un registro que no existe**. Si un canal nuevo no
  contesta, antes de revisar el bus conviene probar **otro registro del mismo esclavo**: si ése sí
  contesta, el bus está bien y lo que está mal es el mapa.

⏳ **Falta probar el `modbus write` (FC06)**, y ⚠ **no conviene hacerlo contra un caudalímetro**:
escribir un holding register puede cambiarle la configuración. Su lugar natural es el paso 7, contra
el control de presión, que es el dispositivo que se escribe por diseño.

### ✅ Paso 6b: el Modbus entra al poleo

`prvPolearModbus()` en `tkSys.c`. Los canales habilitados se leen en cada ciclo y quedan en
`dr->fModbus[]`, de donde los toma el frame.

**Usa `modbus_leer_canal()`, la misma función que el comando `modbus ch`** — no una copia. Mismo
criterio que `poll` con `tkSys_poll()` y que `lte data` con `wan_sesion_datos()`.

#### ⭐ El riel del módulo se prende TEMPRANO, y eso no es un detalle

Un caudalímetro tarda segundos en arrancar. El AVR prende `EN_PWR_QMBUS` **antes** de medir las
analógicas, así ese arranque transcurre **durante el barrido de 1,4 s del INA3221**: el tiempo total
es el mismo pero **no se paga**, porque se solapa con trabajo que había que hacer igual.

Acá se hace lo mismo con una mejora: en vez de esperar "2 s más" como el AVR —un número que deja de
valer si alguien cambia el `sensors_pwr_settle_time`— se **mide** cuánto pasó desde que se prendió y
se espera sólo lo que falte. Si las analógicas ya tardaron más que el arranque, no se espera nada.

**El transceiver se prende recién al polear**: está listo en microsegundos, y prenderlo toma
`pwrLOCK_RS485` — que es lo que evita que el tickless se coma bytes de las tramas. **Modbus es todo
ráfagas de bytes pegados**, exactamente el caso contra el que se puso ese candado el 2026-08-12.

#### El riel del módulo sólo se apaga si el equipo va a dormir

Es el `if ( u_get_sleep_time(false) > 0 )` del AVR. En continuo el poleo vuelve en `timerpoll`
segundos, así que apagarlo obligaría a pagar otra vez los 5 s de arranque en cada vuelta — además de
**ciclar la alimentación del caudalímetro una vez por minuto, para siempre**.

##### ⛔ Y ahí apareció un bug, al juntar las dos tareas

`wan_segundos_apagado()` (el `u_get_sleep_time()` portado) devolvía **0 para `SILENT`**, con el
comentario *"no debería llegar: SILENT ni entra al estado APAGADO"*. Era cierto **para `tkWan`**, que
nunca la consulta en ese modo.

Pero `tkSys` la usa para **otra pregunta**: *"¿el equipo va a dormir hasta el próximo ciclo?"*. Y con
un 0 dejaba el riel del caudalímetro **encendido para siempre**, justo en el único modo donde el
equipo está a batería y no transmite nunca. Exactamente al revés de lo que hace falta.

⭐ **La lección es sobre la función, no sobre el modo**: una misma función respondiendo dos preguntas
parecidas —"cuánto duerme el modem" y "¿el equipo duerme?"— tiene un caso donde las respuestas
divergen, y ese caso es invisible mientras haya un solo llamador. Ahora `SILENT` devuelve
`timerdial`, que es **literalmente cierto** (el modem va a seguir apagado ese tiempo y todos los que
vengan) y es lo que las dos preguntas necesitan.

#### Un canal que no se pudo leer viaja como -9999

Decisión de Pablo (2026-09-21), la misma que para las analógicas y el contador. Se marca en
`usInvalidos` —cinco bits nuevos, consecutivos, indexados con `dataINVALIDO_MODBUS0 << i`— la consola
dice `SIN_DATO`, y el frame emite **-9999**.

⛔ **Rellenar con cero sería lo peor**: un caudal de `0.000` es un valor perfectamente creíble, así
que un cero inventado se mezcla con los buenos y después no hay forma de separarlos. Es el mismo
criterio que la firma del RTC y el `estado_asumido` de la válvula.

Y cuando un canal falla **se dice por qué**, no sólo que falló:

```
MODBUS:: ch0 [CAU0] SIN DATO: SIN RESPUESTA (timeout)
```

Hay un `_Static_assert` que verifica que los canales entren en los 16 bits de `usInvalidos`: hoy
llegan al bit 11.

#### ✅ Validado en banco (2026-09-21): el canal muerto viaja como -9999

Dos canales configurados, uno contra el esclavo real y otro contra una dirección que no contesta:

```
cmd>poll
MODBUS:: ch1 [qa0] SIN DATO: SIN RESPUESTA (timeout)
21/09/26 17:17:24;CAU0=766.000;qa0=SIN_DATO;V0=0;bt3v3=3.273;bt12v=7.312;
  [!] campos sin dato (0x0100): modbus

cmd>frame
…&CAU0=766.000&qa0=-9999.000&V0=0&bt3v3=3.268&bt12v=7.332
  (148 bytes de 512)
  [!] hay campos en -9999: no se pudieron medir
```

| Criterio | Resultado |
|---|---|
| El canal bueno | ✅ `CAU0=766.000`, el mismo valor que el comando `modbus read` |
| ⭐ **El canal muerto** | ✅ `SIN_DATO` en consola, **`-9999.000`** en el frame |
| El motivo | ✅ `SIN RESPUESTA (timeout)`, y dice **qué canal** |
| El bitmask | ✅ `0x0100` = bit 8 = el canal 1, como corresponde |
| El largo del frame | ✅ 148 de 512 |

#### ⛔ Y un bug propio: los canales se imprimían DOS VECES

En la primera corrida la consola mostró `CAU0=766.000;qa0=SIN_DATO;CAU0=766.000;qa0=0.000;`. **Ya
existía** un bloque que imprimía los canales Modbus —escrito en el paso 2, cuando los valores eran
siempre cero— y yo agregué otro.

⚠ **Lo peor no era la duplicación sino la discrepancia**: el bloque viejo no consultaba
`usInvalidos`, así que **la misma línea decía `qa0=SIN_DATO` y `qa0=0.000`**. Si hubiera quedado sólo
el viejo, el canal muerto se habría impreso como un cero perfectamente creíble.

⚠ **Cómo me lo perdí, que es la parte útil**: busqué con `grep … | head` y el resultado quedó
**truncado exactamente en 10 líneas**, justo antes de las que importaban. Un `head` que corta
silenciosamente en el límite es indistinguible de "no hay más". Para verificar que algo *no existe*,
el `head` sobra.

De paso, el bloque que quedó se movió **después del contador**, para que la consola y el frame se
lean en el mismo orden.

## ✅ Paso 7: la doble consigna del control de presión

`Application/drivers/drv_cpres.{h,c}` (el diálogo) y `Application/tasks/tkCtlPres.{h,c}` (cuándo),
más el comando `cpres`. Portado de `ULIBS/cpres.c` + `XLIBS/consignas.c` + `FWDLGX_tkCtlPres.c`.

Es un dispositivo Modbus en el RS485, esclavo **`0x64`** fijo, registro **1**, alimentado por
`EN_PWR_CPRES` (PB15). **Por eso el paso 6 iba antes**: sin el motor Modbus esto no se puede hacer.

### ⭐ Una consigna no es una escritura: es una SECUENCIA que tarda

El dato que lo explica lo dio Pablo (**2026-09-22**) y no está en el código del AVR: **las
electroválvulas se mueven de a una, por consumo**, nunca a la vez. O sea que el FC06 sólo *arranca*
el trabajo; el dispositivo después se toma su tiempo.

Por eso el registro tiene el **bit 7 = RUN**, y por eso el diálogo son tres pasos donde ninguno
sobra:

1. leer el status y **esperar IDLE** — con el dispositivo trabajando, una orden nueva pisaría la
   anterior;
2. escribir el comando (FC06);
3. esperar y **releer hasta IDLE** — recién ahí la consigna se aplicó.

**Una consigna completa tarda 30 a 45 s.** Durante casi todo ese tiempo el micro **duerme**: lo único
encendido es un GPIO, que sobrevive al Stop 2 — el mismo razonamiento del INA3221 y la válvula TOYI.

### ⛔ El dispositivo NO recuerda dónde quedaron las válvulas

Dato de Pablo (2026-09-22): **al perder alimentación olvida la posición**; la sabe recién después de
una orden. Y eso explica un valor del status que parecía un caso raro: los bits de posición admiten
`2` y `3` = *desconocido*, y **ése es el estado normal cada vez que se lo enciende**.

⚠ **La posición FÍSICA sí sobrevive** —las válvulas quedan donde las dejaron, confirmado por Pablo—
así que lo que se pierde es *quién lo sabe*, no el estado del proceso. Tres consecuencias:

- **La posición leída no sirve como fuente de verdad**: el driver la informa para diagnóstico y nada
  más.
- **La orden es siempre absoluta**, nunca diferencial.
- **El único que podría saber qué consigna está aplicada es el datalogger.**

#### ⛔ Y por eso NO se guarda la consigna aplicada, aunque se podría

Se evaluó ponerla en la SRAM del MCP79410 —que sobrevive al reset— para ahorrarse el movimiento del
arranque. **Se descartó, y el argumento es el que importa:**

| | |
|---|---|
| Si el datalogger **recuerda** y recuerda **mal** —alguien movió las válvulas a mano, la pila falló— | **no lo corrige nunca**: queda con la consigna equivocada indefinidamente |
| Si **aplica siempre al arrancar** | es **autocorrectivo**: si estaba bien el movimiento es redundante, y si estaba mal lo arregla |

Con un dispositivo que no puede informar su estado, esa propiedad vale más que el movimiento que se
ahorra. **El AVR tiene razón y se copia tal cual.**

⚠ Lo que ningún diseño resuelve acá: si alguien mueve las válvulas a mano, el datalogger no se entera
hasta la próxima consigna.

### ⭐ Por qué 45 segundos: es muestreo al DOBLE de la resolución

La explicación es de Pablo y el número no es arbitrario. La configuración tiene resolución de **un
minuto**, así que muestreando cada 45 s **siempre caen una o dos muestras dentro de la ventana**:
*"La prioridad es que se ejecute la consigna si está configurada; que no se pierda."*

Por eso la comparación es de **igualdad exacta** de `hhmm` y no hace falta ninguna ventana, ni
recordar "ya se aplicó hoy", ni ningún estado.

⚠ **Y por eso hay que esperar al cambio de minuto después de ejecutar.** Si caen dos muestras en el
mismo minuto, la segunda volvería a mandar la orden. **El AVR se salva por accidente**: la consigna
tarda 30-45 s, así que el chequeo siguiente cae inevitablemente en otro minuto. Acá es **explícito**
(`prvEsperarCambioDeMinuto()`), porque depender de cuánto tarde el dispositivo es depender de un
número que no controlamos y que otro equipo podría bajar.

⭐ **El resultado es que el período se autoajusta**: cuando no hay nada que hacer muestrea rápido y no
pierde; cuando ejecuta, se sale del minuto y no repite.

ℹ️ **De paso, una preocupación mía que no era**: propuse cambiar la igualdad exacta por una ventana
—"¿qué consigna corresponde ahora?"— para no perder la consigna si se saltaba una vuelta. Pablo lo
desarmó con dos preguntas: con el tick a 512 Hz que esta tarea no corra en 45 s no es realista, y
**la ventana introducía un problema nuevo** —la condición seguiría siendo cierta las doce horas, así
que ante un dispositivo mudo reintentaría cada 45 s—. El diseño del AVR ya estaba bien.

### ⚠ El mutex del bus RS485 pasa a ser obligatorio

`tkSys` polea Modbus y `tkCtlPres` habla con el control de presión **por el mismo transceiver**.
Hasta ahora `drv_rs485` no tenía exclusión porque había un solo usuario; el AVR sí la tiene
(`sem_RS485`).

Sin ella, una consigna que caiga en medio de un poleo **intercala tramas**. Los CRC las descartan
—así que no hay datos falsos, que es lo importante— pero **los dos lados fallan sin entender por
qué** y el poleo pierde canales.

⚠ **Protege la SESIÓN, no la transacción.** Ponerlo adentro de `drv_modbus` no alcanzaría: cada
transacción quedaría atómica, pero una tarea podría **apagar el riel** mientras la otra está en medio
de su diálogo. Y el handshake del control de presión sólo tiene sentido si nadie se mete entre sus
tres pasos.

Los dos lados lo piden distinto, y la diferencia es deliberada:

| Quién | Espera | Por qué |
|---|---|---|
| `tkCtlPres` | **`portMAX_DELAY`** | perder la vuelta sería **perder la consigna** justo en el minuto en que había que aplicarla. Es el `rs485_ENTER_CRITICAL()` del AVR |
| `tkSys` | 60 s, y si no lo consigue **saltea el poleo** marcando los canales inválidos | mejor perder un poleo de Modbus que colgar a la tarea que además mide las analógicas, el RTC y guarda el registro |

### ⛔ Con la hora no confiable NO se aplica ninguna consigna

Diferencia deliberada con el AVR, que no lo chequea. Tras un arranque en frío el MCP79410 devuelve
`2001-01-01 00:xx`, y ese `hhmm` puede coincidir con una consigna configurada **por casualidad**.

**Aplicar la consigna nocturna a las diez de la mañana es peor que no aplicar nada**: el equipo de
presión queda operando mal y nadie se entera. Se usa el mismo chequeo que `tkSys` —la firma de la
SRAM más el año de compilación—.

⚠ El costo es que tras un arranque en frío la consigna queda sin aplicar **hasta que el reloj se
ponga en hora**, lo que pasa en la primera sesión con el `AT+CCLK?` del módulo o el `CLOCK=` del
servidor.

### El comando `cpres`

```
cpres                     configuracion, riel y estado de la tarea
cpres status              lee el registro del dispositivo
cpres diurna | nocturna   aplica una consigna
cpres open|close v0|v1    mueve una valvula externa
```

⚠ **`cpres status` es la única forma de ver el registro sin mandar una orden**, y sirve para medir
cuánto tarda de verdad el dispositivo: leerlo antes y después de una orden acota los tiempos que hoy
son los del AVR y están **sin verificar**.

⚠ Los comandos **no pasan por `tkCtlPres`**: si la tarea está viva puede aplicar una consigna en el
medio. Para trabajar tranquilo, **`kill cpres`** — mismo criterio que `kill wan`.

### ⏳ Lo que este paso NO incluye todavía

Las órdenes **`VOPEN`/`VCLOSE`/`EXT_V0/V1_*`** que el servidor manda en la respuesta a un frame de
datos. La infraestructura está —`tkCtlPres_orden()` las recibe por notificación, que es lo que hace
el AVR para que `tkWan` no se bloquee 45 s en medio de una sesión— pero **falta parsearlas en
`wan_frame`**. Entran después de validar la consigna, que es lo que se puede probar hoy.

### Tres arreglos de la consola (Pablo, 2026-09-22)

- **`modbus` y `cpres` no figuraban en el `help`.** Registrar un comando y agregarlo a la tabla de
  ayuda son **dos pasos independientes**, y el segundo es fácil de olvidar porque el comando funciona
  igual. Ahora están los dos.
- **`status` dice si cada tarea está viva o matada.** Hacía falta: el `kill` es **cooperativo**, así
  que entre pedirlo y que ocurra pasa hasta una vuelta entera de esa tarea, y no había forma de
  confirmar que se hubiera suspendido de verdad. Se informan **los dos estados por separado**
  —`MATADA` cuando `eTaskGetState()` dice `eSuspended`, y `kill PEDIDO (todavia corriendo)` cuando la
  bandera está puesta pero la tarea no llegó—, porque confundirlos haría creer que se puede tocar el
  periférico **mientras la tarea todavía lo usa**.
- **`status` muestra la ocupación de la ventana**: registros ocupados, libres y el porcentaje, más
  los **pisados** si los hubo y los lotes de la microSD sin transmitir. No es un adorno: es **cuánto
  aguanta el equipo sin transmitir**, y los pisados son registros que ya se perdieron — información
  de campo que antes había que ir a buscar con `fs`.

#### ⛔ Y de paso: el `help` MENTÍA sobre el parser

Decía *"(matchea por prefijo: 'res'/'reb', 'st'/'se'…)"*, y eso **dejó de ser cierto el
2026-09-08**, cuando el parser pasó a exigir el comando completo. Quedó ahí catorce días.

**Una ayuda que miente es peor que no tener ayuda**: el que la lee prueba `st`, no funciona, y
termina dudando de la consola en vez del texto. Ahora dice `el comando va COMPLETO: 'status', no
'st'`.

⚠ Y el propio `help <comando>` **todavía matcheaba por prefijo**, que era la última pieza del
mecanismo viejo: `help c` caía en el primer comando que empezara con `c` sin decir por qué. Ahora usa
igualdad, la misma regla que el parser.

#### ⛔ El bus mete un byte de RUIDO, y la lectura por trama lo tomaba por la respuesta

Encontrado en banco el **2026-09-22**, en la primera prueba contra el control de presión:

```
MB TX (8):[64][03][00][01][00][01][DC][3F]
MB RX (1):[00]                              <- y ahí se cortaba
ERROR: trama demasiado corta
```

La trama de salida era **idéntica byte a byte** a la capturada del AVR, así que ese lado estaba
descartado de entrada.

⭐ **Lo que cerró el diagnóstico fue pedirle a una dirección que NO EXISTE**: `modbus read 99 …`
devolvió exactamente el mismo `[00]`. O sea que **el byte lo genera el bus, no el esclavo** — es el
artefacto que deja la línea al soltarse el DE: sin nadie manejando el par, el receptor ve un nivel
indefinido, interpreta un start bit falso y entrega un byte de ceros.

Es el mismo método de siempre —**cambiar una sola cosa y ver si el síntoma sigue**— y costó un
comando.

⚠ **Y el bug era nuestro.** `drv_rs485_read_frame()` espera el primer byte y a partir de ahí corta al
primer silencio de `t3.5`, así que ese byte espurio **terminaba la lectura** y la respuesta de verdad
—que llega decenas de ms después— se perdía.

⭐ **Por qué el AVR no lo sufre, y es el patrón de los acuses rezagados otra vez**: aquel **acumula
todo durante un segundo** en un buffer lineal y recién después busca la respuesta ahí adentro, así
que la basura del principio le queda delante sin molestar. Leer una trama delimitada por silencio es
mejor para todo lo demás —bloquea en el kernel en vez de polear cada 50 ms— pero **tiene que tolerar
lo que aquel buffer toleraba sin pensarlo**.

`prvLeerRespuesta()` lo resuelve con tres cosas, y ninguna sobra:

1. **Sigue leyendo** hasta juntar el largo esperado o agotar el timeout total, en vez de conformarse
   con la primera trama.
2. **Descarta el prefijo hasta la dirección del esclavo.** Modbus RTU no tiene byte de inicio, así
   que lo primero que puede ser una respuesta es el SLA: cualquier cosa antes es ruido por
   definición.
3. **Trunca al largo esperado**, porque el mismo artefacto puede aparecer **después** — y ahí sería
   peor: el CRC se calcularía tomando el byte de ruido como parte de la trama y **fallaría una
   respuesta buena**.

⚠ Una **excepción** son 5 bytes y no el largo esperado, así que se reconoce por el bit 7 del fcode o
se esperaría el timeout entero por una respuesta que ya llegó completa.

ℹ️ **Esto no apareció con el caudalímetro** —que contesta rápido y bien— y por eso el paso 6 se
validó sin verlo. El ruido del DE estaba ahí igual; lo que cambió es que un dispositivo más lento
deja la ventana abierta para que se note.

#### ⛔ Este dispositivo NO devuelve el eco del FC06: devuelve su STATUS

Salió de leer su firmware (`SPQ_AVRDA/AUXBOARDS/CONTROL_PRESION/`, que Pablo pasó el 2026-09-22), y
**habría hecho fallar la consigna siempre**:

```c
/* modbus_slave_process_frame06() */
mbus_cb.tx_buffer[4] = 0x0;                          // Reg.value
mbus_cb.tx_buffer[5] = systemVars.status_register;   // <- el STATUS, no el eco
```

Sus propias capturas lo muestran: a un pedido de `05` contesta `01`, y a uno de `06` contesta `04`.
Mi `drv_modbus_escribir()` exigía el eco **del valor** y lo habría rechazado en cada orden.

Ahora se verifica **sólo la dirección del registro**, que sí es eco y sigue teniendo valor: un
esclavo que conteste sobre otro registro no entendió el pedido.

⭐ **Y el valor devuelto pasó de estorbo a dato útil**: es el status **con el bit RUN ya puesto**, o
sea la confirmación de que el trabajo arrancó. Sale por `pusRespuesta`, así que `drv_cpres` lo informa
sin pagar una lectura extra.

#### Lo demás que confirmó su firmware

| | |
|---|---|
| **Velocidad** | `frtos_open(fdRS485, 9600)`, 8N1 — igual que nosotros |
| **Dirección** | `MODBUS_LOCALADDR 0x64` = 100 |
| **FC03 reg 1** | status: responde 7 bytes (`5 + 2·1`), con el status en el byte bajo |
| ℹ️ **FC03 reg 2** | **WATER LEVEL** — un registro que no sabíamos que existía. No se usa hoy |

⚠ **Su FSM de recepción no tiene timeout ni valida CRC: cuenta 8 bytes.** Si alguna vez se
desincroniza —un byte espurio después de su dirección— **se queda esperando para siempre** y no se
recupera sola: hay que cortarle la alimentación. Como el datalogger se la corta en cada orden, en la
práctica se resuelve solo, pero explica por qué conviene no hablarle apenas se enciende.

⚠ **Y su tarea de RS485 espera `starting_flag`**, o sea que el dispositivo tarda en estar listo. Los
**12 s** que espera el AVR antes del primer diálogo (2 de arranque + 10 de estabilización) no son un
número caprichoso.

#### ✅ El dialogo ANDA (banco, 2026-09-22), y el status confirma lo del dispositivo

Con el arreglo del ruido, el primer diálogo con el control de presión:

```
MB TX (8):[64][03][00][01][00][01][DC][3F]
MB: descartados 1 byte(s) de ruido antes de la respuesta
MB RX (7):[64][03][02][00][0A][74][4B]
```

⭐ **El `0x0A` confirma en vivo lo que Pablo había dicho del dispositivo:**

| `0000 1010` | |
|---|---|
| bit 7 = **0** | **IDLE** |
| bits 1-0 = **`10`** = 2 | **V0 desconocida** |
| bits 3-2 = **`10`** = 2 | **V1 desconocida** |

Recién encendido **no sabe dónde están las válvulas** y lo dice en las dos. El valor `2` que en el
código del AVR parecía un caso raro es, efectivamente, **el estado normal al arrancar** — y eso es lo
que hace que la posición leída no sirva como fuente de verdad.

##### `cpres status` respeta el estado previo de los rieles

No es cosmético: ese comando existe para **medir cuánto tarda el dispositivo**, leyendo el status
repetidamente después de una orden hasta que el bit RUN baje. Si apagara los rieles al terminar, cada
lectura le cortaría la alimentación —y con eso **el dispositivo olvida las válvulas y reinicia su
FSM**—, así que la medición sería imposible.

A diferencia de `drv_cpres_comando()`, que sí hace el ciclo completo porque es la operación normal.

⚠ Y cuando **sí** tiene que encenderlo, espera los **12 s** completos y no los 2 del transceiver: su
tarea de RS485 aguarda a que su sistema termine de arrancar, así que hablarle antes es hablarle al
vacío. Eso fue justamente lo que falló en el primer intento.

#### ⭐ La espera larga es por el MOVIMIENTO, no por el arranque (Pablo, 2026-09-22)

Corrige un tiempo que estaba mal repartido. Textual: *"en la medida que NO movemos las
electroválvulas, no hay que esperar más de 1 segundo para que el micro de la doble consigna se active
y responda. La espera larga es sólo cuando damos algún comando que mueve las válvulas."*

⛔ **El AVR espera 10 s antes del primer diálogo** (`cpres_send_command()`), y yo lo había copiado
suponiendo que el dispositivo tardaba en estar listo — su tarea de RS485 aguarda un `starting_flag`,
lo cual parecía confirmarlo. **No es así**: arranca y contesta en un segundo, y esos 10 s eran
precaución heredada.

| | antes | ahora |
|---|---|---|
| Leer el status sin mover nada | 12 s | **1 s** |
| Un ciclo de consigna completo | ~45 s | **~14 s** |

⏳ Lo que sigue sin medir es cuánto tarda **el movimiento** (`MS_EJECUCION`, 10 s). Pablo lo dejó
así: *"son sólo 2 movimientos al día."*

#### ⚠ Y `cpres status` avisa cuando apaga los rieles

Porque si no, el comando **siguiente** falla con `el riel del RS485 esta apagado` y parece un error de
la nada. Pasó en banco: un `cpres status` con los rieles apagados los prende, lee, los apaga — y el
`modbus write` que venía después se encontró con el bus muerto.

⭐ **La orden completa quedó validada en la misma corrida:**

```
cmd>modbus write 100 1 1              <- abrir V0
OK: sla=100 reg=1 <- 1 ; el esclavo devolvio 0x000A

cmd>cpres status
status = 0x08: IDLE
  V0: abierta            <- ya NO es "desconocida"
  V1: desconocida        <- ésta no se movió, así que sigue sin saberse
```

Las dos cosas que confirma: el FC06 **entra** —y devuelve el status, no el eco— y **la posición pasa
a conocerse sólo para la válvula que se movió**, que es exactamente lo que describe el dispositivo.

### ✅ Paso 7 VALIDADO EN BANCO (2026-09-22): la consigna se aplica SOLA

```
cmd>config
  consigna: true, diurna=1222, nocturna=2300
cmd>rtc
  fecha/hora: 2026-09-22 12:21:59 (mar)

tkCtlPres:: son las 12:22 -> consigna DIURNA
CPRES:: consigna DIURNA
CPRES:: orden aceptada, status 0x0A (todavia IDLE)
CPRES:: consigna DIURNA: OK
```

| Criterio | Resultado |
|---|---|
| ⭐ **La tarea dispara sola a la hora exacta** | ✅ 12:22, sin que nadie tipeara nada |
| El ciclo completo del driver | ✅ `OK` |
| No se repitió dentro del minuto | ✅ la espera al cambio de minuto hizo lo suyo |
| El `status` nuevo | ✅ `tkSys MATADA`, `tkWan MATADA (APAGADO)`, `tkCPres activa` |

#### ⛔ `todavia IDLE` es lo NORMAL, y desmiente un comentario que yo había escrito

Ayer anoté que el valor devuelto por el FC06 era *"el status con el bit RUN ya puesto, o sea la
confirmación de que el trabajo arrancó"*. **La traza lo desmiente**: devolvió `0x0A` —IDLE— justo
después de aceptar la consigna.

Su firmware lo explica, y es obvio una vez visto:

```c
xTaskNotify( xHandle_tkSys, 0x01, eSetBits );        // avisa a su propia tarea
...
mbus_cb.tx_buffer[5] = systemVars.status_register;   // y contesta el status de ANTES
```

**Contesta antes de arrancar el trabajo.** Lo que ese valor confirma es que **el dispositivo estaba
libre cuando aceptó la orden** — que también sirve, pero es otra cosa.

⚠ **Y de ahí sale por qué la espera a ciegas después del FC06 no es pereza**: si se leyera el status
enseguida, devolvería el IDLE *de antes* y el comando se daría por terminado **sin que nada se haya
movido**. Con los 10 s contra un movimiento que dura más, el caso no se puede dar; si alguna vez ese
número se acorta, hay que reemplazarlo por *"esperar a ver el RUN puesto y recién entonces esperar a
que baje"*.

#### `status` informa la última consigna aplicada

Pedido de Pablo (2026-09-22), y sólo aparece si la doble consigna está habilitada:

```
doble consigna: diurna 1222, nocturna 2300
  ultima: consigna DIURNA a las 12:22 -> OK
```

⚠ Dice **"desde que arrancó el equipo"** cuando no hay ninguna, y eso es a propósito: **es una
creencia en RAM que se pierde con el reset**. No se persiste por lo explicado más arriba —recordarla
mal sería peor que no recordarla, porque el equipo dejaría de corregir— así que sirve para ver *qué
hizo el equipo*, **no** para saber en qué estado está el dispositivo, que de hecho nadie puede saber.

La **hora** de la última aplicación se informa porque importa: una consigna que falló hace diez horas
no es lo mismo que una que falló recién.

### ⏳ Lo que falta del paso 7

Las órdenes **`VOPEN`/`VCLOSE`/`EXT_V0/V1_*`** que el servidor manda en la respuesta a un frame de
datos. `tkCtlPres_orden()` ya las recibe por notificación; **falta parsearlas en `wan_frame`**.

## ⛔ FLOWCONTROL: se implementó entero y se ELIMINÓ el mismo día (2026-09-22)

Decisión de Pablo, y queda anotada porque **el trabajo de volver a ponerlo no es el problema; el
problema sería no saber por qué se sacó**:

> *"Eliminamos todo lo que tiene que ver con FLOWC. Esta es una funcionalidad que aún no la estamos
> usando así que no vamos a ensuciar el firmware con features que no se usan. Ya modifiqué el
> servidor de modo que no responda más a la configuración con un FLOWC. Tampoco tenemos que mandar el
> hash."*

Se borró: `cfg_flowcontrol.{h,c}`, su bloque en la EEPROM, el hash **`FH`**, el `CONF_FLOWC`, su
aplicador y los comandos `config flow`. **Las dos puntas están de acuerdo**: el servidor ya no lo pide
y el equipo manda **cinco hashes**.

⚠ **Esto NO es volver a la situación transitoria de antes.** Hasta el 2026-09-11 se mandaban cinco
hashes porque el bloque no existía **y el servidor sí lo esperaba**, así que `CONF_ALL` no podía
contestar `CONFIG=OK` nunca. Ahora los cinco son el contrato completo.

⭐ **La regla de que la FSM pase a transmitir datos aunque queden bloques pedidos sigue valiendo
igual**, y no hay que relajarla ahora que el motivo original desapareció: era lo correcto ante el
`FLOWC` que no se podía satisfacer, y lo sigue siendo ante *cualquier* bloque que falle.

### Qué era, por si alguna vez vuelve

⚠ **El nombre engaña y por eso estuvo mal anotado en el plan**: figuraba como que dependía del paso
2b (el EMA del contador). **Es falso.** Flowcontrol es **un temporizador semanal para la
electroválvula TOYI interna**: 14 slots `{día, hhmm, abrir|cerrar}`, sin relación con ningún caudal.

Estructuralmente es **lo mismo que `tkCtlPres`**: tabla de horarios, igualdad exacta de `hhmm`,
acción sobre una válvula. Si vuelve, eso ya está resuelto ahí.

El contrato, relevado y **validado contra la réplica del AVR** antes de borrarlo (los tres casos
coincidieron: `0x24` en defaults, `0xCB` habilitado, `0xC9` con dos slots):

```
hash:      [TRUE] o [FALSE], y por cada slot  [SLOT%02d:%02d,%04d,OPEN|CLOSE]
defaults:  enabled=false, dow=8, ptime=0, action=OPEN
frame:     CLASS=CONF_FLOWC&HASH=0x..      <- se MANDA como CONF_FLOWC
respuesta: CLASS=CONF_FLOWCONTROL&ENABLE=..&S00:--,0000,CLOSE&..   <- y vuelve como CONF_FLOWCONTROL
```

`0x00300` en la EEPROM **queda libre y sin reutilizar**: si vuelve, que caiga donde estaba.

### ⭐ Lo que SÍ quedó, y no es flowcontrol

**`tkFlow`**, reducida a una sola cosa: atender las órdenes **`VOPEN` / `VCLOSE`** que el servidor
manda en la respuesta a un frame de datos, sobre la **válvula TOYI interna**.

| Orden del servidor | Quién | Qué mueve |
|---|---|---|
| `VOPEN` / `VCLOSE` | **`tkFlow`** | la TOYI **interna** (un GPIO) |
| `EXT_V0/V1_OPEN/CLOSE` | **`tkCtlPres`** | las del **control de presión** (Modbus) |

⭐ **Van por notificación y no por llamada directa**: mover la TOYI son 5 s y una orden al control de
presión ~14, y hacerlo dentro de `tkWan` dejaría la sesión con el servidor congelada en medio de un
vaciado. Es lo que hace el AVR y por lo que existen las dos tareas.

⚠ Y `tkFlow` **no abre la válvula al arrancar**, aunque el AVR sí lo haga
(`VALVE_DEFAULT_ACTION()` = `VALVE_open()`): es la decisión del 2026-08-18 — son 5 s de motor en cada
reset, incluidos los diez seguidos de una sesión de flasheo.

### Tres cosas que se aprendieron y NO se borran con el código

1. ⛔ **El comentario del AVR y su código se contradicen en los slots.** El comentario dice
   `S0=…,S13=…` (un dígito) y el código busca `S%02d` → `S00`. **Si el servidor manda un dígito, el
   AVR no encuentra los slots 0 a 9.** Puede ser un bug vivo en producción. Es el mismo patrón de
   `CONF_COUNTERS`, donde el comentario mostraba cuatro campos y el código parseaba seis — y **ahí el
   comentario tenía razón**.
2. ⛔ **El servidor separaba los slots con `:` y no con `=`** (`S00:--,0000,CLOSE`), mientras que el
   `ENABLE` iba con `=`. ⚠ **Criterio de Pablo**: *"El servidor debe mandar tokens similares (el mismo
   separador) en todas las configuraciones."* El AVR nunca se enteró de la inconsistencia porque
   busca sin separador y después tokeniza con `&,;:=` — o sea que **si hay otros lugares del servidor
   con `:`, nadie lo habría notado**.
3. ⚠ **Un campo que entra en el hash y que los dos lados no guardan igual no cierra nunca.** Acá el
   candidato era el día `--` de un slot vacío: el equipo lo guarda como `8` y no se llegó a saber qué
   número usa el servidor. Es el modo de falla del `PST` de ainputs, y la herramienta que lo resuelve
   es la misma: comparar **los strings** del hash, no los valores.

### ⭐ `CONFIG=OK`: el contrato de configuración CIERRA COMPLETO (banco, 2026-09-22)

```
-> …&CLASS=CONF_ALL&UID=…&ICCID=…&CSQ=…&WDG=…&BH=…&AH=…&CH=…&MH=…&PH=…
<- "<html>CLASS=CONF_ALL&CONFIG=OK</html>"

CONFIG=OK: la configuracion del equipo coincide con la del servidor
```

**Es la primera vez.** Hasta acá `CONF_ALL` **no podía** contestar `OK` ni con la configuración
perfecta: el servidor pedía un `FLOWC` que el equipo no mandaba, y eso era estructural, no un
desajuste. Con el bloque eliminado de las dos puntas, los cinco hashes son el contrato entero.

⭐ Y cierra una cadena que empezó el 2026-09-11: la predicción del hash en el host, la aplicación en
el equipo, la aceptación del servidor, y ahora **el acuerdo completo**.

La vuelta salió entera: `PING` → `CONF_ALL` con `OK` → `ONLINE_DATA` → `1 de 1 confirmados`.

#### ⏳ Pendiente menor: el `CSQ` viaja en 0

```
tkWan:: IMEI 860909055244702, senal 0 dBm negativos (NO es una medida)
```

**Ese valor es imposible**: con `|dBm| = 113 − 2·rssi` no hay entero que dé 0 (56 da 1 y 57 da −1).
O sea que **`wan_csq_set()` no llegó a llamarse** — el `strstr( pcRta, "+CSQ:" )` no encontró nada,
así que la respuesta al `AT+CSQ` no llegó o llegó desfasada. Es probablemente el mismo fenómeno de
las respuestas rezagadas que ya apareció con los acuses de los `DATANR` y con el ruido del DE en
Modbus.

⚠ **No bloquea** —el `CSQ` es identidad, no configuración, y no entra en ningún hash, por eso el
servidor aceptó igual— **pero el servidor está registrando señal 0 para este equipo**, y en campo ése
es justo el dato que se quiere mirar cuando algo no transmite.

### ⚠ La versión sube en CADA entrega a banco

Regla de Pablo, 2026-09-08: *"hay que avanzar la version de compilacion en cada caso asi sabemos que
firmware estoy usando"*. **Vamos por `0.0.X` durante toda la fase 2; al terminar la aplicación, pasa
a `1.0.0`.**

No es burocracia: cuando se puso la regla, `FW_VERSION` en `main.h` decía **`"0.0.8"`** mientras los
tags de git iban por **`v0.0.14`** — o sea que el banner mentía sobre qué firmware estaba corriendo,
que es justo la pregunta que más veces hubo que contestar en el bring-up. Se puso al día en
**`0.0.15`**, **continuando la serie de los tags** para que no queden dos numeraciones conviviendo
(era la arruga que estaba anotada acá).

Los tres campos que identifican al equipo viven juntos en `main.h`, bloque *USER CODE*, y son los que
viajan en el frame:

```c
#define FW_NOMBRE   "FWDLGARM_R1"   /* el BANNER de la consola, NO el frame */
#define FW_TYPE     "FWDLGARM"      /* = TYPE: el tipo de firmware, SIN revisión */
#define FW_VERSION  "0.0.73"        /* = VER                                 */
#define FW_HW       "SPQ_ARM_R1"    /* = HW: la PLACA, con su revisión       */
```

`status` los imprime tal cual van a viajar, así se verifica de un vistazo con qué identidad se
presenta el equipo. Y `FW_FECHA` sale de `__DATE__`/`__TIME__`: **un número de versión se olvida de
subir, la fecha de compilación no miente nunca** — por eso están las dos cosas.

### El plan, y en qué paso estamos

| # | Paso | Estado |
|---|---|---|
| **1** | **Configuración persistente** en la M24M01 (5 bloques, hashes, comandos) | ✅ **validado en banco el 2026-09-08** |
| **2** | `dataRcd` y el poleo (`tkSys`) | ✅ **validado el 2026-09-09** (falta el caudal → 2b) |
| **2b** | ⏳ **El caudal del contador** — EMA por pulso, decay y slew-rate | pendiente, ver abajo |
| **3** | ⭐ **El frame, sin modem** | ✅ **anda en banco**; ⏳ falta compararlo contra un AVR real |
| **4** | Almacenamiento: FS circular sobre la EEPROM | ✅ **validado el 2026-09-09** |
| **4b** | La microSD como extensión: la EEPROM es una VENTANA | ✅ **validado el 2026-09-09** |
| **5a** | **La sesión mínima: configurar el módulo y el `PING`** | ✅ **validado el 2026-09-09** |
| **5b-1** | `CONF_ALL`: los hashes y qué pide el servidor | ✅ **validado el 2026-09-11** |
| **5b-2** | Los `CONF_*`: parsear y aplicar la configuración | ✅ **validado el 2026-09-11** — el hash cierra en la 2.ª sesión |
| **5c** | Los frames de datos y el vaciado | ✅ **VALIDADO el 2026-09-21**: ventana y lotes |
| **5d** | **`tkWan`: la FSM. El equipo transmite solo** | ✅ **VALIDADO el 2026-09-21** |
| **6a** | **Modbus: el motor** (transaccion, codecs, comando) | ✅ **VALIDADO el 2026-09-21** |
| **6b** | Modbus: el enganche al poleo de `tkSys` | ✅ **VALIDADO el 2026-09-21** |
| **7** | Consigna (`tkCtlPres`) — ⚠ **es Modbus** | ✅ **VALIDADO el 2026-09-22** |
| ~~7b~~ | ~~`tkFlow`/flowcontrol~~ | ⛔ **ELIMINADO el 2026-09-22**: no se usa. Quedan sólo las órdenes `VOPEN`/`VCLOSE` |
| 8 | Watchdog cooperativo + `tkCtl` definitivo | |
| 9 | Pulido: sync del RTC, `BOR_LEV`, Release, consumo | |

✅ Los modos `PWR_RTU` y `PWR_SILENT` **entraron el 2026-09-12** — ver la sección de los cinco modos.

### ⭐ El objetivo es SUSTITUIR a FWDLGX, así que el criterio es PARIDAD FUNCIONAL

Dicho por Pablo el **2026-09-12**: *"la idea que tengo es terminar con este firmware y probarlo en
campo para poder estar seguros de sustituir al FWDLGX sin problemas. Luego vemos si agregamos mqtt.
Aún falta consignas, modbus, flowcontrol, etc."*

Dos consecuencias que cambian lo que estaba anotado:

1. ⚠ **`tkFlow`/flowcontrol vuelve al alcance.** Estaba afuera desde el 2026-09-07; si el equipo tiene
   que reemplazar al AVR en campo, tiene que hacer lo mismo que él. **Depende del paso 2b** (el
   caudal), que es su insumo.
2. ⚠ **Entonces el `FH` deja de ser una omisión permanente.** Hoy mandamos cinco hashes y el servidor
   pide `FLOWC` en todas las sesiones; cuando flowcontrol exista, se manda el sexto y eso se termina.
   **La regla de que la FSM pase a transmitir datos aunque queden bloques pedidos sigue valiendo
   igual** —es lo correcto ante cualquier bloque que no se pueda configurar—, pero deja de ser el
   único motivo por el que la configuración nunca cierra.

⏳ **MQTT queda para DESPUÉS de campo.** El módulo lo soporta nativo (sección 5.4 de su manual, desde
la versión de firmware 1.3.25) y `wan_frame.c` ya está desacoplado del transporte —arma strings, no
habla con el modem—, así que el hash y los frames sobrevivirían intactos. Lo que cambia es el modelo
de sesión: HTTP es request/response síncrono y MQTT es asíncrono. **Rinde en `RTU` con alimentación
externa, no en `DISCRETO`**: lo que aporta es que el servidor pueda *empujar* órdenes en segundos en
vez de esperar a que el equipo pregunte. A batería, una sesión MQTT persistente no cierra por
consumo.

### ✅ Los cinco modos de energía: entran `RTU` y `SILENT` (2026-09-12)

A pedido de Pablo. Estaban fuera de alcance desde el 2026-09-07 y **sus números quedaron reservados
en el enum justamente para poder agregarlos sin invalidar nada** — cosa que resultó ser exactamente
lo necesario:

#### ⚠ El hash lleva el NÚMERO del modo, no su nombre

`base_hash()` del AVR emite **`[PWRMODO:%d]`**, así que `RTU` tiene que valer **3** y `SILENT` **4**,
igual que allá. Reordenar ese enum cambiaría el hash de todos los equipos y el servidor pediría
reconfigurar para siempre. Con los números correctos, **agregar los dos modos no invalida ninguna
configuración guardada ni mueve ningún hash existente**.

Predicho con la réplica del cálculo del servidor, sobre la configuración de banco (`TPOLL=60`,
`TDIAL=3600`, `PWRON=0630`, `PWROFF=1800`):

| `pwrmodo` | valor | `BH` |
|---|---|---|
| `CONTINUO` | 0 | `0x8D` ← el que informó el equipo |
| `DISCRETO` | 1 | `0x66` ← el que tenía antes de configurarse |
| `MIXTO` | 2 | `0x41` |
| **`RTU`** | **3** | **`0xDF`** |
| **`SILENT`** | **4** | **`0xD4`** |

Que los dos primeros coincidan con lo que ya se vio en banco es lo que da confianza en los otros tres.

#### Qué hace cada uno

| Modo | Modem | Almacenamiento |
|---|---|---|
| **`RTU`** | **siempre encendido** ("(RTU) continuo" en el AVR) | ⛔ **ninguno: si no hay enlace, el dato se DESCARTA** |
| **`SILENT`** | **nunca se enciende** — el AVR entra en `APAGADO` y se queda ahí para siempre | la microSD |

#### ⛔ `RTU` es la única pérdida deliberada de datos del equipo, así que se CUENTA

Descartar una muestra va contra todo lo demás que hace este firmware —el centinela `-9999`, la firma
del RTC, el `estado_asumido` de la válvula—, y acá es correcto porque **un RTU es una unidad remota,
no un datalogger**. Pero un RTU con el enlace caído **se ve exactamente igual que uno funcionando**:
no hay ningún síntoma. Por eso los descartes se cuentan y se informan por consola, con el mismo
criterio que `ulPisados` de la ventana: *hacer visible lo que se perdió*.

✅ **Resuelto con el paso 5d**: `wan_hay_enlace()` es quien lo dice. Hasta que existió `tkWan`
descartaba **siempre**, porque no había a quién preguntarle.

#### En `SILENT` la microSD deja de ser una extensión y pasa a ser el DESTINO

Decisión de implementación (2026-09-12): **se sigue usando la ventana de la EEPROM como buffer**, no
se escribe la SD en cada muestra. Con `timerpoll` de 60 s eso serían ~1440 ciclos de
encender/montar/escribir/desmontar/apagar por día contra **uno cada 33 horas**, y FAT es frágil justo
ante el corte de alimentación — es el razonamiento entero del paso 4b, que no cambia porque cambie el
modo.

El resultado observable es el mismo que pidió Pablo —los datos terminan en la microSD— porque en este
modo **la ventana no se vacía nunca por transmisión**, así que siempre llega al umbral y siempre
vuelca.

⛔ **Pero hay una diferencia real y hay que gritarla: sin tarjeta, en `SILENT` los datos SE PIERDEN**
cuando la ventana da la vuelta. En los otros modos la microSD es una extensión y el equipo degrada
bien sin ella; acá es el único camino. Por eso `tkSys` avisa en el momento en que un volcado falla
estando en este modo, y `config` lo dice al imprimir el modo — que los datos dependan de que haya una
tarjeta puesta no puede quedar implícito.

#### `fs sd retirar`: el procedimiento para llevarse la tarjeta

En `SILENT` la única forma de sacar los datos es **leer la microSD en una PC**, y al momento de ir a
buscarla siempre hay un remanente en la ventana que todavía no llegó al umbral del 90 %. Ese
remanente se pierde si alguien saca la tarjeta y listo. El comando lo vuelca y **da un veredicto**
(pedido de Pablo, 2026-09-12):

```
cmd>fs sd retirar
volcando el remanente de la ventana (37 registros)...
SD:: 37 registros volcados a LOTE0004.DAT

LISTO: la ventana quedo vacia y la tarjeta esta apagada.
       YA PUEDE RETIRAR LA MICROSD (4 lotes guardados).
```

⚠ **No es un alias de `fs sd dump`, y la diferencia es justamente el veredicto.** Un volcado fallido
—tarjeta llena, error de escritura— deja la ventana intacta a propósito, pero si el técnico saca la
tarjeta igual **se lleva datos incompletos y no se entera hasta que abre los archivos en la oficina**.
Acá se le dice en una línea si puede sacarla o no.

Que después sea seguro retirarla no es casualidad: `prvDesmontar()` desmonta **y corta la
alimentación** de la tarjeta al terminar cada operación, así que cuando vuelve el prompt ya está
fría.

### ⚠ El hash de configuración: la trampa del paso 1

**No es un checksum de la struct: es un Pearson de 8 bits sobre un STRING FORMATEADO.**

```
[TIMERPOLL:%03d]   [A0:TRUE,pA,4,20,0.00,10.00,0.00]   [C0:TRUE,CAU0,5.300,CAUDAL,200.00,0.25]
```

El servidor calcula el suyo y los compara. **Si difiere un solo carácter** —un `%03d` que salga `%d`,
un `TRUE` en minúscula, un decimal de más— el hash cambia y el servidor **pide reconfigurar ese bloque
en cada sesión, para siempre**. No se ve en el banco: se ve como tráfico infinito en campo.

Por eso se portaron **carácter por carácter** la tabla de Pearson de 256 bytes y cada string de
formato, y por eso `cfg_hash.h` empieza con esa advertencia.

Dos detalles que costaron atención:

- **Cada campo se hashea por separado**, sobre un buffer que se limpia entre uno y otro. **No** es el
  hash de un string único con todo concatenado: daría distinto.
- ⚠ **El buffer de 64 bytes es parte del contrato.** Si un string no entra se trunca, y el hash
  cambia; mantener el mismo límite es lo que garantiza que los dos equipos se comporten igual también
  en el borde.
- ⚠ En el AVR `char` es **unsigned** y en ARM es **signed**. El índice de la tabla sale de
  `seed ^ ch`, así que un carácter de más de 0x7F daría un índice negativo. `cfg_hash_char()` castea a
  `uint8_t` para reproducir el AVR — con nombres ASCII no se llega ahí nunca, pero el día que alguien
  configure un canal con un acento el bug sería mudo.

**Cómo se validó, y sin hardware**: se compiló la implementación nueva para el host junto con una
transcripción literal de las funciones del AVR, y se compararon los cinco hashes en tres
configuraciones (defaults, una realista, y una con nombres al límite del buffer). **Los 15
coincidieron.** El test está en el scratchpad de la sesión, no en el repo.

⏳ **Lo que ese test NO cierra**: corre con glibc, y el equipo real usa newlib (ARM) contra avr-libc
(AVR). Un caso de borde de redondeo en `%.02f` podría diferir. La verificación final es barata:
`config` imprime los cinco hashes, se ponen la misma configuración en los dos equipos y se comparan.

### La configuración vive en la EEPROM externa (paso 1)

`Application/config/`, comando `config`.

**⚠ El STM32L496 no tiene EEPROM interna.** En el AVR la configuración iba a la NVM interna del micro
y la EEPROM externa era el filesystem entero. Acá las dos cosas conviven en la **M24M01**, que tiene
128 KB de sobra (decisión de Pablo, 2026-09-07):

```
0x00000 - 0x00FFF     4 KB   configuración   (usa 320 B; el resto es para crecer)
0x01000 - 0x1FFFF   124 KB   filesystem      -> 1984 registros de 64 B
```

El FS todavía **no se escribe**: esta etapa sólo le reserva el espacio, para no tener que discutir el
mapa después. De paso queda con casi el doble de registros que el AVR (1984 contra 1024).

| Bloque | Dirección | Tamaño |
|---|---|---|
| `base` | `0x00000` | 16 / 64 B |
| `ainputs` | `0x00040` | 88 / 192 B |
| `counter` | `0x00100` | 40 / 64 B |
| `modbus` | `0x00140` | 168 / 384 B |
| `consigna` | `0x002C0` | 8 / 64 B |

Hay un **`_Static_assert` por bloque**: si uno crece más de lo reservado, lo dice el compilador y no se
descubre en campo pisando el bloque siguiente.

**Un bloque, un checksum**, igual que el AVR: si uno se corrompe, **sólo ése** cae a sus valores por
defecto y lo dice por consola. Un checksum malo **no impide arrancar** — en un equipo desatendido es
mejor medir con la configuración de fábrica que no arrancar.

⚠ **Por qué cada `cfg_*_defaults()` hace `memset()` antes de llenar**: el checksum se calcula sobre
`sizeof(struct) - 1`, o sea sobre la struct entera **incluido el relleno del compilador**. Con padding
sin inicializar, el checksum de una configuración recién puesta no coincidiría con el de la misma
configuración releída. En el AVR no se veía porque las structs eran globales y arrancaban en cero.

`config default` y `config load` trabajan **sólo en RAM**: nada se graba hasta `config save`. Es a
propósito — así un `config default` mal tipeado se deshace con un `config load`.

### ✅ Paso 1 validado en banco (2026-09-08)

Los cuatro criterios de aceptación pasaron:

| Criterio | Resultado |
|---|---|
| ⭐ **Los hashes** | `BH=0x25 AH=0xE5 CH=0x3F MH=0xD5 PH=0x2A` — **exactamente los predichos** por el test del host |
| Persistencia | `config save` → reset → la configuración volvió idéntica, con los mismos hashes |
| Bloque corrupto | `ee wr 0x00000 PABLO` + reset → **cayó sólo `base`**; los otros cuatro sobrevivieron |
| Validaciones | `config ainput 0 true pA 20 4 0 10 0` rechazado (imin ≥ imax) |

⭐ **Que los hashes coincidan con la predicción es lo que cierra el riesgo del contrato**: era lo
único que el test del host no podía demostrar, porque corre con glibc y el equipo usa newlib. Ahora
está probado que las dos formatean igual los `%.02f` de la configuración.

⛔ **Un bug que salió de esta etapa: la tabla de comandos se llenó.** Al registrar `config` se llegó a
17 y `CMDLINE_MAX_COMMANDS` era **16**, así que **`reboot` quedó sin registrar**. El driver lo avisaba
—`CMD: ERROR: tabla de comandos llena (16)`— pero el mensaje sale en medio del chorro de arranque y
pasa inadvertido; desde afuera el síntoma es un comando que "no existe". Subido a **24**, con margen
para los comandos que trae la fase 2.

### ✅ El pendiente del `reset` se cerró, con el diagnóstico dado vuelta (2026-09-08)

`reboot` era un **experimento**, no una comodidad: existía para separar dos causas que desde afuera se
ven iguales. Su comentario decía qué significaba cada resultado — *"reboot anda y reset mata la placa
→ es la LÍNEA DE NRST"*, *"los dos matan la placa → no es NRST"*.

**El experimento dio su resultado, y es el contrario del que se venía suponiendo:**

| | Se creía | Medido el 2026-09-08 |
|---|---|---|
| `reset` (NVIC_SystemReset, pulsa NRST) | colgaba la placa | ✅ **anda** — reinicia e informa `SOFT PIN` |
| `reboot` (salto tibio, sin NRST) | andaba | ⛔ **cuelga**, hay que ciclar la alimentación |

**El problema nunca fue la línea de NRST.** Y `reboot` no falla por un detalle corregible: saltar al
vector **sin resetear nada** deja el I2C, el SPI, las UARTs, el LPTIM y FreeRTOS corriendo con sus
interrupciones pendientes, y después los `MX_*_Init()` los reprograman en caliente. Con el firmware
chico del bring-up sobrevivía; con el I2C hablándole a la EEPROM al arrancar para cargar la
configuración, no. **Se eliminó el comando**, y la explicación quedó en `tkCmd.c` en su lugar para
que nadie lo reponga sin leerla.

### ✅ El parser dejó de matchear por prefijo (2026-09-08)

Estaba anotado como pendiente desde el principio: `strncmp( comando, tipeado, largo_tipeado )` hacía
que **`r` ejecutara `reset`** y `s` ejecutara `status`. Se cerró al entrar los comandos de
configuración, que además de reiniciar pueden **borrar la configuración** (`config default`). Ahora el
largo tiene que coincidir: comando completo.

### ⚠ `printf` con `%f`: hay que habilitarlo, y el síntoma de que falta es que no imprime NADA

El proyecto linkea con `--specs=nano.specs`, y el `printf` de newlib-nano **no trae soporte de punto
flotante salvo que se pida explícitamente**. Sin el flag, un `%.02f` no imprime un número equivocado:
deja el campo **vacío**, en medio de una línea que por lo demás sale bien. Es de los síntomas que
hacen buscar el error en el driver durante una tarde.

Se habilita en *Project → Properties → C/C++ Build → Settings → Tool Settings*, en el **nodo raíz
`MCU/MPU Settings`**: ☑ `Use float with printf from newlib-nano (-u _printf_float)`. **Con el combo
`Configuration` en `[ All configurations ]`** — misma trampa que los include paths de `Application/`:
tildado sólo en Debug, anda en banco y falla en Release meses después. Cuesta 7-10 KB de flash, que
con 47 KB usados de 1024 KB no aprieta. Habilitado por Pablo el **2026-08-14**.

**Cómo verificar que quedó**, y ojo que el flag se llama distinto en cada archivo:

```bash
grep -c 'nanoprintffloat' .cproject      # 2 = las DOS configuraciones, Debug y Release
grep -c '_printf_float'   Debug/makefile # 1 = llegó a la línea de link
```

Si el primero da **1**, se tildó en una sola configuración y hay que rehacerlo con
`[ All configurations ]`.

Dos consecuencias: el `printf` con float **usa 100-200 bytes más de stack** por llamada (revisar los
*high water mark* cuando se empiecen a imprimir magnitudes), y **el comando `ina` sigue formateando a
mano** a propósito, para que un comando de diagnóstico no dependa de una opción del `.cproject` que
una reconfiguración del proyecto puede perder.

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

### microSD + FatFs: diseño pendiente (⏸ diferido a las capas de aplicación)

> **Estado al 2026-08-14.** El **driver de la tarjeta ya está** y validado (`v0.0.10`, ver arriba):
> lee y escribe sectores. Lo que sigue en esta sección es la capa de arriba, que **Pablo difirió a
> propósito** hasta terminar de poblar el hardware. La decisión que la destraba —**¿la SD es
> almacenamiento primario o exportación?**— sigue abierta y hay que tomarla antes de escribir código.

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

**1. ✅ RESUELTA: sí, R001 le corta la alimentación** (`EN_PWR_SD`, PB3, un SI2301 de canal P).
Confirmado y validado en banco el 2026-08-14: con la tarjeta puesta y el riel apagado el equipo
consume lo mismo que sin ella. Ver la sección de la etapa 1 más arriba.

Eso arrastra tres consecuencias, y las tres siguen valiendo para la etapa de FatFs:

- Hay que **desmontar y remontar** en cada ciclo (`f_mount(NULL, …)` + re-init), porque al cortar la
  tarjeta pierde estado. El re-init —74 clocks, CMD0, ACMD41 hasta salir de idle— cuesta de decenas
  a cientos de ms. `drv_sd_arrancar()` ya lo hace.
- Por eso la política **no puede ser "escribir cada muestra"**: hay que acumular en RAM y volcar de a
  bloques. Cuántas muestras se toleran perder define el tamaño del buffer.
- Al cortar, **todos** los pines que van a la tarjeta quedan en alta impedancia. Ya está resuelto en
  `prvPinesBus()`, y por dos motivos distintos: el back-powering del SPI y el pull-up de `SD_DET`.

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
