/*
 * drv_lte.c  -  ver drv_lte.h
 *
 * Etapa 1: sólo las fuentes y el nivel del PWRKEY.
 */

#include "drv_lte.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"

/*==============================================================================
 * API pública
 *============================================================================*/

void drv_lte_init( void )
{
    /* Todo abajo: los dos rieles cortados y el PWRKEY suelto. MX_GPIO_Init() ya
       los deja así; esto lo deja dicho igual, porque depender de un default del
       `.ioc` para el estado de reposo de un modem no es aceptable. */
    drv_lte_pwrkey( false );
    drv_lte_3v8   ( false );
    drv_lte_dcin  ( false );
}
//------------------------------------------------------------------------------
void drv_lte_rieles( bool bOn )
{
    if( bOn )
    {
        /* DCIN primero: si el TPS62130 cuelga de DCIN —todavía sin confirmar, ver
           el header— habilitarlo antes sería pedirle que regule sin entrada. */
        drv_lte_dcin( true );
        vTaskDelay( pdMS_TO_TICKS( DRV_LTE_MS_ENTRE_RIELES ) );
        drv_lte_3v8( true );
    }
    else
    {
        /* Y al apagar, al revés: primero el que está aguas abajo. */
        drv_lte_3v8 ( false );
        drv_lte_dcin( false );
    }
}
//------------------------------------------------------------------------------
void drv_lte_dcin( bool bOn )
{
    /* TPS22810: EN = 1 prende. Mismo criterio que los rieles del RS485 y el del
       divisor de 12 V, y opuesto al SI2301 de la microSD. */
    HAL_GPIO_WritePin( EN_LTE_DCIN_GPIO_Port, EN_LTE_DCIN_Pin,
                       bOn ? GPIO_PIN_SET : GPIO_PIN_RESET );
}
//------------------------------------------------------------------------------
void drv_lte_3v8( bool bOn )
{
    /* TPS62130: EN = 1 prende. */
    HAL_GPIO_WritePin( EN_LTE_3V8_GPIO_Port, EN_LTE_3V8_Pin,
                       bOn ? GPIO_PIN_SET : GPIO_PIN_RESET );
}
//------------------------------------------------------------------------------
void drv_lte_pwrkey( bool bApretado )
{
    /*
     * ⚠ Acá el nombre importa más que de costumbre: el argumento es "el botón
     * está apretado", NO "el pin está en alto"... aunque en este circuito
     * coincidan. El transistor invierte: PA5 en 1 hunde el colector y el PWRKEY
     * del módulo ve un BAJO, que es su nivel activo.
     *
     * Se escribe así, y no como "poner PA5 en alto", para que el día que cambie
     * el circuito —un transistor menos, o uno más— lo que haya que corregir sea
     * esta línea y no todos los llamadores.
     */
    HAL_GPIO_WritePin( LTE_PWR_GPIO_Port, LTE_PWR_Pin,
                       bApretado ? GPIO_PIN_SET : GPIO_PIN_RESET );
}
//------------------------------------------------------------------------------
bool drv_lte_dcin_estado( void )
{
    return ( HAL_GPIO_ReadPin( EN_LTE_DCIN_GPIO_Port, EN_LTE_DCIN_Pin ) == GPIO_PIN_SET );
}
//------------------------------------------------------------------------------
bool drv_lte_3v8_estado( void )
{
    return ( HAL_GPIO_ReadPin( EN_LTE_3V8_GPIO_Port, EN_LTE_3V8_Pin ) == GPIO_PIN_SET );
}
//------------------------------------------------------------------------------
bool drv_lte_pwrkey_estado( void )
{
    return ( HAL_GPIO_ReadPin( LTE_PWR_GPIO_Port, LTE_PWR_Pin ) == GPIO_PIN_SET );
}
//------------------------------------------------------------------------------
