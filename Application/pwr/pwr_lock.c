/*
 * pwr_lock.c  -  ver pwr_lock.h
 */

#include "pwr_lock.h"
#include "stm32l4xx_hal.h"

static volatile uint32_t ulCandados = 0U;

/*
 * Se protege con PRIMASK guardado y restaurado, no con taskENTER_CRITICAL().
 *
 * El motivo es el port: vPortSuppressTicksAndSleep() consulta el estado con las
 * interrupciones YA cortadas, y taskENTER_CRITICAL()/taskEXIT_CRITICAL() las
 * volvería a habilitar al salir, justo en medio de la cuenta de ticks. Guardar y
 * reponer el estado previo funciona igual desde tarea, desde ISR y desde ahí.
 *
 * (Hasta el 2026-08-12 el caso ISR era real: TERM_SENSE tomaba el candado desde
 * la EXTI. Ahora se polea desde tkCtl, pero el modem y la microSD van a volver a
 * necesitarlo desde interrupción.)
 */
static inline uint32_t prvEntrar( void )
{
    uint32_t ulPrimask = __get_PRIMASK();
    __disable_irq();
    return ulPrimask;
}

static inline void prvSalir( uint32_t ulPrimask )
{
    __set_PRIMASK( ulPrimask );
}

//------------------------------------------------------------------------------
void pwr_lock_acquire( pwr_lock_id_t id )
{
    if( id >= pwrLOCK_COUNT )
    {
        return;
    }

    uint32_t ulPrimask = prvEntrar();
    ulCandados |= ( 1UL << id );
    prvSalir( ulPrimask );
}
//------------------------------------------------------------------------------
void pwr_lock_release( pwr_lock_id_t id )
{
    if( id >= pwrLOCK_COUNT )
    {
        return;
    }

    uint32_t ulPrimask = prvEntrar();
    ulCandados &= ~( 1UL << id );
    prvSalir( ulPrimask );
}
//------------------------------------------------------------------------------
bool pwr_deep_sleep_permitido( void )
{
    return ( ulCandados == 0U );
}
//------------------------------------------------------------------------------
uint32_t pwr_lock_estado( void )
{
    return ulCandados;
}
//------------------------------------------------------------------------------
