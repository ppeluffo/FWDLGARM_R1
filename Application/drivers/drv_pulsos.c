/*
 * drv_pulsos.c  -  ver drv_pulsos.h
 */

#include "drv_pulsos.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"

/* Número de bit del pin dentro del puerto: PA12 -> 12. Es para indexar los
   campos de 2 bits de MODER/PUPDR, no la máscara CNT0_Pin. */
#define CNT0_BIT        12U

/*
 * Escritos por la ISR, leídos por las tareas: volatile para que el compilador no
 * los cachee en un registro. La escritura de un uint32_t alineado es atómica en
 * el M4, así que para LEER alcanza con esto; para el leer-y-descontar de
 * drv_pulsos_tomar() no, y de ahí la sección crítica.
 */
static volatile uint32_t ulTotal   = 0UL;
static volatile uint32_t ulParcial = 0UL;

/*==============================================================================
 * La interrupción
 *
 * Es todo lo que corre por pulso, y es a propósito que sea así de poco: la ISR
 * despierta al micro de Stop 2, y cada instante que se quede acá es corriente a
 * 60 MHz. No llama a ninguna función de FreeRTOS —no hay nada que despertar,
 * porque nadie espera un pulso suelto— y por eso tampoco necesita el
 * portYIELD_FROM_ISR() de rigor.
 *
 * El pending bit de la EXTI lo limpia HAL_GPIO_EXTI_IRQHandler() antes de
 * llamarnos.
 *============================================================================*/
void HAL_GPIO_EXTI_Callback( uint16_t usGpioPin )
{
    if( usGpioPin == CNT0_Pin )
    {
        ulTotal++;
        ulParcial++;
    }
}

/*==============================================================================
 * API pública
 *============================================================================*/

void drv_pulsos_init( void )
{
    ulTotal   = 0UL;
    ulParcial = 0UL;

    /* Descartar un flanco que hubiera quedado latcheado antes de que existieran
       los contadores: la EXTI puede tener el pendiente puesto desde antes de
       habilitarse el NVIC, y sin esto el primer pulso del equipo sería uno que
       nunca ocurrió. */
    __HAL_GPIO_EXTI_CLEAR_IT( CNT0_Pin );
    NVIC_ClearPendingIRQ( CNT0_EXTI_IRQn );
}
//------------------------------------------------------------------------------
uint32_t drv_pulsos_total( void )
{
    return ulTotal;
}
//------------------------------------------------------------------------------
uint32_t drv_pulsos_tomar( void )
{
    uint32_t ulTomados;

    /* Leer y descontar tiene que ser UNA operación: si entre las dos cayera un
       pulso, ponerlo en cero lo borraría. Se descuenta lo leído en vez de
       escribir cero — adentro de la sección crítica da lo mismo, pero deja dicho
       cuál es la intención.

       La sección crítica sube BASEPRI a 5, que es lo que enmascara a esta EXTI.
       Ver la advertencia del header sobre la prioridad. */
    taskENTER_CRITICAL();
    {
        ulTomados  = ulParcial;
        ulParcial -= ulTomados;
    }
    taskEXIT_CRITICAL();

    return ulTomados;
}
//------------------------------------------------------------------------------
uint32_t drv_pulsos_pendientes( void )
{
    return ulParcial;
}
//------------------------------------------------------------------------------
void drv_pulsos_reset( void )
{
    taskENTER_CRITICAL();
    {
        ulTotal   = 0UL;
        ulParcial = 0UL;
    }
    taskEXIT_CRITICAL();
}
//------------------------------------------------------------------------------
bool drv_pulsos_nivel_pin( void )
{
    return ( HAL_GPIO_ReadPin( CNT0_GPIO_Port, CNT0_Pin ) == GPIO_PIN_SET );
}
//------------------------------------------------------------------------------
void drv_pulsos_config( drv_pulsos_cfg_t *pxCfg )
{
    if( pxCfg == NULL )
    {
        return;
    }

    /* Los campos del GPIO son de 2 bits por pin. */
    pxCfg->ulModer = ( CNT0_GPIO_Port->MODER >> ( 2U * CNT0_BIT ) ) & 0x3U;
    pxCfg->ulPupdr = ( CNT0_GPIO_Port->PUPDR >> ( 2U * CNT0_BIT ) ) & 0x3U;
}
//------------------------------------------------------------------------------
