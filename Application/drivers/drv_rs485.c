/*
 * drv_rs485.c  -  ver drv_rs485.h
 */

#include "drv_rs485.h"
#include "drv_uart.h"
#include "pwr_lock.h"
#include "main.h"

/* Pines del bus: los tres se mueven juntos entre función alternada y analógico.
   PB1 es el DE, que lo maneja el periférico. */
#define BUS_PINS    ( RS485_TX_Pin | RS485_RX_Pin | RS485_RTS_Pin )
#define BUS_PORT      RS485_TX_GPIO_Port    /* los tres están en GPIOB */

typedef struct {
    GPIO_TypeDef *pxPort;
    uint16_t      usPin;
    bool          bEncendido;
} rs485_riel_t;

static rs485_riel_t xRieles[ rs485RAIL_COUNT ] = {
    [ rs485RAIL_BUS   ] = { EN_PWR_RS485_GPIO_Port, EN_PWR_RS485_Pin, false },
    [ rs485RAIL_QMBUS ] = { EN_PWR_QMBUS_GPIO_Port, EN_PWR_QMBUS_Pin, false },
    [ rs485RAIL_CPRES ] = { EN_PWR_CPRES_GPIO_Port, EN_PWR_CPRES_Pin, false },
};

/*------------------------------------------------------------------------------
 * Estado de los pines del bus.
 *
 * Con el transceiver sin alimentación, un pin del micro manejado en alto le
 * inyecta corriente por los diodos de protección y lo alimenta por las patas.
 * El modo ANALÓGICO es el de menor fuga del STM32: desconecta el Schmitt trigger
 * y no deja pull activo, así que ni entrega ni consume.
 *----------------------------------------------------------------------------*/
static void prvPinesBus( bool bActivos )
{
    GPIO_InitTypeDef xGpio = { 0 };

    xGpio.Pin = BUS_PINS;

    if( bActivos )
    {
        xGpio.Mode      = GPIO_MODE_AF_PP;
        xGpio.Pull      = GPIO_NOPULL;
        /* A 9600 baudios el slew rate no aporta nada y los flancos lentos
           ensucian menos un cable largo. LOW es la elección correcta acá, no una
           economía. */
        xGpio.Speed     = GPIO_SPEED_FREQ_LOW;
        xGpio.Alternate = GPIO_AF7_USART3;
    }
    else
    {
        xGpio.Mode = GPIO_MODE_ANALOG;
        xGpio.Pull = GPIO_NOPULL;
    }

    HAL_GPIO_Init( BUS_PORT, &xGpio );
}

//------------------------------------------------------------------------------
bool drv_rs485_init( void )
{
    /* Explícito aunque los pull-down de 100 K ya los dejan apagados: el firmware
       no debe depender de una resistencia para un estado que le corresponde. */
    for( uint32_t i = 0U; i < rs485RAIL_COUNT; i++ )
    {
        drv_rs485_power( ( rs485_rail_t ) i, false );
    }

    return true;
}
//------------------------------------------------------------------------------
void drv_rs485_power( rs485_rail_t eRail, bool bOn )
{
    if( eRail >= rs485RAIL_COUNT )
    {
        return;
    }

    /*
     * El orden importa en las dos direcciones, y es simétrico:
     *
     *  al PRENDER  -> primero la energía, después los pines. Al revés, los pines
     *                 quedarían manejando un chip todavía sin alimentar, que es
     *                 exactamente lo que queremos evitar.
     *  al APAGAR   -> primero los pines a alta impedancia, después la energía. Al
     *                 revés queda la misma ventana, aunque más corta.
     */
    if( bOn )
    {
        HAL_GPIO_WritePin( xRieles[ eRail ].pxPort, xRieles[ eRail ].usPin, GPIO_PIN_SET );

        if( eRail == rs485RAIL_BUS )
        {
            /* Sin el candado, el tickless entra en Stop 2 en cuanto la tarea se
               bloquee y el USART3 —que come de PCLK1— se queda sin reloj: no
               transmite y, peor, no puede recibir la respuesta del esclavo. */
            pwr_lock_acquire( pwrLOCK_RS485 );

            prvPinesBus( true );
            drv_uart_rx_flush( drvUART_RS485 );
            ( void ) drv_uart_rx_enable( drvUART_RS485 );
        }
    }
    else
    {
        if( eRail == rs485RAIL_BUS )
        {
            drv_uart_rx_disable( drvUART_RS485 );
            prvPinesBus( false );

            pwr_lock_release( pwrLOCK_RS485 );
        }

        HAL_GPIO_WritePin( xRieles[ eRail ].pxPort, xRieles[ eRail ].usPin, GPIO_PIN_RESET );
    }

    xRieles[ eRail ].bEncendido = bOn;
}
//------------------------------------------------------------------------------
bool drv_rs485_power_estado( rs485_rail_t eRail )
{
    return ( eRail < rs485RAIL_COUNT ) ? xRieles[ eRail ].bEncendido : false;
}
//------------------------------------------------------------------------------
int16_t drv_rs485_write( const char *pvBuffer, uint16_t xBytes )
{
    /* Rechazar con el bus apagado, en vez de dejar que falle de formas raras: sin
       esto los pines están en analógico y la transmisión se va al vacío, con la
       tarea esperando el semáforo hasta el timeout. */
    if( xRieles[ rs485RAIL_BUS ].bEncendido == false )
    {
        return -1;
    }

    return drv_uart_write( drvUART_RS485, pvBuffer, xBytes );
}
//------------------------------------------------------------------------------
int16_t drv_rs485_read( char *pvBuffer, uint16_t xBytes, TickType_t xTicksToWait )
{
    if( xRieles[ rs485RAIL_BUS ].bEncendido == false )
    {
        return -1;
    }

    return drv_uart_read( drvUART_RS485, pvBuffer, xBytes, xTicksToWait );
}
//------------------------------------------------------------------------------
void drv_rs485_rx_flush( void )
{
    drv_uart_rx_flush( drvUART_RS485 );
}
//------------------------------------------------------------------------------
