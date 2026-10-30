/*
 * fw_update.c  --  ver fw_update.h para la arquitectura y por qué.
 *
 * ETAPA 2 de 5: **examinar la tarjeta. Todavía no se escribe un solo byte de
 * flash.** El equipo sigue arrancando del banco 1 pase lo que pase acá.
 */

#include "fw_update.h"

#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "main.h"
#include "frtos-io.h"
#include "fs_sd.h"

/*------------------------------------------------------------------------------
 * CRC-32 por NIBBLE: 16 entradas en vez de 256.
 *
 * ⭐ 64 bytes de flash contra 1 KB, y el resultado es idéntico — se procesa
 * medio byte por vuelta. Sobre los 176 KB de una imagen son ~350.000 vueltas,
 * milisegundos a 60 MHz, y el que manda es el tiempo de leer la microSD.
 */
static const uint32_t pulCrcTabla[ 16 ] = {
    0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
    0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
    0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
    0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
};

uint32_t fw_update_crc32( uint32_t ulPrev, const void *pvDatos, uint32_t ulLargo )
{
    const uint8_t *pucD = ( const uint8_t * ) pvDatos;
    uint32_t       ulCrc = ~ulPrev;          /* init 0xFFFFFFFF en la 1.ª vuelta */

    if( pvDatos == NULL )
    {
        return ulPrev;
    }

    while( ulLargo-- > 0UL )
    {
        ulCrc ^= *pucD++;
        ulCrc = pulCrcTabla[ ulCrc & 0x0FU ] ^ ( ulCrc >> 4 );
        ulCrc = pulCrcTabla[ ulCrc & 0x0FU ] ^ ( ulCrc >> 4 );
    }

    return ~ulCrc;
}

/*------------------------------------------------------------------------------
 * El header tal como vive en el archivo.
 *
 * ⚠ `packed` **no es por el tamaño**: sin él el compilador podría insertar
 * relleno y los offsets dejarían de coincidir con los que escribe `mkfw.py`.
 * El `_Static_assert` de abajo es lo que lo prueba en cada compilación, no una
 * promesa en un comentario. Es la misma razón por la que el registro de
 * muestras va packed.
 */
typedef struct __attribute__( ( packed ) ) {
    uint32_t ulMagic;
    uint16_t usHdrVer;
    uint16_t usHdrLen;
    char     pcVersion[ FW_UPDATE_VER_LEN ];
    char     pcHw[ FW_UPDATE_HW_LEN ];
    uint32_t ulImgLen;
    uint32_t ulImgCrc;
    uint32_t ulHdrCrc;
    uint8_t  pucReserva[ 12 ];
} fw_header_t;

_Static_assert( sizeof( fw_header_t ) == FW_UPDATE_HDR_LEN,
                "el header del firmware tiene que ser de 64 bytes exactos" );

/* Lo que cubre el CRC del header: todo lo anterior a su propio campo.
   ⚠ Con `offsetof` y NO con `sizeof - algo`: es la trampa que hacía que la FAT
   se declarara inválida en cada arranque (2026-09-09). */
#define FW_HDR_CRC_RANGO    offsetof( fw_header_t, ulHdrCrc )

/*------------------------------------------------------------------------------*/
const char *fw_update_str( fw_result_t xRes )
{
    switch( xRes )
    {
        case fwHAY_NUEVO:      return "hay un firmware nuevo y valido";
        case fwSIN_TARJETA:    return "no hay microSD";
        /* ⚠ NO nombra el archivo: el enum no sabe cuál se pidió, y decir
           "FIRMWARE.BIN" cuando se pidió otro manda a buscar al lado
           equivocado — me costó una vuelta entera en el banco el 2026-10-30.
           El nombre lo imprime el comando dos líneas antes. */
        case fwNO_HAY:         return "la tarjeta NO TIENE ese archivo";
        case fwNO_SE_PUDO_LEER:return "no se pudo leer el archivo";
        case fwMAGIC:          return "no es un firmware de este mecanismo (magic)";
        case fwHDR_VER:        return "formato de header desconocido: firmware mas nuevo que este";
        case fwHDR_CRC:        return "el header esta CORRUPTO";
        case fwHW:             return "es el firmware de OTRO producto";
        case fwLARGO:          return "el largo no cierra, o no entra en el banco";
        case fwIMG_CRC:        return "la imagen esta CORRUPTA";
        case fwMISMA_VERSION:  return "es la misma version que ya esta corriendo";
        default:               return "?";
    }
}

/*------------------------------------------------------------------------------
 * ⚠ Un buffer en la pila sería de 512 bytes sobre una tarea que ya usa 28 %.
 * Estático: esta función **no es reentrante y no tiene por qué serlo** — la
 * tarjeta tiene un solo dueño y esto corre una vez al arrancar.
 */
static uint8_t pucBloque[ 512 ];

fw_result_t fw_update_examinar( const char *pcArchivo, fw_info_t *pxInfo )
{
    fw_header_t xHdr;
    uint32_t    ulTam = 0UL;
    uint32_t    ulCrc = 0UL;
    uint32_t    ulLeidos = 0UL;
    fw_result_t xRes;

    if( pxInfo != NULL )
    {
        memset( pxInfo, 0, sizeof( fw_info_t ) );
    }

    if( pcArchivo == NULL )
    {
        pcArchivo = FW_UPDATE_ARCHIVO;
    }

    if( !fs_sd_fw_abrir( pcArchivo, &ulTam ) )
    {
        /* `fs_sd_fw_abrir()` distingue los tres casos; acá se traducen. */
        return fs_sd_fw_ultimo_error();
    }

    /* ---- 1. el header ---------------------------------------------------- */
    if( fs_sd_fw_leer( &xHdr, ( uint32_t ) sizeof( xHdr ) ) != ( int32_t ) sizeof( xHdr ) )
    {
        fs_sd_fw_cerrar();
        return fwNO_SE_PUDO_LEER;
    }

    if( pxInfo != NULL )
    {
        /* Se copia ANTES de validar nada: en un archivo corrupto, ver la
           versión que *dice* tener ya orienta al que está mirando. */
        memcpy( pxInfo->pcVersion, xHdr.pcVersion, FW_UPDATE_VER_LEN );
        memcpy( pxInfo->pcHw,      xHdr.pcHw,      FW_UPDATE_HW_LEN  );
        pxInfo->pcVersion[ FW_UPDATE_VER_LEN - 1U ] = '\0';
        pxInfo->pcHw[ FW_UPDATE_HW_LEN - 1U ]       = '\0';
        pxInfo->ulLargo = xHdr.ulImgLen;
        pxInfo->ulCrc   = xHdr.ulImgCrc;
    }

    xRes = fwHAY_NUEVO;

    if( xHdr.ulMagic != FW_UPDATE_MAGIC )
    {
        xRes = fwMAGIC;
    }
    else if( fw_update_crc32( 0UL, &xHdr, FW_HDR_CRC_RANGO ) != xHdr.ulHdrCrc )
    {
        /* ⭐ El CRC del header va ANTES que el resto de las validaciones: sin
           él, un largo o una versión corruptos se interpretarían como datos
           buenos y el diagnóstico apuntaría al lugar equivocado. */
        xRes = fwHDR_CRC;
    }
    else if( ( xHdr.usHdrVer != FW_UPDATE_HDR_VER ) ||
             ( xHdr.usHdrLen != FW_UPDATE_HDR_LEN ) )
    {
        xRes = fwHDR_VER;
    }
    else if( strncmp( xHdr.pcHw, FW_HW, FW_UPDATE_HW_LEN ) != 0 )
    {
        xRes = fwHW;
    }
    else if( ( xHdr.ulImgLen == 0UL ) ||
             ( xHdr.ulImgLen > FW_UPDATE_BANCO_BYTES ) ||
             ( ( xHdr.ulImgLen % 8UL ) != 0UL ) ||
             ( ( xHdr.ulImgLen + FW_UPDATE_HDR_LEN ) != ulTam ) )
    {
        /* ⚠ El múltiplo de 8 no es un capricho: el L4 programa la flash de a
           DOBLE PALABRA, así que una imagen que no lo cumpla no se puede
           escribir entera. Mejor rechazarla acá que a mitad del grabado. */
        xRes = fwLARGO;
    }

    if( xRes != fwHAY_NUEVO )
    {
        fs_sd_fw_cerrar();
        return xRes;
    }

    /* ---- 2. el CRC de la imagen ------------------------------------------ */
    /*
     * Se lee el archivo ENTERO. Tarda, y es el precio de no escribir la flash
     * con basura: **un CRC que no se verifica no sirve de nada**, y el único
     * momento barato para verificarlo es antes de tocar nada.
     */
    while( ulLeidos < xHdr.ulImgLen )
    {
        uint32_t ulPedir = xHdr.ulImgLen - ulLeidos;
        int32_t  lN;

        if( ulPedir > sizeof( pucBloque ) )
        {
            ulPedir = sizeof( pucBloque );
        }

        lN = fs_sd_fw_leer( pucBloque, ulPedir );

        if( lN <= 0 )
        {
            fs_sd_fw_cerrar();
            return fwNO_SE_PUDO_LEER;
        }

        ulCrc     = fw_update_crc32( ulCrc, pucBloque, ( uint32_t ) lN );
        ulLeidos += ( uint32_t ) lN;
    }

    fs_sd_fw_cerrar();

    if( pxInfo != NULL )
    {
        pxInfo->ulCrcLeido = ulCrc;
    }

    if( ulCrc != xHdr.ulImgCrc )
    {
        return fwIMG_CRC;
    }

    /* ---- 3. ¿es DISTINTA de la que corre? -------------------------------- */
    /*
     * ⚠ DISTINTA, no superior (decisión de Pablo, 2026-10-30). Si sólo
     * aceptara mayores, volver atrás obligaría a fabricar un número de versión
     * falso — y volver atrás es justo lo que hace falta cuando algo sale mal.
     *
     * Lo que evita reprogramar en bucle es renombrar el archivo a `.OK`, no
     * esta comparación.
     */
    if( strncmp( xHdr.pcVersion, FW_VERSION, FW_UPDATE_VER_LEN ) == 0 )
    {
        return fwMISMA_VERSION;
    }

    return fwHAY_NUEVO;
}

/*==============================================================================
 * ETAPA 3 — ESCRIBIR EL BANCO 2
 *
 * ⚠ No cambia el banco de arranque: el equipo sigue arrancando del banco 1 pase
 * lo que pase acá. El commit es la etapa 4.
 *============================================================================*/

/*
 * ⛔⛔ ESTAS TRES CONSTANTES NO SE PONEN DE MEMORIA.
 *
 * `FLASH_PAGE_SIZE` aparece DOS veces en `stm32l4xx_hal_flash.h`: vale `0x1000`
 * para la familia L4P5/L4R/L4S y `0x800` para el L496, y un `grep` descuidado
 * muestra la primera. Si la página fuera de 4 KB y acá se usaran 2, el borrado
 * dejaría media imagen sin borrar **y el error aparecería como un CRC que no
 * cierra, sin decir por qué**.
 *
 * ⭐ Por eso van estos asserts y no un comentario: si alguna vez cambia la HAL,
 * el micro o el `#if`, lo dice el compilador. Es la lección del volcado de
 * `ADC_CR` mal decodificado, que costó tres días por una posición de bit puesta
 * de memoria.
 */
_Static_assert( FLASH_PAGE_SIZE == 0x800UL,
                "la pagina del L496 son 2 KB: revisar el #if de la HAL" );
_Static_assert( FW_UPDATE_BANCO_BYTES == ( 512UL * 1024UL ),
                "el banco del L496 son 512 KB" );
_Static_assert( ( FW_UPDATE_BANCO_BYTES / FLASH_PAGE_SIZE ) <= 256UL,
                "el campo PNB del FLASH_CR son 8 bits: 256 paginas por banco" );

/* El banco 2 empieza acá. ⚠ Es su dirección FÍSICA; cuando el micro arranque de
   él, el silicio lo remapea a 0x08000000 ("Set Bank Swap to Bank2", AN2606
   fig. 102) — y por eso el mismo binario sirve para los dos bancos. */
#define FW_BANCO2_BASE      ( FLASH_BASE + FW_UPDATE_BANCO_BYTES )

uint32_t fw_update_crc_banco2( uint32_t ulLargo )
{
    if( ( ulLargo == 0UL ) || ( ulLargo > FW_UPDATE_BANCO_BYTES ) )
    {
        return 0UL;
    }

    /* ⭐ Se lee por el bus, como cualquier memoria: el banco 2 está mapeado y
       legible mientras se ejecuta del banco 1. */
    return fw_update_crc32( 0UL, ( const void * ) FW_BANCO2_BASE, ulLargo );
}

/*------------------------------------------------------------------------------*/
static bool prvBorrarBanco2( uint32_t ulBytes )
{
    FLASH_EraseInitTypeDef xBorrado;
    uint32_t               ulPaginas = ( ulBytes + FLASH_PAGE_SIZE - 1UL ) / FLASH_PAGE_SIZE;
    uint32_t               ulError   = 0xFFFFFFFFUL;

    xBorrado.TypeErase = FLASH_TYPEERASE_PAGES;
    xBorrado.Banks     = FLASH_BANK_2;
    xBorrado.Page      = 0UL;           /* ⚠ RELATIVA AL BANCO: la HAL la mete
                                           en PNB y elige el banco con BKER */
    xBorrado.NbPages   = ulPaginas;

    if( HAL_FLASHEx_Erase( &xBorrado, &ulError ) != HAL_OK )
    {
        xprintf( "FW:: ERROR borrando la pagina %lu del banco 2 (err 0x%08lX)\r\n",
                 ( unsigned long ) ulError, ( unsigned long ) HAL_FLASH_GetError() );
        return false;
    }

    return true;
}

/*------------------------------------------------------------------------------*/
bool fw_update_grabar( const char *pcArchivo, fw_info_t *pxInfo, fw_result_t *pxRes )
{
    fw_info_t   xInfo;
    fw_result_t xRes;
    uint32_t    ulEscritos = 0UL;
    uint32_t    ulCrc;
    bool        bOk = true;

    if( pcArchivo == NULL )
    {
        pcArchivo = FW_UPDATE_ARCHIVO;
    }

    /* ---- 1. el CRC del ARCHIVO, antes de tocar un solo byte de flash ----- */
    xRes = fw_update_examinar( pcArchivo, &xInfo );

    if( pxInfo != NULL ) { *pxInfo = xInfo; }
    if( pxRes  != NULL ) { *pxRes  = xRes;  }

    if( xRes != fwHAY_NUEVO )
    {
        /* ⭐ Se verifica DE NUEVO aunque el llamador ya haya corrido `fw`: son
           1,7 s contra el riesgo de escribir la flash con un archivo que entre
           medio se cambió, o que nunca se verificó porque nadie corrió el
           comando. El que graba no hereda la confianza de otro. */
        return false;
    }

    xprintf( "FW:: borrando el banco 2 (%lu paginas de %lu B)...\r\n",
             ( unsigned long ) ( ( xInfo.ulLargo + FLASH_PAGE_SIZE - 1UL ) / FLASH_PAGE_SIZE ),
             ( unsigned long ) FLASH_PAGE_SIZE );

    if( HAL_FLASH_Unlock() != HAL_OK )
    {
        xprintf( "FW:: ERROR: no se pudo desbloquear la flash\r\n" );
        return false;
    }

    if( !prvBorrarBanco2( xInfo.ulLargo ) )
    {
        ( void ) HAL_FLASH_Lock();
        return false;
    }

    /* ---- 2. escribir, leyendo el archivo de nuevo ------------------------ */
    xprintf( "FW:: escribiendo %lu bytes...\r\n", ( unsigned long ) xInfo.ulLargo );

    if( !fs_sd_fw_abrir( pcArchivo, NULL ) )
    {
        ( void ) HAL_FLASH_Lock();
        return false;
    }

    /* saltear el header: lo que va a la flash es la imagen cruda */
    if( fs_sd_fw_leer( pucBloque, FW_UPDATE_HDR_LEN ) != ( int32_t ) FW_UPDATE_HDR_LEN )
    {
        fs_sd_fw_cerrar();
        ( void ) HAL_FLASH_Lock();
        return false;
    }

    while( ( ulEscritos < xInfo.ulLargo ) && bOk )
    {
        uint32_t ulPedir = xInfo.ulLargo - ulEscritos;
        int32_t  lN;
        uint32_t i;

        if( ulPedir > sizeof( pucBloque ) ) { ulPedir = sizeof( pucBloque ); }

        lN = fs_sd_fw_leer( pucBloque, ulPedir );

        if( lN <= 0 )
        {
            bOk = false;
            break;
        }

        /*
         * ⚠ De a DOBLE PALABRA (64 bits): es la única granularidad que el L4
         * admite para programar. Por eso `mkfw.py` rellena la imagen a múltiplo
         * de 8 con 0xFF —el valor de la flash borrada— y por eso
         * `fw_update_examinar()` rechaza un largo que no lo sea: mejor acá que
         * a mitad del grabado.
         *
         * El `memcpy` a una variable alineada no es cosmético: `pucBloque + i`
         * es un `uint8_t*` y leerlo como `uint64_t*` sería un acceso
         * desalineado, que en Cortex-M4 es undefined behaviour.
         */
        for( i = 0U; i < ( uint32_t ) lN; i += 8U )
        {
            uint64_t ullDato;

            memcpy( &ullDato, &pucBloque[ i ], sizeof( ullDato ) );

            if( HAL_FLASH_Program( FLASH_TYPEPROGRAM_DOUBLEWORD,
                                   FW_BANCO2_BASE + ulEscritos + i,
                                   ullDato ) != HAL_OK )
            {
                xprintf( "FW:: ERROR escribiendo en 0x%08lX (err 0x%08lX)\r\n",
                         ( unsigned long ) ( FW_BANCO2_BASE + ulEscritos + i ),
                         ( unsigned long ) HAL_FLASH_GetError() );
                bOk = false;
                break;
            }
        }

        ulEscritos += ( uint32_t ) lN;
    }

    fs_sd_fw_cerrar();
    ( void ) HAL_FLASH_Lock();

    if( !bOk )
    {
        return false;
    }

    /* ---- 3. ⭐⭐ RELEER DE LA FLASH y verificar -------------------------- */
    /*
     * Acá está el valor de esta etapa. `HAL_FLASH_Program()` devolvió OK en
     * cada una de las ~31.000 escrituras, y eso **no prueba nada**: una celda
     * que no tomó el dato, una página que no se borró del todo o un bit débil
     * devuelven OK igual. Lo único que prueba que el banco 2 tiene el firmware
     * es leerlo de vuelta.
     *
     * ⚠ Y se lee del BUS, no del buffer que acabamos de escribir: comparar
     * contra lo que creemos haber escrito no detectaría nada.
     */
    ulCrc = fw_update_crc_banco2( xInfo.ulLargo );

    if( pxInfo != NULL )
    {
        pxInfo->ulCrcLeido = ulCrc;
    }

    if( ulCrc != xInfo.ulCrc )
    {
        xprintf( "FW:: ⛔ el banco 2 NO verifica: 0x%08lX contra 0x%08lX\r\n",
                 ( unsigned long ) ulCrc, ( unsigned long ) xInfo.ulCrc );
        return false;
    }

    return true;
}
