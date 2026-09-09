/*
 * wan_frame.c  -  ver wan_frame.h
 *
 * ⭐ Lo de acá es el CONTRATO con el servidor. No se "mejora": se reproduce.
 */

#include <stdio.h>
#include <string.h>

#include "wan_frame.h"
#include "cfg_nvm.h"
#include "frtos-io.h"
#include "main.h"

/*
 * ⏳ PROVISORIO de esta etapa. Ver wan_frame.h: lo definitivo lo lee tkWAN del
 * modem con `AT+IMEI?` en el paso 5, y queda fijado para toda la corrida.
 */
static char pcImei[ 16 ] = "000000000000000";

//------------------------------------------------------------------------------
const char *wan_imei( void )
{
    return pcImei;
}
//------------------------------------------------------------------------------
void wan_imei_set( const char *pcNuevo )
{
    if( pcNuevo == NULL )
    {
        return;
    }

    /*
     * Sólo se acepta si tiene pinta de IMEI: 15 dígitos. Un parseo que falle a
     * medias —porque el módulo contestó otra cosa, o llegó cortado— dejaría al
     * equipo presentándose ante el servidor con basura, y eso es peor que
     * seguir con los ceros, que al menos son reconocibles.
     */
    size_t i;

    for( i = 0U; i < 15U; i++ )
    {
        if( ( pcNuevo[ i ] < '0' ) || ( pcNuevo[ i ] > '9' ) )
        {
            return;
        }
    }

    memcpy( pcImei, pcNuevo, 15U );
    pcImei[ 15 ] = '\0';
}
//------------------------------------------------------------------------------
uint16_t wan_frame_ping( char *pcBuf, uint16_t usSize )
{
    if( ( pcBuf == NULL ) || ( usSize == 0U ) )
    {
        return 0U;
    }

    int iN = snprintf( pcBuf, usSize, "ID=%s&HW=%s&TYPE=%s&VER=%s&CLASS=PING",
                       wan_imei(), FW_HW, FW_TYPE, FW_VERSION );

    if( ( iN < 0 ) || ( ( uint16_t ) iN >= usSize ) )
    {
        return 0U;
    }

    return ( uint16_t ) iN;
}
//------------------------------------------------------------------------------
uint16_t wan_frame_data( char *pcBuf, uint16_t usSize, const dataRcd_t *pxDr,
                         bool bConRespuesta )
{
    uint16_t usIdx = 0U;
    bool     bOvf  = false;
    uint8_t  i;

    if( ( pcBuf == NULL ) || ( pxDr == NULL ) || ( usSize == 0U ) )
    {
        return 0U;
    }

    memset( pcBuf, 0, usSize );

    /*
     * Append acotado. El AVR llegó a este mismo patrón después de que el frame
     * desbordara `modem_tx_buffer` y corrompiera variables globales: con 9
     * canales habilitados y nombres normales pasa de 255 bytes. Acá el buffer es
     * de 512, pero la cota se mantiene igual — un frame truncado del otro lado
     * es un registro con campos perdidos, y eso hay que saberlo, no descubrirlo.
     */
    #define FRAME_APPEND( ... )                                                     \
        do {                                                                        \
            if( !bOvf )                                                             \
            {                                                                       \
                int _n = snprintf( &pcBuf[ usIdx ], ( size_t ) ( usSize - usIdx ),  \
                                   __VA_ARGS__ );                                   \
                if( ( _n < 0 ) || ( ( uint16_t ) _n >= ( uint16_t ) ( usSize - usIdx ) ) ) \
                    bOvf = true;                                                    \
                else                                                                \
                    usIdx += ( uint16_t ) _n;                                       \
            }                                                                       \
        } while( 0 )

    /* ---- Encabezado: quién soy y qué mando --------------------------- */
    FRAME_APPEND( "ID=%s&HW=%s&TYPE=%s&VER=%s&CLASS=%s",
                  wan_imei(), FW_HW, FW_TYPE, FW_VERSION,
                  bConRespuesta ? "DATA" : "DATANR" );

    /* ---- Fecha y hora ------------------------------------------------ */
    /* ⚠ DATE es YYMMDD, en ese orden. El año son los dos últimos dígitos: el
       MCP79410 no tiene siglo. */
    FRAME_APPEND( "&DATE=%02d%02d%02d",
                  pxDr->xRtc.year, pxDr->xRtc.month, pxDr->xRtc.day );
    FRAME_APPEND( "&TIME=%02d%02d%02d",
                  pxDr->xRtc.hour, pxDr->xRtc.min, pxDr->xRtc.sec );

    /* ---- Analógicas: 2 decimales ------------------------------------- */
    for( i = 0U; i < CFG_AINPUTS_NRO_CANALES; i++ )
    {
        if( xCfgAinputs.xCanal[ i ].bEnabled )
        {
            float fValor = ( pxDr->usInvalidos & ( uint16_t ) ( dataINVALIDO_AIN0 << i ) )
                           ? WAN_CENTINELA_SIN_DATO
                           : pxDr->fAinputs[ i ];

            FRAME_APPEND( "&%s=%0.2f", xCfgAinputs.xCanal[ i ].pcName, fValor );
        }
    }

    /* ---- Contador: 3 decimales --------------------------------------- */
    if( xCfgCounter.bEnabled )
    {
        float fValor = ( pxDr->usInvalidos & dataINVALIDO_CONTADOR )
                       ? WAN_CENTINELA_SIN_DATO
                       : pxDr->fContador;

        FRAME_APPEND( "&%s=%0.3f", xCfgCounter.pcName, fValor );
    }

    /* ---- Modbus: 3 decimales ----------------------------------------- */
    if( xCfgModbus.bEnabled )
    {
        for( i = 0U; i < CFG_MODBUS_NRO_CANALES; i++ )
        {
            if( xCfgModbus.xCanal[ i ].bEnabled )
            {
                FRAME_APPEND( "&%s=%0.3f", xCfgModbus.xCanal[ i ].pcName, pxDr->fModbus[ i ] );
            }
        }
    }

    /* ---- Válvula ------------------------------------------------------ */
    /* ⚠ 0 = abierta, 1 = cerrada. Al revés de lo intuitivo, y así lo espera el
       servidor desde la V1.2.5 del AVR. No "corregirlo". */
    FRAME_APPEND( "&V0=%d", ( int ) pxDr->ucValvula );

    /* ---- Rieles ------------------------------------------------------- */
    FRAME_APPEND( "&bt3v3=%0.3f", ( pxDr->usInvalidos & dataINVALIDO_BT3V3 )
                                  ? WAN_CENTINELA_SIN_DATO : pxDr->fBt3v3 );
    FRAME_APPEND( "&bt12v=%0.3f", ( pxDr->usInvalidos & dataINVALIDO_BT12V )
                                  ? WAN_CENTINELA_SIN_DATO : pxDr->fBt12v );

    #undef FRAME_APPEND

    if( bOvf )
    {
        xprintf( "WAN:: ERROR: el frame no entra en %u bytes, DESCARTADO !!\r\n",
                 ( unsigned ) usSize );
        return 0U;
    }

    return usIdx;
}
//------------------------------------------------------------------------------
