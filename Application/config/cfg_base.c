/*
 * cfg_base.c  -  ver cfg_base.h
 */

#include <stdlib.h>
#include <string.h>

#include "cfg_base.h"
#include "cfg_hash.h"
#include "frtos-io.h"

cfg_base_t xCfgBase;

/* Los mismos límites que el AVR. TDIAL_MIN_DISCRETO son 900 s: por debajo de eso
   el modem pasaría más tiempo arrancando que transmitiendo. */
#define CFG_BASE_TDIAL_MIN_DISCRETO   900U
#define CFG_BASE_TIMERPOLL_MAX      86400U
#define CFG_BASE_TIMERDIAL_MAX      86400U

//------------------------------------------------------------------------------
void cfg_base_defaults( void )
{
    /* memset antes de llenar: el checksum se calcula sobre la struct ENTERA,
       relleno incluido, así que el padding tiene que ser determinístico. En el
       AVR la struct era global (BSS en cero) y el asunto no se veía; acá se hace
       explícito. Vale para los cinco bloques. */
    memset( &xCfgBase, 0, sizeof( xCfgBase ) );

    xCfgBase.usTimerDial  = 0U;
    xCfgBase.usTimerPoll  = 60U;
    xCfgBase.ePwrModo     = PWR_CONTINUO;
    xCfgBase.usPwrHhmmOn  = 2330U;
    xCfgBase.usPwrHhmmOff = 630U;
}
//------------------------------------------------------------------------------
const char *cfg_base_pwrmodo_str( void )
{
    switch( xCfgBase.ePwrModo )
    {
        case PWR_CONTINUO: return "CONTINUO";
        case PWR_DISCRETO: return "DISCRETO";
        case PWR_MIXTO:    return "MIXTO";
        case PWR_RTU:      return "RTU";
        case PWR_SILENT:   return "SILENT";
        default:           return "???";
    }
}
//------------------------------------------------------------------------------
void cfg_base_print( void )
{
    xprintf( "  timerpoll: %u s\r\n", ( unsigned ) xCfgBase.usTimerPoll );
    xprintf( "  timerdial: %u s\r\n", ( unsigned ) xCfgBase.usTimerDial );
    xprintf( "  pwrmodo  : %s\r\n",   cfg_base_pwrmodo_str() );

    if( xCfgBase.ePwrModo == PWR_MIXTO )
    {
        xprintf( "  pwron    : %04u\r\n", ( unsigned ) xCfgBase.usPwrHhmmOn );
        xprintf( "  pwroff   : %04u\r\n", ( unsigned ) xCfgBase.usPwrHhmmOff );
    }

    /*
     * Los dos modos nuevos cambian algo que no se ve mirando los números, así
     * que lo dice el texto. Sobre todo el SILENT: que los datos dependan de que
     * haya una tarjeta puesta no puede quedar implícito.
     */
    if( xCfgBase.ePwrModo == PWR_RTU )
    {
        xprintf( "             el modem queda SIEMPRE encendido\r\n" );
        xprintf( "             [!] sin enlace, el dato se DESCARTA: no se guarda\r\n" );
    }

    if( xCfgBase.ePwrModo == PWR_SILENT )
    {
        xprintf( "             el modem NO se enciende nunca: no transmite\r\n" );
        xprintf( "             [!] los datos terminan en la microSD: SIN TARJETA SE PIERDEN\r\n" );
    }
}
//------------------------------------------------------------------------------
uint8_t cfg_base_hash( void )
{
    /*
     * ⚠ Cada campo se hashea POR SEPARADO, sobre un buffer que se limpia entre
     * uno y otro. NO es el hash de un string único con todos los campos
     * concatenados: el resultado sería distinto. Es así en el AVR y así tiene
     * que ser acá. Ver cfg_hash.h.
     */
    char     pcBuf[ CFG_HASH_BUFFER_SIZE ];
    uint16_t usIdx;
    uint8_t  ucHash = 0;
    bool     bOvf   = false;

    #define CFG_BASE_HASH_FIELD( ... )                                          \
        do {                                                                    \
            memset( pcBuf, 0, sizeof( pcBuf ) );                                \
            usIdx = 0U;                                                         \
            bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx, __VA_ARGS__ ); \
            ucHash = cfg_hash_string( ucHash, pcBuf );                          \
        } while( 0 )

    CFG_BASE_HASH_FIELD( "[TIMERPOLL:%03d]", ( int ) xCfgBase.usTimerPoll  );
    CFG_BASE_HASH_FIELD( "[TIMERDIAL:%03d]", ( int ) xCfgBase.usTimerDial  );
    CFG_BASE_HASH_FIELD( "[PWRMODO:%d]",     ( int ) xCfgBase.ePwrModo     );
    CFG_BASE_HASH_FIELD( "[PWRON:%04d]",     ( int ) xCfgBase.usPwrHhmmOn  );
    CFG_BASE_HASH_FIELD( "[PWROFF:%04d]",    ( int ) xCfgBase.usPwrHhmmOff );

    #undef CFG_BASE_HASH_FIELD

    if( bOvf )
    {
        xprintf( "BASE:: ERROR: hash buffer overflow !!\r\n" );
    }

    return ucHash;
}
//------------------------------------------------------------------------------
bool cfg_base_set_timerpoll( const char *pcVal )
{
    if( pcVal == NULL )
    {
        return false;
    }

    long lVal = atol( pcVal );

    if( ( lVal <= 0 ) || ( lVal > ( long ) CFG_BASE_TIMERPOLL_MAX ) )
    {
        return false;
    }

    xCfgBase.usTimerPoll = ( uint16_t ) lVal;
    return true;
}
//------------------------------------------------------------------------------
bool cfg_base_set_timerdial( const char *pcVal )
{
    if( pcVal == NULL )
    {
        return false;
    }

    long lVal = atol( pcVal );

    if( ( lVal < 0 ) || ( lVal > ( long ) CFG_BASE_TIMERDIAL_MAX ) )
    {
        return false;
    }

    /*
     * En DISCRETO hay un piso: cada disque prende el modem, espera registro en
     * red y transmite. Con un timerdial chico el equipo pasaría la vida
     * arrancando el modem, que es el mayor consumo de todos.
     */
    if( ( xCfgBase.ePwrModo == PWR_DISCRETO ) &&
        ( lVal > 0 ) && ( lVal < ( long ) CFG_BASE_TDIAL_MIN_DISCRETO ) )
    {
        xprintf( "ERROR: en DISCRETO el timerdial minimo es %u s\r\n",
                 ( unsigned ) CFG_BASE_TDIAL_MIN_DISCRETO );
        return false;
    }

    xCfgBase.usTimerDial = ( uint16_t ) lVal;
    return true;
}
//------------------------------------------------------------------------------
bool cfg_base_set_pwrmodo( const char *pcVal )
{
    if( pcVal == NULL )
    {
        return false;
    }

    if( ( strcasecmp( pcVal, "continuo" ) == 0 ) )
    {
        xCfgBase.ePwrModo = PWR_CONTINUO;
        return true;
    }

    if( ( strcasecmp( pcVal, "discreto" ) == 0 ) )
    {
        xCfgBase.ePwrModo = PWR_DISCRETO;
        return true;
    }

    if( ( strcasecmp( pcVal, "mixto" ) == 0 ) )
    {
        xCfgBase.ePwrModo = PWR_MIXTO;
        return true;
    }

    if( ( strcasecmp( pcVal, "rtu" ) == 0 ) )
    {
        xCfgBase.ePwrModo = PWR_RTU;
        return true;
    }

    if( ( strcasecmp( pcVal, "silent" ) == 0 ) )
    {
        xCfgBase.ePwrModo = PWR_SILENT;
        return true;
    }

    return false;
}
//------------------------------------------------------------------------------
bool cfg_base_modo_sin_modem( void )
{
    return ( xCfgBase.ePwrModo == PWR_SILENT );
}
//------------------------------------------------------------------------------
bool cfg_base_modo_sin_memoria( void )
{
    return ( xCfgBase.ePwrModo == PWR_RTU );
}
//------------------------------------------------------------------------------
/* Una hora en formato HHMM. Se valida como hora de verdad y no sólo como número
   menor a 2400: 1999 pasaría ese filtro y no existe. */
static bool prvHhmmValido( long lVal )
{
    if( ( lVal < 0 ) || ( lVal > 2359 ) )
    {
        return false;
    }

    return ( ( lVal % 100 ) < 60 );
}
//------------------------------------------------------------------------------
bool cfg_base_set_pwron( const char *pcVal )
{
    if( pcVal == NULL )
    {
        return false;
    }

    long lVal = atol( pcVal );

    if( !prvHhmmValido( lVal ) )
    {
        return false;
    }

    xCfgBase.usPwrHhmmOn = ( uint16_t ) lVal;
    return true;
}
//------------------------------------------------------------------------------
bool cfg_base_set_pwroff( const char *pcVal )
{
    if( pcVal == NULL )
    {
        return false;
    }

    long lVal = atol( pcVal );

    if( !prvHhmmValido( lVal ) )
    {
        return false;
    }

    xCfgBase.usPwrHhmmOff = ( uint16_t ) lVal;
    return true;
}
//------------------------------------------------------------------------------
