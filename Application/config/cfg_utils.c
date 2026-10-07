/*
 * cfg_utils.c  -  ver cfg_utils.h
 */

#include <string.h>
#include <strings.h>

#include "cfg_utils.h"

size_t cfg_strlcpy( char *pcDst, const char *pcSrc, size_t xSize )
{
    size_t xLargo = strlen( pcSrc );

    if( xSize != 0U )
    {
        size_t xCopiar = ( xLargo >= xSize ) ? ( xSize - 1U ) : xLargo;

        memcpy( pcDst, pcSrc, xCopiar );
        pcDst[ xCopiar ] = '\0';
    }

    return xLargo;
}
//------------------------------------------------------------------------------
bool cfg_str2bool( const char *pcStr, bool *pbOut )
{
    if( ( pcStr == NULL ) || ( pbOut == NULL ) )
    {
        return false;
    }

    if( ( strcasecmp( pcStr, "true" ) == 0 ) ||
        ( strcasecmp( pcStr, "si"   ) == 0 ) ||
        ( strcasecmp( pcStr, "on"   ) == 0 ) ||
        ( strcmp    ( pcStr, "1"    ) == 0 ) )
    {
        *pbOut = true;
        return true;
    }

    if( ( strcasecmp( pcStr, "false" ) == 0 ) ||
        ( strcasecmp( pcStr, "no"    ) == 0 ) ||
        ( strcasecmp( pcStr, "off"   ) == 0 ) ||
        ( strcmp    ( pcStr, "0"     ) == 0 ) )
    {
        *pbOut = false;
        return true;
    }

    return false;
}
//------------------------------------------------------------------------------
