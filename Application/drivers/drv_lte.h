/*
 * drv_lte.h
 *
 * Modem LTE: la energía, el pin de encendido y la UART.
 *
 * ⚠ **El módulo es un WH-LTE-7S1-E** (USR IOT), decidido por Pablo el 2026-09-04.
 * NO es el SIM7080G: aquel se evaluó mientras se rediseñaba la fuente y quedó
 * afuera. Es un **LTE Cat-1 DTU** —serie a LTE— que del lado del micro es una UART
 * y nada más: se le habla en AT para configurarlo y después transporta bytes en
 * modo transparente (TCP/UDP/HTTPD/SMS).
 *
 * Documentación en `Datasheets/Componentes/PUSR/`: manual de hardware, manual de
 * usuario y el juego de comandos AT.
 *
 * ⚠ **LA UART DEL MÓDULO ES DE 3,0 V, NO DE 3,3.** El manual de hardware es
 * explícito: *"UART interface: AT commands and data transmission, **TTL-3.0V**"* y
 * *"When the I/O of the user's MCU is not 3.0V, **level matching is needed**"*,
 * con un circuito de referencia a transistores. Los dos sentidos no son
 * igual de riesgosos:
 *
 *  - **Modem → micro (PA1)**: los 3,0 V del módulo entran cómodos, porque el
 *    `VIH` del STM32 a 3,3 V es 0,7 x 3,3 = **2,31 V**. Anda sin adaptación.
 *  - ⚠ **Micro → modem (PA0)**: los 3,3 V del STM32 le entran **0,3 V por encima**
 *    de su dominio de 3,0 V. Muchos módulos lo toleran, pero **el fabricante pide
 *    adaptación de niveles y esto hay que verificarlo en la placa**. Si el módulo
 *    no contesta o contesta con basura, éste es el primer sospechoso — antes que
 *    el firmware.
 *
 * **El baudrate es 115200** (Pablo, 2026-09-04). El módulo acepta de 1200 a 921600.
 *
 * ---------------------------------------------------------------------------
 * LOS PINES  (confirmados por Pablo el 2026-09-04)
 *
 *   PC13  EN_LTE_DCIN  -> TPS22810 (load switch, EN=1 PRENDE): la FUENTE del modem
 *   PA5   LTE_PWR      -> transistor -> el power switch del módulo
 *   PA0   LTE_TXD      -> UART4_TX   (sale del micro, entra al modem)
 *   PA1   LTE_RXD      -> UART4_RX
 *
 * **⚠ `LTE_PWR` está INVERTIDO por el transistor, y coincide con lo que pide el
 * módulo.** El manual: *"PWRKEY: Power pin, **pull up by default**"*, *"Power on
 * and off, **low level off**"*, y *"When the module is powered on, can pull down
 * the PWRKEY pin to restart the module"*. O sea que el nivel activo es BAJO, y el
 * transistor lo produce:
 *
 *     PA5 = 1  ->  transistor conduciendo  ->  PWRKEY en BAJO  ->  "botón apretado"
 *     PA5 = 0  ->  transistor cortado      ->  PWRKEY en ALTO  ->  suelto (reposo)
 *
 * Y por eso el reposo es **PA5 = 0**, no sólo por el nivel lógico: con PA5 en 1 el
 * transistor drena permanentemente la corriente del pull-up del colector, que en
 * este módulo va a `VBAT`.
 *
 * ⏳ **Cuánto dura el pulso, el manual NO lo dice para `PWRKEY`.** Lo que sí dice
 * es que *"POWER_KEY and RESET have the same function to control the power on and
 * off"*, y para `RESET` da **0,5 s** (*"pull down the RESET pin for 0.5s, then
 * pull up or open it"*). O sea que **0,5 s es la referencia razonable de dónde
 * empezar a probar, no un dato del `PWRKEY`** — por eso
 * `drv_lte_pwrkey_pulso()` recibe la duración por argumento y no hay ninguna
 * constante que la fije.
 *
 * ⏳ **La POLÍTICA de encendido tampoco vive acá** (criterio de Pablo,
 * 2026-08-18): cuántos reintentos y cuándo se abre una sesión son decisiones de la
 * capa de aplicación.
 *
 * ---------------------------------------------------------------------------
 * ⚡ LA FUENTE ES EN CASCADA: UN SOLO INTERRUPTOR
 *
 * Fuente rediseñada, validada en banco el **2026-09-04** (esquemático
 * `Nueva fuente 3V8.png`). El convertidor cuelga **del load switch**, no de la
 * batería:
 *
 *     12VRAIL --[ TPS22810, EN_LTE_DCIN (PC13) ]--> 12V_LTE_RAIL
 *                                                        |
 *                                        JP1 ------------+------------ LMR33630
 *                                         |                                |
 *                                      DCIN del modem                     JP2
 *                                      (+ C1, 470 uF)                      |
 *                                                                     3V8RAIL
 *
 * **`JP1` y `JP2` son EXCLUYENTES**: el modem se alimenta *o* por `DCIN` a 12 V
 * *o* por `3V8RAIL`, nunca por los dos. Existen para poder intercambiar módulos
 * sin cambiar componentes, y **el firmware no los ve**: en las dos posiciones el
 * único control es `EN_LTE_DCIN`.
 *
 * ✅ **Y no es sólo una convención de la placa: el módulo lo exige.** El manual,
 * sobre el pin 16 (`VCAP`, 3,4-4,2 V): *"Power supply: 3.4~4.2V, recommend 3.8V.
 * **Can not use with DCIN simultaneously**"*. Sus dos entradas son **5-16 V por
 * `DCIN`** (pines 13/14, típico 12 V) o **3,8 V por `VCAP`**, nunca las dos. Los
 * jumpers de R001 implementan exactamente eso.
 *
 * ⚠ **Ojo con el desacople si se usa `JP2`.** Para la entrada de 5-16 V el
 * fabricante pide **220 µF** y la placa pone `C1` = 470 µF, de sobra. Pero para
 * la entrada de 3,8 V pide bastante más —su circuito de referencia son
 * **470 + 220 + 22 µF** más los cerámicos— y del lado de `JP2` la placa tiene
 * `C27` = 100 µF + 100 nF. Es un **Cat-1**, que transmite con picos bastante más
 * grandes que un Cat-M1, así que si en configuración 3V8 el módulo se resetea o
 * se cae de la red al transmitir, **el sospechoso es el bulk y no el firmware**.
 *
 * ⛔ **Esto reemplaza a la topología vieja**, la del TPS62130, donde las dos
 * fuentes colgaban en paralelo de la batería y `EN_LTE_3V8` (PA4) habilitaba el
 * convertidor por separado. Aquello se rediseñó porque se murieron dos chips, y de
 * paso arregló un problema que el firmware podía provocar: con `EN` en un pin del
 * micro y `VIN` colgando del load switch, habilitar el 3V8 con DCIN apagado ponía
 * `EN` **3,3 V por encima de `VIN`**, violando el máximo absoluto de `VIN + 0,3 V`.
 * En el diseño nuevo el `EN` del LMR33630 va **atado a `VIN`**, como bendice la
 * hoja de datos, y **PA4 quedó sin función**.
 *
 * ⚠ **Y por eso cortar la energía dejó de ser inocente**: `drv_lte_power( false )`
 * le saca al módulo **todo**. Cortarle la alimentación a un módulo que está
 * corriendo es la forma conocida de corromperle la flash interna, así que la regla
 * es **apagarlo primero por su power switch o por AT, esperar, y recién ahí cortar
 * la fuente**. Hoy este driver deja hacerlo en cualquier orden porque el orden
 * correcto lo fija la política de sesiones, que es de la aplicación.
 *
 * Dos consecuencias más de la cascada:
 *
 *  - **El apagado no es instantáneo.** El `QOD` del TPS22810, con `R1` = 330 Ohm,
 *    tarda **~0,5 s** en descargar los 470 uF de `C1` en configuración `DCIN`
 *    (tau = 155 ms) y ~3 ms en configuración 3V8. Importa el día que se haga un
 *    ciclo de energía para resetear el módulo: cortar y reponer enseguida no
 *    resetea nada.
 *  - **Encender pega un tirón.** Llenar `C1` son 5,64 mC; si el origen no aportara
 *    nada, `12VRAIL` caería a 8,2 V por milisegundos. No es brownout —la fuente de
 *    3V3 sigue trabajando— pero **no hay que medir `vin` ni los 4-20 mA en el
 *    instante de encender el modem**.
 *
 * ---------------------------------------------------------------------------
 * ⚠ EL CANDADO DE ENERGÍA ES DE CORRECTITUD, NO DE CONSUMO
 *
 * Mientras el modem está alimentado, este driver mantiene tomado `pwrLOCK_WAN`, y
 * eso **no es negociable**: sin él se pierden bytes.
 *
 * El 2026-08-12 se descubrió que `vPortSuppressTicksAndSleep()` hace
 * `__disable_irq()` alrededor de parar y rearmar el LPTIM1, y que esa ventana de
 * **~100 us** alcanzaba para comerse bytes de la consola **a 9600**, donde un byte
 * dura 1042 us. Acá el modem corre mucho más rápido: **a 115200 un byte dura 87
 * us**, o sea que en esa misma ventana no entra uno, entran más de uno. Y a
 * diferencia de la consola, el modem **habla sin que nadie le pregunte** —los
 * avisos asíncronos llegan cuando el módulo quiere—, así que no alcanza con tomar
 * el candado alrededor de cada diálogo: hay que tenerlo mientras el módulo esté
 * encendido.
 *
 * El costo es que con el modem prendido la placa no baja de Sleep y consume ~3,5
 * mA. Al lado de lo que come el modem transmitiendo, es ruido.
 *
 * ⚠ El candado del TX de la UART es **otro bit** (`pwrLOCK_WAN_TX`, lo toma
 * `drv_uart`). Compartirlo sería un bug fino: los candados son un bitmask y no un
 * contador, así que el release del final de un write le soltaría el candado a la
 * sesión entera.
 *
 * ---------------------------------------------------------------------------
 * ⚠ PC13 NO ES UN GPIO CUALQUIERA
 *
 * Está en el dominio de backup, alimentado a través del power switch. El DS11585
 * es explícito: *"the switch only sinks a limited amount of current (3 mA)… the
 * speed should not exceed 2 MHz with a maximum load of 30 pF… must not be used as
 * current sources"*.
 *
 * Para el `EN` de un TPS22810 está perfecto —entrada de alta impedancia, ~1 uA, y
 * una señal continua— pero **el día que alguien quiera colgar otra cosa de PC13 la
 * respuesta puede ser distinta**.
 *
 * Y hay un segundo detalle, menos obvio: *"After a Backup domain power-up, PC13,
 * PC14 and PC15 operate as GPIOs. Their function then depends on the content of
 * the RTC registers, **which are not reset by the system reset**."* Si alguna vez
 * se habilita la salida del RTC (`RTC_OUT`, la calibración, `TAMP1` o `WKUP2`),
 * **PC13 deja de ser GPIO y pasa a manejarlo el RTC** — y esa configuración
 * sobrevive al reset. El síntoma sería un modem que se prende o se apaga solo sin
 * que ninguna línea de código lo toque. Hoy está limpio
 * (`hrtc.Init.OutPut = RTC_OUTPUT_DISABLE`); conviene no tocarlo.
 */

#ifndef APPLICATION_DRIVERS_DRV_LTE_H_
#define APPLICATION_DRIVERS_DRV_LTE_H_

#include <stdbool.h>
#include <stdint.h>

/*------------------------------------------------------------------------------
 * Silencio que cierra una respuesta del módulo.
 *
 * La lectura se delimita por silencio, igual que una trama Modbus, porque el largo
 * de una respuesta AT depende de lo que el módulo tenga para decir: esperar una
 * cantidad fija de bytes cuelga justo cuando el módulo contesta menos de lo
 * previsto, que es lo que hace cuando hay un error.
 *
 * 50 ms son ~26 ticks, muy por encima del piso de resolución de 1,95 ms, y muy por
 * encima de la separación entre bytes de una ráfaga a 115200 (87 us).
 *----------------------------------------------------------------------------*/
#define DRV_LTE_MS_SILENCIO         50U

/*------------------------------------------------------------------------------
 * Techo por omisión para que el módulo empiece a contestar. Un AT simple contesta
 * en milisegundos; los comandos de red tardan mucho más y llevan su propio
 * timeout, por eso `drv_lte_at()` lo recibe como argumento.
 *----------------------------------------------------------------------------*/
#define DRV_LTE_MS_RESPUESTA        1000U

/*------------------------------------------------------------------------------
 * Lo que hay que esperar entre encender la fuente y tocar el power switch: el
 * soft-start del TPS22810 —5,5 ms con `CT` = 27 nF— más el del LMR33630, más
 * margen. Es el riel, no el módulo: **cuánto tarda el WH-LTE-7S1-E en estar listo
 * es otro número, y todavía no se midió.**
 *----------------------------------------------------------------------------*/
#define DRV_LTE_MS_ARRANQUE_RIEL    50U

/*------------------------------------------------------------------------------
 * La SECUENCIA DE ESCAPE al modo comando.
 *
 * ⚠ **`+++` solo NO alcanza, y ése es el error natural.** El módulo arranca en
 * modo transparente —lo que entra por la serie sale por la red— y para poder
 * hablarle en AT hay que hacer un intercambio de TRES TIEMPOS, en el que el
 * módulo pide una confirmación:
 *
 *     micro -> "+++"    (sin CR: es una contraseña, no un comando)
 *     modem -> "a"
 *     micro -> "a"      (sin CR)
 *     modem -> "+ok"    <- a partir de acá acepta AT
 *
 * El segundo tramo es lo que hace que **a mano desde la consola no se pueda**:
 * entre la `a` que contesta el módulo y la `a` que hay que devolverle no da el
 * tiempo de tipear un comando. Por eso esto es una función y no una receta.
 *
 * La `+++` es en realidad la **contraseña de comando**, configurable con
 * `AT+CMDPW` (1 a 10 bytes); `+++` es el valor de fábrica. Si algún día alguien la
 * cambia, esto deja de funcionar y el síntoma es indistinguible de un TX roto.
 *
 * ⏳ **Los tiempos no están en el manual** y las constantes de abajo son un punto
 * de partida generoso, para ajustar en banco.
 *
 * **El valor de retorno es un diagnóstico, no un booleano**, y en el bring-up vale
 * más que el éxito: `lteESC_SIN_A` deja abierta la duda de si el TX llega,
 * mientras que **`lteESC_SIN_OK` PRUEBA que el TX funciona** —el módulo contestó
 * a algo que le mandamos— y manda a mirar el segundo tramo.
 *----------------------------------------------------------------------------*/
typedef enum {
    lteESC_OK = 0,      /* llegó el "+ok": el módulo está en modo comando   */
    lteESC_SIN_A,       /* no contestó la "a" al "+++"                      */
    lteESC_SIN_OK,      /* contestó la "a" pero no el "+ok"                 */
    lteESC_ERROR_TX     /* la UART no pudo transmitir                       */
} drv_lte_escape_t;

#define DRV_LTE_MS_GUARDA_ESC   500U    /* silencio en la línea antes del "+++" */
#define DRV_LTE_MS_ESPERA_A     1000U
#define DRV_LTE_MS_ESPERA_OK    1000U

drv_lte_escape_t drv_lte_escape( void );

/*------------------------------------------------------------------------------
 * Arranque. Deja los dos pines de control en 0: modem sin alimentar y el power
 * switch suelto. Es el estado de reposo y el de menor consumo.
 *----------------------------------------------------------------------------*/
void drv_lte_init( void );

/*------------------------------------------------------------------------------
 * La energía del modem: el load switch de la entrada, que en la topología en
 * cascada alimenta **todo**. **No toca el power switch**: alimentar al modem y
 * encenderlo son dos cosas distintas, y esta función hace sólo la primera.
 *
 * Además toma y suelta `pwrLOCK_WAN`, y limpia el buffer de recepción al prender
 * —lo que haya quedado ahí es de la sesión anterior—.
 *
 * ⚠ Apagar le corta la alimentación al módulo. Ver la advertencia de arriba.
 *----------------------------------------------------------------------------*/
void drv_lte_power( bool bOn );

/*------------------------------------------------------------------------------
 * El power switch.
 *
 * ⚠ `drv_lte_pwrkey( true )` lo deja **apretado hasta que alguien lo suelte**: no
 * hay ningún temporizador adentro. `drv_lte_pwrkey_pulso()` sí lo suelta, y existe
 * para poder cronometrar en el banco cuánto necesita este módulo — el número
 * todavía no se midió, así que la duración va por argumento y no como constante.
 *----------------------------------------------------------------------------*/
void drv_lte_pwrkey( bool bApretado );
void drv_lte_pwrkey_pulso( uint32_t ulMs );

/*------------------------------------------------------------------------------
 * La UART.
 *
 * `drv_lte_at()` es el diálogo completo: descarta lo que haya pendiente, manda
 * `pcCmd` seguido de CR, y devuelve la respuesta delimitada por silencio. El
 * buffer vuelve terminado en '\0'. Devuelve los bytes recibidos, 0 si el módulo no
 * contestó dentro de `ulTimeoutMs`, y -1 si los argumentos no sirven.
 *
 * ⚠ **Descartar lo pendiente antes de preguntar tiene un costo**: si el módulo
 * había mandado un aviso asíncrono justo antes, se pierde. Es la elección correcta
 * para el banco —donde lo que se quiere es la respuesta a ESTE comando, sin
 * arrastrar basura de la anterior— pero una sesión de datos de verdad tiene que
 * leer el flujo completo y parsearlo, no preguntar y descartar.
 *
 * `drv_lte_write/read` son el acceso crudo, para el puente con la terminal y para
 * cuando la aplicación maneje el flujo ella misma.
 *----------------------------------------------------------------------------*/
int16_t drv_lte_at( const char *pcCmd, char *pcRta, uint16_t xRtaSize, uint32_t ulTimeoutMs );

int16_t drv_lte_write( const char *pcBuf, uint16_t xBytes );
int16_t drv_lte_read ( char *pcBuf, uint16_t xBytes, uint32_t ulTimeoutMs );
void    drv_lte_flush( void );

/* Nivel eléctrico de cada pin, leído del pin y no de una variable: si algo los
   moviera por afuera del driver, acá se vería. */
bool drv_lte_power_estado ( void );
bool drv_lte_pwrkey_estado( void );

#endif /* APPLICATION_DRIVERS_DRV_LTE_H_ */
