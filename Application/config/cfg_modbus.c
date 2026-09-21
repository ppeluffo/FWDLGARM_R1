/*
 * cfg_modbus.c  -  ver cfg_modbus.h
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cfg_modbus.h"

/* Sólo por `DRV_MODBUS_MAX_REGS`: la configuración no puede admitir algo que el
   driver no pueda recibir. Es la única dependencia de config hacia un driver. */
#include "drv_modbus.h"
#include "cfg_hash.h"
#include "cfg_utils.h"
#include "frtos-io.h"

cfg_modbus_t xCfgModbus;

/* Los strings van en el mismo orden que el enum: se indexan directo. Si alguien
   agrega un tipo, tiene que agregarlo en los dos lados. */
static const char * const pcTipoStr [] = { "U16", "I16", "U32", "I32", "FLOAT" };
static const char * const pcCodecStr[] = { "C0123", "C1032", "C3210", "C2301" };

//------------------------------------------------------------------------------
const char *cfg_modbus_tipo_str( cfg_modbus_tipo_t eTipo )
{
    return ( eTipo <= CFG_MB_FLOAT ) ? pcTipoStr[ eTipo ] : "???";
}
//------------------------------------------------------------------------------
const char *cfg_modbus_codec_str( cfg_modbus_codec_t eCodec )
{
    return ( eCodec <= CFG_MB_C2301 ) ? pcCodecStr[ eCodec ] : "???";
}
//------------------------------------------------------------------------------
void cfg_modbus_defaults( void )
{
    uint8_t i;

    memset( &xCfgModbus, 0, sizeof( xCfgModbus ) );

    xCfgModbus.bEnabled    = false;
    xCfgModbus.ucLocalAddr = 0x01U;

    for( i = 0U; i < CFG_MODBUS_NRO_CANALES; i++ )
    {
        xCfgModbus.xCanal[ i ].bEnabled       = false;
        cfg_strlcpy( xCfgModbus.xCanal[ i ].pcName, "X", CFG_PARAMNAME_LENGTH );
        xCfgModbus.xCanal[ i ].ucSlaveAddress = 0U;
        xCfgModbus.xCanal[ i ].usRegAddress   = 0U;
        xCfgModbus.xCanal[ i ].ucNroRegs      = 1U;
        xCfgModbus.xCanal[ i ].ucFcode        = 3U;
        xCfgModbus.xCanal[ i ].eTipo          = CFG_MB_U16;
        xCfgModbus.xCanal[ i ].eCodec         = CFG_MB_C0123;
        xCfgModbus.xCanal[ i ].ucDivisorP10   = 0U;
    }
}
//------------------------------------------------------------------------------
void cfg_modbus_print( void )
{
    uint8_t i;

    xprintf( "  modbus: %s, localaddr=%u\r\n",
             xCfgModbus.bEnabled ? "true" : "false",
             ( unsigned ) xCfgModbus.ucLocalAddr );

    for( i = 0U; i < CFG_MODBUS_NRO_CANALES; i++ )
    {
        xprintf( "  m%u: %s,%s,%u,%u,%u,%u,%s,%s,%u\r\n", ( unsigned ) i,
                 xCfgModbus.xCanal[ i ].bEnabled ? "true" : "false",
                 xCfgModbus.xCanal[ i ].pcName,
                 ( unsigned ) xCfgModbus.xCanal[ i ].ucSlaveAddress,
                 ( unsigned ) xCfgModbus.xCanal[ i ].usRegAddress,
                 ( unsigned ) xCfgModbus.xCanal[ i ].ucNroRegs,
                 ( unsigned ) xCfgModbus.xCanal[ i ].ucFcode,
                 cfg_modbus_tipo_str ( xCfgModbus.xCanal[ i ].eTipo  ),
                 cfg_modbus_codec_str( xCfgModbus.xCanal[ i ].eCodec ),
                 ( unsigned ) xCfgModbus.xCanal[ i ].ucDivisorP10 );
    }
}
//------------------------------------------------------------------------------
uint8_t cfg_modbus_hash( void )
{
    /* ⚠ Formato exacto del AVR: primero el bloque local, después uno por canal.
       Ver cfg_hash.h. */
    char     pcBuf[ CFG_HASH_BUFFER_SIZE ];
    uint16_t usIdx;
    uint8_t  ucHash = 0;
    uint8_t  i;
    bool     bOvf;

    memset( pcBuf, 0, sizeof( pcBuf ) );
    usIdx = 0U;
    bOvf  = !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                              xCfgModbus.bEnabled ? "[TRUE,%02d]" : "[FALSE,%02d]",
                              ( int ) xCfgModbus.ucLocalAddr );
    if( bOvf )
    {
        xprintf( "MODBUS:: ERROR: hash buffer overflow !!\r\n" );
    }
    ucHash = cfg_hash_string( ucHash, pcBuf );

    for( i = 0U; i < CFG_MODBUS_NRO_CANALES; i++ )
    {
        memset( pcBuf, 0, sizeof( pcBuf ) );
        usIdx = 0U;
        bOvf  = false;

        bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx, "[M%d:", ( int ) i );
        bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                                  xCfgModbus.xCanal[ i ].bEnabled ? "TRUE," : "FALSE," );
        bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                                  "%s,%02d,%04d,%02d,%02d,",
                                  xCfgModbus.xCanal[ i ].pcName,
                                  ( int ) xCfgModbus.xCanal[ i ].ucSlaveAddress,
                                  ( int ) xCfgModbus.xCanal[ i ].usRegAddress,
                                  ( int ) xCfgModbus.xCanal[ i ].ucNroRegs,
                                  ( int ) xCfgModbus.xCanal[ i ].ucFcode );
        bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                                  "%s,", cfg_modbus_tipo_str( xCfgModbus.xCanal[ i ].eTipo ) );
        bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                                  "%s,", cfg_modbus_codec_str( xCfgModbus.xCanal[ i ].eCodec ) );
        bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                                  "%02d]", ( int ) xCfgModbus.xCanal[ i ].ucDivisorP10 );

        if( bOvf )
        {
            xprintf( "MODBUS:: ERROR: hash buffer overflow (canal %u) !!\r\n", ( unsigned ) i );
        }

        ucHash = cfg_hash_string( ucHash, pcBuf );
    }

    return ucHash;
}
//------------------------------------------------------------------------------
bool cfg_modbus_set_enable( const char *pcVal )
{
    bool bEnable;

    if( !cfg_str2bool( pcVal, &bEnable ) )
    {
        return false;
    }

    xCfgModbus.bEnabled = bEnable;
    return true;
}
//------------------------------------------------------------------------------
bool cfg_modbus_set_localaddr( const char *pcVal )
{
    if( pcVal == NULL )
    {
        return false;
    }

    long lVal = atol( pcVal );

    /* 0 es la dirección de broadcast y 248..255 están reservadas por la norma. */
    if( ( lVal < 1 ) || ( lVal > 247 ) )
    {
        xprintf( "ERROR: la direccion modbus va de 1 a 247\r\n" );
        return false;
    }

    xCfgModbus.ucLocalAddr = ( uint8_t ) lVal;
    return true;
}
//------------------------------------------------------------------------------
bool cfg_modbus_parse_tipo( const char *pcStr, cfg_modbus_tipo_t *peOut )
{
    uint8_t i;

    for( i = 0U; i <= ( uint8_t ) CFG_MB_FLOAT; i++ )
    {
        if( strcasecmp( pcStr, pcTipoStr[ i ] ) == 0 )
        {
            *peOut = ( cfg_modbus_tipo_t ) i;
            return true;
        }
    }

    return false;
}
//------------------------------------------------------------------------------
bool cfg_modbus_parse_codec( const char *pcStr, cfg_modbus_codec_t *peOut )
{
    uint8_t i;

    for( i = 0U; i <= ( uint8_t ) CFG_MB_C2301; i++ )
    {
        if( strcasecmp( pcStr, pcCodecStr[ i ] ) == 0 )
        {
            *peOut = ( cfg_modbus_codec_t ) i;
            return true;
        }
    }

    return false;
}
//------------------------------------------------------------------------------
/*
 * ⚠ **Un argumento en NULL significa "no cambiar este campo"**, y el ÚNICO
 * obligatorio es el nombre. No es una comodidad: es la regla del AVR
 * (`modbus_config_channel()`), y **el servidor la usa**. El 2026-09-11 contestó
 * `C0=FALSE,X,1.0,CAUDAL` —cuatro campos de los seis— y exigirlos todos hacía
 * que el bloque entero se descartara.
 *
 * Los que vengan se validan **contra los que ya están**, y recién si el
 * conjunto completo cierra se escribe: así un campo suelto no puede dejar la
 * configuración en un estado que ninguna validación aprobó.
 */
bool cfg_modbus_set_canal( uint8_t ucCh, const char *pcEnable, const char *pcName,
                           const char *pcSlaveAddr, const char *pcRegAddr,
                           const char *pcNroRegs, const char *pcFcode,
                           const char *pcTipo, const char *pcCodec,
                           const char *pcDivisor )
{
    if( ucCh >= CFG_MODBUS_NRO_CANALES )
    {
        return false;
    }

    if( pcName == NULL )
    {
        return false;
    }

    const cfg_modbus_canal_t *pxAct = &xCfgModbus.xCanal[ ucCh ];

    bool bEnable = pxAct->bEnabled;

    if( ( pcEnable != NULL ) && !cfg_str2bool( pcEnable, &bEnable ) )
    {
        return false;
    }

    cfg_modbus_tipo_t  eTipo  = pxAct->eTipo;
    cfg_modbus_codec_t eCodec = pxAct->eCodec;

    if( ( pcTipo != NULL ) && !cfg_modbus_parse_tipo( pcTipo, &eTipo ) )
    {
        xprintf( "ERROR: tipo invalido (U16|I16|U32|I32|FLOAT)\r\n" );
        return false;
    }

    if( ( pcCodec != NULL ) && !cfg_modbus_parse_codec( pcCodec, &eCodec ) )
    {
        xprintf( "ERROR: codec invalido (C0123|C1032|C3210|C2301)\r\n" );
        return false;
    }

    long lSlave   = ( pcSlaveAddr != NULL ) ? atol( pcSlaveAddr ) : ( long ) pxAct->ucSlaveAddress;
    long lReg     = ( pcRegAddr   != NULL ) ? atol( pcRegAddr   ) : ( long ) pxAct->usRegAddress;
    long lNroRegs = ( pcNroRegs   != NULL ) ? atol( pcNroRegs   ) : ( long ) pxAct->ucNroRegs;
    long lFcode   = ( pcFcode     != NULL ) ? atol( pcFcode     ) : ( long ) pxAct->ucFcode;
    long lDivisor = ( pcDivisor   != NULL ) ? atol( pcDivisor   ) : ( long ) pxAct->ucDivisorP10;

    if( ( lSlave < 1 ) || ( lSlave > 247 ) )
    {
        xprintf( "ERROR: la direccion del esclavo va de 1 a 247\r\n" );
        return false;
    }

    if( ( lReg < 0 ) || ( lReg > 65535 ) )
    {
        return false;
    }

    /*
     * Un tipo de 32 bits necesita 2 registros de 16. Dejarlo en 1 leería la
     * mitad del número y el valor saldría mal SIN error: es un dato plausible y
     * equivocado, que es lo peor que puede mandar un datalogger.
     */
    uint8_t ucRegsMin = ( ( eTipo == CFG_MB_U32 ) || ( eTipo == CFG_MB_I32 ) ||
                          ( eTipo == CFG_MB_FLOAT ) ) ? 2U : 1U;

    /*
     * ⛔ El techo NO es el del protocolo (125): es el del BUFFER DE RECEPCIÓN
     * del driver, y por eso el número sale de `drv_modbus.h`.
     *
     * El AVR acepta cualquier valor acá y después, al recibir, trunca la trama e
     * imprime un error — o sea que la configuración se guarda bien y el canal
     * falla en campo. Rechazarlo al configurar es la diferencia entre un mensaje
     * que alguien está mirando y uno que nadie va a leer.
     */
    if( ( lNroRegs < ( long ) ucRegsMin ) || ( lNroRegs > ( long ) DRV_MODBUS_MAX_REGS ) )
    {
        xprintf( "ERROR: nro de registros: %s necesita al menos %u, y el maximo es %u\r\n",
                 cfg_modbus_tipo_str( eTipo ), ( unsigned ) ucRegsMin,
                 ( unsigned ) DRV_MODBUS_MAX_REGS );
        return false;
    }

    /* 3 = holding registers, 4 = input registers. Son los dos que sabe leer. */
    if( ( lFcode != 3 ) && ( lFcode != 4 ) )
    {
        xprintf( "ERROR: fcode tiene que ser 3 o 4\r\n" );
        return false;
    }

    if( ( lDivisor < 0 ) || ( lDivisor > 9 ) )
    {
        return false;
    }

    xCfgModbus.xCanal[ ucCh ].bEnabled       = bEnable;
    xCfgModbus.xCanal[ ucCh ].ucSlaveAddress = ( uint8_t )  lSlave;
    xCfgModbus.xCanal[ ucCh ].usRegAddress   = ( uint16_t ) lReg;
    xCfgModbus.xCanal[ ucCh ].ucNroRegs      = ( uint8_t )  lNroRegs;
    xCfgModbus.xCanal[ ucCh ].ucFcode        = ( uint8_t )  lFcode;
    xCfgModbus.xCanal[ ucCh ].eTipo          = eTipo;
    xCfgModbus.xCanal[ ucCh ].eCodec         = eCodec;
    xCfgModbus.xCanal[ ucCh ].ucDivisorP10   = ( uint8_t )  lDivisor;
    cfg_strlcpy( xCfgModbus.xCanal[ ucCh ].pcName, pcName, CFG_PARAMNAME_LENGTH );

    return true;
}
//------------------------------------------------------------------------------
