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
#define FW_VERSION  "0.0.43"        /* = VER                                 */
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
| 5c | Los frames de datos y el vaciado | |
| 5d | Los modos continuo / discreto / mixto | |
| 6 | Modbus | |
| 7 | Consigna (`tkCtlPres`) | |
| 8 | Watchdog cooperativo + `tkCtl` definitivo | |
| 9 | Pulido: sync del RTC, `BOR_LEV`, Release, consumo | |

**Fuera de alcance por decisión de Pablo (2026-09-07)**: `tkFlow`/flowcontrol y los modos `PWR_RTU` y
`PWR_SILENT`. Existen en el AVR; acá no entran todavía.

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
