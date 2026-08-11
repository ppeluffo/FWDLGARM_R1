/*
 * FWDLGARM_R1_lptim_tick.c
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

/* El handle lo declara y lo inicializa CubeMX en main.c (MX_LPTIM1_Init), que corre
   antes de arrancar el scheduler. */
extern LPTIM_HandleTypeDef hlptim1;

/* La provee el port (port.c): incrementa el tick del kernel y pende PendSV si hace falta. */
extern void xPortSysTickHandler( void );

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
/*----------------------------------------------------------------------------*/
