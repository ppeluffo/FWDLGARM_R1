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

### Estado actual (2026-08-10)

**La placa está poblada sólo parcialmente.** Hoy hay montados: la **fuente**, el **micro**, la
**interfaz de programación SWD** (PA13/PA14), **LEDs en PB9 y PA2** y el **cristal de 32.768 kHz en
PC14/PC15** con sus condensadores de carga a GND. Nada más: no hay modem LTE, ni RS485, ni
dispositivos I2C, ni microSD, ni front-end analógico.

Esto define el alcance de lo que se puede validar en banco: **clock, LSE, LED y SWD**. El resto del
mapa de pines de `interfases_pines.csv` es el diseño completo de R001, no hardware presente — no
tiene sentido escribir drivers contra periféricos que todavía no están montados.

**El proyecto se borró y se rehízo de cero el 2026-08-10.** Hubo un primer intento que llegó a tener
FreeRTOS andando con un task de LED, pero se descartó entero para arrancar limpio, sin respaldo ni
repo (ver control de versiones). Lo que hay hoy es un **esqueleto CubeMX mínimo, sin RTOS**, y es un
punto conocido-bueno validado en banco: el LED parpadea.

- `SystemClock_Config()`: **MSI (range 6 = 4 MHz) → PLL `PLLM=1`, `PLLN=30`, `/2` → 60 MHz**,
  `FLASH_LATENCY_3`, voltage scale 1, AHB/APB1/APB2 sin divisor. **Sin ninguna dependencia del
  cristal.** Confirmado desde el silicio: `RCC_CSR` leído por SWD da `MSISRANGE = 0b0110`.
- **Sin FreeRTOS, sin periféricos, sin BSP.** El `.ioc` declara sólo **NVIC, RCC y SYS**, y dos
  pines: `PB9 = LED` y `PA2 = LED2`, ambos `GPIO_Output`.
- El lazo de `main()` togglea **sólo PB9** con `HAL_Delay(1000)` → 0,5 Hz. **La línea de PA2 está
  comentada**, así que LED2 se configura pero no se usa.
- `LED_PORT`/`LED_PIN`/`LED2_PORT`/`LED2_PIN` están definidos a mano en `USER CODE BEGIN PD`,
  en paralelo a los `LED_GPIO_Port`/`LED_Pin` que genera CubeMX en `main.h`. Conviven sin
  conflicto: el `MX_GPIO_Init()` generado usa los de CubeMX y el lazo usa los propios.
- `Error_Handler()` es **el genérico de CubeMX** (`__disable_irq()` + `while(1)` mudo). El patrón de
  destellos de diagnóstico que existía en el proyecto anterior **se perdió al borrarlo** y no está
  reimplementado.
- **El LSE está deshabilitado** (PC14/PC15 libres): `SystemClock_Config()` enciende únicamente MSI.
  **Es deliberado** — se avanza con el clock interno y el cristal se aborda más adelante. Ojo con la
  trampa: el `.ioc` trae `RCC.RTCClockSelection=RCC_RTCCLKSOURCE_LSE`, `RCC.RTCFreq_Value=32768` y
  hasta `RCC.LSE_VALUE=32768`, pero el RTC **no** está en `Mcu.IPx`, así que son valores residuales
  del árbol de clocks y **no** se genera código de LSE (ver más abajo).
- **Binario actual:** `text 6308 · data 12 · bss 1572` (`arm-none-eabi-size Debug/FWDLGARM_R1.elf`).

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

### El LSE y CubeMX: asignar los pines NO alcanza

Trampa ya verificada en este proyecto. Asignar PC14/PC15 a `LSE-External-Oscillator` en la pestaña
Pinout es **necesario pero no suficiente**: CubeMX genera el encendido del LSE sólo si **algún
periférico lo consume** en la pestaña *Clock Configuration*. Un oscilador sin destino se considera
no usado y no se emite código para él.

Evidencia en el prototipo `FWDLGZ`, que tenía LPTIM1 activado y PC14/PC15 asignados:

- `RCC.LPTIM1Freq_Value=80000000` y `Lptim1ClockSelection = RCC_LPTIM1CLKSOURCE_PCLK` — el mux de
  LPTIM1 quedó en PCLK, no en LSE.
- `SystemClock_Config()` enciende `LSI|MSI` (el LSI por el IWDG), pero **no** el LSE.

**Cómo verificar que quedó bien** (no fiarse de `LSE_VALUE` en `RCC.IPParameters`: sólo aparece si
se cambia el valor por defecto, así que su ausencia no prueba nada):

- En el `.ioc`: el consumidor tiene que estar **realmente habilitado**, o sea figurar en la lista
  `Mcu.IPx` / `Mcu.IPNb`. **No alcanza con que su `<Perif>Freq_Value` valga 32768**: el `.ioc` de
  `FWDLGARM_R1` es la prueba — tiene `RCC.RTCFreq_Value=32768` y
  `RCC.RTCClockSelection=RCC_RTCCLKSOURCE_LSE`, pero el RTC no está entre los IP (`Mcu.IP0..2` =
  NVIC/RCC/SYS) y **no se genera una sola línea de LSE**. Esos campos son el estado del árbol de
  clocks, no evidencia de que haya un consumidor.
- En `SystemClock_Config()`: tiene que aparecer `RCC_OSCILLATORTYPE_LSE` y `LSEState = RCC_LSE_ON`.
  **Éste es el chequeo que vale** — los dos anteriores pueden mentir, éste no.
- En el MspInit del periférico: el mux en LSE (`RCC_RTCCLKSOURCE_LSE`, `RCC_LPTIM1CLKSOURCE_LSE`).
- Por eso `port_lptim_tick.c` termina haciendo a mano `HAL_PWR_EnableBkUpAccess()` +
  `__HAL_RCC_LSE_CONFIG(RCC_LSE_ON)` + `__HAL_RCC_LPTIM1_CONFIG(...LSE)`. **Es un parche sobre una
  configuración incompleta, no el camino recomendado.**

Configuración correcta en CubeMX:

1. **RCC → Mode → Low Speed Clock (LSE): `Crystal/Ceramic Resonator`** (esto asigna PC14/PC15).
2. **Clock Configuration → rutear el LSE a un consumidor**: activar LPTIM1 y poner su mux en **LSE**,
   o activar el RTC con *Clock Source = LSE*. Ambos son internos al micro → validables con la placa
   actual.
3. **RCC → Parameter Settings → LSE Drive Capability**: elegirlo a propósito (consumo vs. margen de
   arranque), no dejar el default.

Regenerando, `SystemClock_Config()` incorpora `RCC_OSCILLATORTYPE_LSE` con `LSEState = RCC_LSE_ON`.

### Notas de bring-up del LSE

El cristal de 32.768 kHz es la base de todo el bajo consumo (tick por LPTIM1 en modo Stop, RTC, RTC
de calendario), así que conviene validarlo temprano:

- **Dominio de backup:** el LSE vive detrás del bit `DBP` de `PWR->CR1`. Si se usa
  `HAL_RCC_OscConfig()` (la vía que genera CubeMX), la HAL lo destraba sola. Sólo hace falta
  `HAL_PWR_EnableBkUpAccess()` explícito si se toca el LSE **por registros**, como hace
  `port_lptim_tick.c`.
- El arranque de un cristal de 32 kHz es lento (cientos de ms). La espera de `LSERDY` necesita
  timeout: si el firmware queda colgado en ese `while`, sospechar del cristal o de los condensadores
  antes que del código.
- Los condensadores de carga tienen que corresponder al `CL` del cristal montado — verificar contra
  el esquemático de `Hardware/R001/`.
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

Orden a seguir, con el LSE postergado a propósito para no frenar el avance:

1. ✅ **Clock + LED, bare-metal** — MSI → PLL → 60 MHz, LED en PB9. **Es el estado actual.**
2. ⏳ **FreeRTOS desde CubeMX**. Ya se hizo una vez y funcionó, así que las trampas están conocidas
   (ver abajo); se perdió al rehacer el proyecto y hay que volver a hacerlo.
3. **TERM / USART1** — consola y `printf`; a partir de acá el resto se depura con herramientas.
4. I2C (RTC, monitor de corriente) → RS485/Modbus → microSD/SPI → entradas analógicas → modem LTE.
5. **LSE + tick por LPTIM1** — recién acá se cambia el tick del kernel al LPTIM1 alimentado por el
   cristal (`port_lptim_tick.c` de `FWDLGZ` como referencia). Es lo que habilita el modo Stop.
6. **Bajo consumo** al final: modos Stop y tickless, ya con todo funcionando y medible.

Los pasos 5 y 6 son los que justifican el cristal; hasta entonces el clock interno alcanza.

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

### Control de versiones — pendiente, y ya costó caro

`FWDLGARM_R1` **no es un repo git**. El 2026-08-10 el proyecto se borró entero para rehacerlo desde
cero, **sin respaldo**: se perdieron el `Error_Handler()` con patrón de destellos de diagnóstico y
las funciones auxiliares `led_config()` / `error_delay_ms()`, además de la etapa con FreeRTOS ya
validada en banco. Nada de eso era recuperable.

Conviene inicializarlo y etiquetar cada etapa validada, reusando el `.gitignore` del prototipo
`FWDLGZ` (ignora `Debug/`, `Release/`, `.metadata/`, `*.launch`, `*.ioc.bak`).

## Build & flash

El proyecto es **STM32CubeIDE 2.2.0** (`/opt/st/stm32cubeide_2.2.0/stm32cubeide`), con el workspace
Eclipse en `Firmware/` (`Firmware/.metadata/`); ambos proyectos STM32 están importados ahí.

El build normal es desde el IDE. `Debug/` contiene los makefiles **generados** por CDT
(toolchain *GNU Tools for STM32 14.3.rel1*), así que `make` dentro de `Debug/` reproduce el mismo
build — pero:

- **`Debug/` es generado, no se edita a mano**: CubeIDE lo regenera y pisa los cambios. Para agregar
  fuentes o includes hay que hacerlo en las propiedades del proyecto (`.cproject`), no en los `.mk`.
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
| I2C | PB13 SCL, PB14 SDA |
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
