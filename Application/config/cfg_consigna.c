/*
 * cfg_consigna.c  -  ver cfg_consigna.h
 */

#include <stdlib.h>
#include <string.h>

#include "cfg_consigna.h"
#include "cfg_hash.h"
#include "cfg_utils.h"
#include "frtos-io.h"

cfg_consigna_t xCfgConsigna;

//------------------------------------------------------------------------------
void cfg_consigna_defaults( void )
{
    memset( &xCfgConsigna, 0, sizeof( xCfgConsigna ) );

    xCfgConsigna.bEnabled   = false;
    xCfgConsigna.usDiurna   = 700U;
    xCfgConsigna.usNocturna = 2300U;
}
//------------------------------------------------------------------------------
void cfg_consigna_print( void )
{
    xprintf( "  consigna: %s, diurna=%04u, nocturna=%04u\r\n",
             xCfgConsigna.bEnabled ? "true" : "false",
             ( unsigned ) xCfgConsigna.usDiurna,
             ( unsigned ) xCfgConsigna.usNocturna );
}
//------------------------------------------------------------------------------
uint8_t cfg_consigna_hash( void )
{
    /* ⚠ Un solo append con todo adentro; el AVR lo hace así. Ver cfg_hash.h. */
    char     pcBuf[ CFG_HASH_BUFFER_SIZE ];
    uint16_t usIdx = 0U;
    bool     bOvf;

    memset( pcBuf, 0, sizeof( pcBuf ) );

    bOvf = !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                             xCfgConsigna.bEnabled ? "[TRUE,%04d,%04d]" : "[FALSE,%04d,%04d]",
                             ( int ) xCfgConsigna.usDiurna,
                             ( int ) xCfgConsigna.usNocturna );

    if( bOvf )
    {
        xprintf( "CONSIGNA:: ERROR: hash buffer overflow !!\r\n" );
    }

    return cfg_hash_string( 0U, pcBuf );
}
//------------------------------------------------------------------------------
static bool prvHhmmValido( long lVal )
{
    if( ( lVal < 0 ) || ( lVal > 2359 ) )
    {
        return false;
    }

    return ( ( lVal % 100 ) < 60 );
}
//------------------------------------------------------------------------------
bool cfg_consigna_set( const char *pcEnable, const char *pcDiurna, const char *pcNocturna )
{
    if( ( pcEnable == NULL ) || ( pcDiurna == NULL ) || ( pcNocturna == NULL ) )
    {
        return false;
    }

    bool bEnable;

    if( !cfg_str2bool( pcEnable, &bEnable ) )
    {
        return false;
    }

    long lDiurna   = atol( pcDiurna );
    long lNocturna = atol( pcNocturna );

    if( !prvHhmmValido( lDiurna ) || !prvHhmmValido( lNocturna ) )
    {
        xprintf( "ERROR: las horas van en formato HHMM (0000..2359)\r\n" );
        return false;
    }

    /* Iguales no tiene sentido: nunca habría cambio de consigna, y el equipo
       quedaría con la última que se le mandó sin que nada lo delate. */
    if( lDiurna == lNocturna )
    {
        xprintf( "ERROR: diurna y nocturna no pueden ser la misma hora\r\n" );
        return false;
    }

    xCfgConsigna.bEnabled   = bEnable;
    xCfgConsigna.usDiurna   = ( uint16_t ) lDiurna;
    xCfgConsigna.usNocturna = ( uint16_t ) lNocturna;

    return true;
}
//------------------------------------------------------------------------------
