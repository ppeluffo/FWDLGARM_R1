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

/*------------------------------------------------------------------------------
 * Pisa la weak que genera CubeMX en freertos.c.
 */
void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime )
{
    uint32_t   ulCuentasDormidas;
    TickType_t xTicksDormidos;

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

    /* El kernel puede haber quedado listo para correr entre que decidió dormir y que
       cortamos las interrupciones. */
    if( eTaskConfirmSleepModeStatus() == eAbortSleep )
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

    /* ---- Duerme acá. El LPTIM1 sigue contando del LSE en los dos modos. ----
     *
     * Stop 2 apaga el PLL y deja sin reloj a casi todos los periféricos, así que
     * no se puede entrar si alguien los necesita: una USART transmitiendo se
     * cortaría a la mitad, y con una terminal conectada no podría recibir. Los
     * drivers lo señalizan con los candados de pwr_lock.h.
     *
     * Cuando hay un candado tomado se duerme igual, pero en Sleep: la CPU se
     * apaga hasta la próxima interrupción y los relojes siguen vivos. Se pierde
     * el grueso del ahorro, pero sólo mientras alguien esté trabajando de verdad.
     */
    if( pwr_deep_sleep_permitido() )
    {
        HAL_PWREx_EnterSTOP2Mode( PWR_STOPENTRY_WFI );

        /* Stop apaga el PLL y devuelve el SYSCLK al MSI. Hay que rearmar el árbol ANTES
           de tocar cualquier cosa del HAL, porque sus timeouts se calculan con
           SystemCoreClock. En Sleep no hace falta: los clocks nunca se perdieron. */
        SystemClock_Config();
    }
    else
    {
        __WFI();
    }

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
       no entre un tick de más: la cuenta de todo el sueño la hace vTaskStepTick().

       Se pierde el resto de la división (menos de un tick, < 1,95 ms) cada vez que algo
       despierta al micro antes de tiempo. No afecta a los timestamps, que los estampa el
       RTC; si alguna medición mostrara que importa, habría que arrastrar el resto. */
    ( void ) HAL_LPTIM_Counter_Stop_IT( &hlptim1 );
    __HAL_LPTIM_CLEAR_FLAG( &hlptim1, LPTIM_FLAG_ARRM );
    NVIC_ClearPendingIRQ( LPTIM1_IRQn );
    if( HAL_LPTIM_Counter_Start_IT( &hlptim1, LPTIM_TICK_ARR ) != HAL_OK )
    {
        Error_Handler();
    }

    xTicksDormidos = ( TickType_t ) ( ulCuentasDormidas / LPTIM_COUNTS_POR_TICK );
    if( xTicksDormidos > xExpectedIdleTime )
    {
        xTicksDormidos = xExpectedIdleTime;   /* vTaskStepTick() no admite pasarse */
    }
    if( xTicksDormidos > 0U )
    {
        vTaskStepTick( xTicksDormidos );
    }

    /* OJO: HAL_GetTick() se queda atrás lo que haya durado el sueño, porque TIM6 estuvo
       parado. Sirve para los timeouts del HAL, que son relativos, pero NO como hora. */
    HAL_ResumeTick();

    __enable_irq();
}
/*----------------------------------------------------------------------------*/
