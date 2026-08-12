/*
 * drv_term_sense.c  -  ver drv_term_sense.h
 */

#include "drv_term_sense.h"
#include "pwr_lock.h"
#include "main.h"

/* Escrito por tkCtl, leído por tkCmd: volatile para que el compilador no lo
   cachee en un registro. La escritura de un bool alineado es atómica en el M4,
   así que no hace falta más que esto. */
static volatile bool     bTerminalPresente = false;
static volatile uint32_t ulCambios         = 0U;

/* Número de bit del pin dentro del puerto: PB5 -> 5. Es para indexar los campos
   de 2 bits de MODER/PUPDR, no la máscara TERM_SENSE_Pin. */
#define TERM_SENSE_BIT      5U

/*
 * Lee el pin y sincroniza el candado de energía.
 *
 * Se lee el NIVEL, que es la única fuente de verdad. Un esquema basado en
 * flancos puede quedar invertido de forma permanente si se pierde uno; un nivel
 * muestreado converge solo, siempre.
 */
static void prvActualizar( void )
{
    /* Activo en BAJO: 0 = terminal conectada. */
    bool bPresente = ( HAL_GPIO_ReadPin( TERM_SENSE_GPIO_Port, TERM_SENSE_Pin ) == GPIO_PIN_RESET );

    if( bPresente != bTerminalPresente )
    {
        ulCambios++;
    }

    bTerminalPresente = bPresente;

    /* Idempotente: los candados son un bitmask, no un contador, así que volver a
       tomar el mismo candado en cada vuelta no desbalancea nada. */
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
void drv_term_sense_poll( void )
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
uint32_t drv_term_sense_cambios( void )
{
    return ulCambios;
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
}
//------------------------------------------------------------------------------
