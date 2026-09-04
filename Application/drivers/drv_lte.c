/*
 * drv_lte.c  -  ver drv_lte.h
 */

#include <string.h>

#include "drv_lte.h"
#include "drv_uart.h"
#include "pwr_lock.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"

/*==============================================================================
 * Energía y encendido
 *============================================================================*/

void drv_lte_init( void )
{
    /* Todo abajo: el load switch cortado y el power switch suelto. MX_GPIO_Init()
       ya los deja así; esto lo deja dicho igual, porque depender de un default del
       `.ioc` para el estado de reposo de un modem no es aceptable. */
    drv_lte_pwrkey( false );
    drv_lte_power ( false );
}
//------------------------------------------------------------------------------
void drv_lte_power( bool bOn )
{
    /*
     * TPS22810: EN = 1 prende. Mismo criterio que los rieles del RS485 y el del
     * divisor de 12 V, y opuesto al SI2301 de la microSD.
     *
     * Es el ÚNICO interruptor de energía del modem: el convertidor de 3,8 V cuelga
     * de la salida de este load switch, así que apagarlo apaga las dos ramas. Ver
     * la topología y la advertencia sobre cortarle la alimentación a un módulo que
     * está corriendo, en el header.
     */
    HAL_GPIO_WritePin( EN_LTE_DCIN_GPIO_Port, EN_LTE_DCIN_Pin,
                       bOn ? GPIO_PIN_SET : GPIO_PIN_RESET );

    if( bOn )
    {
        /* Lo que haya en el buffer es de la sesión anterior. */
        drv_uart_rx_flush( drvUART_LTE );

        /*
         * ⚠ El candado va DESPUÉS de encender y ANTES de que el módulo pueda decir
         * una palabra, y es de correctitud: sin él, el `__disable_irq()` del
         * tickless se come bytes. A 115200 esa ventana es más larga que un byte
         * entero. Ver el header.
         */
        pwr_lock_acquire( pwrLOCK_WAN );
    }
    else
    {
        pwr_lock_release( pwrLOCK_WAN );
    }
}
//------------------------------------------------------------------------------
void drv_lte_pwrkey( bool bApretado )
{
    /*
     * ⚠ Acá el nombre importa más que de costumbre: el argumento es "el botón
     * está apretado", NO "el pin está en alto"... aunque en este circuito
     * coincidan. El transistor invierte: PA5 en 1 hunde el colector y el módulo ve
     * un BAJO, que es su nivel activo.
     *
     * Se escribe así, y no como "poner PA5 en alto", para que el día que cambie
     * el circuito —un transistor menos, o uno más— lo que haya que corregir sea
     * esta línea y no todos los llamadores.
     */
    HAL_GPIO_WritePin( LTE_PWR_GPIO_Port, LTE_PWR_Pin,
                       bApretado ? GPIO_PIN_SET : GPIO_PIN_RESET );
}
//------------------------------------------------------------------------------
void drv_lte_pwrkey_pulso( uint32_t ulMs )
{
    drv_lte_pwrkey( true );
    vTaskDelay( pdMS_TO_TICKS( ulMs ) );
    drv_lte_pwrkey( false );
}

/*==============================================================================
 * La UART
 *============================================================================*/

int16_t drv_lte_write( const char *pcBuf, uint16_t xBytes )
{
    return drv_uart_write( drvUART_LTE, pcBuf, xBytes );
}
//------------------------------------------------------------------------------
int16_t drv_lte_read( char *pcBuf, uint16_t xBytes, uint32_t ulTimeoutMs )
{
    return drv_uart_read_frame( drvUART_LTE, pcBuf, xBytes,
                                pdMS_TO_TICKS( ulTimeoutMs ),
                                pdMS_TO_TICKS( DRV_LTE_MS_SILENCIO ) );
}
//------------------------------------------------------------------------------
void drv_lte_flush( void )
{
    drv_uart_rx_flush( drvUART_LTE );
}
//------------------------------------------------------------------------------
drv_lte_escape_t drv_lte_escape( void )
{
    char    pcRta[ 32 ];
    int16_t sLeidos;

    /*
     * La guarda de silencio primero: la contraseña se reconoce como tal porque
     * llega AISLADA, no en medio de un flujo de datos. Si el módulo estaba
     * transmitiendo, hay que dejarlo callar.
     */
    drv_uart_rx_flush( drvUART_LTE );
    vTaskDelay( pdMS_TO_TICKS( DRV_LTE_MS_GUARDA_ESC ) );
    drv_uart_rx_flush( drvUART_LTE );

    /* ⚠ SIN CR. Esto no es un comando, es una contraseña: cualquier byte extra
       pegado la invalida. Es exactamente por lo que "+++AT" no funciona. */
    if( drv_uart_write( drvUART_LTE, "+++", 3U ) < 0 )
    {
        return lteESC_ERROR_TX;
    }

    sLeidos = drv_lte_read( pcRta, ( uint16_t ) sizeof( pcRta ) - 1U, DRV_LTE_MS_ESPERA_A );

    if( sLeidos <= 0 )
    {
        return lteESC_SIN_A;
    }

    pcRta[ sLeidos ] = '\0';

    /* Se busca la 'a' en cualquier posición y no se exige que sea el único byte:
       el módulo puede tener algo a medio salir cuando llega el "+++". */
    if( memchr( pcRta, 'a', ( size_t ) sLeidos ) == NULL )
    {
        return lteESC_SIN_A;
    }

    /* El segundo tramo, también sin CR. */
    if( drv_uart_write( drvUART_LTE, "a", 1U ) < 0 )
    {
        return lteESC_ERROR_TX;
    }

    sLeidos = drv_lte_read( pcRta, ( uint16_t ) sizeof( pcRta ) - 1U, DRV_LTE_MS_ESPERA_OK );

    if( sLeidos <= 0 )
    {
        return lteESC_SIN_OK;
    }

    pcRta[ sLeidos ] = '\0';

    return ( strstr( pcRta, "+ok" ) != NULL ) ? lteESC_OK : lteESC_SIN_OK;
}
//------------------------------------------------------------------------------
int16_t drv_lte_at( const char *pcCmd, char *pcRta, uint16_t xRtaSize, uint32_t ulTimeoutMs )
{
    if( ( pcCmd == NULL ) || ( pcRta == NULL ) || ( xRtaSize < 2U ) )
    {
        return -1;
    }

    /* Que la respuesta sea de ESTE comando y no el arrastre del anterior. Tiene un
       costo —se pierde un aviso asíncrono que hubiera llegado recién— y está
       anotado en el header. */
    drv_uart_rx_flush( drvUART_LTE );

    size_t xLargo = strlen( pcCmd );

    if( ( xLargo > 0U ) &&
        ( drv_uart_write( drvUART_LTE, pcCmd, ( uint16_t ) xLargo ) < 0 ) )
    {
        return -1;
    }

    /*
     * El CR va en una escritura aparte a propósito: así el comando llega tal cual
     * lo escribió el llamador, sin tener que copiarlo a un buffer intermedio para
     * agregarle un byte. Son dos transmisiones seguidas por la misma UART, y el
     * mutex de drv_uart_write() garantiza que nadie se meta en el medio... pero
     * NO que sean atómicas entre sí: si alguna vez dos tareas le hablaran al modem
     * a la vez, esto habría que revisarlo. Hoy le habla sólo tkCmd.
     */
    if( drv_uart_write( drvUART_LTE, "\r", 1U ) < 0 )
    {
        return -1;
    }

    /* -1 en el largo para poder cerrar la cadena siempre. */
    int16_t sLeidos = drv_lte_read( pcRta, xRtaSize - 1U, ulTimeoutMs );

    pcRta[ ( sLeidos > 0 ) ? ( uint16_t ) sLeidos : 0U ] = '\0';

    return sLeidos;
}

/*==============================================================================
 * Estado de los pines
 *============================================================================*/

bool drv_lte_power_estado( void )
{
    return ( HAL_GPIO_ReadPin( EN_LTE_DCIN_GPIO_Port, EN_LTE_DCIN_Pin ) == GPIO_PIN_SET );
}
//------------------------------------------------------------------------------
bool drv_lte_pwrkey_estado( void )
{
    return ( HAL_GPIO_ReadPin( LTE_PWR_GPIO_Port, LTE_PWR_Pin ) == GPIO_PIN_SET );
}
//------------------------------------------------------------------------------
