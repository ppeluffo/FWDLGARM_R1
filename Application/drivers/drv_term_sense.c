/*
 * drv_term_sense.c  -  ver drv_term_sense.h
 */

#include "drv_term_sense.h"
#include "pwr_lock.h"
#include "main.h"

static volatile bool     bTerminalPresente = false;
static volatile uint32_t ulEventos         = 0U;   /* disparos de la EXTI, para diagnóstico */

/* Número de bit de la línea EXTI. Es el número de pin, no la máscara: PB5 -> 5. */
#define TERM_SENSE_BIT      5U

/*
 * Actualiza el estado LEYENDO EL PIN, no deduciéndolo del flanco.
 *
 * Es a propósito: al enchufar o desenchufar el conector hay rebote mecánico, que
 * genera varios flancos seguidos. Si el estado se dedujera del tipo de flanco,
 * un rebote podría dejarlo invertido de forma permanente. Leyendo el pin, el
 * estado converge solo al valor real después del último rebote.
 */
static void prvActualizar( void )
{
    /* Activo en BAJO: 0 = terminal conectada. */
    bool bPresente = ( HAL_GPIO_ReadPin( TERM_SENSE_GPIO_Port, TERM_SENSE_Pin ) == GPIO_PIN_RESET );

    bTerminalPresente = bPresente;

    if( bPresente )
    {
        pwr_lock_acquire( pwrLOCK_TERM );
    }
    else
    {
        pwr_lock_release( pwrLOCK_TERM );
    }
}

//------------------------------------------------------------------------------
void drv_term_sense_init( void )
{
    prvActualizar();
}
//------------------------------------------------------------------------------
bool drv_term_sense_presente( void )
{
    return bTerminalPresente;
}
//------------------------------------------------------------------------------
bool drv_term_sense_nivel_pin( void )
{
    return ( HAL_GPIO_ReadPin( TERM_SENSE_GPIO_Port, TERM_SENSE_Pin ) == GPIO_PIN_SET );
}
//------------------------------------------------------------------------------
uint32_t drv_term_sense_eventos( void )
{
    return ulEventos;
}
//------------------------------------------------------------------------------
void drv_term_sense_config( drv_term_sense_cfg_t *pxCfg )
{
    if( pxCfg == NULL )
    {
        return;
    }

    /* Los campos del GPIO son de 2 bits por pin. */
    pxCfg->ulModer = ( TERM_SENSE_GPIO_Port->MODER >> ( 2U * TERM_SENSE_BIT ) ) & 0x3U;
    pxCfg->ulPupdr = ( TERM_SENSE_GPIO_Port->PUPDR >> ( 2U * TERM_SENSE_BIT ) ) & 0x3U;

    pxCfg->bImr  = ( ( EXTI->IMR1  >> TERM_SENSE_BIT ) & 1U ) != 0U;
    pxCfg->bRtsr = ( ( EXTI->RTSR1 >> TERM_SENSE_BIT ) & 1U ) != 0U;
    pxCfg->bFtsr = ( ( EXTI->FTSR1 >> TERM_SENSE_BIT ) & 1U ) != 0U;

    pxCfg->bNvic = ( NVIC_GetEnableIRQ( TERM_SENSE_EXTI_IRQn ) != 0U );
}

/*------------------------------------------------------------------------------
 * Callback de EXTI. Pisa la weak de la HAL; la llama HAL_GPIO_EXTI_IRQHandler()
 * desde EXTI9_5_IRQHandler(), que genera CubeMX en stm32l4xx_it.c.
 *----------------------------------------------------------------------------*/
void HAL_GPIO_EXTI_Callback( uint16_t GPIO_Pin )
{
    if( GPIO_Pin == TERM_SENSE_Pin )
    {
        ulEventos++;
        prvActualizar();
    }
}
//------------------------------------------------------------------------------
