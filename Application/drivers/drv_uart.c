/*
 * drv_uart.c  -  ver drv_uart.h
 */

#include "drv_uart.h"
#include "pwr_lock.h"
#include "main.h"
#include "stream_buffer.h"
#include "semphr.h"

/* Los handles los crea CubeMX en main.c. Es el único punto donde este driver se
   ata a lo generado; de acá para arriba nadie los ve. */
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart3;

/*------------------------------------------------------------------------------
 * La tabla
 *----------------------------------------------------------------------------*/

typedef struct {
    /* Configuración: fija, se escribe al declarar la tabla. */
    UART_HandleTypeDef  *pxHal;
    uint8_t             *pucRxStore;
    size_t               xRxSize;
    pwr_lock_id_t        eTxLock;

    /* Estado: lo llena drv_uart_init(). */
    StreamBufferHandle_t xRx;
    StaticStreamBuffer_t xRxCtrl;
    SemaphoreHandle_t    xTxDone;
    StaticSemaphore_t    xTxDoneCtrl;
    SemaphoreHandle_t    xTxMutex;
    StaticSemaphore_t    xTxMutexCtrl;

    uint8_t              ucRxByte;    /* destino del Receive_IT, un byte por vez */

    volatile uint32_t    ulErrores;      /* entradas al callback de error   */
    volatile uint32_t    ulUltimoError;  /* banderas de la última vez       */
} drv_uart_t;

static uint8_t pucTermRxStore [ DRV_UART_TERM_RXSIZE  + 1U ]; /* +1: el stream buffer usa uno de guarda */
static uint8_t pucRs485RxStore[ DRV_UART_RS485_RXSIZE + 1U ];

static drv_uart_t xUarts[ drvUART_COUNT ] = {
    [ drvUART_TERM ] = {
        .pxHal      = &huart1,
        .pucRxStore = pucTermRxStore,
        .xRxSize    = DRV_UART_TERM_RXSIZE,
        .eTxLock    = pwrLOCK_TERM_TX,
    },
    [ drvUART_RS485 ] = {
        .pxHal      = &huart3,
        .pucRxStore = pucRs485RxStore,
        .xRxSize    = DRV_UART_RS485_RXSIZE,
        .eTxLock    = pwrLOCK_RS485,
    },
};

/* Resuelve el handle de la HAL -> fila de la tabla. Lo usan los callbacks, que
   son globales y sólo reciben el handle. Con drvUART_COUNT pequeño el lazo es
   más barato que cualquier otra indirección. */
static drv_uart_t *prvDesdeHal( UART_HandleTypeDef *pxHal )
{
    for( uint32_t i = 0U; i < drvUART_COUNT; i++ )
    {
        if( xUarts[ i ].pxHal == pxHal )
        {
            return &xUarts[ i ];
        }
    }
    return NULL;
}

/*------------------------------------------------------------------------------
 * API
 *----------------------------------------------------------------------------*/

bool drv_uart_init( void )
{
    for( uint32_t i = 0U; i < drvUART_COUNT; i++ )
    {
        drv_uart_t *px = &xUarts[ i ];

        px->xRx = xStreamBufferCreateStatic( px->xRxSize, 1U, px->pucRxStore, &px->xRxCtrl );
        px->xTxDone  = xSemaphoreCreateBinaryStatic( &px->xTxDoneCtrl );
        px->xTxMutex = xSemaphoreCreateMutexStatic( &px->xTxMutexCtrl );

        if( ( px->xRx == NULL ) || ( px->xTxDone == NULL ) || ( px->xTxMutex == NULL ) )
        {
            return false;
        }

        if( drv_uart_rx_enable( ( drv_uart_id_t ) i ) == false )
        {
            return false;
        }
    }
    return true;
}
//------------------------------------------------------------------------------
int16_t drv_uart_write( drv_uart_id_t id, const char *pvBuffer, uint16_t xBytes )
{
    if( ( id >= drvUART_COUNT ) || ( pvBuffer == NULL ) || ( xBytes == 0U ) )
    {
        return -1;
    }

    drv_uart_t *px = &xUarts[ id ];
    int16_t     sRet;

    /* Serializa escritores: dos tareas haciendo xprintf a la vez entrelazarían
       las líneas y, peor, romperían el Transmit_IT. */
    if( xSemaphoreTake( px->xTxMutex, portMAX_DELAY ) != pdTRUE )
    {
        return -1;
    }

    /* Sin esto el tickless puede entrar en Stop 2 apenas la tarea se bloquee, y
       la USART se queda sin reloj a mitad de la trama. */
    pwr_lock_acquire( px->eTxLock );

    if( HAL_UART_Transmit_IT( px->pxHal, ( const uint8_t * ) pvBuffer, xBytes ) == HAL_OK )
    {
        /* El semáforo lo da HAL_UART_TxCpltCallback(). El timeout es una red de
           seguridad: si la transmisión se perdiera, la tarea no queda colgada. */
        sRet = ( xSemaphoreTake( px->xTxDone, pdMS_TO_TICKS( DRV_UART_TX_TIMEOUT_MS ) ) == pdTRUE )
               ? ( int16_t ) xBytes
               : -1;
    }
    else
    {
        sRet = -1;
    }

    pwr_lock_release( px->eTxLock );
    xSemaphoreGive( px->xTxMutex );

    return sRet;
}
//------------------------------------------------------------------------------
int16_t drv_uart_read( drv_uart_id_t id, char *pvBuffer, uint16_t xBytes, TickType_t xTicksToWait )
{
    if( ( id >= drvUART_COUNT ) || ( pvBuffer == NULL ) || ( xBytes == 0U ) )
    {
        return -1;
    }

    /* Acá está la diferencia grande contra el driver viejo: la tarea se BLOQUEA
       en el kernel. Sin poleo, sin vTaskDelay(1), sin despertar al micro. */
    size_t xLeidos = xStreamBufferReceive( xUarts[ id ].xRx, pvBuffer, xBytes, xTicksToWait );

    return ( int16_t ) xLeidos;
}
//------------------------------------------------------------------------------
uint32_t drv_uart_errores( drv_uart_id_t id )
{
    return ( id < drvUART_COUNT ) ? xUarts[ id ].ulErrores : 0U;
}
//------------------------------------------------------------------------------
uint32_t drv_uart_ultimo_error( drv_uart_id_t id )
{
    return ( id < drvUART_COUNT ) ? xUarts[ id ].ulUltimoError : 0U;
}
//------------------------------------------------------------------------------
void drv_uart_rx_flush( drv_uart_id_t id )
{
    if( id < drvUART_COUNT )
    {
        ( void ) xStreamBufferReset( xUarts[ id ].xRx );
    }
}
//------------------------------------------------------------------------------
bool drv_uart_rx_enable( drv_uart_id_t id )
{
    if( id >= drvUART_COUNT )
    {
        return false;
    }

    drv_uart_t *px = &xUarts[ id ];
    return ( HAL_UART_Receive_IT( px->pxHal, &px->ucRxByte, 1U ) == HAL_OK );
}
//------------------------------------------------------------------------------
void drv_uart_rx_disable( drv_uart_id_t id )
{
    if( id < drvUART_COUNT )
    {
        ( void ) HAL_UART_AbortReceive_IT( xUarts[ id ].pxHal );
    }
}

/*------------------------------------------------------------------------------
 * Callbacks de la HAL. Pisan las weak del driver, corren en contexto de ISR.
 *----------------------------------------------------------------------------*/

void HAL_UART_RxCpltCallback( UART_HandleTypeDef *huart )
{
    drv_uart_t *px = prvDesdeHal( huart );

    if( px == NULL )
    {
        return;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    ( void ) xStreamBufferSendFromISR( px->xRx, &px->ucRxByte, 1U, &xHigherPriorityTaskWoken );

    /* Rearmar la recepción del próximo byte. */
    ( void ) HAL_UART_Receive_IT( px->pxHal, &px->ucRxByte, 1U );

    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}
//------------------------------------------------------------------------------
void HAL_UART_TxCpltCallback( UART_HandleTypeDef *huart )
{
    drv_uart_t *px = prvDesdeHal( huart );

    if( px == NULL )
    {
        return;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    ( void ) xSemaphoreGiveFromISR( px->xTxDone, &xHigherPriorityTaskWoken );

    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}
//------------------------------------------------------------------------------
void HAL_UART_ErrorCallback( UART_HandleTypeDef *huart )
{
    /*
     * Sin esto la recepción muere en silencio. Un overrun o un error de framing
     * —basura en la línea al enchufar el conector, o un byte que llega mientras
     * se rearma el Receive_IT— deja a la HAL fuera del estado de recepción y no
     * vuelve sola. Es la falla clásica de "la consola andaba y de golpe dejó de
     * recibir". Se limpia el error y se rearma.
     */
    drv_uart_t *px = prvDesdeHal( huart );

    if( px == NULL )
    {
        return;
    }

    /* Se anota ANTES de limpiar, que es la única oportunidad de saber qué pasó. */
    px->ulUltimoError = HAL_UART_GetError( px->pxHal );
    px->ulErrores++;

    __HAL_UART_CLEAR_OREFLAG( px->pxHal );
    __HAL_UART_CLEAR_NEFLAG( px->pxHal );
    __HAL_UART_CLEAR_FEFLAG( px->pxHal );
    __HAL_UART_CLEAR_PEFLAG( px->pxHal );

    ( void ) HAL_UART_Receive_IT( px->pxHal, &px->ucRxByte, 1U );
}
//------------------------------------------------------------------------------
