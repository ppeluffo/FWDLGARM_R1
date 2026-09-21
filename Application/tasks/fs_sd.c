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

/*
 * Buffer de un sector, compartido por el volcado y por el formateo.
 *
 * Es uno solo y estático a propósito: **nunca se usan a la vez** —hay una sola
 * tarea dueña de la SD, que es de dónde sale la serialización en este diseño (ver
 * `_FS_REENTRANT = 0`)— y 512 bytes no entran en el stack de ninguna tarea de
 * este firmware.
 */
static char pcBufSector[ _MAX_SS ];

/*------------------------------------------------------------------------------
 * Indicador de actividad. Ver fs_sd.h.
 *
 * Se imprime **uno cada FS_SD_PROGRESO_CADA sectores** y no en cada uno: a 9600
 * cada carácter son ~1 ms más el mutex y el semáforo del TX, así que un carácter
 * por sector le agregaría segundos a la operación que se quiere acompañar. Con 32
 * el molinete gira visiblemente y el costo se pierde en el ruido.
 *----------------------------------------------------------------------------*/
#define FS_SD_PROGRESO_CADA     32U

static const char pcGiro[] = { '|', '/', '-', '\\' };

static bool     bProgresoOn;
static uint32_t ulProgresoSectores;
static uint8_t  ucProgresoIdx;
//------------------------------------------------------------------------------
void fs_sd_progreso( void )
{
    if( !bProgresoOn )
    {
        return;
    }

    if( ( ++ulProgresoSectores % FS_SD_PROGRESO_CADA ) != 0U )
    {
        return;
    }

    /* El backspace deja el cursor donde estaba, así que el molinete gira en el
       lugar en vez de llenar la pantalla de caracteres. */
    xputChar( pcGiro[ ucProgresoIdx++ & 0x03U ] );
    xputChar( '\b' );
}
//------------------------------------------------------------------------------
static void prvProgresoIniciar( void )
{
    ulProgresoSectores = 0U;
    ucProgresoIdx      = 0U;
    bProgresoOn        = true;
}
//------------------------------------------------------------------------------
static void prvProgresoTerminar( void )
{
    bProgresoOn = false;

    /* Borra el último carácter del molinete: sin esto queda una barra suelta
       pegada al mensaje que viene después. */
    xputChar( ' ' );
    xputChar( '\b' );
}

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
 * Enciende y arranca la tarjeta, SIN montar.
 *
 * Está separado de `prvMontar()` porque el formateo lo necesita así: `f_mkfs()`
 * trabaja sobre una tarjeta inicializada pero **sin filesystem que montar** —que
 * es justamente el caso que viene a resolver—.
 */
static bool prvEncender( void )
{
    drv_sd_power( true );

    /* El orden es obligatorio: `drv_sd_presente()` devuelve false con el riel
       apagado, porque ahí el pin de detección está en alta impedancia. */
    if( !drv_sd_presente() )
    {
        xprintf( "SD:: no hay tarjeta (SD_DET en alto)\r\n" );
        drv_sd_power( false );
        return false;
    }

    if( !drv_sd_arrancar() )
    {
        xprintf( "SD:: la tarjeta NO inicializa (CMD0/ACMD41) - probar 'sd' para el detalle\r\n" );
        drv_sd_power( false );
        return false;
    }

    return true;
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

    if( !prvEncender() )
    {
        return false;
    }

    /*
     * ⚠ Cada fallo imprime SU causa, y los llamadores no agregan nada.
     *
     * La primera prueba de banco (2026-09-09) mostró dos mensajes pegados —"no
     * se pudo montar" y "no hay tarjeta"— que se contradecían entre sí: la
     * tarjeta estaba presente y había inicializado, y lo único que falló fue el
     * montaje. Un diagnóstico que acusa a la causa equivocada cuesta más que no
     * tenerlo.
     *
     * El `FRESULT` se imprime con su número porque es lo que separa las
     * hipótesis: 1 = DISK_ERR (la lectura de sectores falla, o sea diskio o
     * driver), 3 = NOT_READY (disk_initialize dijo que no), 13 = NO_FILESYSTEM
     * (los sectores se leen bien pero no hay una FAT donde debería).
     */
    FRESULT xRes = f_mount( &xFs, "", 1 );

    if( xRes != FR_OK )
    {
        xprintf( "SD:: f_mount fallo, FRESULT=%d%s\r\n", ( int ) xRes,
                 ( xRes == FR_NO_FILESYSTEM ) ? " (NO_FILESYSTEM: la tarjeta no esta en FAT - 'fs sd format borrar')" :
                 ( xRes == FR_DISK_ERR )      ? " (DISK_ERR: la lectura de sectores falla)" :
                 ( xRes == FR_NOT_READY )     ? " (NOT_READY: disk_initialize rechazo)" : "" );
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

    prvProgresoIniciar();

    /*
     * ⭐ Se guarda **sólo la parte de datos**, desde `DATE=` — sin el prefijo
     * `ID=..&HW=..&TYPE=..&VER=..&CLASS=..`, que se construye al transmitir.
     *
     * El motivo de fondo es que el prefijo **no pertenece al dato**: el `ID` es
     * el IMEI del módulo, así que un lote guardado con el prefijo y transmitido
     * después de cambiar el módulo saldría con el IMEI viejo. Ver `wan_frame.h`.
     */
    for( uint16_t i = 0U; i < xSt.usCount; i++ )
    {
        if( !fs_datos_peek( &xDr, i ) )
        {
            continue;   /* un registro corrupto no aborta el lote entero */
        }

        uint16_t usLargo = wan_frame_datos( pcBufSector, sizeof( pcBufSector ) - 2U, &xDr );

        if( usLargo == 0U )
        {
            continue;
        }

        pcBufSector[ usLargo++ ] = '\r';
        pcBufSector[ usLargo++ ] = '\n';

        UINT uxEscritos;

        if( ( f_write( &xFile, pcBufSector, usLargo, &uxEscritos ) != FR_OK ) ||
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

    prvProgresoTerminar();

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
bool fs_sd_format( void )
{
    if( !prvEncender() )
    {
        return false;
    }

    /*
     * ⚠ Hay que REGISTRAR el volumen aunque no se pueda montar.
     *
     * `f_mkfs()` necesita un objeto `FATFS` asociado al drive, y con una tarjeta
     * en exFAT o virgen el montaje falla — que es justamente el caso que se viene
     * a arreglar. Por eso va `f_mount( …, 0 )`: la opción 0 **registra sin
     * montar**, no toca la tarjeta, y no puede fallar por no encontrar un
     * filesystem.
     */
    ( void ) f_mount( &xFs, "", 0 );
    bMontado = false;

    xprintf( "SD:: formateando... (escribe las dos FAT sector por sector, puede tardar)  " );
    prvProgresoIniciar();

    /*
     * `FM_FAT | FM_FAT32` y NO `FM_ANY`: `FM_ANY` incluye `FM_EXFAT`, y esta
     * configuración de FatFs no lee exFAT (`_FS_EXFAT = 0`) — dejaría la tarjeta
     * formateada en algo que el propio equipo no puede montar después. Que
     * elija entre FAT16 y FAT32 según el tamaño está bien; salirse de ahí, no.
     *
     * Sin `FM_SFD`, o sea **con tabla de particiones**, que es como vienen las SD
     * de fábrica y lo que espera cualquier lector de tarjetas.
     *
     * `au = 0` deja que FatFs elija el tamaño de cluster según la capacidad.
     */
    FRESULT xRes = f_mkfs( "", FM_FAT | FM_FAT32, 0,
                           pcBufSector, ( UINT ) sizeof( pcBufSector ) );

    prvProgresoTerminar();
    xprintf( "\r\n" );

    if( xRes != FR_OK )
    {
        xprintf( "SD:: f_mkfs fallo, FRESULT=%d%s\r\n", ( int ) xRes,
                 ( xRes == FR_MKFS_ABORTED ) ? " (MKFS_ABORTED: la tarjeta es muy chica o no se puede leer)" :
                 ( xRes == FR_DISK_ERR )     ? " (DISK_ERR: fallo la escritura de sectores)" :
                 ( xRes == FR_NOT_READY )    ? " (NOT_READY)" : "" );
        prvDesmontar();
        return false;
    }

    /*
     * Se monta para verificar que quedó usable. Formatear y NO comprobarlo
     * dejaría al equipo diciendo "listo" sobre una tarjeta que después va a
     * rechazar el primer volcado — y eso se descubriría 33 horas más tarde, con
     * los datos ya en riesgo.
     */
    if( f_mount( &xFs, "", 1 ) != FR_OK )
    {
        xprintf( "SD:: [!] formateo hecho pero la tarjeta NO monta\r\n" );
        prvDesmontar();
        return false;
    }

    bMontado = true;

    DWORD  ulLibres;
    FATFS *pxFsPtr;
    const char *pcTipo = ( xFs.fs_type == FS_FAT32 ) ? "FAT32" :
                         ( xFs.fs_type == FS_FAT16 ) ? "FAT16" : "FAT12";

    if( f_getfree( "", &ulLibres, &pxFsPtr ) == FR_OK )
    {
        xprintf( "SD:: formateada en %s, %lu KB libres\r\n", pcTipo,
                 ( unsigned long ) ( ( ulLibres * pxFsPtr->csize ) / 2UL ) );
    }
    else
    {
        xprintf( "SD:: formateada en %s\r\n", pcTipo );
    }

    /* El contador de lotes vuelve a cero: los archivos que numeraba ya no
       existen, y arrancar de LOTE0001 es lo menos confuso al mirar la tarjeta. */
    prvContadorGrabar( 0U );

    prvDesmontar();
    return true;
}
//------------------------------------------------------------------------------
void fs_sd_listar( void )
{
    DIR     xDir;
    FILINFO xInfo;

    if( !prvMontar() )
    {
        return;     /* prvMontar() ya explicó por qué */
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

    if( pcNombre == NULL )
    {
        return;
    }

    if( !prvMontar() )
    {
        return;     /* prvMontar() ya explicó por qué */
    }

    if( f_open( &xFile, pcNombre, FA_READ ) != FR_OK )
    {
        xprintf( "SD:: no se pudo abrir %s\r\n", pcNombre );
        prvDesmontar();
        return;
    }

    for( uint16_t i = 0U; i < usLineas; i++ )
    {
        if( f_gets( pcBufSector, ( int ) sizeof( pcBufSector ), &xFile ) == NULL )
        {
            break;
        }

        /* Sin xprintf: la línea es un frame y puede pasar los 160 bytes de su
           buffer. Ver wan_frame. */
        ( void ) frtos_write( fdTERM, pcBufSector, ( uint16_t ) strlen( pcBufSector ) );
    }

    ( void ) f_close( &xFile );
    prvDesmontar();
}
//------------------------------------------------------------------------------

/*==============================================================================
 * LEER UN LOTE PARA TRANSMITIRLO (paso 5c, segunda mitad)
 *
 * ⚠ **El archivo queda ABIERTO y la tarjeta encendida** durante todo el envío
 * del lote, que pueden ser 1984 líneas y varios minutos. Es deliberado, y el
 * argumento es de proporción: mientras se transmite, **el modem está encendido
 * consumiendo decenas de mA**, contra los 0,2-1 mA de la microSD. Optimizar eso
 * costaría reabrir y remontar la tarjeta cada pocas líneas —del orden de 200
 * ciclos por lote— para ahorrar algo que es ruido al lado del modem.
 *
 * La alternativa de leer el lote entero a RAM no existe: 1984 líneas de ~153
 * bytes son **300 KB** contra los 256 KB del micro.
 *============================================================================*/

static FIL  xLote;
static bool bLoteAbierto;
static char pcLoteNombre[ FS_SD_NOMBRE_LARGO ];

//------------------------------------------------------------------------------
bool fs_sd_lote_abrir( char *pcNombre, uint16_t usSize )
{
    if( bLoteAbierto )
    {
        /* Abrir dos veces sin cerrar dejaría el `FIL` anterior perdido y la
           tarjeta encendida para siempre. */
        fs_sd_lote_cerrar( false );
    }

    if( !fs_sd_lote_mas_viejo( pcLoteNombre, sizeof( pcLoteNombre ) ) )
    {
        return false;   /* no hay lotes, o no hay tarjeta */
    }

    if( !prvMontar() )
    {
        return false;   /* prvMontar() ya explicó por qué */
    }

    if( f_open( &xLote, pcLoteNombre, FA_READ ) != FR_OK )
    {
        xprintf( "SD:: no se pudo abrir %s\r\n", pcLoteNombre );
        prvDesmontar();
        return false;
    }

    bLoteAbierto = true;

    if( ( pcNombre != NULL ) && ( usSize > 0U ) )
    {
        ( void ) snprintf( pcNombre, usSize, "%s", pcLoteNombre );
    }

    return true;
}
//------------------------------------------------------------------------------
bool fs_sd_lote_leer( char *pcLinea, uint16_t usSize )
{
    if( !bLoteAbierto || ( pcLinea == NULL ) || ( usSize < 2U ) )
    {
        return false;
    }

    if( f_gets( pcLinea, ( int ) usSize, &xLote ) == NULL )
    {
        return false;   /* se terminó el archivo */
    }

    /*
     * ⚠ Hay que sacar el CRLF: `f_gets` lo devuelve, el frame no lo lleva, y el
     * módulo delimita las tramas por SILENCIO. Un `\r\n` al final del payload
     * viajaría adentro del GET como parte del último campo — del lado del
     * servidor, `bt12v=7.273\r\n` no es el mismo valor que `bt12v=7.273`.
     */
    uint16_t usLargo = ( uint16_t ) strlen( pcLinea );

    while( ( usLargo > 0U ) &&
           ( ( pcLinea[ usLargo - 1U ] == '\n' ) || ( pcLinea[ usLargo - 1U ] == '\r' ) ) )
    {
        pcLinea[ --usLargo ] = '\0';
    }

    return ( usLargo > 0U );
}
//------------------------------------------------------------------------------
void fs_sd_lote_cerrar( bool bBorrar )
{
    if( !bLoteAbierto )
    {
        return;
    }

    ( void ) f_close( &xLote );
    bLoteAbierto = false;

    /*
     * ⚠ El borrado va **con la tarjeta todavía montada**, antes de desmontar.
     * Y sólo si el llamador confirmó que el lote entero llegó: si se corta en el
     * medio, el archivo queda y se retransmite completo la próxima vez.
     *
     * Eso significa **duplicados** de lo que ya había llegado, y es aceptable a
     * propósito: el servidor indexa por la fecha que viaja adentro de cada
     * frame, así que un registro repetido se sobrescribe a sí mismo. La
     * alternativa —un puntero de línea persistente— agregaría un estado más que
     * se puede corromper, para evitar algo que no hace daño.
     */
    if( bBorrar )
    {
        if( f_unlink( pcLoteNombre ) == FR_OK )
        {
            xprintf( "SD:: %s transmitido y BORRADO\r\n", pcLoteNombre );
        }
        else
        {
            /* No se pudo borrar: se va a retransmitir entero la próxima vez.
               Molesto pero inofensivo; lo que no puede pasar es no enterarse. */
            xprintf( "SD:: [!] no se pudo borrar %s: se va a retransmitir\r\n",
                     pcLoteNombre );
        }
    }

    prvDesmontar();
}
//------------------------------------------------------------------------------
