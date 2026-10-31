/*
 * fw_update.c  --  ver fw_update.h para la arquitectura y por qué.
 *
 * ETAPA 2 de 5: **examinar la tarjeta. Todavía no se escribe un solo byte de
 * flash.** El equipo sigue arrancando del banco 1 pase lo que pase acá.
 */

#include "fw_update.h"
#include "drv_eeprom.h"

#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "main.h"
#include "frtos-io.h"
#include "fs_sd.h"
#include "wdg.h"
#include "eventos.h"

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

/*------------------------------------------------------------------------------*/
void fw_update_estado_banco2( fw_banco2_t *pxEstado )
{
    const uint32_t *pulB1 = ( const uint32_t * ) FLASH_BASE;
    const uint32_t *pulB2 = ( const uint32_t * ) FW_BANCO2_BASE;

    if( pxEstado == NULL )
    {
        return;
    }

    pxEstado->ulSpBanco1    = pulB1[ 0 ];
    pxEstado->ulSpBanco2    = pulB2[ 0 ];
    pxEstado->ulResetBanco2 = pulB2[ 1 ];
    pxEstado->bEnBlanco     = ( pulB2[ 0 ] == 0xFFFFFFFFUL );

    /*
     * ⭐⭐ EL CRITERIO ES "¿ESTO PARECE UNA TABLA DE VECTORES?", Y CAMBIÓ.
     *
     * Hasta el 2026-10-30 se compara contra el banco 1 —los 16 bits altos del
     * stack pointer—, razonando que el banco 1 **es el que está corriendo**, así
     * que su SP es por definición uno que el bootloader del dual-bank boot
     * aceptó. Eso tenía sentido mientras el arranque del banco 2 lo decidiera
     * `BFB2`; con `BFB2` abandonado y el commit copiando bytes, el bootloader
     * ya no mira nada y la pregunta dejó de ser la suya.
     *
     * ⛔⛔ Y ADEMÁS DABA UN FALSO POSITIVO, MEDIDO EN BANCO (2026-10-30): el
     * `0.0.20` tiene `_estack = 0x20040000` y el `0.0.21` `0x2003FFF8`, o sea
     * `0x2004` contra `0x2003`. Con el ciclo completo desde la microSD, el
     * comando declaró *"no parece un firmware valido"* sobre la imagen que
     * arrancó y quedó corriendo perfecta. ⭐ Los **dos** valores son correctos:
     * el stack **decrece** desde `_estack`, así que el tope de SRAM1 es un
     * valor inicial legítimo —justo el borde donde los 16 bits altos cambian,
     * que es el caso que el comentario viejo nombraba y el código no cubría—.
     *
     * ⚠ Un mensaje que manda a dudar de un archivo bueno cuesta más que no
     * tener mensaje: es la familia del `help` que mentía sobre el parser y del
     * `R1 = 0x00` que acusó a dos pistas sanas.
     *
     * Lo que se chequea ahora son las dos palabras que definen un arranque:
     *
     *   SP    en SRAM1, **con el tope inclusive** (`_estack` puede ser el tope)
     *         y alineado —la AAPCS pide 8, se exige 4 para no rechazar de más—
     *   reset dentro de la flash y **impar**: el bit 0 es el bit Thumb, y un
     *         vector par en un Cortex-M es un HardFault al primer salto
     */
    {
        const uint32_t ulSramIni = SRAM1_BASE;
        const uint32_t ulSramFin = SRAM1_BASE + ( 256UL * 1024UL );   /* inclusive */
        const uint32_t ulSp      = pxEstado->ulSpBanco2;
        const uint32_t ulRst     = pxEstado->ulResetBanco2;

        pxEstado->bPlausible = ( !pxEstado->bEnBlanco )                     &&
                               ( ulSp  >= ulSramIni ) && ( ulSp <= ulSramFin ) &&
                               ( ( ulSp & 3UL ) == 0UL )                    &&
                               ( ulRst >= FLASH_BASE )                      &&
                               ( ulRst <  ( FLASH_BASE + ( 2UL * FW_UPDATE_BANCO_BYTES ) ) ) &&
                               ( ( ulRst & 1UL ) == 1UL );
    }
}

/*==============================================================================
 * ETAPA 4 — EL COMMIT
 *============================================================================*/

uint8_t fw_update_banco_activo( void )
{
    /* FB_MODE = 1 -> el banco 2 está mapeado en 0x08000000, o sea que estamos
       corriendo de él. Lo pone el bootloader al hacer el swap. */
    return ( ( SYSCFG->MEMRMP & SYSCFG_MEMRMP_FB_MODE ) != 0UL ) ? 2U : 1U;
}

uint8_t fw_update_banco_de_arranque( void )
{
    return ( ( FLASH->OPTR & FLASH_OPTR_BFB2 ) != 0UL ) ? 2U : 1U;
}

bool fw_update_arrancar_de( uint8_t ucBanco )
{
    FLASH_OBProgramInitTypeDef xOb;

    if( ( ucBanco != 1U ) && ( ucBanco != 2U ) )
    {
        return false;
    }

    memset( &xOb, 0, sizeof( xOb ) );
    xOb.OptionType = OPTIONBYTE_USER;
    xOb.USERType   = OB_USER_BFB2;      /* ⭐ SÓLO este bit: la HAL usa una
                                           máscara, así que IWDG_SW, BOR_LEV y
                                           los demás quedan como están */
    xOb.USERConfig = ( ucBanco == 2U ) ? OB_BFB2_ENABLE : OB_BFB2_DISABLE;

    if( HAL_FLASH_Unlock() != HAL_OK )
    {
        xprintf( "FW:: ERROR: no se pudo desbloquear la flash\r\n" );
        return false;
    }

    if( HAL_FLASH_OB_Unlock() != HAL_OK )
    {
        ( void ) HAL_FLASH_Lock();
        xprintf( "FW:: ERROR: no se pudieron desbloquear los option bytes\r\n" );
        return false;
    }

    if( HAL_FLASHEx_OBProgram( &xOb ) != HAL_OK )
    {
        xprintf( "FW:: ERROR escribiendo BFB2 (err 0x%08lX)\r\n",
                 ( unsigned long ) HAL_FLASH_GetError() );
        ( void ) HAL_FLASH_OB_Lock();
        ( void ) HAL_FLASH_Lock();
        return false;
    }

    /*
     * ⛔ A PARTIR DE ACÁ NO SE VUELVE. `HAL_FLASH_OB_Launch()` recarga los
     * option bytes con un RESET DEL SISTEMA.
     *
     * ⚠ El `vTaskDelay()` no es cortesía: la consola es por interrupción con un
     * buffer, así que sin esperar, el mensaje de arriba se pierde en el reset y
     * desde afuera el equipo "se reinicia solo sin decir nada" — que es
     * exactamente el síntoma que uno NO quiere ver en el comando más delicado
     * del firmware.
     */
    xprintf( "\r\nFW:: BFB2 escrito. Reiniciando para arrancar del BANCO %u...\r\n",
             ( unsigned ) ucBanco );
    vTaskDelay( pdMS_TO_TICKS( 500 ) );

    ( void ) HAL_FLASH_OB_Launch();

    /* No se llega. Si se llegara, el launch falló y hay que dejar todo cerrado. */
    ( void ) HAL_FLASH_OB_Lock();
    ( void ) HAL_FLASH_Lock();
    xprintf( "FW:: ⛔ el OB_Launch no reinicio: los option bytes NO se recargaron\r\n" );
    return false;
}

/*==============================================================================
 * EL COMMIT: copiar el banco 2 al banco 1
 *------------------------------------------------------------------------------
 * ⛔⛔ TODO LO QUE SIGUE CORRE DESDE RAM, Y NO ES UNA PRECAUCION: se está
 *     borrando el banco DESDE EL QUE SE EJECUTA. En el instante en que arranca
 *     el mass erase, la flash del banco 1 deja de existir — y ahí viven la HAL,
 *     `.rodata`, `xprintf` y la tabla de vectores. Una sola llamada a cualquiera
 *     de ellos es un HardFault sin retorno.
 *
 *     Por eso acá NO se usa la HAL: los registros de flash se manejan a mano,
 *     con los bits verificados en `stm32l496xx.h` (ver el commit). Es la lección
 *     del volcado de `ADC_CR` mal decodificado — las posiciones no se ponen de
 *     memoria.
 *
 * ⭐⭐ Y EL ORDEN ES EL DISEÑO: el PRIMER DOUBLEWORD —el `SP` inicial y el
 *     `Reset_Handler`— se escribe AL FINAL. Mientras no esté, `0x08000000` lee
 *     `0xFFFFFFFF` y entra el mecanismo del silicio (DS11585 §3.8):
 *
 *        "if the first flash memory location is found empty, the flash empty
 *         check mechanism forces boot from the system memory"
 *
 *     O sea que durante los ~2,65 s de la copia el equipo NO queda ladrillo:
 *     arranca el bootloader de ST y se recupera por SWD o por serie.
 *
 *        antes del borrado     -> el firmware viejo, intacto
 *        durante la copia      -> flash vacia -> bootloader de ST (recuperable)
 *        los 82 us del final   -> ⛔ el unico caso malo
 *        despues               -> el firmware nuevo
 *
 * ⚠ Ese empty check está activo PORQUE `nSWBOOT0 = 1`. El datasheet dice que
 *   ponerlo en 0 lo desactiva: NO CAMBIARLO (y ver la nota de `BFB2` en
 *   CLAUDE.md, donde eso ya se probó y se revirtió).
 *
 * Los tiempos salen del DS11585, no de una estimación:
 *      tME        borrar un banco entero   22,13 ms tip   <- no 128 x tERASE
 *      tPROG_PAGE 2 KB                     20,91 ms tip
 *      tPROG      un doubleword            81,69 us tip
 *============================================================================*/

/* Las dos llaves que destraban `FLASH->CR`. Van literales y no por el define de
   la HAL: este código no puede depender de nada que viva en flash. */
#define FW_CMT_KEY1             0x45670123UL
#define FW_CMT_KEY2             0xCDEF89ABUL

/* Todos los flags de error de `FLASH_SR`, juntos. Verificados en el header:
   OPERR(1) PROGERR(3) WRPERR(4) PGAERR(5) SIZERR(6) PGSERR(7) MISERR(8) FASTERR(9) */
#define FW_CMT_SR_ERRORES       ( FLASH_SR_OPERR   | FLASH_SR_PROGERR | FLASH_SR_WRPERR  | \
                                  FLASH_SR_PGAERR  | FLASH_SR_SIZERR  | FLASH_SR_PGSERR  | \
                                  FLASH_SR_MISERR  | FLASH_SR_FASTERR )

/*------------------------------------------------------------------------------
 * Espera a que la flash termine, pateando el perro.
 *
 * ⛔⛔ VA EN `.RamFunc`, Y NO ALCANZA CON `inline`: con `-O0` —que es como se
 *    compila todo el bring-up— **GCC NO INLINEA NADA**, el `inline` es una
 *    sugerencia. La primera versión quedó en flash y `prvCommitEnRam()` la
 *    llamaba por un *veneer*: con el banco 1 ya borrado eso es un HardFault y
 *    el equipo muerto. Compila sin un warning y falla sólo al ejecutarse.
 *    ⭐ Lo cazo `arm-none-eabi-nm`, y por eso el build lo verifica (ver abajo).
 * ⭐ El kick no es cosmético: el IWDG tiene una ventana de ~32,8 s y sigue
 *    contando acá. La copia son ~2,7 s, así que entra con margen — pero si
 *    alguna espera se estirara, un reset a mitad del banco 1 es exactamente
 *    el desenlace que este diseño evita.
 */
__attribute__( ( section( ".RamFunc" ), noinline ) )
static uint32_t prvEsperarFlash( void )
{
    uint32_t ulSr;

    do
    {
        IWDG->KR = 0x0000AAAAUL;
        ulSr = FLASH->SR;
    } while( ( ulSr & FLASH_SR_BSY ) != 0UL );

    return ( ulSr & FW_CMT_SR_ERRORES );
}

/*------------------------------------------------------------------------------
 * Programa UN doubleword. Es la unidad del L4: 64 bits, y la escritura la
 * dispara el SEGUNDO word de 32.
 */
__attribute__( ( section( ".RamFunc" ), noinline ) )
static uint32_t prvProgramarDw( uint32_t ulDir, uint32_t ulLo, uint32_t ulHi )
{
    uint32_t ulErr;

    FLASH->SR  = FW_CMT_SR_ERRORES | FLASH_SR_EOP;
    FLASH->CR |= FLASH_CR_PG;

    *( volatile uint32_t * )( ulDir        ) = ulLo;
    __ISB();
    *( volatile uint32_t * )( ulDir + 4UL ) = ulHi;

    ulErr = prvEsperarFlash();

    FLASH->SR  = FLASH_SR_EOP;
    FLASH->CR &= ~FLASH_CR_PG;

    return ulErr;
}

/*------------------------------------------------------------------------------
 * La copia entera, desde RAM. NO RETORNA si sale bien: resetea.
 *
 * Devuelve un `fw_commit_t` sólo en los caminos de error, y en ésos el banco 1
 * quedó SIN su primer doubleword a propósito — o sea, el equipo arranca el
 * bootloader de ST en vez de ejecutar un firmware a medias.
 */
__attribute__( ( section( ".RamFunc" ), noinline ) )
static uint8_t prvCommitEnRam( uint32_t ulLargo )
{
    uint32_t ulPrimLo;
    uint32_t ulPrimHi;
    uint32_t ulOfs;
    uint32_t ulErr;

    /* ⭐ El primer doubleword se guarda ANTES de tocar nada: después del mass
       erase seguiría siendo legible (vive en el banco 2), pero tenerlo en un
       registro hace que el camino final no dependa de ninguna lectura. */
    ulPrimLo = *( volatile uint32_t * )( FW_BANCO2_BASE        );
    ulPrimHi = *( volatile uint32_t * )( FW_BANCO2_BASE + 4UL );

    /* Las caches guardan lo que había ANTES de borrar: con ellas prendidas, la
       verificación de abajo leería el firmware viejo y daría OK sobre un banco
       que no se escribió. */
    FLASH->ACR &= ~( FLASH_ACR_ICEN | FLASH_ACR_DCEN );
    FLASH->ACR |=  ( FLASH_ACR_ICRST | FLASH_ACR_DCRST );
    FLASH->ACR &= ~( FLASH_ACR_ICRST | FLASH_ACR_DCRST );

    if( ( FLASH->CR & FLASH_CR_LOCK ) != 0UL )
    {
        FLASH->KEYR = FW_CMT_KEY1;
        FLASH->KEYR = FW_CMT_KEY2;
    }

    ( void ) prvEsperarFlash();
    FLASH->SR = FW_CMT_SR_ERRORES | FLASH_SR_EOP;

    /*--- 1. borrar el BANCO 1 ENTERO: 22 ms, contra 2,8 s pagina por pagina ---*/
    FLASH->CR |= FLASH_CR_MER1;
    FLASH->CR |= FLASH_CR_STRT;
    ulErr = prvEsperarFlash();
    FLASH->CR &= ~FLASH_CR_MER1;

    if( ulErr != 0UL )
    {
        FLASH->CR |= FLASH_CR_LOCK;
        return fwCMT_BORRADO;
    }

    /*--- 2. copiar TODO MENOS el primer doubleword ---*/
    for( ulOfs = 8UL; ulOfs < ulLargo; ulOfs += 8UL )
    {
        uint32_t ulLo = *( volatile uint32_t * )( FW_BANCO2_BASE + ulOfs        );
        uint32_t ulHi = *( volatile uint32_t * )( FW_BANCO2_BASE + ulOfs + 4UL );

        /* \u2b50 Saltear lo que ya est\u00e1 borrado es lo que permite copiar el banco
           ENTERO sin pagarlo en tiempo: el banco 1 reci\u00e9n borrado ya vale
           0xFF, as\u00ed que escribir ah\u00ed no cambia nada. Y de paso el llamador no
           necesita saber el largo de la imagen. */
        if( ( ulLo == 0xFFFFFFFFUL ) && ( ulHi == 0xFFFFFFFFUL ) )
        {
            IWDG->KR = 0x0000AAAAUL;
            continue;
        }

        ulErr = prvProgramarDw( FLASH_BASE + ulOfs, ulLo, ulHi );
        if( ulErr != 0UL )
        {
            FLASH->CR |= FLASH_CR_LOCK;
            return fwCMT_ESCRITURA;
        }
    }

    /*--- 3. VERIFICAR antes de armar el arranque -------------------------------
       ⭐ Es lo que hace que un error no se descubra en el próximo reset. Si acá
          falla, el primer doubleword NO se escribe y el equipo arranca el
          bootloader: feo, pero recuperable y visible. */
    for( ulOfs = 8UL; ulOfs < ulLargo; ulOfs += 4UL )
    {
        if( *( volatile uint32_t * )( FLASH_BASE     + ulOfs ) !=
            *( volatile uint32_t * )( FW_BANCO2_BASE + ulOfs ) )
        {
            FLASH->CR |= FLASH_CR_LOCK;
            return fwCMT_VERIFICACION;
        }
        IWDG->KR = 0x0000AAAAUL;
    }

    /*--- 4. y recién ahora, los 82 us que arman el arranque ---*/
    ulErr = prvProgramarDw( FLASH_BASE, ulPrimLo, ulPrimHi );
    FLASH->CR |= FLASH_CR_LOCK;

    if( ulErr != 0UL )
    {
        return fwCMT_ESCRITURA;
    }

    /*--- 5. reset. No vuelve. ---*/
    SCB->AIRCR = ( 0x5FAUL << SCB_AIRCR_VECTKEY_Pos ) | SCB_AIRCR_SYSRESETREQ_Msk;
    __DSB();

    for( ;; )
    {
        /* el reset tarda unos ciclos en llegar */
    }
}

/*------------------------------------------------------------------------------*/
const char *fw_update_commit_str( fw_commit_t xRes )
{
    switch( xRes )
    {
        case fwCMT_OK:           return "OK";
        case fwCMT_SIN_IMAGEN:   return "el banco 2 no tiene un firmware valido";
        case fwCMT_BORRADO:      return "fallo el borrado del banco 1";
        case fwCMT_ESCRITURA:    return "fallo la escritura del banco 1";
        case fwCMT_VERIFICACION: return "se escribio y al releer NO coincide";
        case fwCMT_NO_EN_RAM:    return "BUG: una funcion del commit no quedo en RAM";
        default:                 return "?";
    }
}

/*------------------------------------------------------------------------------
 * \u00bfEsa funcion vive en RAM?
 *
 * \u26a0 El bit 0 de un puntero a funcion de Thumb es el flag de modo, no parte de
 *   la direccion: hay que limpiarlo o toda direccion par pareceria impar.
 */
static bool prvEnRam( const void *pvFn )
{
    uint32_t ulDir = ( ( uint32_t ) pvFn ) & ~1UL;

    return ( ( ulDir >= SRAM1_BASE ) &&
             ( ulDir <  ( SRAM1_BASE + ( 256UL * 1024UL ) ) ) );
}

/*------------------------------------------------------------------------------*/
fw_commit_t fw_update_commit( uint32_t ulLargo )
{
    uint32_t ulPrimLo;
    uint8_t  ucRes;

    if( ( ulLargo == 0UL ) || ( ulLargo > FW_UPDATE_BANCO_BYTES ) )
    {
        ulLargo = FW_UPDATE_BANCO_BYTES;
    }
    ulLargo &= ~7UL;                      /* multiplo de 8: el L4 programa de a dw */

    /* El banco 2 tiene que tener ALGO. No se valida el CRC aca --el commit no
       sabe el largo de la imagen-- pero un banco borrado no tiene un SP, y con
       eso alcanza para no borrar el banco 1 al divino boton. */
    ulPrimLo = *( const volatile uint32_t * ) FW_BANCO2_BASE;
    if( ( ulPrimLo == 0xFFFFFFFFUL ) || ( ulPrimLo == 0UL ) )
    {
        return fwCMT_SIN_IMAGEN;
    }

    /* \u26d4 Desde aca y hasta el reset no puede ejecutarse una sola instruccion
       que viva en el banco 1. La tabla de vectores tambien esta ahi, asi que
       una interrupcion seria fatal: por eso las interrupciones se CORTAN, no
       se bajan de prioridad. */
    /* \u2b50\u2b50 LA RED QUE NO DEPENDE DE MI MEMORIA: si alguna de las tres funciones
       del commit no quedo en RAM, no se toca la flash. Es barato y convierte
       un bug fatal --banco 1 borrado y HardFault-- en un mensaje.

       \u26a0 Ya paso una vez: `static inline` no alcanza con -O0 (GCC no inlinea),
       y dos de las tres quedaron en flash. Compilo sin un warning. */
    if( !prvEnRam( ( const void * ) prvCommitEnRam ) ||
        !prvEnRam( ( const void * ) prvProgramarDw ) ||
        !prvEnRam( ( const void * ) prvEsperarFlash ) )
    {
        return fwCMT_NO_EN_RAM;
    }

    __disable_irq();
    __DSB();
    __ISB();

    ucRes = prvCommitEnRam( ulLargo );

    /* \u26a0 Solo se llega aca si la copia FALLO. El banco 1 quedo sin su primer
       doubleword, asi que un reset manda al bootloader de ST --que es
       recuperable-- en vez de ejecutar un firmware a medias. Se reponen las
       interrupciones para poder informarlo por la consola, que todavia vive en
       RAM... pero su codigo NO: el banco 1 esta borrado.

       \u2b50 Por eso NO se intenta imprimir: se resetea y que el bootloader hable.
          Intentar un xprintf aca seria saltar a flash borrada. */
    ( void ) ucRes;

    SCB->AIRCR = ( 0x5FAUL << SCB_AIRCR_VECTKEY_Pos ) | SCB_AIRCR_SYSRESETREQ_Msk;
    __DSB();
    for( ;; )
    {
    }
}

/*==============================================================================
 * LA MARCA DE "EN PRUEBA" (etapa 5)
 *============================================================================*/

/*------------------------------------------------------------------------------
 * ⚠ El checksum cubre TODO LO QUE HAY ANTES del campo `ucChecksum`, y eso se
 *   escribe con `offsetof` — NO con `sizeof - 1`.
 *
 *   Parecen lo mismo y sólo coinciden cuando el campo es literalmente el último
 *   byte, o sea cuando no hay relleno al final: una propiedad del compilador, no
 *   del diseño. Es el bug que en el paso 4 hacía que la FAT se declarara
 *   inválida en CADA arranque, y que encima acusaba a la pila del MCP79410.
 */
static uint8_t prvPruebaChecksum( const fw_prueba_t *pxM )
{
    const uint8_t *puc = ( const uint8_t * ) pxM;
    uint32_t       i;
    uint8_t        ucSum = 0U;

    for( i = 0UL; i < offsetof( fw_prueba_t, ucChecksum ); i++ )
    {
        ucSum = ( uint8_t ) ( ucSum + puc[ i ] );
    }

    return ucSum;
}

/*------------------------------------------------------------------------------*/
static bool prvPruebaGrabar( fw_prueba_t *pxM )
{
    pxM->ucChecksum = prvPruebaChecksum( pxM );

    return ( drv_eeprom_write( FW_PRUEBA_ADDR, ( const char * ) pxM,
                               sizeof( fw_prueba_t ) ) == ( int32_t ) sizeof( fw_prueba_t ) );
}

/*------------------------------------------------------------------------------*/
bool fw_prueba_leer( fw_prueba_t *pxM )
{
    fw_prueba_t xM;

    if( drv_eeprom_read( FW_PRUEBA_ADDR, ( char * ) &xM, sizeof( xM ) )
        != ( int32_t ) sizeof( xM ) )
    {
        return false;
    }

    if( ( xM.ulMagic != FW_PRUEBA_MAGIC ) ||
        ( xM.ucChecksum != prvPruebaChecksum( &xM ) ) )
    {
        return false;
    }

    /* ⚠ Que el string esté terminado no se asume: viene de la EEPROM, y un
       byte volado lo convertiría en una lectura fuera de rango. */
    xM.pcVersionPrevia[ FW_UPDATE_VER_LEN - 1U ] = '\0';

    if( pxM != NULL )
    {
        *pxM = xM;
    }

    return true;
}

/*------------------------------------------------------------------------------*/
bool fw_prueba_poner( const char *pcVersionPrevia )
{
    fw_prueba_t xM;
    uint32_t    i;

    ( void ) memset( &xM, 0, sizeof( xM ) );
    xM.ulMagic     = FW_PRUEBA_MAGIC;
    xM.ucArranques = 0U;

    if( pcVersionPrevia != NULL )
    {
        for( i = 0UL; ( i < ( FW_UPDATE_VER_LEN - 1U ) ) && ( pcVersionPrevia[ i ] != '\0' ); i++ )
        {
            xM.pcVersionPrevia[ i ] = pcVersionPrevia[ i ];
        }
    }

    return prvPruebaGrabar( &xM );
}

/*------------------------------------------------------------------------------*/
uint8_t fw_prueba_contar( void )
{
    fw_prueba_t xM;

    if( !fw_prueba_leer( &xM ) )
    {
        return 0U;
    }

    /* ⚠ No se deja desbordar: con el contador dando la vuelta, un equipo que
       nunca confirma volvería a "recién instalado" cada 256 arranques y el
       aviso se apagaría solo. */
    if( xM.ucArranques < 0xFFU )
    {
        xM.ucArranques++;
        ( void ) prvPruebaGrabar( &xM );
    }

    return xM.ucArranques;
}

/*------------------------------------------------------------------------------*/
void fw_prueba_confirmar( void )
{
    uint32_t ulCero = 0UL;

    if( !fw_prueba_leer( NULL ) )
    {
        return;                 /* idempotente: sin marca no hay nada que hacer */
    }

    /* ⭐ Alcanza con romper la magia: son 4 bytes contra 22, y es una sola
       escritura de página. El resto queda como basura que nadie va a leer,
       porque sin magia la marca no valida. */
    ( void ) drv_eeprom_write( FW_PRUEBA_ADDR, ( const char * ) &ulCero, sizeof( ulCero ) );
}

/*==============================================================================
 * ETAPA 6 — EL DISPARO AUTOMATICO AL ARRANCAR
 *
 * Ver `fw_update.h` para la secuencia y por que el orden es ese.
 *============================================================================*/
void fw_update_auto( void )
{
#if ( FW_UPDATE_AUTO == 1 )
    fw_info_t   xInfo;
    fw_result_t xRes;
    fw_banco2_t xB2;
    fw_commit_t xCmt;

    /*
     * ---- 1. examinar ----------------------------------------------------
     *
     * Lee el archivo ENTERO para verificar el CRC de la imagen: ~2 s con el
     * riel de la microSD encendido. Es el precio de no escribir la flash con
     * un archivo que nadie verifico, y se paga una sola vez por tarjeta
     * —despues el archivo queda renombrado y este camino sale por `fwNO_HAY`—.
     */
    xRes = fw_update_examinar( FW_UPDATE_ARCHIVO, &xInfo );

    /*
     * ⭐ EL CASO DE TODOS LOS ARRANQUES DE TODOS LOS EQUIPOS, y se CALLA.
     *
     * Sin tarjeta, o con una tarjeta que no trae firmware, no hay nada que
     * decir: el arranque ya imprime bastante y una linea mas por algo que
     * nunca va a pasar es ruido que tapa lo que si importa.
     */
    if( ( xRes == fwSIN_TARJETA ) || ( xRes == fwNO_HAY ) )
    {
        return;
    }

    /*
     * ⭐ MISMA VERSION: el archivo ya corre en el equipo, asi que se renombra.
     *
     * Es el caso que queda tras una actualizacion a mano —`fw grabar` + `fw
     * commit aplicar` dejan el archivo como estaba— y sin esto el equipo
     * pagaria los ~2 s de releer y verificar el CRC en CADA arranque, para
     * siempre, para llegar a la misma conclusion.
     *
     * ⭐ Y deja una sola regla, facil de explicar a un tecnico: **`FIRMWARE.OK`
     *    significa "este archivo ya es el que corre en el equipo"**.
     */
    if( xRes == fwMISMA_VERSION )
    {
        xprintf( "\r\nFW:: la tarjeta trae la version que ya corre (%s)\r\n", FW_VERSION );

        if( fs_sd_renombrar( FW_UPDATE_ARCHIVO, FW_UPDATE_APLICADO ) )
        {
            xprintf( "     renombrado a %s: no se vuelve a leer en cada arranque\r\n",
                     FW_UPDATE_APLICADO );
        }
        return;
    }

    /*
     * ⚠ CUALQUIER OTRA COSA SE DICE Y NO SE APLICA.
     *
     * Archivo corrupto, de otro producto, imagen con el CRC mal: el equipo
     * arranca normal y el banco 1 no se toca, pero **quedar callado seria lo
     * peor**. Un tecnico que dejo una tarjeta y se fue creyendo que el equipo
     * se iba a actualizar no tiene otra forma de enterarse — y el renglon
     * queda en el registro de eventos, que es lo que se lee despues.
     */
    if( xRes != fwHAY_NUEVO )
    {
        xprintf( "\r\nFW:: ⛔ hay un %s en la tarjeta y NO se puede aplicar: %s\r\n",
                 FW_UPDATE_ARCHIVO, fw_update_str( xRes ) );
        xprintf( "     el equipo arranca normal. Mirar con 'fw'\r\n" );
        eventos_log( "FW: %s NO aplicable: %s", FW_UPDATE_ARCHIVO, fw_update_str( xRes ) );
        return;
    }

    /* ---- 2. a partir de aca SI se va a escribir la flash ---------------- */
    xprintf( "\r\n" );
    xprintf( "FW:: ⭐ ACTUALIZACION AUTOMATICA DESDE LA microSD\r\n" );
    xprintf( "  corriendo  : %s\r\n", FW_VERSION );
    xprintf( "  la tarjeta : %s  (%lu bytes, CRC 0x%08lX verificado)\r\n",
             xInfo.pcVersion, ( unsigned long ) xInfo.ulLargo,
             ( unsigned long ) xInfo.ulCrcLeido );
    xprintf( "  ⚠ graba el banco 2 y RESETEA. No desconectar la alimentacion.\r\n\r\n" );

    /*
     * ⚠ LA PRORROGA DEL WATCHDOG, y es necesaria aunque el plazo sean 90 s.
     *
     * Grabar el banco 2 son ~13 s —borrar 126 paginas, escribir 250 KB de a
     * 64 bits y releerlos— y por dentro no hay ningun lugar donde reportar.
     * Entra con margen en los 90 s del plazo, pero depender de eso seria
     * depender de un numero que esta puesto para otra cosa: si manana el plazo
     * baja, esto se rompe por un motivo que no se parece a su causa. Es el
     * mismo criterio que `f_mkfs()` y el volcado de la ventana.
     */
    wdg_kick_largo( 60000UL );

    /* ---- 3. grabar el banco 2, y verificarlo releyendo la flash --------- */
    if( !fw_update_grabar( FW_UPDATE_ARCHIVO, &xInfo, &xRes ) )
    {
        xprintf( "\r\nFW:: ⛔ NO se pudo grabar el banco 2: %s\r\n", fw_update_str( xRes ) );
        xprintf( "     ⭐ el banco 1 NO se toco: el equipo sigue en %s\r\n", FW_VERSION );
        eventos_log( "FW: fallo el grabado del banco 2: %s", fw_update_str( xRes ) );
        return;
    }

    xprintf( "  ==> banco 2 escrito y VERIFICADO (CRC 0x%08lX releido de la flash)\r\n",
             ( unsigned long ) xInfo.ulCrcLeido );

    /* ---- 4. ¿parece una tabla de vectores? ------------------------------ */
    /*
     * ⚠ Avisa pero NO impide, igual que en el comando: el CRC ya probo que la
     * flash tiene lo mismo que el archivo, y lo que esto mira es si el archivo
     * parece un firmware. Rechazar por este chequeo seria peor —ya dio un
     * falso positivo medido en banco, ver `fw_update_estado_banco2()`—.
     */
    fw_update_estado_banco2( &xB2 );

    if( !xB2.bPlausible )
    {
        xprintf( "  ⚠ el banco 2 no parece una tabla de vectores:\r\n" );
        xprintf( "    SP 0x%08lX  reset 0x%08lX  (se aplica igual)\r\n",
                 ( unsigned long ) xB2.ulSpBanco2, ( unsigned long ) xB2.ulResetBanco2 );
    }

    /*
     * ---- 5. ⭐⭐ RENOMBRAR, Y SI FALLA NO SE COMMITEA --------------------
     *
     * Va ANTES del commit porque el commit **no vuelve**, y es obligatorio
     * porque es lo unico que corta el bucle de reprogramacion: la version se
     * compara contra la del HEADER, y nada garantiza que el header diga la
     * verdad sobre la imagen que trae. Con un archivo cuyo header miente
     * —`mkfw_casos.py` fabrica uno asi— la comparacion no se satisface nunca y
     * el equipo se reprogramaria en cada arranque. Ver `fs_sd_renombrar()`.
     *
     * ⭐ Abortar es barato y deja todo en un estado conocido: el banco 1
     * intacto, el banco 2 con la imagen ya verificada, y `fw commit aplicar`
     * termina el trabajo a mano.
     */
    if( !fs_sd_renombrar( FW_UPDATE_ARCHIVO, FW_UPDATE_APLICADO ) )
    {
        xprintf( "\r\nFW:: ⛔ NO se pudo renombrar %s a %s, asi que NO se aplica.\r\n",
                 FW_UPDATE_ARCHIVO, FW_UPDATE_APLICADO );
        xprintf( "     Sin el renombrado el equipo se reprogramaria en CADA arranque:\r\n" );
        xprintf( "     la version sale del header del archivo, que puede no decir la\r\n" );
        xprintf( "     verdad sobre la imagen. El banco 2 YA quedo escrito y verificado,\r\n" );
        xprintf( "     asi que para aplicarlo igual: 'fw commit aplicar'\r\n" );
        eventos_log( "FW: fallo el renombrado a %s: NO se aplico", FW_UPDATE_APLICADO );
        return;
    }

    xprintf( "  ==> %s renombrado a %s\r\n", FW_UPDATE_ARCHIVO, FW_UPDATE_APLICADO );

    /* ---- 6. la marca de "a prueba" y el registro, volcado --------------- */
    /*
     * ⚠ La marca se pone ANTES del commit y no despues, porque despues no hay
     * despues. Si se pone y el commit falla, el costo es un aviso de mas en el
     * arranque siguiente; al reves —commit hecho y marca sin poner— el
     * firmware nuevo quedaria SIN vigilancia, que es justo lo que la etapa 5
     * viene a evitar. La asimetria decide.
     */
    if( !fw_prueba_poner( FW_VERSION ) )
    {
        xprintf( "  ⚠ no se pudo dejar la marca de 'a prueba' en la EEPROM:\r\n" );
        xprintf( "    el firmware nuevo NO va a quedar vigilado. Se aplica igual.\r\n" );
    }

    /* ⭐ Volcado ANTES de tocar la flash: si el equipo no vuelve, esta linea es
       lo unico que va a quedar para saber que fue una actualizacion y no una
       falla espontanea. (El buffer vive en la SRAM2 y sobrevive al reset, pero
       la tarjeta esta encendida aca mismo: no hay razon para no escribirlo.) */
    eventos_log( "FW: AUTO %s -> %s, aplicando (SP 0x%08lX)",
                 FW_VERSION, xInfo.pcVersion, ( unsigned long ) xB2.ulSpBanco2 );
    ( void ) eventos_volcar();

    xprintf( "\r\n  aplicando... (la consola se corta aca)\r\n" );

    /* que el ultimo caracter salga por la UART antes de cortar las
       interrupciones: a 9600, un byte son 1042 us */
    vTaskDelay( pdMS_TO_TICKS( 200 ) );

    /* ---- 7. el commit. NO VUELVE si sale bien. -------------------------- */
    xCmt = fw_update_commit( 0UL );

    /* ⚠ Solo se llega aca con `fwCMT_SIN_IMAGEN` o `fwCMT_NO_EN_RAM`, que se
       detectan ANTES de tocar nada. Los demas errores resetean desde RAM,
       porque para entonces el codigo de esta funcion ya no existe. */
    xprintf( "\r\nFW:: ⛔ NO se aplico: %s\r\n", fw_update_commit_str( xCmt ) );
    eventos_log( "FW: AUTO fallo el commit: %s", fw_update_commit_str( xCmt ) );
#endif /* FW_UPDATE_AUTO */
}
