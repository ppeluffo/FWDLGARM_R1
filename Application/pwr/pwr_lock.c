/*
 * pwr_lock.c  -  ver pwr_lock.h
 */

#include "pwr_lock.h"
#include "stm32l4xx_hal.h"

static volatile uint32_t ulCandados = 0U;

/*
 * Se protege con PRIMASK guardado y restaurado, no con taskENTER_CRITICAL(),
 * porque estas funciones se llaman TAMBIÉN desde ISR (la de TERM_SENSE) y desde
 * el port con las interrupciones ya cortadas. Guardar y reponer el estado previo
 * es lo único que funciona en los tres contextos sin habilitar interrupciones
 * donde no corresponde.
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
