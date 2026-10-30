/*
 * port_lptim_tick.c
 *
 * Tick de FreeRTOS sobre LPTIM1, clockeado desde el LSE (32.768 kHz).
 *
 * POR QUÉ
 * -------
 * El tick por defecto del port GCC/ARM_CM4F sale del SysTick, que se alimenta del HCLK
 * y por lo tanto MUERE en los modos Stop del STM32L4. El LPTIM1, en cambio, sigue
 * contando en Stop mientras el LSE esté vivo. Mover el tick al LPTIM1 es el requisito
 * para el tickless de bajo consumo (etapa 6b); esta etapa (6a) sólo cambia la fuente
 * del tick y deja el comportamiento temporal idéntico al de antes.
 *
 * CÓMO
 * ----
 * vPortSetupTimerInterrupt() es weak en el port (port.c:679) y la llama
 * xPortStartScheduler() justo antes de lanzar la primera tarea. Definirla acá la pisa,
 * con lo cual el SysTick nunca se configura ni se arranca: el SysTick_Handler() que
 * aporta cmsis_os2.c queda muerto y NO hay doble tick. El timebase de la HAL
 * (HAL_IncTick) sigue en TIM6, que es un periférico aparte y no se toca.
 *
 * LA CUENTA
 * ---------
 *     LSE 32768 Hz  /  prescaler 32  =  1024 Hz de contador
 *     1024 Hz  /  configTICK_RATE_HZ 512  =  2 cuentas por tick   ->  ARR = 1
 *
 * El tick sale EXACTO del cristal: sin error de redondeo y sin deriva acumulada. Por eso
 * el tick es de 512 Hz y no de 1000 — 1000 no divide a 32768 y dejaba un sesgo permanente
 * de +0,71 %, unos 10 minutos por día. Ver CLAUDE.md.
 *
 * De dónde sale cada número: el prescaler /32 y el mux LPTIM1<-LSE los configura CubeMX
 * (main.c MX_LPTIM1_Init / stm32l4xx_hal_msp.c HAL_LPTIM_MspInit). El período (ARR) NO
 * está en el .ioc: lo pone este archivo en tiempo de ejecución.
 */

#include "main.h"
#include "pwr_lock.h"
#include "port_lptim_tick.h"   /* ⚠ ARRIBA: define PORT_SIN_TICKLESS, que se usa en #if */

/*
 * ---------------------------------------------------------------------------
 * PORT_SIN_TICKLESS  (2026-09-30)
 * ---------------------------------------------------------------------------
 * En 1 el equipo NO duerme nunca: el idle gira y el reloj queda a 60 MHz.
 *
 * ⭐ Hoy está en 0, o sea que el tickless ANDA: quién decide si se duerme son
 * los candados de energía, que es el mecanismo de verdad. Este define queda
 * como martillo para descartar al port de un diagnóstico sin tocar nada más.
 *
 * ⭐ Se apaga acá y no en el .ioc a propósito. `configUSE_TICKLESS_IDLE` vive en
 * FreeRTOSConfig.h, que lo genera CubeMX: cambiarlo a mano dejaría el .ioc
 * diciendo una cosa y el binario haciendo otra — la desincronización que ya
 * costó un día con el LSE. Esta función, en cambio, es nuestra.
 *
 * ⚠ El costo es todo el consumo: **~3,5 mA en vez de µA**. Es para el banco,
 * mientras se valida la consola: el RX no puede funcionar en Stop 2, y hasta que
 * TERM_SENSE sea confiable el tickless deja la consola muerta la mayor parte del
 * tiempo. Se vuelve a 0 cuando eso se resuelva, y ahí hay que **medir el reposo
 * de nuevo** — es el criterio de aceptación de cada etapa.
 */
/* ⭐ El define vive en `port_lptim_tick.h`, porque lo necesitan DOS unidades de
   compilación: este archivo y `tkSys.c`, que sólo instrumenta el tick si el
   tickless está activo. Es la misma razón por la que `ARRANCA_WDG` está en
   `main.h` y no en `main.c`. */

/* ⚠ Este #define va ACÁ, antes de todo, y no al lado de la función que lo usa:
   el preprocesador evalúa cada #if en el orden del archivo, así que una
   condición escrita más arriba que la definición se lee como 0 y el bloque
   equivocado se compila. Ya pasó dos veces en este proyecto —con un #include
   bajo un #if de un define declarado más abajo— y el síntoma es mudo: compila,
   y lo que no debía entrar entra.
*/

/* El handle lo declara y lo inicializa CubeMX en main.c (MX_LPTIM1_Init), que corre
   antes de arrancar el scheduler. */
extern LPTIM_HandleTypeDef hlptim1;

/* La provee el port (port.c): incrementa el tick del kernel y pende PendSV si hace falta. */
extern void xPortSysTickHandler( void );

/* La genera CubeMX en main.c. No está en main.h, así que se declara acá: al salir de
   Stop hay que rearmar el árbol de clocks con ella. */
extern void SystemClock_Config( void );

#define LSE_HZ                  32768UL
#define LPTIM_PRESCALER         32UL                              /* espejo de LPTIM_PRESCALER_DIV32 */
#define LPTIM_CLOCK_HZ          ( LSE_HZ / LPTIM_PRESCALER )      /* 1024 Hz                         */
#define LPTIM_COUNTS_POR_TICK   ( LPTIM_CLOCK_HZ / configTICK_RATE_HZ )
#define LPTIM_TICK_ARR          ( LPTIM_COUNTS_POR_TICK - 1UL )   /* el período es ARR + 1           */

/*
 * Estas tres condiciones son las que hacen que el diseño cierre. Se verifican en tiempo
 * de compilación para que un cambio de configTICK_RATE_HZ en CubeMX no pase inadvertido.
 *
 * OJO: no se pueden chequear con #if porque configTICK_RATE_HZ viene casteado
 * —((TickType_t)512)— y el preprocesador no sabe qué es TickType_t.
 */
_Static_assert( ( LSE_HZ % ( LPTIM_PRESCALER * configTICK_RATE_HZ ) ) == 0UL,
                "configTICK_RATE_HZ no divide exacto al LSE prescalado: el tick tendria deriva permanente." );
_Static_assert( LPTIM_COUNTS_POR_TICK >= 2UL,
                "Hacen falta al menos 2 cuentas por tick: ARR = 0 es ilegal en el LPTIM (IS_LPTIM_PERIOD exige >= 1)." );
_Static_assert( LPTIM_TICK_ARR <= 0xFFFFUL,
                "ARR no entra en los 16 bits del LPTIM." );

/*------------------------------------------------------------------------------
 * Pisa la weak del port. La llama xPortStartScheduler() con las interrupciones
 * enmascaradas por BASEPRI, así que acá NO se puede usar nada que dependa de
 * HAL_GetTick() (por suerte LPTIM_WaitForFlag() usa un lazo de conteo, no el tick).
 */
void vPortSetupTimerInterrupt( void )
{
    /*
     * La prioridad NVIC del LPTIM1 la fija CubeMX en HAL_LPTIM_MspInit(), que ya corrió.
     * Tiene que ser la más baja de todas: el tick del kernel no puede interrumpir a
     * ninguna otra ISR. Si alguna regeneración de CubeMX la pierde, el síntoma sería
     * sutil y difícil de rastrear, así que se verifica acá y se cae en el Error_Handler().
     */
    if( NVIC_GetPriority( LPTIM1_IRQn ) != configLIBRARY_LOWEST_INTERRUPT_PRIORITY )
    {
        Error_Handler();
    }

    /* Arranca en modo continuo con la IRQ de match de autoreload. Adentro habilita el
       LPTIM, carga ARR esperando el flag ARROK y lanza el conteo. */
    if( HAL_LPTIM_Counter_Start_IT( &hlptim1, LPTIM_TICK_ARR ) != HAL_OK )
    {
        Error_Handler();
    }
}

/*------------------------------------------------------------------------------
 * ISR del tick.
 *
 * Pisa la weak del driver. La cadena es:
 *     LPTIM1_IRQHandler()  (stm32l4xx_it.c, generado por CubeMX)
 *       -> HAL_LPTIM_IRQHandler()   limpia el flag y despacha
 *          -> acá
 *
 * Engancharse por el callback y no por el bloque USER CODE de stm32l4xx_it.c tiene una
 * ventaja concreta: no hay una sola línea propia dentro de los archivos generados, así
 * que una regeneración de CubeMX no puede romper el tick. El costo es el despacho del
 * HAL_LPTIM_IRQHandler, ~1 us cada 1,95 ms: 0,05 % de CPU.
 */
void HAL_LPTIM_AutoReloadMatchCallback( LPTIM_HandleTypeDef *hlptim )
{
    if( hlptim->Instance == LPTIM1 )
    {
        xPortSysTickHandler();
    }
}

/*==============================================================================
 * TICKLESS
 *
 * Con configUSE_TICKLESS_IDLE = 2, la tarea idle llama a vPortSuppressTicksAndSleep()
 * en lugar de quemar CPU. Acá se reprograma el LPTIM1 para que la próxima interrupción
 * caiga al final del sueño, se entra en Stop 2, y al despertar se le informa al kernel
 * cuántos ticks pasaron con vTaskStepTick().
 *
 * NO se usa la implementación nativa del port porque ésa se apoya en el SysTick, que
 * se alimenta del HCLK y muere en Stop. Ese es el motivo de todo este archivo.
 *============================================================================*/

/* El contador es de 16 bits: 65536 cuentas a 1024 Hz = 64 s de techo por sueño. Si la
   tarea idle pide más, se duerme de a 64 s y vuelve a llamar. */
#define TICKLESS_MAX_TICKS      ( 0x10000UL / LPTIM_COUNTS_POR_TICK )

/* Piso: cada ciclo de sueño cuesta fijo unos ~400 us de CPU despierta —dos esperas del
   flag ARROK (3 ciclos de LSE cada una) más el relock del PLL en SystemClock_Config()—.
   Por debajo de esto no compensa ni en energía ni en jitter. */
#define TICKLESS_MIN_TICKS      3UL

#if ( PORT_SIN_TICKLESS == 0 )

/*------------------------------------------------------------------------------
 * EL RESTO QUE SE ARRASTRA, Y LOS CONTADORES QUE DICEN SI ALCANZA.
 *
 * ⛔⛔ EL PROBLEMA, medido en la instalación de campo del 2026-10-20:
 *
 *     el RTC avanzó 185,21 h   mientras el tick contó 180,95 h
 *     el RTC adelanta +430 ppm (lo dice el servidor) -> el TICK ATRASA 2,31 %
 *
 * ⛔ Y NO es inocuo: el `dT` del caudal **se mide en ticks**, así que un tick que
 * atrasa da intervalos cortos y **caudal de más en la misma proporción** — en un
 * dato que viaja al servidor y que ve el cliente.
 *
 * ⚠⚠ **PERO LA ARITMÉTICA NO CIERRA, Y POR ESO ACÁ HAY DOS COSAS Y NO UNA.**
 *
 * `LPTIM_COUNTS_POR_TICK` son **2**, así que el resto de la división vale 0 ó 1
 * cuenta: a lo sumo 0,977 ms por despertada, 0,488 en promedio. Con las ~4.800
 * despertadas/hora de esa instalación eso explica:
 *
 *     4800 x 0,488 ms = 2,3 s/h  =  651 ppm        de los 23.504 medidos
 *
 * ⭐ **O sea que arrastrar el resto corrige el 3 % del problema.** Está bien
 * hacerlo —es correcto y es gratis— pero **quien afirme que con esto se arregló
 * se va a equivocar**: faltan 97 de cada 100 ppm.
 *
 * ⏳ EL SOSPECHOSO QUE QUEDA, sin confirmar: cada ciclo de sueño para y rearranca
 * el LPTIM **dos veces** (al dormirse y al despertar), y `LPTIM_Disable()` de la
 * HAL no es barato — hace `__HAL_RCC_LPTIM1_FORCE_RESET()` y después reprograma
 * esperando `CMPOK`, `ARROK` y `REPOK`, tres flags de ~92 µs cada uno. Durante
 * todo eso **el contador está reseteado y el tiempo real corre sin contarse**.
 *
 * ⛔ **No se toca nada de eso todavía, a propósito.** Es exactamente la situación
 * en que este proyecto ya se quemó: el 2026-10-02 se hicieron cuatro "arreglos"
 * sobre un ADC que funcionaba bien, guiados por un instrumento que mentía. Acá
 * primero va el instrumento —los contadores de abajo— y después el arreglo que
 * ellos señalen.
 */
static uint32_t ulRestoCuentas     = 0U;   /* se arrastra a la despertada siguiente */
static uint32_t ulCiclosSueno      = 0U;   /* cuántas veces se durmió de verdad     */
static uint32_t ulCuentasInformadas = 0U;  /* lo que se le contabilizó al kernel    */
static uint32_t ulRestoRescatado   = 0U;   /* lo que ANTES se perdía: prueba el fix */

/*------------------------------------------------------------------------------
 * Los contadores del tickless, para que `status` y el registro de eventos puedan
 * mostrarlos. Devuelve el resto pendiente; los demás por puntero (admite NULL).
 */
uint32_t port_tickless_stats( uint32_t *pulCiclos, uint32_t *pulCuentas,
                              uint32_t *pulRescatado )
{
    if( pulCiclos != NULL )    { *pulCiclos    = ulCiclosSueno;       }
    if( pulCuentas != NULL )   { *pulCuentas   = ulCuentasInformadas; }
    if( pulRescatado != NULL ) { *pulRescatado = ulRestoRescatado;    }

    return ulRestoCuentas;
}

/*------------------------------------------------------------------------------
 * ⭐⭐ EL INSTRUMENTO QUE CIERRA EL CASO: el tick contra el RTC INTERNO.
 *
 * El atraso del tick se descubrió **cruzando el log del equipo contra el reloj
 * del MCP79410**, y eso tiene dos problemas: hace falta una corrida larga con el
 * registro de eventos encendido, y el MCP79410 tiene su propia deriva (+430 ppm
 * con el cristal sin condensadores), así que hay que descontarla con lo que diga
 * el servidor. Tres instrumentos para medir una cosa.
 *
 * ⭐ El RTC **interno** del STM32 no tiene ninguno de esos problemas:
 *
 *   - corre del **mismo LSE** que el LPTIM del tick, así que el cristal se
 *     cancela y lo que queda es **exactamente lo que pierde el mecanismo**;
 *   - **sigue contando en Stop 2** y el firmware no lo toca nunca;
 *   - está ahí desde `v0.0.3` y no cuesta un microamper: es el consumidor que
 *     destrabó el encendido del LSE.
 *
 * Se llama periódicamente (desde el poleo) y acumula diferencias cortas, así que
 * no necesita la fecha: alcanza con h:m:s y corregir el cruce de medianoche.
 *
 * ⚠ La primera llamada sólo toma la referencia y devuelve false.
 */
bool port_tick_vs_rtc( uint32_t *pulSegRtc, uint32_t *pulSegTick )
{
    extern RTC_HandleTypeDef hrtc;

    static uint32_t ulRtcAnterior  = 0U;
    static uint32_t ulTickAnterior = 0U;
    static uint32_t ulRtcAcum      = 0U;
    static uint32_t ulTickAcum     = 0U;
    static bool     bHayReferencia = false;

    RTC_TimeTypeDef xT;
    RTC_DateTypeDef xD;
    uint32_t        ulRtc, ulTick;

    /* ⚠ El orden NO es opcional: leer la hora bloquea los registros sombra y
       leer la fecha los libera. Sin la segunda lectura el RTC se congela. */
    if( HAL_RTC_GetTime( &hrtc, &xT, RTC_FORMAT_BIN ) != HAL_OK )
    {
        return false;
    }
    ( void ) HAL_RTC_GetDate( &hrtc, &xD, RTC_FORMAT_BIN );

    ulRtc  = ( (uint32_t) xT.Hours * 3600U ) + ( (uint32_t) xT.Minutes * 60U ) +
             (uint32_t) xT.Seconds;
    ulTick = ( uint32_t ) ( xTaskGetTickCount() / configTICK_RATE_HZ );

    if( bHayReferencia )
    {
        uint32_t ulDeltaRtc = ulRtc - ulRtcAnterior;

        if( ulRtc < ulRtcAnterior )
        {
            ulDeltaRtc = ( 86400U - ulRtcAnterior ) + ulRtc;   /* cruzó medianoche */
        }

        /* ⚠ Sólo se acumulan intervalos razonables. Si alguien pone el RTC en
           hora, o si pasó demasiado entre dos llamadas, el intervalo se descarta
           en vez de contaminar el acumulado con un salto que no es deriva. */
        if( ulDeltaRtc <= 3600U )
        {
            ulRtcAcum  += ulDeltaRtc;
            ulTickAcum += ( ulTick - ulTickAnterior );
        }
    }

    ulRtcAnterior  = ulRtc;
    ulTickAnterior = ulTick;
    bHayReferencia = true;

    if( pulSegRtc != NULL )  { *pulSegRtc  = ulRtcAcum;  }
    if( pulSegTick != NULL ) { *pulSegTick = ulTickAcum; }

    return ( ulRtcAcum > 0U );
}

/*------------------------------------------------------------------------------
 * CNT vive en el dominio del LSE, asíncrono respecto del bus: el RM exige leerlo dos
 * veces seguidas y aceptar el valor sólo cuando ambas lecturas coinciden.
 */
static uint32_t prvLeerCNT( void )
{
    uint32_t ulPrimera, ulSegunda;

    do
    {
        ulPrimera = LPTIM1->CNT;
        ulSegunda = LPTIM1->CNT;
    } while( ulPrimera != ulSegunda );

    return ulSegunda;
}
#endif /* PORT_SIN_TICKLESS: sólo la usa el camino del tickless */

/*------------------------------------------------------------------------------
 * Pisa la weak que genera CubeMX en freertos.c.
 */

void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime )
{
#if ( PORT_SIN_TICKLESS == 1 )
    /* Volver sin dormir es exactamente lo que hace el port cuando hay un candado
       de energía tomado, así que el kernel ya tolera este camino. */
    ( void ) xExpectedIdleTime;
    return;
#else
    uint32_t   ulCuentasDormidas;
    TickType_t xTicksDormidos;

    /*
     * ⚠ SI HAY UN CANDADO TOMADO, ACÁ NO SE HACE NADA. Volver y que el idle gire.
     *
     * Esta es LA política de energía del datalogger, y no es una optimización:
     *
     *   terminal conectada  -> nunca se duerme (lo marca tkCtl poleando TERM_SENSE)
     *   poleo Modbus o modem-> la tarea levanta su candado antes y lo baja después
     *   el resto del tiempo -> Stop 2, que es el 95 % y donde vive la batería
     *
     * POR QUÉ ES UN PROBLEMA DE CORRECTITUD Y NO DE CONSUMO (2026-08-12):
     *
     * Todo lo que sigue a este return corre con __disable_irq(), que enmascara
     * por PRIMASK TODAS las interrupciones sin importar su prioridad. Entre parar
     * y rearmar el LPTIM1 dos veces —cada espera del flag ARROK son 3 ciclos de
     * LSE, ~92 us— la ventana se acerca a los 1042 us que dura un byte a 9600.
     *
     * Con eso, un byte que llega mientras dura la ventana se queda en RDR sin que
     * nadie lo lea, y el siguiente lo pisa: overrun. Se midió con la secuencia de
     * tres bytes pegados de una flecha del terminal: sobrevivía el primero y se
     * perdían los otros dos, un error de UART por flecha. Con tecleo humano no se
     * ve NUNCA, porque las teclas van a 100 ms; pero Modbus es todo ráfagas.
     *
     * Ojo con "arreglarlo" acortando la ventana: la haría menos probable, que en
     * un bug dependiente del momento es peor que no tocarlo — pasa el banco y
     * falla en campo.
     *
     * ⏳ PENDIENTE, decidido con Pablo el 2026-08-12: acá va a ir un __WFI()
     * pelado en vez de girar. No pierde bytes —el peligro nunca fue el WFI sino
     * el __disable_irq() de abajo— y bajaría el consumo activo a un tercio. Se
     * deja para cuando se pueda MEDIR, no ahora: primero seguro, después barato.
     */
    if( pwr_deep_sleep_permitido() == false )
    {
        return;
    }

    if( xExpectedIdleTime > TICKLESS_MAX_TICKS )
    {
        xExpectedIdleTime = TICKLESS_MAX_TICKS;
    }

    if( xExpectedIdleTime < TICKLESS_MIN_TICKS )
    {
        return;
    }

    /* A partir de acá no puede correr ninguna ISR: el WFI igual despierta con PRIMASK
       en 1, pero el handler queda pendiente hasta el __enable_irq() del final. Eso es
       lo que permite hacer la cuenta de ticks sin que nadie la pise. */
    __disable_irq();
    __DSB();
    __ISB();

    /*
     * El kernel puede haber quedado listo para correr entre que decidió dormir y
     * que cortamos las interrupciones.
     *
     * Y se rechequea el candado por la misma razón: la primera consulta se hizo
     * con las interrupciones habilitadas, así que una ISR pudo tomar uno en el
     * medio. Hoy ninguna lo hace —TERM_SENSE se polea— pero el modem y la microSD
     * van a necesitarlo desde interrupción, y entonces esta línea es lo único que
     * separa "duerme cuando no debe" de "anda".
     */
    if( ( eTaskConfirmSleepModeStatus() == eAbortSleep ) ||
        ( pwr_deep_sleep_permitido() == false ) )
    {
        __enable_irq();
        return;
    }

    /* TIM6 mantiene el timebase de la HAL interrumpiendo a 1 kHz: si queda vivo nos
       despierta a los 2 ms y el tickless no sirve de nada. */
    HAL_SuspendTick();

    /* Reprogramar el LPTIM1 para que la IRQ caiga al final del sueño. Se para y se
       vuelve a arrancar en vez de escribir ARR al vuelo: parar resetea CNT, con lo
       cual el sueño empieza siempre desde cero y la cuenta de abajo es directa. */
    ( void ) HAL_LPTIM_Counter_Stop_IT( &hlptim1 );
    if( HAL_LPTIM_Counter_Start_IT( &hlptim1,
                                    ( xExpectedIdleTime * LPTIM_COUNTS_POR_TICK ) - 1UL ) != HAL_OK )
    {
        Error_Handler();
    }

    /* ---- Duerme acá. El LPTIM1 sigue contando del LSE. ----
     *
     * Siempre Stop 2: si hubiera un candado tomado no habríamos llegado hasta acá,
     * porque se vuelve al principio de la función. Antes existía un camino
     * alternativo que dormía en Sleep con el candado tomado; se sacó el 2026-08-12
     * al descubrir que el problema no era el MODO de sueño sino el __disable_irq()
     * que lo rodea, y que ese camino sufría igual.
     */
    HAL_PWREx_EnterSTOP2Mode( PWR_STOPENTRY_WFI );

    /* Stop apaga el PLL y devuelve el SYSCLK al MSI. Hay que rearmar el árbol ANTES
       de tocar cualquier cosa del HAL, porque sus timeouts se calculan con
       SystemCoreClock. */
    SystemClock_Config();

    /* ¿Durmió todo o lo despertó otra cosa? Si el flag de autoreload está puesto, el
       período se completó; si no, CNT dice hasta dónde llegó. */
    if( __HAL_LPTIM_GET_FLAG( &hlptim1, LPTIM_FLAG_ARRM ) )
    {
        ulCuentasDormidas = xExpectedIdleTime * LPTIM_COUNTS_POR_TICK;
    }
    else
    {
        ulCuentasDormidas = prvLeerCNT();
    }

    /* Volver al tick normal. El flag y el pendiente del NVIC se limpian a mano para que
       no entre un tick de más: la cuenta de todo el sueño la hace vTaskStepTick(). */
    ( void ) HAL_LPTIM_Counter_Stop_IT( &hlptim1 );
    __HAL_LPTIM_CLEAR_FLAG( &hlptim1, LPTIM_FLAG_ARRM );
    NVIC_ClearPendingIRQ( LPTIM1_IRQn );
    if( HAL_LPTIM_Counter_Start_IT( &hlptim1, LPTIM_TICK_ARR ) != HAL_OK )
    {
        Error_Handler();
    }

    /*
     * ⭐ EL RESTO SE ARRASTRA, no se descarta.
     *
     * Con 2 cuentas por tick, `ulCuentasDormidas / 2` tira 0 ó 1 cuenta cada vez
     * que algo despierta al micro antes de tiempo —y el contador de pulsos lo
     * hace miles de veces por hora—. Sumando el sobrante a la cuenta siguiente,
     * el error deja de acumularse: cada dos despertadas impares se recupera un
     * tick entero.
     *
     * ⚠ Y el arrastre es seguro frente al clamp de abajo: el resto siempre es
     * menor que `LPTIM_COUNTS_POR_TICK`, así que `(N*C + r)/C` sigue dando N y
     * nunca empuja el resultado por encima de `xExpectedIdleTime`.
     */
    ulCiclosSueno++;
    ulRestoRescatado += ( ulCuentasDormidas % LPTIM_COUNTS_POR_TICK );

    ulCuentasDormidas += ulRestoCuentas;
    ulRestoCuentas     = ulCuentasDormidas % LPTIM_COUNTS_POR_TICK;

    xTicksDormidos = ( TickType_t ) ( ulCuentasDormidas / LPTIM_COUNTS_POR_TICK );
    if( xTicksDormidos > xExpectedIdleTime )
    {
        xTicksDormidos = xExpectedIdleTime;   /* vTaskStepTick() no admite pasarse */

        /* ⚠ Si alguna vez se recortara, el resto se descarta: arrastrar lo que
           no entró lo haría crecer sin techo y el próximo sueño contabilizaría
           de más. Preferible perder una cuenta que inventar un tick. */
        ulRestoCuentas = 0U;
    }
    if( xTicksDormidos > 0U )
    {
        ulCuentasInformadas += ( uint32_t ) xTicksDormidos * LPTIM_COUNTS_POR_TICK;
        vTaskStepTick( xTicksDormidos );
    }

    /* OJO: HAL_GetTick() se queda atrás lo que haya durado el sueño, porque TIM6 estuvo
       parado. Sirve para los timeouts del HAL, que son relativos, pero NO como hora. */
    HAL_ResumeTick();

    __enable_irq();
#endif /* PORT_SIN_TICKLESS */
}/*----------------------------------------------------------------------------*/
