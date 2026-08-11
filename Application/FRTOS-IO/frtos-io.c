/*
 * frtos-io.c  -  ver frtos-io.h
 */

#include <stdio.h>
#include <string.h>

#include "frtos-io.h"
#include "drv_uart.h"
#include "semphr.h"

/*------------------------------------------------------------------------------
 * Juego de operaciones de cada clase de periférico.
 *
 * Es lo que reemplaza a las cinco copias de frtos_{read,write,ioctl}_uartN del
 * código AVR: todas las UART comparten estas tres funciones y se distinguen por
 * el campo ucInstancia de la tabla de abajo.
 *----------------------------------------------------------------------------*/
typedef struct {
    int16_t ( *write )( uint8_t ucInstancia, const char *pvBuffer, uint16_t xBytes );
    int16_t ( *read  )( uint8_t ucInstancia, char *pvBuffer, uint16_t xBytes, TickType_t xTicksToWait );
    int16_t ( *ioctl )( uint8_t ucInstancia, uint32_t ulRequest, void *pvValue );
} frtos_ops_t;

/*------------------------------------------------------------------------------
 * Operaciones de UART
 *----------------------------------------------------------------------------*/

static int16_t prvUartWrite( uint8_t ucInstancia, const char *pvBuffer, uint16_t xBytes )
{
    return drv_uart_write( ( drv_uart_id_t ) ucInstancia, pvBuffer, xBytes );
}

static int16_t prvUartRead( uint8_t ucInstancia, char *pvBuffer, uint16_t xBytes, TickType_t xTicksToWait )
{
    return drv_uart_read( ( drv_uart_id_t ) ucInstancia, pvBuffer, xBytes, xTicksToWait );
}

static int16_t prvUartIoctl( uint8_t ucInstancia, uint32_t ulRequest, void *pvValue )
{
    drv_uart_id_t id = ( drv_uart_id_t ) ucInstancia;

    switch( ulRequest )
    {
        case ioctl_UART_CLEAR_RX_BUFFER:
            drv_uart_rx_flush( id );
            return 0;

        case ioctl_UART_ENABLE_RX:
        case ioctl_UART_ENABLE_RX_INT:
            return ( drv_uart_rx_enable( id ) == true ) ? 0 : -1;

        case ioctl_UART_DISABLE_RX:
        case ioctl_UART_DISABLE_RX_INT:
            drv_uart_rx_disable( id );
            return 0;

        case ioctl_SET_TIMEOUT:
            /* El timeout de lectura se pasa por fd, ver xTimeouts. */
            return 0;

        default:
            return -1;
    }
}

static const frtos_ops_t xUartOps = {
    .write = prvUartWrite,
    .read  = prvUartRead,
    .ioctl = prvUartIoctl,
};

/*------------------------------------------------------------------------------
 * La tabla de file descriptors.
 *
 * Las filas en NULL son los periféricos cuyo hardware todavía no está poblado:
 * el fd existe para que el código de aplicación compile, pero cualquier
 * operación devuelve -1 en vez de escribir en un periférico inexistente.
 *----------------------------------------------------------------------------*/
typedef struct {
    const frtos_ops_t *pxOps;
    uint8_t            ucInstancia;
} frtos_fd_t;

static const frtos_fd_t xFdTable[ fdCOUNT ] = {
    [ fdTERM   ] = { &xUartOps, drvUART_TERM },
    [ fdWAN    ] = { NULL, 0 },
    [ fdRS485A ] = { NULL, 0 },
    [ fdI2C    ] = { NULL, 0 },
    [ fdNVM    ] = { NULL, 0 },
};

/* Timeout de lectura por fd, lo fija ioctl_SET_TIMEOUT. Arranca en portMAX_DELAY:
   una lectura sin timeout explícito bloquea, que es lo que quiere la consola. */
static TickType_t xTimeouts[ fdCOUNT ];

static inline const frtos_fd_t *prvFd( file_descriptor_t fd )
{
    if( ( fd >= fdCOUNT ) || ( xFdTable[ fd ].pxOps == NULL ) )
    {
        return NULL;
    }
    return &xFdTable[ fd ];
}

/*------------------------------------------------------------------------------
 * API genérica
 *----------------------------------------------------------------------------*/

bool frtos_open_all( void )
{
    for( uint32_t i = 0U; i < fdCOUNT; i++ )
    {
        xTimeouts[ i ] = portMAX_DELAY;
    }

    return drv_uart_init();
}
//------------------------------------------------------------------------------
int16_t frtos_write( file_descriptor_t fd, const char *pvBuffer, uint16_t xBytes )
{
    const frtos_fd_t *px = prvFd( fd );

    return ( px != NULL ) ? px->pxOps->write( px->ucInstancia, pvBuffer, xBytes ) : -1;
}
//------------------------------------------------------------------------------
int16_t frtos_read( file_descriptor_t fd, char *pvBuffer, uint16_t xBytes )
{
    const frtos_fd_t *px = prvFd( fd );

    return ( px != NULL ) ? px->pxOps->read( px->ucInstancia, pvBuffer, xBytes, xTimeouts[ fd ] ) : -1;
}
//------------------------------------------------------------------------------
int16_t frtos_ioctl( file_descriptor_t fd, uint32_t ulRequest, void *pvValue )
{
    const frtos_fd_t *px = prvFd( fd );

    if( px == NULL )
    {
        return -1;
    }

    /* El timeout lo administra esta capa, no el driver: es una propiedad del fd. */
    if( ulRequest == ioctl_SET_TIMEOUT )
    {
        xTimeouts[ fd ] = ( pvValue != NULL ) ? *( ( TickType_t * ) pvValue ) : portMAX_DELAY;
        return 0;
    }

    return px->pxOps->ioctl( px->ucInstancia, ulRequest, pvValue );
}

/*------------------------------------------------------------------------------
 * printf
 *----------------------------------------------------------------------------*/

static char              cPrintfBuffer[ XPRINTF_BUFFER_SIZE ];
static SemaphoreHandle_t xPrintfMutex = NULL;
static StaticSemaphore_t xPrintfMutexCtrl;

static int prvVfprintf( file_descriptor_t fd, const char *fmt, va_list ap )
{
    /* Creación perezosa: xprintf() puede llamarse antes de frtos_open_all(). */
    if( xPrintfMutex == NULL )
    {
        xPrintfMutex = xSemaphoreCreateMutexStatic( &xPrintfMutexCtrl );
    }

    if( xSemaphoreTake( xPrintfMutex, portMAX_DELAY ) != pdTRUE )
    {
        return -1;
    }

    int iLen = vsnprintf( cPrintfBuffer, sizeof( cPrintfBuffer ), fmt, ap );

    if( iLen > 0 )
    {
        /* vsnprintf devuelve lo que HABRÍA escrito: si no entró, se manda lo que hay. */
        if( iLen >= ( int ) sizeof( cPrintfBuffer ) )
        {
            iLen = ( int ) sizeof( cPrintfBuffer ) - 1;
        }
        ( void ) frtos_write( fd, cPrintfBuffer, ( uint16_t ) iLen );
    }

    ( void ) xSemaphoreGive( xPrintfMutex );

    return iLen;
}
//------------------------------------------------------------------------------
int xprintf( const char *fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    int iRet = prvVfprintf( fdTERM, fmt, ap );
    va_end( ap );
    return iRet;
}
//------------------------------------------------------------------------------
int xfprintf( file_descriptor_t fd, const char *fmt, ... )
{
    va_list ap;
    va_start( ap, fmt );
    int iRet = prvVfprintf( fd, fmt, ap );
    va_end( ap );
    return iRet;
}
//------------------------------------------------------------------------------
void xputChar( char c )
{
    ( void ) frtos_write( fdTERM, &c, 1U );
}
//------------------------------------------------------------------------------
