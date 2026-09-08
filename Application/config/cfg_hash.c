/*
 * cfg_hash.c  -  ver cfg_hash.h
 *
 * ⚠ La tabla se copió literal de FWDLGX 2.0.12 (`SRC/ULIBS/utils.c`). Es parte
 * del contrato con el servidor: no se reordena, no se regenera, no se "mejora".
 */

#include <stdarg.h>
#include <stdio.h>

#include "cfg_hash.h"

static const uint8_t pucHashTable[ 256 ] = {
     93, 153, 124,  98, 233, 146, 184, 207, 215,  54, 208, 223, 254, 216, 162, 141,
     10, 148, 232, 115,   7, 202,  66,  31,   1,  33,  51, 145, 198, 181,  13,  95,
    242, 110, 107, 231, 140, 170,  44, 176, 166,   8,   9, 163, 150, 105, 113, 149,
    171, 152,  58, 133, 186,  27,  53, 111, 210,  96,  35, 240,  36, 168,  67, 213,
     12, 123, 101, 227, 182, 156, 190, 205, 218, 139,  68, 217,  79,  16, 196, 246,
    154, 116,  29, 131, 197, 117, 127,  76,  92,  14,  38,  99,   2, 219, 192, 102,
    252,  74,  91, 179,  71, 155,  84, 250, 200, 121, 159,  78,  69,  11,  63,   5,
    126, 157, 120, 136, 185,  88, 187, 114, 100, 214, 104, 226,  40, 191, 194,  50,
    221, 224, 128, 172, 135, 238,  25, 212,   0, 220, 251, 142, 211, 244, 229, 230,
     46,  89, 158, 253, 249,  81, 164, 234, 103,  59,  86, 134,  60, 193, 109,  77,
    180, 161, 119, 118, 195,  82,  49,  20, 255,  90,  26, 222,  39,  75, 243, 237,
     17,  72,  48, 239,  70,  19,   3,  65, 206,  32, 129,  57,  62,  21,  34, 112,
      4,  56, 189,  83, 228, 106,  61,   6,  24, 165, 201, 167, 132,  45, 241, 247,
     97,  30, 188, 177, 125,  42,  18, 178,  85, 137,  41, 173,  43, 174,  73, 130,
    203, 236, 209, 235,  15,  52,  47,  37,  22, 199, 245,  23, 144, 147, 138,  28,
    183,  87, 248, 160,  55,  64, 204,  94, 225, 143, 175, 169,  80, 151, 108, 122
};

/*==============================================================================
 * API
 *============================================================================*/

uint8_t cfg_hash_char( uint8_t ucSeed, char cCh )
{
    /*
     * ⚠ El índice sale de `seed ^ (int)ch`, y en el AVR `char` es UNSIGNED por
     * defecto mientras que en ARM es SIGNED. Con un carácter de más de 0x7F eso
     * daría un índice negativo —comportamiento indefinido— y un hash distinto al
     * del equipo en producción. El cast a `uint8_t` reproduce el AVR.
     *
     * Con nombres ASCII nunca se llega ahí, pero el día que alguien configure un
     * canal con un acento el bug sería mudo y carísimo de encontrar.
     */
    uint8_t ucEntry = ( uint8_t ) ( ucSeed ^ ( uint8_t ) cCh );

    return pucHashTable[ ucEntry ];
}
//------------------------------------------------------------------------------
uint8_t cfg_hash_string( uint8_t ucSeed, const char *pcStr )
{
    uint8_t ucHash = ucSeed;

    if( pcStr == NULL )
    {
        return ucHash;
    }

    while( *pcStr != '\0' )
    {
        ucHash = cfg_hash_char( ucHash, *pcStr++ );
    }

    return ucHash;
}
//------------------------------------------------------------------------------
bool cfg_hash_append( char *pcBuf, uint16_t usBufSize, uint16_t *pusIdx,
                      const char *pcFmt, ... )
{
    va_list xArgs;
    int     iN;

    if( ( pcBuf == NULL ) || ( pusIdx == NULL ) || ( *pusIdx >= usBufSize ) )
    {
        return false;
    }

    va_start( xArgs, pcFmt );
    iN = vsnprintf( &pcBuf[ *pusIdx ], ( size_t ) ( usBufSize - *pusIdx ), pcFmt, xArgs );
    va_end( xArgs );

    if( ( iN < 0 ) || ( ( uint16_t ) iN >= ( uint16_t ) ( usBufSize - *pusIdx ) ) )
    {
        /* Truncado: NO se avanza el índice. Es lo que hace el AVR. */
        return false;
    }

    *pusIdx += ( uint16_t ) iN;
    return true;
}
//------------------------------------------------------------------------------
uint8_t cfg_checksum( const uint8_t *pucData, uint16_t usSize )
{
    uint8_t  ucCks = 0;
    uint16_t i;

    for( i = 0U; i < usSize; i++ )
    {
        ucCks = ( uint8_t ) ( ( ucCks + pucData[ i ] ) % 256U );
    }

    return ucCks;
}
//------------------------------------------------------------------------------
