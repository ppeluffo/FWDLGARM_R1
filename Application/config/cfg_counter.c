/*
 * cfg_counter.c  -  ver cfg_counter.h
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cfg_counter.h"
#include "cfg_hash.h"
#include "cfg_utils.h"
#include "frtos-io.h"

cfg_counter_t xCfgCounter;

//------------------------------------------------------------------------------
void cfg_counter_defaults( void )
{
    memset( &xCfgCounter, 0, sizeof( xCfgCounter ) );

    cfg_strlcpy( xCfgCounter.pcName, "X", CFG_PARAMNAME_LENGTH );
    xCfgCounter.bEnabled     = false;
    xCfgCounter.fMagPP       = 1.0f;
    xCfgCounter.eModoMedida  = CFG_CNT_CAUDAL;
    xCfgCounter.fQmax        = 100.0f;
    xCfgCounter.fAlpha       = 0.25f;
    xCfgCounter.ulAgingMinMs = 300000UL;
}
//------------------------------------------------------------------------------
void cfg_counter_print( void )
{
    xprintf( "  c0: %s,%s,magpp=%.03f,%s,qmax=%.02f,alpha=%.02f\r\n",
             xCfgCounter.bEnabled ? "true" : "false",
             xCfgCounter.pcName,
             xCfgCounter.fMagPP,
             ( xCfgCounter.eModoMedida == CFG_CNT_CAUDAL ) ? "CAUDAL" : "PULSOS",
             xCfgCounter.fQmax,
             xCfgCounter.fAlpha );
}
//------------------------------------------------------------------------------
uint8_t cfg_counter_hash( void )
{
    /*
     * ⚠ Formato exacto del AVR, y ojo con el ORDEN: magpp va ANTES del modo, y
     * QMAX y alpha van al final. Ver cfg_hash.h.
     */
    char     pcBuf[ CFG_HASH_BUFFER_SIZE ];
    uint16_t usIdx = 0U;
    bool     bOvf  = false;

    memset( pcBuf, 0, sizeof( pcBuf ) );

    bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx, "[C0:" );
    bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                              xCfgCounter.bEnabled ? "TRUE," : "FALSE," );
    bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx, "%s,", xCfgCounter.pcName );
    bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx, "%.03f,", xCfgCounter.fMagPP );
    bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                              ( xCfgCounter.eModoMedida == CFG_CNT_CAUDAL ) ? "CAUDAL," : "PULSOS," );
    bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx, "%.2f,", xCfgCounter.fQmax );
    bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx, "%.2f]", xCfgCounter.fAlpha );

    if( bOvf )
    {
        xprintf( "COUNTER:: ERROR: hash buffer overflow !!\r\n" );
    }

    return cfg_hash_string( 0U, pcBuf );
}
//------------------------------------------------------------------------------
bool cfg_counter_set( const char *pcEnable, const char *pcName, const char *pcMagPP,
                      const char *pcModo, const char *pcQmax, const char *pcAlpha )
{
    if( ( pcEnable == NULL ) || ( pcName == NULL ) || ( pcMagPP == NULL ) ||
        ( pcModo == NULL )   || ( pcQmax == NULL ) || ( pcAlpha == NULL ) )
    {
        return false;
    }

    bool bEnable;

    if( !cfg_str2bool( pcEnable, &bEnable ) )
    {
        return false;
    }

    cfg_counter_modo_t eModo;

    if( strcasecmp( pcModo, "caudal" ) == 0 )
    {
        eModo = CFG_CNT_CAUDAL;
    }
    else if( strcasecmp( pcModo, "pulsos" ) == 0 )
    {
        eModo = CFG_CNT_PULSOS;
    }
    else
    {
        return false;
    }

    float fMagPP = ( float ) atof( pcMagPP );
    float fQmax  = ( float ) atof( pcQmax );
    float fAlpha = ( float ) atof( pcAlpha );

    /* magpp es el divisor de la conversión pulsos->magnitud: en cero o negativo
       no significa nada, y en cero además dividiría por cero. */
    if( fMagPP <= 0.0f )
    {
        xprintf( "ERROR: magpp tiene que ser mayor que cero\r\n" );
        return false;
    }

    if( fQmax <= 0.0f )
    {
        xprintf( "ERROR: qmax tiene que ser mayor que cero\r\n" );
        return false;
    }

    /*
     * alpha es el peso del filtro exponencial del caudal: en 0 la medida nueva
     * no entra nunca y el caudal queda congelado; por encima de 1 el filtro
     * amplifica en vez de suavizar y oscila. El rango útil es (0, 1].
     */
    if( ( fAlpha <= 0.0f ) || ( fAlpha > 1.0f ) )
    {
        xprintf( "ERROR: alpha tiene que estar entre 0 (exclusive) y 1\r\n" );
        return false;
    }

    xCfgCounter.bEnabled    = bEnable;
    xCfgCounter.fMagPP      = fMagPP;
    xCfgCounter.fQmax       = fQmax;
    xCfgCounter.eModoMedida = eModo;
    xCfgCounter.fAlpha      = fAlpha;
    cfg_strlcpy( xCfgCounter.pcName, pcName, CFG_PARAMNAME_LENGTH );

    return true;
}
//------------------------------------------------------------------------------
