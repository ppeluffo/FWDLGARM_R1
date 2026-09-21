/*
 * drv_modbus.c
 *
 * La transacción Modbus RTU. El porqué del diseño está en el header.
 */

#include <string.h>

#include "drv_modbus.h"
#include "drv_rs485.h"
#include "frtos-io.h"

//------------------------------------------------------------------------------
/*
 * Una respuesta de lectura son 5 + 2*N bytes; una de escritura, 8. El `+ 4` es
 * aire para que una trama con basura pegada al final no se descarte por largo
 * antes de que el CRC pueda opinar.
 */
#define MB_RX_BUFFER    ( 5U + DRV_MODBUS_MAX_PAYLOAD + 4U )
#define MB_TX_BUFFER    8U      /* SLA + FC + 2 + 2 + CRC: los tres fcodes     */

static uint8_t pucTx[ MB_TX_BUFFER ];
static uint8_t pucRx[ MB_RX_BUFFER ];

static bool    bDebug     = false;
static uint8_t ucExcepcion = 0U;

//------------------------------------------------------------------------------
uint16_t drv_modbus_crc16( const uint8_t *pucMsg, uint8_t ucSize )
{
    /*
     * El CRC de Modbus: polinomio 0xA001 (reflejado), semilla 0xFFFF. Portado
     * **tal cual** del AVR, que a su vez es el de la especificación.
     *
     * ⚠ El resultado sale con los bytes al revés de como viajan: el LOW va
     * primero en la trama. Los llamadores de acá lo tienen en cuenta.
     */
    uint16_t usCrc = 0xFFFFU;
    uint16_t usPos;
    int      i;

    if( pucMsg == NULL )
    {
        return 0U;
    }

    for( usPos = 0U; usPos < ucSize; usPos++ )
    {
        usCrc ^= ( uint16_t ) pucMsg[ usPos ];

        for( i = 8; i != 0; i-- )
        {
            if( ( usCrc & 0x0001U ) != 0U )
            {
                usCrc >>= 1;
                usCrc ^= 0xA001U;
            }
            else
            {
                usCrc >>= 1;
            }
        }
    }

    return usCrc;
}
//------------------------------------------------------------------------------
const char *drv_modbus_error_str( mb_result_t eRes )
{
    switch( eRes )
    {
        case mbOK:               return "OK";
        case mbPARAMETRO:        return "parametro fuera de rango (no se transmitio)";
        case mbBUS_APAGADO:      return "el riel del RS485 esta apagado";
        case mbSIN_RESPUESTA:    return "SIN RESPUESTA (timeout)";
        case mbTRAMA_CORTA:      return "trama demasiado corta";
        case mbCRC:              return "CRC ERROR";
        case mbOTRO_ESCLAVO:     return "contesto OTRO esclavo";
        case mbOTRO_FCODE:       return "la respuesta es de otra funcion";
        case mbEXCEPCION:        return "EXCEPCION del esclavo";
        case mbLARGO_INESPERADO: return "el byte count no coincide con lo pedido";
        default:                 return "?";
    }
}
//------------------------------------------------------------------------------
void drv_modbus_debug( bool bOn )
{
    bDebug = bOn;
}
//------------------------------------------------------------------------------
bool drv_modbus_debug_estado( void )
{
    return bDebug;
}
//------------------------------------------------------------------------------
uint8_t drv_modbus_ultima_excepcion( void )
{
    return ucExcepcion;
}
//------------------------------------------------------------------------------
static void prvTraza( const char *pcQue, const uint8_t *pucBuf, uint8_t ucLargo )
{
    uint8_t i;

    if( !bDebug )
    {
        return;
    }

    xprintf( "MB %s (%u):", pcQue, ( unsigned ) ucLargo );

    for( i = 0U; i < ucLargo; i++ )
    {
        xprintf( "[%02X]", ( unsigned ) pucBuf[ i ] );
    }

    xprintf( "\r\n" );
}
//------------------------------------------------------------------------------
/*
 * Manda el ADU que está en `pucTx` y trae la respuesta a `pucRx`.
 *
 * El CRC del pedido se calcula acá, no en el llamador: así no hay forma de
 * transmitir una trama sin CRC, que es un error que en el AVR es posible porque
 * cada sitio se lo agrega por su cuenta.
 */
static int16_t prvTransaccion( uint8_t ucLargoSinCrc )
{
    uint16_t usCrc = drv_modbus_crc16( pucTx, ucLargoSinCrc );

    pucTx[ ucLargoSinCrc++ ] = ( uint8_t ) ( usCrc & 0x00FFU );          /* LOW  */
    pucTx[ ucLargoSinCrc++ ] = ( uint8_t ) ( ( usCrc & 0xFF00U ) >> 8 ); /* HIGH */

    /*
     * ⚠ Limpiar el buffer de RX **antes** de transmitir, no después de recibir.
     * Lo que haya quedado de una transacción anterior —una respuesta que llegó
     * tarde, el eco de una colisión— se tomaría por la respuesta de ésta. Es el
     * mismo cuidado que `drv_lte_at()` y por la misma razón.
     */
    drv_rs485_rx_flush();

    prvTraza( "TX", pucTx, ucLargoSinCrc );

    if( drv_rs485_write( ( const char * ) pucTx, ucLargoSinCrc ) != ( int16_t ) ucLargoSinCrc )
    {
        return -1;
    }

    int16_t sLeidos = drv_rs485_read_frame( ( char * ) pucRx, sizeof( pucRx ),
                                            pdMS_TO_TICKS( DRV_MODBUS_MS_TIMEOUT ),
                                            pdMS_TO_TICKS( DRV_MODBUS_MS_SILENCIO ) );

    if( sLeidos > 0 )
    {
        prvTraza( "RX", pucRx, ( uint8_t ) sLeidos );
    }
    else if( bDebug )
    {
        xprintf( "MB RX: nada en %u ms\r\n", ( unsigned ) DRV_MODBUS_MS_TIMEOUT );
    }

    return sLeidos;
}
//------------------------------------------------------------------------------
/*
 * Lo común a las dos operaciones: largo mínimo, CRC, dirección, fcode y
 * excepción. **El orden importa y no es arbitrario.**
 *
 * El CRC va PRIMERO —antes que la dirección o el fcode— porque con el CRC malo
 * la trama no se puede interpretar en absoluto: leer el byte 0 de una trama
 * corrupta y anunciar "contestó otro esclavo" sería inventar un diagnóstico
 * sobre datos que ya se sabe que están mal. El protocolo dice exactamente eso:
 * una trama con CRC incorrecto **se ignora**.
 */
static mb_result_t prvValidar( int16_t sLeidos, uint8_t ucSla, uint8_t ucFcode )
{
    if( sLeidos <= 0 )
    {
        return mbSIN_RESPUESTA;
    }

    /* Lo más corto que puede ser una respuesta válida es una excepción: SLA +
       FC|0x80 + código + CRC = 5 bytes. */
    if( sLeidos < 5 )
    {
        return mbTRAMA_CORTA;
    }

    uint8_t  ucLargo  = ( uint8_t ) sLeidos;
    uint16_t usCalc   = drv_modbus_crc16( pucRx, ucLargo - 2U );
    uint16_t usRecib  = ( uint16_t ) pucRx[ ucLargo - 2U ] |
                        ( ( uint16_t ) pucRx[ ucLargo - 1U ] << 8 );

    if( usCalc != usRecib )
    {
        if( bDebug )
        {
            xprintf( "MB: CRC rx=0x%04X calc=0x%04X\r\n",
                     ( unsigned ) usRecib, ( unsigned ) usCalc );
        }
        return mbCRC;
    }

    if( pucRx[ 0 ] != ucSla )
    {
        return mbOTRO_ESCLAVO;
    }

    /*
     * ⛔ LA EXCEPCIÓN: el bit 7 del fcode encendido.
     *
     * Esto es lo que el AVR no mira, y por eso **decodifica el código de error
     * como si fuera el dato**. Una excepción tiene CRC válido y 5 bytes de
     * largo: pasa todos los chequeos que aquel hace.
     */
    if( pucRx[ 1 ] == ( uint8_t ) ( ucFcode | 0x80U ) )
    {
        ucExcepcion = pucRx[ 2 ];
        return mbEXCEPCION;
    }

    if( pucRx[ 1 ] != ucFcode )
    {
        return mbOTRO_FCODE;
    }

    return mbOK;
}
//------------------------------------------------------------------------------
mb_result_t drv_modbus_leer( uint8_t ucSla, uint8_t ucFcode, uint16_t usReg,
                             uint8_t ucNroRegs,
                             uint8_t *pucPayload, uint8_t *pucLargo )
{
    if( pucLargo != NULL )
    {
        *pucLargo = 0U;
    }

    if( ( pucPayload == NULL ) ||
        ( ucNroRegs == 0U ) || ( ucNroRegs > DRV_MODBUS_MAX_REGS ) ||
        ( ( ucFcode != 3U ) && ( ucFcode != 4U ) ) )
    {
        return mbPARAMETRO;
    }

    if( !drv_rs485_power_estado( rs485RAIL_BUS ) )
    {
        return mbBUS_APAGADO;
    }

    ucExcepcion = 0U;

    pucTx[ 0 ] = ucSla;
    pucTx[ 1 ] = ucFcode;
    pucTx[ 2 ] = ( uint8_t ) ( ( usReg & 0xFF00U ) >> 8 );
    pucTx[ 3 ] = ( uint8_t ) ( usReg & 0x00FFU );
    pucTx[ 4 ] = 0x00U;                 /* cantidad de registros, HIGH */
    pucTx[ 5 ] = ucNroRegs;             /* ...y LOW                    */

    int16_t     sLeidos = prvTransaccion( 6U );
    mb_result_t eRes    = prvValidar( sLeidos, ucSla, ucFcode );

    if( eRes != mbOK )
    {
        return eRes;
    }

    /*
     * El byte 2 es el *byte count*, y **tiene que coincidir con lo que pedimos**.
     * Si no coincide, la trama es sintácticamente válida pero responde a otra
     * cosa; copiar igual sería leer bytes que nadie prometió.
     */
    uint8_t ucCount = pucRx[ 2 ];

    if( ( ucCount != ( uint8_t ) ( 2U * ucNroRegs ) ) ||
        ( ( uint16_t ) ucCount + 5U > ( uint16_t ) sLeidos ) )
    {
        return mbLARGO_INESPERADO;
    }

    memcpy( pucPayload, &pucRx[ 3 ], ucCount );

    if( pucLargo != NULL )
    {
        *pucLargo = ucCount;
    }

    return mbOK;
}
//------------------------------------------------------------------------------
mb_result_t drv_modbus_escribir( uint8_t ucSla, uint16_t usReg, uint16_t usValor )
{
    if( !drv_rs485_power_estado( rs485RAIL_BUS ) )
    {
        return mbBUS_APAGADO;
    }

    ucExcepcion = 0U;

    pucTx[ 0 ] = ucSla;
    pucTx[ 1 ] = 6U;
    pucTx[ 2 ] = ( uint8_t ) ( ( usReg & 0xFF00U ) >> 8 );
    pucTx[ 3 ] = ( uint8_t ) ( usReg & 0x00FFU );
    pucTx[ 4 ] = ( uint8_t ) ( ( usValor & 0xFF00U ) >> 8 );
    pucTx[ 5 ] = ( uint8_t ) ( usValor & 0x00FFU );

    int16_t     sLeidos = prvTransaccion( 6U );
    mb_result_t eRes    = prvValidar( sLeidos, ucSla, 6U );

    if( eRes != mbOK )
    {
        return eRes;
    }

    /*
     * ⭐ La respuesta de un 06 correcto es el **eco exacto** del pedido, y acá
     * se verifica. El AVR lo deja pasar —*"No se analiza la respuesta ya que es
     * echo"*— y con eso una escritura que el esclavo aplicó a otro registro, o
     * con otro valor, se ve **idéntica a una exitosa**.
     *
     * Importa justo donde más duele: la consigna del control de presión escribe
     * un comando de válvula y después pregunta si terminó. Si la escritura no
     * entró, lo que se está esperando no va a pasar nunca.
     */
    if( ( sLeidos < 8 ) ||
        ( pucRx[ 2 ] != pucTx[ 2 ] ) || ( pucRx[ 3 ] != pucTx[ 3 ] ) ||
        ( pucRx[ 4 ] != pucTx[ 4 ] ) || ( pucRx[ 5 ] != pucTx[ 5 ] ) )
    {
        return mbLARGO_INESPERADO;
    }

    return mbOK;
}
//------------------------------------------------------------------------------
