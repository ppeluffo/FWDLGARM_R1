/*
 * cfg_flowcontrol.c  -  ver cfg_flowcontrol.h
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cfg_flowcontrol.h"
#include "cfg_hash.h"
#include "cfg_utils.h"
#include "frtos-io.h"

cfg_flowcontrol_t xCfgFlow;

/* Índice = número del día. El 0 no lo usa el servicio —su comparación es contra
   el día real, que va de 1 a 7— pero el AVR lo imprime como `**`. */
static const char * const pcDowStr[] = {
    "**", "LU", "MA", "MI", "JU", "VI", "SA", "DO", "--"
};

//------------------------------------------------------------------------------
const char *cfg_flowcontrol_dow_str( uint8_t ucDow )
{
    return ( ucDow <= CFG_FLOW_DOW_LIBRE ) ? pcDowStr[ ucDow ] : "?";
}
//------------------------------------------------------------------------------
void cfg_flowcontrol_defaults( void )
{
    uint8_t i;

    /* El memset va primero por el relleno del compilador: el checksum se calcula
       sobre la struct entera, así que padding sin inicializar haría que una
       configuración recién puesta no coincida consigo misma al releerla. */
    memset( &xCfgFlow, 0, sizeof( xCfgFlow ) );

    xCfgFlow.bEnabled = false;

    for( i = 0U; i < CFG_FLOW_NRO_SLOTS; i++ )
    {
        /* Los defaults del AVR, y tienen que ser éstos exactos: entran en el
           hash, así que un equipo recién configurado tiene que dar lo mismo que
           el servidor calcula para él. */
        xCfgFlow.xSlot[ i ].ucDow   = CFG_FLOW_DOW_LIBRE;
        xCfgFlow.xSlot[ i ].usPtime = 0U;
        xCfgFlow.xSlot[ i ].bAbrir  = true;     /* VALVE_OPEN */
    }
}
//------------------------------------------------------------------------------
void cfg_flowcontrol_print( void )
{
    uint8_t i;
    uint8_t ucUsados = 0U;

    xprintf( "  flowcontrol: %s\r\n", xCfgFlow.bEnabled ? "true" : "false" );

    for( i = 0U; i < CFG_FLOW_NRO_SLOTS; i++ )
    {
        if( xCfgFlow.xSlot[ i ].ucDow >= CFG_FLOW_DOW_LIBRE )
        {
            continue;   /* los libres no se imprimen: serían 14 líneas de ruido */
        }

        xprintf( "    s%02u: %s %04u %s\r\n", ( unsigned ) i,
                 cfg_flowcontrol_dow_str( xCfgFlow.xSlot[ i ].ucDow ),
                 ( unsigned ) xCfgFlow.xSlot[ i ].usPtime,
                 xCfgFlow.xSlot[ i ].bAbrir ? "OPEN" : "CLOSE" );
        ucUsados++;
    }

    if( ucUsados == 0U )
    {
        xprintf( "    (ningun slot configurado)\r\n" );
    }

    /*
     * ⚠ EL AVISO NO ES OPCIONAL, y por eso va aunque no haya slots.
     *
     * La tabla se guarda y viaja en el hash, pero **todavía no se ejecuta**
     * (alcance acordado con Pablo el 2026-09-22). Sin esta línea, alguien
     * configura horarios, los ve guardados, y espera que la válvula se mueva
     * sola. Un equipo que *parece* hacer algo que no hace es peor que uno que no
     * lo ofrece.
     */
    xprintf( "    [!] los horarios NO se ejecutan todavia: solo se configuran.\r\n" );
    xprintf( "        La valvula se mueve por 'ev' o por orden del servidor.\r\n" );
}
//------------------------------------------------------------------------------
uint8_t cfg_flowcontrol_hash( void )
{
    char     pcBuf[ CFG_HASH_BUFFER_SIZE ];
    uint16_t usIdx;
    uint8_t  ucHash = 0U;
    uint8_t  i;
    bool     bOvf;

    /* ---- Primero el enable, en su propio buffer ---- */
    memset( pcBuf, 0, sizeof( pcBuf ) );
    usIdx = 0U;

    bOvf = !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                             xCfgFlow.bEnabled ? "[TRUE]" : "[FALSE]" );

    ucHash = cfg_hash_string( ucHash, pcBuf );

    /* ---- Y después un buffer LIMPIO por cada slot ---- */
    for( i = 0U; i < CFG_FLOW_NRO_SLOTS; i++ )
    {
        memset( pcBuf, 0, sizeof( pcBuf ) );
        usIdx = 0U;

        /* `[SLOT%02d:%02d,%04d,OPEN]` — literal del AVR. El `%02d` del día y el
           `%04d` de la hora importan: un cero de menos cambia el hash. */
        bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                                  "[SLOT%02d:%02d,%04d,",
                                  ( int ) i,
                                  ( int ) xCfgFlow.xSlot[ i ].ucDow,
                                  ( int ) xCfgFlow.xSlot[ i ].usPtime );

        bOvf |= !cfg_hash_append( pcBuf, sizeof( pcBuf ), &usIdx,
                                  xCfgFlow.xSlot[ i ].bAbrir ? "OPEN]" : "CLOSE]" );

        ucHash = cfg_hash_string( ucHash, pcBuf );
    }

    if( bOvf )
    {
        xprintf( "FLOWC:: ERROR: hash buffer overflow !!\r\n" );
    }

    return ucHash;
}
//------------------------------------------------------------------------------
bool cfg_flowcontrol_set_enable( const char *pcEnable )
{
    bool bEnable = xCfgFlow.bEnabled;

    if( ( pcEnable == NULL ) || !cfg_str2bool( pcEnable, &bEnable ) )
    {
        return false;
    }

    xCfgFlow.bEnabled = bEnable;

    return true;
}
//------------------------------------------------------------------------------
static uint8_t prvParseDow( const char *pcDow )
{
    uint8_t i;

    if( pcDow == NULL )
    {
        return CFG_FLOW_DOW_LIBRE;
    }

    /* Del 1 (LU) al 7 (DO). El 0 (`**`) no se acepta desde afuera: el servicio
       compara contra el día real, que nunca vale 0, así que un slot con 0 no se
       ejecutaría nunca y sería una configuración que miente. */
    for( i = 1U; i <= 7U; i++ )
    {
        if( strcasecmp( pcDow, pcDowStr[ i ] ) == 0 )
        {
            return i;
        }
    }

    /* Cualquier otra cosa deja el slot LIBRE, igual que el AVR. Es su forma de
       desactivar uno: no hay un comando "borrar slot". */
    return CFG_FLOW_DOW_LIBRE;
}
//------------------------------------------------------------------------------
bool cfg_flowcontrol_set_slot( uint8_t ucSlot, const char *pcDow,
                               const char *pcPtime, const char *pcAccion )
{
    /* ⚠ `>=`, no `>`: los índices válidos son 0..13. Ver el header. */
    if( ucSlot >= CFG_FLOW_NRO_SLOTS )
    {
        xprintf( "ERROR: el slot va de 0 a %u\r\n",
                 ( unsigned ) ( CFG_FLOW_NRO_SLOTS - 1U ) );
        return false;
    }

    uint8_t ucDow = prvParseDow( pcDow );

    /*
     * ⛔ Un slot LIBRE conserva igual su hora y su acción, y NO se fuerzan.
     *
     * La primera versión los pisaba con `0` y `OPEN` razonando que un slot
     * apagado no los necesita. **Pero esos campos entran en el hash**: el
     * servidor manda `S00:--,0000,CLOSE` —día inválido, pero acción `CLOSE`— y
     * si acá se guardara `OPEN`, el `FH` nunca coincidiría con el suyo y pediría
     * reconfigurar `FLOWC` en todas las sesiones.
     *
     * Es exactamente el modo de falla del `PST` de ainputs (2026-09-11): un
     * campo que entra en el hash y que los dos lados no guardan igual **no
     * cierra nunca**.
     */
    if( ucDow >= CFG_FLOW_DOW_LIBRE )
    {
        xCfgFlow.xSlot[ ucSlot ].ucDow = CFG_FLOW_DOW_LIBRE;
        /* y se sigue de largo: la hora y la acción se aplican abajo */
    }

    /* Un campo en NULL quiere decir "dejá ese campo como está" — la regla del
       AVR para todo lo que llega del servidor. */
    uint16_t usPtime = ( pcPtime != NULL )
                       ? ( uint16_t ) atol( pcPtime )
                       : xCfgFlow.xSlot[ ucSlot ].usPtime;

    bool bAbrir = xCfgFlow.xSlot[ ucSlot ].bAbrir;

    if( pcAccion != NULL )
    {
        if( strcasecmp( pcAccion, "OPEN" ) == 0 )
        {
            bAbrir = true;
        }
        else if( strcasecmp( pcAccion, "CLOSE" ) == 0 )
        {
            bAbrir = false;
        }
        else
        {
            xprintf( "ERROR: la accion es OPEN o CLOSE\r\n" );
            return false;
        }
    }

    /* La hora se valida: un `2500` se guardaría y no se dispararía nunca, que es
       peor que rechazarlo — el técnico creería que quedó puesto. */
    if( ( ( usPtime / 100U ) > 23U ) || ( ( usPtime % 100U ) > 59U ) )
    {
        xprintf( "ERROR: la hora va en hhmm, de 0000 a 2359\r\n" );
        return false;
    }

    xCfgFlow.xSlot[ ucSlot ].ucDow   = ( ucDow >= CFG_FLOW_DOW_LIBRE )
                                       ? CFG_FLOW_DOW_LIBRE : ucDow;
    xCfgFlow.xSlot[ ucSlot ].usPtime = usPtime;
    xCfgFlow.xSlot[ ucSlot ].bAbrir  = bAbrir;

    return true;
}
//------------------------------------------------------------------------------
