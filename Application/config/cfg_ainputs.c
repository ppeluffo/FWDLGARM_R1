/*
 * cfg_ainputs.c  -  ver cfg_ainputs.h
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cfg_ainputs.h"
#include "cfg_hash.h"
#include "cfg_utils.h"
#include "frtos-io.h"

cfg_ainputs_t xCfgAinputs;

#define CFG_AIN_SETTLE_MAX      180U    /* segundos; más que eso es un error de tipeo */

//------------------------------------------------------------------------------
void cfg_ainputs_defaults( void )
{
    uint8_t i;

    memset( &xCfgAinputs, 0, sizeof( xCfgAinputs ) );

    xCfgAinputs.ucSensorsPwrSettleTime = 0U;

    for( i = 0U; i < CFG_AINPUTS_NRO_CANALES; i++ )
    {
        xCfgAinputs.xCanal[ i ].bEnabled = false;
        xCfgAinputs.xCanal[ i ].ucImin   = 0U;
        xCfgAinputs.xCanal[ i ].ucImax   = 20U;
        xCfgAinputs.xCanal[ i ].fMmin    = 0.0f;
        xCfgAinputs.xCanal[ i ].fMmax    = 10.0f;
        xCfgAinputs.xCanal[ i ].fOffset  = 0.0f;
        cfg_strlcpy( xCfgAinputs.xCanal[ i ].pcName, "X", CFG_PARAMNAME_LENGTH );
    }
}
//------------------------------------------------------------------------------
void cfg_ainputs_print( void )
{
    uint8_t i;

    xprintf( "  pwr settle time: %u s\r\n", ( unsigned ) xCfgAinputs.ucSensorsPwrSettleTime );

    for( i = 0U; i < CFG_AINPUTS_NRO_CANALES; i++ )
    {
        xprintf( "  a%u: %s,%s,%u,%u,%.02f,%.02f,%.02f\r\n", ( unsigned ) i,
                 xCfgAinputs.xCanal[ i ].bEnabled ? "true" : "false",
                 xCfgAinputs.xCanal[ i ].pcName,
                 ( unsigned ) xCfgAinputs.xCanal[ i ].ucImin,
                 ( unsigned ) xCfgAinputs.xCanal[ i ].ucImax,
                 xCfgAinputs.xCanal[ i ].fMmin,
                 xCfgAinputs.xCanal[ i ].fMmax,
                 xCfgAinputs.xCanal[ i ].fOffset );
    }
}
//------------------------------------------------------------------------------
uint8_t cfg_ainputs_hash( void )
{
    /* ⚠ Formato exacto del AVR. Ver cfg_hash.h antes de tocar una coma. */
    char     pcBuf[ CFG_HASH_BUFFER_SIZE ];
    uint16_t usIdx;
    uint8_t  ucHash = 0;
    uint8_t  i;
    bool     bOvf;

    /* Primero el settle time, en su propio bloque. */
    memset( pcBuf, 0, sizeof( pcBuf ) );
    usIdx = 0U;
    bOvf  = !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                              "[PST:%03d]", ( int ) xCfgAinputs.ucSensorsPwrSettleTime );
    if( bOvf )
    {
        xprintf( "AIN:: ERROR: hash buffer overflow !!\r\n" );
    }
    ucHash = cfg_hash_string( ucHash, pcBuf );

    /* Y después un bloque por canal. */
    for( i = 0U; i < CFG_AINPUTS_NRO_CANALES; i++ )
    {
        memset( pcBuf, 0, sizeof( pcBuf ) );
        usIdx = 0U;
        bOvf  = false;

        bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                                  xCfgAinputs.xCanal[ i ].bEnabled ? "[A%d:TRUE," : "[A%d:FALSE,",
                                  ( int ) i );
        bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                                  "%s,", xCfgAinputs.xCanal[ i ].pcName );
        bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                                  "%d,%d,", ( int ) xCfgAinputs.xCanal[ i ].ucImin,
                                            ( int ) xCfgAinputs.xCanal[ i ].ucImax );
        bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                                  "%.02f,%.02f,", xCfgAinputs.xCanal[ i ].fMmin,
                                                  xCfgAinputs.xCanal[ i ].fMmax );
        bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                                  "%.02f]", xCfgAinputs.xCanal[ i ].fOffset );

        if( bOvf )
        {
            xprintf( "AIN:: ERROR: hash buffer overflow (canal %u) !!\r\n", ( unsigned ) i );
        }

        ucHash = cfg_hash_string( ucHash, pcBuf );
    }

    return ucHash;
}
//------------------------------------------------------------------------------
bool cfg_ainputs_set_canal( uint8_t ucCh, const char *pcEnable, const char *pcName,
                            const char *pcImin, const char *pcImax,
                            const char *pcMmin, const char *pcMmax,
                            const char *pcOffset )
{
    if( ucCh >= CFG_AINPUTS_NRO_CANALES )
    {
        return false;
    }

    if( ( pcEnable == NULL ) || ( pcName == NULL ) || ( pcImin == NULL ) ||
        ( pcImax == NULL )   || ( pcMmin == NULL ) || ( pcMmax == NULL ) ||
        ( pcOffset == NULL ) )
    {
        return false;
    }

    bool bEnable;

    if( !cfg_str2bool( pcEnable, &bEnable ) )
    {
        return false;
    }

    long lImin = atol( pcImin );
    long lImax = atol( pcImax );

    /*
     * imin < imax no es un capricho: la conversión divide por (imax - imin). Con
     * los dos iguales sería una división por cero, y al revés la recta quedaría
     * invertida sin que nadie lo note hasta ver los datos en el servidor.
     */
    if( ( lImin < 0 ) || ( lImax > 20 ) || ( lImin >= lImax ) )
    {
        xprintf( "ERROR: imin/imax fuera de rango (0..20, imin<imax)\r\n" );
        return false;
    }

    float fMmin = ( float ) atof( pcMmin );
    float fMmax = ( float ) atof( pcMmax );

    if( fMmin == fMmax )
    {
        xprintf( "ERROR: mmin y mmax no pueden ser iguales\r\n" );
        return false;
    }

    xCfgAinputs.xCanal[ ucCh ].bEnabled = bEnable;
    xCfgAinputs.xCanal[ ucCh ].ucImin   = ( uint8_t ) lImin;
    xCfgAinputs.xCanal[ ucCh ].ucImax   = ( uint8_t ) lImax;
    xCfgAinputs.xCanal[ ucCh ].fMmin    = fMmin;
    xCfgAinputs.xCanal[ ucCh ].fMmax    = fMmax;
    xCfgAinputs.xCanal[ ucCh ].fOffset  = ( float ) atof( pcOffset );
    cfg_strlcpy( xCfgAinputs.xCanal[ ucCh ].pcName, pcName, CFG_PARAMNAME_LENGTH );

    return true;
}
//------------------------------------------------------------------------------
bool cfg_ainputs_set_settle_time( const char *pcVal )
{
    if( pcVal == NULL )
    {
        return false;
    }

    long lVal = atol( pcVal );

    if( ( lVal < 0 ) || ( lVal > ( long ) CFG_AIN_SETTLE_MAX ) )
    {
        return false;
    }

    xCfgAinputs.ucSensorsPwrSettleTime = ( uint8_t ) lVal;
    return true;
}
//------------------------------------------------------------------------------
float cfg_ainputs_convertir( uint8_t ucCh, float fMa )
{
    if( ucCh >= CFG_AINPUTS_NRO_CANALES )
    {
        return -999.0f;
    }

    const cfg_ainput_canal_t *px = &xCfgAinputs.xCanal[ ucCh ];

    /* En el AVR el denominador se calcula sobre los uint8 y se compara contra 0;
       acá es float pero el caso es el mismo: sin span no hay recta. */
    float fSpanI = ( float ) px->ucImax - ( float ) px->ucImin;

    if( fSpanI == 0.0f )
    {
        return -999.0f;
    }

    float fPendiente = ( px->fMmax - px->fMmin ) / fSpanI;
    float fMag       = px->fMmin + ( fMa - ( float ) px->ucImin ) * fPendiente;

    fMag += px->fOffset;

    /* Ver el header: sin esto, un cero con ruido sale como "-0.00" en el frame. */
    if( fabsf( fMag ) < 0.01f )
    {
        fMag = 0.0f;
    }

    return fMag;
}
//------------------------------------------------------------------------------
