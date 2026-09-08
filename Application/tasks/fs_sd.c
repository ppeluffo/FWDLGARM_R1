/*
 * fs_sd.c  -  ver fs_sd.h
 */

#include <stdio.h>
#include <string.h>

#include "fs_sd.h"
#include "fs_datos.h"
#include "wan_frame.h"
#include "drv_sd.h"
#include "drv_rtc79410.h"
#include "frtos-io.h"
#include "fatfs.h"

/*
 * El contador de lotes vive junto a la FAT en la SRAM del RTC. `fs_datos` ocupa
 * los primeros bytes del área de usuario; éste va justo después.
 */
#define FS_SD_CONTADOR_SRAM_ADDR    ( DRV_RTC_SRAM_USUARIO + 16U )

_Static_assert( ( FS_SD_CONTADOR_SRAM_ADDR + 4U ) <= DRV_RTC_SRAM_SIZE,
                "el contador de lotes no entra en la SRAM del RTC" );

static FATFS xFs;
static bool  bMontado;

//------------------------------------------------------------------------------
/*
 * ⚠ `get_fattime()` NO está acá: vive en el bloque USER CODE de
 * `FATFS/App/fatfs.c`, que es donde CubeMX genera su declaración.
 *
 * Estuvo un rato en este archivo y el linker lo cazó enseguida —`multiple
 * definition of get_fattime`—, porque CubeMX ya emite una versión que devuelve
 * 0. Ponerla en el bloque USER CODE de ese archivo es lo correcto: sobrevive a
 * las regeneraciones y queda donde el que busca la función espera encontrarla.
 */
//------------------------------------------------------------------------------
static uint32_t prvContadorLeer( void )
{
    uint32_t ulVal = 0U;

    if( drv_rtc_sram_leer( FS_SD_CONTADOR_SRAM_ADDR, ( char * ) &ulVal, 4U ) != 4 )
    {
        return 0U;
    }

    /* Sin respaldo la SRAM lee 0xFFFFFFFF. Empezar de cero es correcto: los
       nombres que ya existan en la tarjeta se saltean al buscar uno libre. */
    return ( ulVal == 0xFFFFFFFFUL ) ? 0U : ulVal;
}
//------------------------------------------------------------------------------
static void prvContadorGrabar( uint32_t ulVal )
{
    ( void ) drv_rtc_sram_escribir( FS_SD_CONTADOR_SRAM_ADDR, ( const char * ) &ulVal, 4U );
}
//------------------------------------------------------------------------------
static void prvNombreDeLote( uint32_t ulNro, char *pcNombre, uint16_t usSize )
{
    snprintf( pcNombre, usSize, "%s%04lu%s", FS_SD_PREFIJO,
              ( unsigned long ) ( ulNro % 10000UL ), FS_SD_EXTENSION );
}
//------------------------------------------------------------------------------
/*
 * Enciende, arranca y monta. Es la parte cara, y por eso el diseño de ventana
 * existe: esto se paga una vez por volcado, no una vez por muestra.
 */
static bool prvMontar( void )
{
    if( bMontado )
    {
        return true;
    }

    drv_sd_power( true );

    /* El orden es obligatorio: `drv_sd_presente()` devuelve false con el riel
       apagado, porque ahí el pin de detección está en alta impedancia. */
    if( !drv_sd_presente() )
    {
        drv_sd_power( false );
        return false;
    }

    if( !drv_sd_arrancar() )
    {
        xprintf( "SD:: la tarjeta no inicializa\r\n" );
        drv_sd_power( false );
        return false;
    }

    if( f_mount( &xFs, "", 1 ) != FR_OK )
    {
        xprintf( "SD:: no se pudo montar (formateada en FAT?)\r\n" );
        drv_sd_power( false );
        return false;
    }

    bMontado = true;
    return true;
}
//------------------------------------------------------------------------------
static void prvDesmontar( void )
{
    if( bMontado )
    {
        ( void ) f_mount( NULL, "", 0 );
        bMontado = false;
    }

    /*
     * Y se apaga. Es lo que hace que la tarjeta no cueste nada entre volcados:
     * una microSD consume 0,2-1 mA sólo por estar alimentada, contra los ~6 µA
     * de la placa dormida.
     */
    drv_sd_power( false );
}
//------------------------------------------------------------------------------
bool fs_sd_volcar_ventana( void )
{
    fs_datos_stats_t xSt;
    dataRcd_t        xDr;
    char             pcNombre[ FS_SD_NOMBRE_LARGO ];
    static char      pcFrame[ WAN_FRAME_BUFFER_SIZE ];
    FIL              xFile;
    uint16_t         usEscritos = 0U;
    bool             bOk        = true;

    fs_datos_stats( &xSt );

    if( xSt.usCount == 0U )
    {
        return true;    /* nada que volcar no es un error */
    }

    if( !prvMontar() )
    {
        return false;
    }

    /* Un nombre libre: si el contador se perdió con el respaldo, se saltean los
       que ya existan en vez de pisarlos. */
    uint32_t ulNro = prvContadorLeer();
    uint32_t ulIntentos = 0U;

    do
    {
        ulNro++;
        prvNombreDeLote( ulNro, pcNombre, sizeof( pcNombre ) );
        ulIntentos++;
    }
    while( ( f_stat( pcNombre, NULL ) == FR_OK ) && ( ulIntentos < 10000UL ) );

    if( f_open( &xFile, pcNombre, FA_CREATE_NEW | FA_WRITE ) != FR_OK )
    {
        xprintf( "SD:: no se pudo crear %s\r\n", pcNombre );
        prvDesmontar();
        return false;
    }

    /* Un frame por línea, en el mismo formato en que se van a transmitir. */
    for( uint16_t i = 0U; i < xSt.usCount; i++ )
    {
        if( !fs_datos_peek( &xDr, i ) )
        {
            continue;   /* un registro corrupto no aborta el lote entero */
        }

        uint16_t usLargo = wan_frame_data( pcFrame, sizeof( pcFrame ) - 2U, &xDr, true );

        if( usLargo == 0U )
        {
            continue;
        }

        pcFrame[ usLargo++ ] = '\r';
        pcFrame[ usLargo++ ] = '\n';

        UINT uxEscritos;

        if( ( f_write( &xFile, pcFrame, usLargo, &uxEscritos ) != FR_OK ) ||
            ( uxEscritos != usLargo ) )
        {
            xprintf( "SD:: ERROR escribiendo %s\r\n", pcNombre );
            bOk = false;
            break;
        }

        usEscritos++;
    }

    /*
     * ⚠ El cierre es parte del criterio de éxito, no un trámite: FatFs mantiene
     * la FAT y el tamaño del archivo en RAM hasta que se cierra. Un `f_close()`
     * que falla significa que el archivo quedó incompleto en la tarjeta.
     */
    if( f_close( &xFile ) != FR_OK )
    {
        xprintf( "SD:: ERROR al cerrar %s\r\n", pcNombre );
        bOk = false;
    }

    if( bOk )
    {
        prvContadorGrabar( ulNro );
    }

    prvDesmontar();

    if( !bOk )
    {
        /* La ventana NO se vacía: los datos siguen ahí y se reintenta después. */
        xprintf( "SD:: volcado fallido, la ventana queda intacta\r\n" );
        return false;
    }

    /*
     * Recién ahora se vacía la ventana. Si se hiciera antes de confirmar el
     * cierre, un corte en el medio se llevaría los datos de los dos lados.
     */
    ( void ) fs_datos_pop( xSt.usCount );

    xprintf( "SD:: %u registros volcados a %s\r\n", ( unsigned ) usEscritos, pcNombre );
    return true;
}
//------------------------------------------------------------------------------
bool fs_sd_lote_mas_viejo( char *pcNombre, uint16_t usSize )
{
    DIR     xDir;
    FILINFO xInfo;
    bool    bHay = false;

    if( ( pcNombre == NULL ) || ( usSize < FS_SD_NOMBRE_LARGO ) )
    {
        return false;
    }

    if( !prvMontar() )
    {
        return false;
    }

    if( f_opendir( &xDir, "" ) == FR_OK )
    {
        char pcMenor[ FS_SD_NOMBRE_LARGO ] = { 0 };

        while( ( f_readdir( &xDir, &xInfo ) == FR_OK ) && ( xInfo.fname[ 0 ] != '\0' ) )
        {
            if( ( xInfo.fattrib & AM_DIR ) != 0U )
            {
                continue;
            }

            if( strncmp( xInfo.fname, FS_SD_PREFIJO, strlen( FS_SD_PREFIJO ) ) != 0 )
            {
                continue;
            }

            /* El más viejo es el de número más chico, y como el nombre tiene el
               número con ceros a la izquierda, alcanza con comparar strings. */
            if( ( !bHay ) || ( strcmp( xInfo.fname, pcMenor ) < 0 ) )
            {
                strncpy( pcMenor, xInfo.fname, sizeof( pcMenor ) - 1U );
                bHay = true;
            }
        }

        ( void ) f_closedir( &xDir );

        if( bHay )
        {
            strncpy( pcNombre, pcMenor, usSize - 1U );
            pcNombre[ usSize - 1U ] = '\0';
        }
    }

    prvDesmontar();
    return bHay;
}
//------------------------------------------------------------------------------
bool fs_sd_borrar_lote( const char *pcNombre )
{
    if( pcNombre == NULL )
    {
        return false;
    }

    if( !prvMontar() )
    {
        return false;
    }

    bool bOk = ( f_unlink( pcNombre ) == FR_OK );

    prvDesmontar();

    if( !bOk )
    {
        xprintf( "SD:: no se pudo borrar %s\r\n", pcNombre );
    }

    return bOk;
}
//------------------------------------------------------------------------------
void fs_sd_stats( fs_sd_stats_t *pxStats )
{
    DIR     xDir;
    FILINFO xInfo;

    if( pxStats == NULL )
    {
        return;
    }

    memset( pxStats, 0, sizeof( fs_sd_stats_t ) );
    pxStats->ulProximoLote = prvContadorLeer() + 1U;

    if( !prvMontar() )
    {
        return;
    }

    pxStats->bPresente = true;

    if( f_opendir( &xDir, "" ) == FR_OK )
    {
        while( ( f_readdir( &xDir, &xInfo ) == FR_OK ) && ( xInfo.fname[ 0 ] != '\0' ) )
        {
            if( ( ( xInfo.fattrib & AM_DIR ) == 0U ) &&
                ( strncmp( xInfo.fname, FS_SD_PREFIJO, strlen( FS_SD_PREFIJO ) ) == 0 ) )
            {
                pxStats->usLotes++;
            }
        }
        ( void ) f_closedir( &xDir );
    }

    DWORD    ulClustersLibres;
    FATFS   *pxFsPtr;

    if( f_getfree( "", &ulClustersLibres, &pxFsPtr ) == FR_OK )
    {
        /* clusters libres x sectores por cluster x 512 / 1024 */
        pxStats->ulLibreKB = ( uint32_t ) ( ulClustersLibres * pxFsPtr->csize ) / 2UL;
    }

    prvDesmontar();
}
//------------------------------------------------------------------------------
void fs_sd_listar( void )
{
    DIR     xDir;
    FILINFO xInfo;

    if( !prvMontar() )
    {
        xprintf( "SD:: no hay tarjeta\r\n" );
        return;
    }

    if( f_opendir( &xDir, "" ) == FR_OK )
    {
        while( ( f_readdir( &xDir, &xInfo ) == FR_OK ) && ( xInfo.fname[ 0 ] != '\0' ) )
        {
            if( ( xInfo.fattrib & AM_DIR ) != 0U )
            {
                continue;
            }

            xprintf( "  %-12s %8lu bytes  %04u-%02u-%02u %02u:%02u\r\n",
                     xInfo.fname, ( unsigned long ) xInfo.fsize,
                     ( unsigned ) ( 1980U + ( ( xInfo.fdate >> 9 ) & 0x7FU ) ),
                     ( unsigned ) ( ( xInfo.fdate >> 5 ) & 0x0FU ),
                     ( unsigned ) (   xInfo.fdate        & 0x1FU ),
                     ( unsigned ) ( ( xInfo.ftime >> 11 ) & 0x1FU ),
                     ( unsigned ) ( ( xInfo.ftime >>  5 ) & 0x3FU ) );
        }
        ( void ) f_closedir( &xDir );
    }

    prvDesmontar();
}
//------------------------------------------------------------------------------
void fs_sd_ver( const char *pcNombre, uint16_t usLineas )
{
    FIL  xFile;
    char pcLinea[ WAN_FRAME_BUFFER_SIZE ];

    if( pcNombre == NULL )
    {
        return;
    }

    if( !prvMontar() )
    {
        xprintf( "SD:: no hay tarjeta\r\n" );
        return;
    }

    if( f_open( &xFile, pcNombre, FA_READ ) != FR_OK )
    {
        xprintf( "SD:: no se pudo abrir %s\r\n", pcNombre );
        prvDesmontar();
        return;
    }

    for( uint16_t i = 0U; i < usLineas; i++ )
    {
        if( f_gets( pcLinea, ( int ) sizeof( pcLinea ), &xFile ) == NULL )
        {
            break;
        }

        /* Sin xprintf: la línea es un frame y puede pasar los 160 bytes de su
           buffer. Ver wan_frame. */
        ( void ) frtos_write( fdTERM, pcLinea, ( uint16_t ) strlen( pcLinea ) );
    }

    ( void ) f_close( &xFile );
    prvDesmontar();
}
//------------------------------------------------------------------------------
