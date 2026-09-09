/*
 * fs_datos.c  -  ver fs_datos.h
 */

#include <stddef.h>
#include <string.h>

#include "fs_datos.h"
#include "cfg_nvm.h"
#include "cfg_hash.h"
#include "drv_eeprom.h"
#include "drv_rtc79410.h"
#include "frtos-io.h"

/*
 * Tag de registro escrito. Mismo valor que el AVR (`FF_WRTAG`). Distingue un
 * registro de memoria virgen —que lee 0xFF— y es lo que permitiría reconstruir
 * la FAT escaneando la EEPROM el día que haga falta.
 */
#define FS_TAG                  0xC5U

/* Dónde vive la FAT dentro de la SRAM del RTC: justo después de la firma. */
#define FS_FAT_SRAM_ADDR        DRV_RTC_SRAM_USUARIO

/*
 * ⚠ `packed` por el mismo motivo que el registro: esto vive en memoria
 * persistente (la SRAM del RTC) y su layout no puede depender del compilador.
 */
typedef struct __attribute__(( packed )) {
    uint16_t usHead;
    uint16_t usTail;
    uint16_t usCount;
    uint16_t usLength;
    uint8_t  ucChecksum;
} fs_fat_t;

/*
 * ⚠ SOBRE QUÉ BYTES SE CALCULA EL CHECKSUM, Y POR QUÉ NO ES `sizeof - 1`.
 *
 * Tiene que ser **todo lo que hay ANTES del campo checksum**, y eso es
 * exactamente `offsetof()`. Escribirlo como `sizeof(fs_fat_t) - 1` parece lo
 * mismo y **no lo es** en cuanto el compilador mete relleno al final: la struct
 * sin `packed` medía 10 bytes con el checksum en el 8, así que `sizeof - 1` daba
 * 9 e **incluía al propio checksum en el rango**.
 *
 * El efecto era exacto y silencioso: al grabar entraba en la cuenta su valor
 * VIEJO y al releer el NUEVO, así que **nunca coincidían**. La FAT se declaraba
 * inválida en cada arranque —culpando a la pila del RTC, que estaba perfecta— y
 * el equipo formateaba, perdiendo todos los registros guardados.
 *
 * Encontrado en banco el 2026-09-09 por Pablo, con el argumento correcto: *"el
 * RTC no se perdió así que no parece haber habido un problema de la batería"*.
 * Un mensaje de error que acusa al componente equivocado cuesta más que no
 * tenerlo.
 */
#define FS_FAT_BYTES_CKS    ( offsetof( fs_fat_t, ucChecksum ) )

/*
 * El registro tal cual queda en la EEPROM. El tag primero para poder reconocerlo
 * sin leer todo, el checksum al final sobre todo lo anterior.
 *
 * ⚠ **`packed` no es para ahorrar espacio: es para que el formato en memoria
 * persistente NO dependa del compilador.**
 *
 * Sin él, entre los campos quedan bytes de relleno que ARM inserta para alinear
 * los `float` — y esos huecos son *invisibles en el código*. El registro pasaba
 * de 64 bytes por ese motivo (lo cazó el `_Static_assert` de abajo), pero el
 * problema de fondo es peor que el tamaño: **el layout dependería de opciones de
 * compilación**. Un cambio de flags, de versión del compilador o el simple
 * agregado de un campo en `dataRcd_t` movería los offsets, y los registros
 * grabados por el firmware anterior se leerían corridos —con checksum válido,
 * porque el checksum se calcula sobre los mismos bytes—. Datos plausibles y
 * falsos, que es el peor desenlace.
 *
 * En el AVR el asunto no existía: es de 8 bits y alinea a byte, así que la
 * struct cruda ya era compacta.
 */
typedef struct __attribute__(( packed )) {
    uint8_t   ucTag;
    dataRcd_t xDr;
    uint8_t   ucChecksum;
} fs_registro_t;

/* Mismo criterio que la FAT: el checksum cubre todo lo anterior a él, y eso es
   `offsetof`, no `sizeof - 1`. Acá coinciden porque la struct es `packed`, pero
   escribirlo así lo deja correcto si algún día deja de serlo. */
#define FS_RCD_BYTES_CKS    ( offsetof( fs_registro_t, ucChecksum ) )

_Static_assert( sizeof( fs_registro_t ) <= FS_DATOS_RCD_SIZE,
                "el registro no entra en FS_DATOS_RCD_SIZE" );
_Static_assert( ( FS_FAT_SRAM_ADDR + sizeof( fs_fat_t ) ) <= DRV_RTC_SRAM_SIZE,
                "la FAT no entra en la SRAM del RTC" );
_Static_assert( ( CFG_NVM_FS_ADDR + ( ( uint32_t ) FS_DATOS_MAX_RCDS * FS_DATOS_RCD_SIZE ) )
                <= DRV_EEPROM_SIZE, "el filesystem se pasa del final de la EEPROM" );

static fs_fat_t xFat;
static uint32_t ulPisados;

//------------------------------------------------------------------------------
static uint32_t prvDireccion( uint16_t usPos )
{
    return CFG_NVM_FS_ADDR + ( ( uint32_t ) usPos * FS_DATOS_RCD_SIZE );
}
//------------------------------------------------------------------------------
static bool prvFatGrabar( void )
{
    xFat.ucChecksum = cfg_checksum( ( const uint8_t * ) &xFat, FS_FAT_BYTES_CKS );

    return ( drv_rtc_sram_escribir( FS_FAT_SRAM_ADDR, ( const char * ) &xFat,
                                    ( uint8_t ) sizeof( xFat ) )
             == ( int16_t ) sizeof( xFat ) );
}
//------------------------------------------------------------------------------
static void prvFatVaciar( void )
{
    memset( &xFat, 0, sizeof( xFat ) );
    xFat.usLength = FS_DATOS_MAX_RCDS;
}
//------------------------------------------------------------------------------
bool fs_datos_init( void )
{
    ulPisados = 0U;

    if( drv_rtc_sram_leer( FS_FAT_SRAM_ADDR, ( char * ) &xFat,
                           ( uint8_t ) sizeof( xFat ) ) != ( int16_t ) sizeof( xFat ) )
    {
        xprintf( "FS:: el RTC no contesta: no hay FAT -> formateo\r\n" );
        prvFatVaciar();
        ( void ) prvFatGrabar();
        return false;
    }

    uint8_t ucCalculado = cfg_checksum( ( const uint8_t * ) &xFat, FS_FAT_BYTES_CKS );

    /*
     * Se valida el checksum Y la coherencia de los punteros.
     *
     * Lo segundo no es redundante: un checksum correcto sobre una FAT de otra
     * versión del firmware —con otro `usLength`, por ejemplo— pasaría el primer
     * chequeo y después haría escribir fuera de rango. Operar con punteros
     * basura es peor que perder los datos: pisaría la configuración, que vive
     * justo antes en la misma EEPROM.
     */
    bool bCoherente = ( ucCalculado == xFat.ucChecksum ) &&
                      ( xFat.usLength == FS_DATOS_MAX_RCDS ) &&
                      ( xFat.usHead   <  FS_DATOS_MAX_RCDS ) &&
                      ( xFat.usTail   <  FS_DATOS_MAX_RCDS ) &&
                      ( xFat.usCount  <= FS_DATOS_MAX_RCDS );

    if( !bCoherente )
    {
        /*
         * La causa más probable no es una corrupción sino que **se perdió el
         * respaldo por pila del MCP79410**, que en este equipo falla de forma
         * intermitente. Los datos siguen en la EEPROM pero sin la FAT no hay
         * forma de saber cuáles son válidos ni en qué orden.
         */
        xprintf( "FS:: FAT invalida (se perdio el respaldo del RTC?) -> formateo\r\n" );
        prvFatVaciar();
        ( void ) prvFatGrabar();
        return false;
    }

    xprintf( "FS:: %u registros guardados de %u\r\n",
             ( unsigned ) xFat.usCount, ( unsigned ) xFat.usLength );

    return true;
}
//------------------------------------------------------------------------------
bool fs_datos_write( const dataRcd_t *pxDr )
{
    fs_registro_t xRcd;

    if( pxDr == NULL )
    {
        return false;
    }

    memset( &xRcd, 0, sizeof( xRcd ) );
    xRcd.ucTag = FS_TAG;
    xRcd.xDr   = *pxDr;
    xRcd.ucChecksum = cfg_checksum( ( const uint8_t * ) &xRcd, FS_RCD_BYTES_CKS );

    if( drv_eeprom_write( prvDireccion( xFat.usHead ), ( const char * ) &xRcd,
                          ( uint32_t ) sizeof( xRcd ) ) != ( int32_t ) sizeof( xRcd ) )
    {
        xprintf( "FS:: ERROR al grabar el registro %u\r\n", ( unsigned ) xFat.usHead );
        return false;
    }

    /* Avanza head en círculo. */
    xFat.usHead = ( uint16_t ) ( ( xFat.usHead + 1U ) % xFat.usLength );

    if( xFat.usCount < xFat.usLength )
    {
        xFat.usCount++;
    }
    else
    {
        /*
         * Estaba lleno: el que acabamos de escribir pisó al más viejo, así que
         * tail tiene que correrse. **El AVR acá rechazaba el registro nuevo**;
         * es el cambio de política que pidió Pablo — en un datalogger el dato
         * reciente vale más que el de hace días.
         *
         * Se avisa porque significa PÉRDIDA DE DATOS, y eso es información de
         * campo: dice que el equipo lleva demasiado tiempo sin poder transmitir.
         */
        xFat.usTail = ( uint16_t ) ( ( xFat.usTail + 1U ) % xFat.usLength );
        ulPisados++;

        if( ulPisados == 1U )
        {
            xprintf( "FS:: [!] memoria LLENA: se empiezan a pisar los registros mas viejos\r\n" );
        }
    }

    return prvFatGrabar();
}
//------------------------------------------------------------------------------
bool fs_datos_peek( dataRcd_t *pxDr, uint16_t usOffset )
{
    fs_registro_t xRcd;

    if( ( pxDr == NULL ) || ( usOffset >= xFat.usCount ) )
    {
        return false;
    }

    uint16_t usPos = ( uint16_t ) ( ( xFat.usTail + usOffset ) % xFat.usLength );

    if( drv_eeprom_read( prvDireccion( usPos ), ( char * ) &xRcd,
                         ( uint32_t ) sizeof( xRcd ) ) != ( int32_t ) sizeof( xRcd ) )
    {
        xprintf( "FS:: ERROR al leer el registro %u\r\n", ( unsigned ) usPos );
        return false;
    }

    if( xRcd.ucTag != FS_TAG )
    {
        xprintf( "FS:: registro %u sin tag (0x%02X): la FAT no coincide con la memoria\r\n",
                 ( unsigned ) usPos, ( unsigned ) xRcd.ucTag );
        return false;
    }

    uint8_t ucCalculado = cfg_checksum( ( const uint8_t * ) &xRcd, FS_RCD_BYTES_CKS );

    if( ucCalculado != xRcd.ucChecksum )
    {
        xprintf( "FS:: registro %u con checksum malo\r\n", ( unsigned ) usPos );
        return false;
    }

    *pxDr = xRcd.xDr;
    return true;
}
//------------------------------------------------------------------------------
uint16_t fs_datos_pop( uint16_t usCuantos )
{
    if( usCuantos > xFat.usCount )
    {
        usCuantos = xFat.usCount;
    }

    if( usCuantos == 0U )
    {
        return 0U;
    }

    xFat.usTail   = ( uint16_t ) ( ( xFat.usTail + usCuantos ) % xFat.usLength );
    xFat.usCount  = ( uint16_t ) ( xFat.usCount - usCuantos );

    /*
     * No se borra nada de la EEPROM: alcanza con mover el puntero. Borrar
     * costaría una escritura por registro —desgaste y tiempo— para no ganar
     * nada: el espacio se reusa igual y el tag se pisa al escribir encima.
     */
    ( void ) prvFatGrabar();

    return usCuantos;
}
//------------------------------------------------------------------------------
void fs_datos_format( void )
{
    prvFatVaciar();
    ulPisados = 0U;
    ( void ) prvFatGrabar();

    /* Tampoco acá se borra la EEPROM, por lo mismo que en pop(): sin FAT que los
       apunte, los registros viejos son inalcanzables. */
    xprintf( "FS:: formateado, %u registros disponibles\r\n", ( unsigned ) xFat.usLength );
}
//------------------------------------------------------------------------------
void fs_datos_stats( fs_datos_stats_t *pxStats )
{
    if( pxStats == NULL )
    {
        return;
    }

    pxStats->usHead    = xFat.usHead;
    pxStats->usTail    = xFat.usTail;
    pxStats->usCount   = xFat.usCount;
    pxStats->usLength  = xFat.usLength;
    pxStats->ulPisados = ulPisados;
}
//------------------------------------------------------------------------------
