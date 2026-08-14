/*
 * drv_sd.c  -  ver drv_sd.h
 */

#include <string.h>

#include "drv_sd.h"
#include "pwr_lock.h"
#include "main.h"

extern SPI_HandleTypeDef hspi3;

/*------------------------------------------------------------------------------
 * Comandos de la norma SD en modo SPI. Los ACMD van precedidos de CMD55.
 *----------------------------------------------------------------------------*/
#define CMD0_GO_IDLE            0U
#define CMD1_SEND_OP_COND       1U      /* MMC                                */
#define CMD8_SEND_IF_COND       8U
#define CMD9_SEND_CSD           9U
#define CMD10_SEND_CID          10U
#define CMD16_SET_BLOCKLEN      16U
#define CMD17_READ_BLOCK        17U
#define CMD24_WRITE_BLOCK       24U
#define CMD55_APP_CMD           55U
#define CMD58_READ_OCR          58U
#define ACMD41_SD_SEND_OP_COND  41U

/* Respuesta R1: 0x00 es "listo", 0x01 es "en idle", y el bit 2 es "comando
   ilegal", que es como una tarjeta v1 contesta un CMD8. Con el bit 7 en 1 la
   respuesta no es válida (la línea está en reposo). */
#define R1_IDLE                 0x01U
#define R1_ILLEGAL_CMD          0x04U

#define TOKEN_INICIO_BLOQUE     0xFEU   /* precede a los datos de una lectura  */
#define RESP_DATO_ACEPTADO      0x05U   /* los 5 bits bajos de la respuesta    */

#define SPI_TIMEOUT_MS          500U

/* Techos de espera de la norma. El de ACMD41 es el que manda: una tarjeta lenta
   puede tardar cientos de ms en salir de idle. */
#define TOUT_ARRANQUE_MS        1000U
#define TOUT_RESPUESTA_BYTES    100U    /* bytes de 0xFF esperando un R1       */
#define TOUT_TOKEN_MS           200U
#define TOUT_BUSY_MS            500U    /* la tarjeta baja MISO mientras graba */

/* Pines del bus, todos juntos porque se mueven juntos entre función alternada y
   analógico. El CS va aparte: es un GPIO común. */
#define BUS_C_PINS      ( SD_SCK_Pin | SD_MISO_Pin | SD_MOSI_Pin )
#define BUS_C_PORT        SD_SCK_GPIO_Port      /* los tres en GPIOC */

/*
 * 512 bytes de 0xFF para transmitir mientras se lee.
 *
 * Es `const`, así que vive en FLASH y no gasta un byte de RAM — de los 977 KB
 * libres, 512 bytes no se discuten. Hace falta porque la norma exige que MOSI
 * quede en ALTO mientras el host lee: `HAL_SPI_Receive()` en full-duplex manda
 * lo que haya quedado en el registro de transmisión, y hay tarjetas que con eso
 * devuelven basura.
 */
static const uint8_t ucRelleno[ DRV_SD_SECTOR_BYTES ] = { [ 0 ... 511 ] = 0xFFU };

static bool      bRielOn   = false;
static sd_tipo_t eTipo     = sdTIPO_NINGUNA;
static bool      bPorBloque = false;    /* SDHC/SDXC: el argumento es el sector */

/*==============================================================================
 * Nivel más bajo: mover bytes por el SPI
 *============================================================================*/

static uint8_t prvXfer( uint8_t ucDato )
{
    uint8_t ucRx = 0xFFU;

    ( void ) HAL_SPI_TransmitReceive( &hspi3, &ucDato, &ucRx, 1U, SPI_TIMEOUT_MS );

    return ucRx;
}
//------------------------------------------------------------------------------
/*
 * Lee `ulLargo` bytes mandando 0xFF, o escribe `ulLargo` bytes descartando lo que
 * vuelve. Uno de los dos punteros puede ser NULL.
 *
 * Va por POLEO, con la HAL. A 7,5 MHz un sector son ~550 µs de CPU girando, que
 * no es gratis pero tampoco domina: el tiempo de una escritura lo pone el
 * programado interno de la tarjeta, que son milisegundos. **Pasar el bloque a
 * DMA es la optimización obvia y está anotada como pendiente**; no se hace ahora
 * porque en el bring-up conviene que el camino de datos sea el más simple posible.
 */
static void prvXferBloque( const uint8_t *pucTx, uint8_t *pucRx, uint16_t usLargo )
{
    if( pucRx == NULL )
    {
        ( void ) HAL_SPI_Transmit( &hspi3, ( uint8_t * ) pucTx, usLargo, SPI_TIMEOUT_MS );
    }
    else
    {
        ( void ) HAL_SPI_TransmitReceive( &hspi3,
                                          ( uint8_t * ) ( ( pucTx != NULL ) ? pucTx : ucRelleno ),
                                          pucRx, usLargo, SPI_TIMEOUT_MS );
    }
}
//------------------------------------------------------------------------------
static void prvCsBajo( void )
{
    HAL_GPIO_WritePin( SD_SS_GPIO_Port, SD_SS_Pin, GPIO_PIN_RESET );
}

static void prvCsAlto( void )
{
    HAL_GPIO_WritePin( SD_SS_GPIO_Port, SD_SS_Pin, GPIO_PIN_SET );

    /* Un byte más con el CS ya alto: la tarjeta necesita ese reloj extra para
       soltar la línea de datos. Sin esto, la transacción siguiente puede empezar
       leyendo el último bit de la anterior. */
    ( void ) prvXfer( 0xFFU );
}
//------------------------------------------------------------------------------
static void prvVelocidad( uint32_t ulPrescaler )
{
    __HAL_SPI_DISABLE( &hspi3 );
    MODIFY_REG( hspi3.Instance->CR1, SPI_CR1_BR, ulPrescaler );
    hspi3.Init.BaudRatePrescaler = ulPrescaler;
    __HAL_SPI_ENABLE( &hspi3 );
}
//------------------------------------------------------------------------------
/*
 * Estado de los pines del bus. Ver la explicación del back-powering en el header:
 * con la tarjeta sin alimentar, un pin en alto la alimenta por los diodos de
 * protección y el corte de energía deja de cortar.
 */
static void prvPinesBus( bool bActivos )
{
    GPIO_InitTypeDef xGpio = { 0 };

    if( bActivos )
    {
        xGpio.Pin       = BUS_C_PINS;
        xGpio.Mode      = GPIO_MODE_AF_PP;
        xGpio.Pull      = GPIO_NOPULL;
        xGpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        xGpio.Alternate = GPIO_AF6_SPI3;
        HAL_GPIO_Init( BUS_C_PORT, &xGpio );

        /* El CS arranca alto: inactivo. */
        HAL_GPIO_WritePin( SD_SS_GPIO_Port, SD_SS_Pin, GPIO_PIN_SET );

        xGpio.Pin   = SD_SS_Pin;
        xGpio.Mode  = GPIO_MODE_OUTPUT_PP;
        xGpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        HAL_GPIO_Init( SD_SS_GPIO_Port, &xGpio );
    }
    else
    {
        xGpio.Mode = GPIO_MODE_ANALOG;
        xGpio.Pull = GPIO_NOPULL;

        xGpio.Pin = BUS_C_PINS;
        HAL_GPIO_Init( BUS_C_PORT, &xGpio );

        xGpio.Pin = SD_SS_Pin;
        HAL_GPIO_Init( SD_SS_GPIO_Port, &xGpio );
    }
}

/*==============================================================================
 * Protocolo SD sobre SPI
 *============================================================================*/

/*
 * CRC7 del encabezado del comando.
 *
 * En modo SPI la tarjeta ignora el CRC de casi todos los comandos, pero NO de
 * CMD0 ni de CMD8 —que se mandan antes de que el chequeo se apague—, así que hay
 * que calcularlo igual. Se calcula siempre y no con las dos constantes de
 * siempre (0x95 y 0x87): así el driver sigue andando si alguna vez se habilita
 * el CRC con CMD59.
 */
static uint8_t prvCrc7( const uint8_t *pucDatos, uint32_t ulLargo )
{
    uint8_t ucCrc = 0U;

    for( uint32_t i = 0U; i < ulLargo; i++ )
    {
        uint8_t ucByte = pucDatos[ i ];

        for( uint32_t b = 0U; b < 8U; b++ )
        {
            ucCrc <<= 1;

            if( ( ( ucByte & 0x80U ) ^ ( ucCrc & 0x80U ) ) != 0U )
            {
                ucCrc ^= 0x09U;         /* x^7 + x^3 + 1 */
            }

            ucByte <<= 1;
        }
    }

    return ( uint8_t ) ( ( ucCrc << 1 ) | 0x01U );
}
//------------------------------------------------------------------------------
/*
 * Manda un comando y devuelve el R1. `0xFF` significa que la tarjeta no contestó.
 *
 * Deja el CS BAJO: los comandos que traen más datos detrás (R3/R7, un bloque)
 * los siguen leyendo desde afuera. Quien llama tiene que cerrar con prvCsAlto().
 */
static uint8_t prvComando( uint8_t ucCmd, uint32_t ulArg )
{
    uint8_t pucTrama[ 6 ];

    pucTrama[ 0 ] = ( uint8_t ) ( 0x40U | ( ucCmd & 0x3FU ) );
    pucTrama[ 1 ] = ( uint8_t ) ( ulArg >> 24 );
    pucTrama[ 2 ] = ( uint8_t ) ( ulArg >> 16 );
    pucTrama[ 3 ] = ( uint8_t ) ( ulArg >> 8  );
    pucTrama[ 4 ] = ( uint8_t ) ( ulArg       );
    pucTrama[ 5 ] = prvCrc7( pucTrama, 5U );

    prvXferBloque( pucTrama, NULL, 6U );

    /* Un byte de gracia: CMD12 devuelve un byte de basura antes del R1. */
    if( ucCmd == 12U )
    {
        ( void ) prvXfer( 0xFFU );
    }

    /* El R1 llega dentro de los próximos bytes; mientras tanto la línea está en
       reposo (0xFF), que se reconoce porque tiene el bit 7 en 1. */
    for( uint32_t i = 0U; i < TOUT_RESPUESTA_BYTES; i++ )
    {
        uint8_t ucR1 = prvXfer( 0xFFU );

        if( ( ucR1 & 0x80U ) == 0U )
        {
            return ucR1;
        }
    }

    return 0xFFU;
}
//------------------------------------------------------------------------------
static uint8_t prvComandoApp( uint8_t ucCmd, uint32_t ulArg )
{
    ( void ) prvComando( CMD55_APP_CMD, 0UL );

    return prvComando( ucCmd, ulArg );
}
//------------------------------------------------------------------------------
/* Espera el token que precede a un bloque de datos. */
static bool prvEsperarToken( void )
{
    TickType_t xInicio = xTaskGetTickCount();

    while( ( xTaskGetTickCount() - xInicio ) < pdMS_TO_TICKS( TOUT_TOKEN_MS ) )
    {
        uint8_t ucTok = prvXfer( 0xFFU );

        if( ucTok == TOKEN_INICIO_BLOQUE )
        {
            return true;
        }

        /* Cualquier cosa que no sea 0xFF y no sea el token es un token de ERROR
           (los 5 bits bajos dicen qué pasó). No tiene sentido seguir esperando. */
        if( ucTok != 0xFFU )
        {
            return false;
        }
    }

    return false;
}
//------------------------------------------------------------------------------
/*
 * Espera a que la tarjeta suelte la línea después de una escritura.
 *
 * Mientras graba mantiene MISO en 0, y puede tardar bastante: el peor caso de la
 * norma son 250 ms, y una tarjeta que decide hacer recolección de basura en ese
 * momento se acerca. NO se puede saltear: mandarle el comando siguiente mientras
 * está ocupada lo pierde.
 */
static bool prvEsperarLibre( void )
{
    TickType_t xInicio = xTaskGetTickCount();

    while( ( xTaskGetTickCount() - xInicio ) < pdMS_TO_TICKS( TOUT_BUSY_MS ) )
    {
        if( prvXfer( 0xFFU ) == 0xFFU )
        {
            return true;
        }
    }

    return false;
}
//------------------------------------------------------------------------------
/* Lee un bloque de `usLargo` bytes precedido por su token, y descarta el CRC16
   —en modo SPI el chequeo viene apagado, así que los dos bytes están pero no
   dicen nada—. */
static bool prvLeerBloque( uint8_t *pucBuffer, uint16_t usLargo )
{
    if( prvEsperarToken() == false )
    {
        return false;
    }

    prvXferBloque( NULL, pucBuffer, usLargo );

    ( void ) prvXfer( 0xFFU );      /* CRC16, byte alto */
    ( void ) prvXfer( 0xFFU );      /* CRC16, byte bajo */

    return true;
}

/*==============================================================================
 * API pública
 *============================================================================*/

bool drv_sd_init( void )
{
    /* Explícito, aunque el pull-up de 100 K ya la deja apagada: el firmware no
       debe depender de una resistencia para un estado que le corresponde. */
    drv_sd_power( false );

    return true;
}
//------------------------------------------------------------------------------
void drv_sd_power( bool bOn )
{
    if( bOn )
    {
        /*
         * Sin el candado, el tickless entra en Stop 2 en cuanto la tarea se
         * bloquee y SPI3 —que come de PCLK1— se queda sin reloj en medio de una
         * transferencia. Con la SD eso es peor que perder un byte: puede dejar un
         * sector a medio escribir.
         */
        pwr_lock_acquire( pwrLOCK_SD );

        /* ⚠ CERO PRENDE. Ver la explicación del SI2301 en el header. */
        HAL_GPIO_WritePin( EN_PWR_SD_GPIO_Port, EN_PWR_SD_Pin, GPIO_PIN_RESET );

        /* Primero la energía, después los pines: al revés, los pines quedarían
           manejando una tarjeta todavía sin alimentar, que es exactamente lo que
           se quiere evitar. */
        vTaskDelay( pdMS_TO_TICKS( 10 ) );      /* rampa de la alimentación */
        prvPinesBus( true );
        prvVelocidad( SPI_BAUDRATEPRESCALER_256 );
    }
    else
    {
        prvPinesBus( false );
        HAL_GPIO_WritePin( EN_PWR_SD_GPIO_Port, EN_PWR_SD_Pin, GPIO_PIN_SET );

        pwr_lock_release( pwrLOCK_SD );

        eTipo      = sdTIPO_NINGUNA;
        bPorBloque = false;
    }

    bRielOn = bOn;
}
//------------------------------------------------------------------------------
bool drv_sd_power_estado( void )
{
    return bRielOn;
}
//------------------------------------------------------------------------------
bool drv_sd_presente( void )
{
    /* El contacto va a GND con la tarjeta puesta: pin en 0 = presente. */
    return ( HAL_GPIO_ReadPin( SD_DET_GPIO_Port, SD_DET_Pin ) == GPIO_PIN_RESET );
}
//------------------------------------------------------------------------------
bool drv_sd_arrancar( void )
{
    uint8_t  ucR1  = 0U;
    uint8_t  pucR7[ 4 ] = { 0 };
    bool     bOk   = false;

    eTipo      = sdTIPO_NINGUNA;
    bPorBloque = false;

    if( ( bRielOn == false ) || ( drv_sd_presente() == false ) )
    {
        return false;
    }

    prvVelocidad( SPI_BAUDRATEPRESCALER_256 );

    /*
     * Los 74 clocks de la norma, CON EL CS ALTO. Es lo que le dice a la tarjeta
     * que arranque en modo SPI en vez de en modo SD nativo, y si se saltea, la
     * tarjeta no contesta el CMD0 y todo lo demás falla sin más pista que eso.
     */
    prvCsAlto();
    for( uint32_t i = 0U; i < 10U; i++ )
    {
        ( void ) prvXfer( 0xFFU );
    }

    prvCsBajo();

    /* CMD0: a idle. Se reintenta porque algunas tarjetas necesitan más de un
       intento después de un corte de alimentación. */
    for( uint32_t i = 0U; i < 10U; i++ )
    {
        ucR1 = prvComando( CMD0_GO_IDLE, 0UL );

        if( ucR1 == R1_IDLE )
        {
            break;
        }
    }

    if( ucR1 != R1_IDLE )
    {
        goto salir;
    }

    /*
     * CMD8: pregunta si soporta el rango de tensión y hace de discriminador de
     * versión. El argumento 0x1AA es "2,7-3,6 V" más un patrón que la tarjeta
     * tiene que devolver tal cual.
     *
     *   contesta bien           -> v2 o posterior
     *   contesta comando ilegal -> v1 o MMC
     */
    ucR1 = prvComando( CMD8_SEND_IF_COND, 0x000001AAUL );

    if( ( ucR1 & R1_ILLEGAL_CMD ) == 0U )
    {
        prvXferBloque( NULL, pucR7, 4U );        /* el resto del R7 */

        if( ( pucR7[ 2 ] != 0x01U ) || ( pucR7[ 3 ] != 0xAAU ) )
        {
            goto salir;                          /* no soporta la tensión */
        }

        /* ACMD41 con HCS en 1: además de arrancar, pregunta si es de alta
           capacidad. Puede tardar cientos de ms. */
        TickType_t xInicio = xTaskGetTickCount();

        do
        {
            ucR1 = prvComandoApp( ACMD41_SD_SEND_OP_COND, 0x40000000UL );

        } while( ( ucR1 != 0U ) &&
                 ( ( xTaskGetTickCount() - xInicio ) < pdMS_TO_TICKS( TOUT_ARRANQUE_MS ) ) );

        if( ucR1 != 0U )
        {
            goto salir;
        }

        /* CMD58 lee el OCR, y su bit 30 (CCS) dice si direcciona por bloque. */
        if( prvComando( CMD58_READ_OCR, 0UL ) != 0U )
        {
            goto salir;
        }

        prvXferBloque( NULL, pucR7, 4U );

        bPorBloque = ( ( pucR7[ 0 ] & 0x40U ) != 0U );
        eTipo      = bPorBloque ? sdTIPO_SDHC : sdTIPO_SDV2;
    }
    else
    {
        /* v1 o MMC: se prueba primero como SD y, si rechaza el ACMD41, como MMC. */
        TickType_t xInicio = xTaskGetTickCount();
        uint8_t    ucCmd   = ACMD41_SD_SEND_OP_COND;

        eTipo = sdTIPO_SDV1;

        if( ( prvComandoApp( ACMD41_SD_SEND_OP_COND, 0UL ) & R1_ILLEGAL_CMD ) != 0U )
        {
            ucCmd = CMD1_SEND_OP_COND;
            eTipo = sdTIPO_MMC;
        }

        do
        {
            ucR1 = ( ucCmd == ACMD41_SD_SEND_OP_COND ) ?
                   prvComandoApp( ucCmd, 0UL ) : prvComando( ucCmd, 0UL );

        } while( ( ucR1 != 0U ) &&
                 ( ( xTaskGetTickCount() - xInicio ) < pdMS_TO_TICKS( TOUT_ARRANQUE_MS ) ) );

        if( ucR1 != 0U )
        {
            eTipo = sdTIPO_NINGUNA;
            goto salir;
        }
    }

    /* Las que direccionan por byte pueden tener otro tamaño de bloque; las SDHC
       lo tienen fijo en 512 y rechazan el comando. */
    if( bPorBloque == false )
    {
        if( prvComando( CMD16_SET_BLOCKLEN, DRV_SD_SECTOR_BYTES ) != 0U )
        {
            eTipo = sdTIPO_NINGUNA;
            goto salir;
        }
    }

    bOk = true;

salir:

    prvCsAlto();

    /* Recién ahora se sube el reloj: todo lo de arriba tiene que ir a menos de
       400 kHz por norma. */
    if( bOk )
    {
        prvVelocidad( DRV_SD_BR_RAPIDO );
    }

    return bOk;
}
//------------------------------------------------------------------------------
sd_tipo_t drv_sd_tipo( void )
{
    return eTipo;
}
//------------------------------------------------------------------------------
const char *drv_sd_tipo_texto( void )
{
    switch( eTipo )
    {
        case sdTIPO_MMC:   return "MMC";
        case sdTIPO_SDV1:  return "SD v1";
        case sdTIPO_SDV2:  return "SD v2";
        case sdTIPO_SDHC:  return "SDHC/SDXC";
        default:           return "sin inicializar";
    }
}
//------------------------------------------------------------------------------
/* Lee un registro de 16 bytes (CID o CSD), que viene como un bloque de datos. */
static bool prvLeerRegistro( uint8_t ucCmd, uint8_t *pucBuffer16 )
{
    bool bOk = false;

    if( eTipo == sdTIPO_NINGUNA )
    {
        return false;
    }

    prvCsBajo();

    if( prvComando( ucCmd, 0UL ) == 0U )
    {
        bOk = prvLeerBloque( pucBuffer16, 16U );
    }

    prvCsAlto();

    return bOk;
}
//------------------------------------------------------------------------------
bool drv_sd_cid( uint8_t *pucBuffer16 )
{
    return prvLeerRegistro( CMD10_SEND_CID, pucBuffer16 );
}
//------------------------------------------------------------------------------
bool drv_sd_csd( uint8_t *pucBuffer16 )
{
    return prvLeerRegistro( CMD9_SEND_CSD, pucBuffer16 );
}
//------------------------------------------------------------------------------
uint32_t drv_sd_sectores( void )
{
    uint8_t pucCsd[ 16 ] = { 0 };

    if( drv_sd_csd( pucCsd ) == false )
    {
        return 0UL;
    }

    /*
     * Hay dos formatos de CSD y no se parecen en nada. Los dos primeros bits
     * dicen cuál es.
     */
    if( ( pucCsd[ 0 ] >> 6 ) == 1U )
    {
        /* CSD v2 (SDHC/SDXC): C_SIZE son 22 bits y la capacidad sale directa. */
        uint32_t ulCSize = ( ( ( uint32_t ) ( pucCsd[ 7 ] & 0x3FU ) ) << 16 ) |
                           ( ( ( uint32_t )   pucCsd[ 8 ]          ) << 8  ) |
                           (   ( uint32_t )   pucCsd[ 9 ]                  );

        return ( ulCSize + 1UL ) * 1024UL;      /* cada unidad son 512 KB */
    }

    /* CSD v1: capacidad = (C_SIZE+1) * 2^(C_SIZE_MULT+2) * 2^READ_BL_LEN */
    uint32_t ulCSize = ( ( ( uint32_t ) ( pucCsd[ 6 ] & 0x03U ) ) << 10 ) |
                       ( ( ( uint32_t )   pucCsd[ 7 ]          ) << 2  ) |
                       ( ( ( uint32_t )   pucCsd[ 8 ]          ) >> 6  );

    uint32_t ulMult   = ( ( ( uint32_t ) ( pucCsd[ 9 ] & 0x03U ) ) << 1 ) |
                        ( ( ( uint32_t )   pucCsd[ 10 ]         ) >> 7 );

    uint32_t ulBlLen  = ( uint32_t ) ( pucCsd[ 5 ] & 0x0FU );

    return ( ulCSize + 1UL ) * ( 1UL << ( ulMult + 2UL ) ) * ( 1UL << ulBlLen ) / DRV_SD_SECTOR_BYTES;
}
//------------------------------------------------------------------------------
/* Las de alta capacidad direccionan por número de sector; las viejas, por byte. */
static uint32_t prvDireccion( uint32_t ulSector )
{
    return bPorBloque ? ulSector : ( ulSector * DRV_SD_SECTOR_BYTES );
}
//------------------------------------------------------------------------------
bool drv_sd_leer_sector( uint32_t ulSector, uint8_t *pucBuffer )
{
    bool bOk = false;

    if( eTipo == sdTIPO_NINGUNA )
    {
        return false;
    }

    prvCsBajo();

    if( prvComando( CMD17_READ_BLOCK, prvDireccion( ulSector ) ) == 0U )
    {
        bOk = prvLeerBloque( pucBuffer, DRV_SD_SECTOR_BYTES );
    }

    prvCsAlto();

    return bOk;
}
//------------------------------------------------------------------------------
bool drv_sd_escribir_sector( uint32_t ulSector, const uint8_t *pucBuffer )
{
    bool bOk = false;

    if( eTipo == sdTIPO_NINGUNA )
    {
        return false;
    }

    prvCsBajo();

    if( prvComando( CMD24_WRITE_BLOCK, prvDireccion( ulSector ) ) == 0U )
    {
        ( void ) prvXfer( 0xFFU );                  /* un byte antes del token */
        ( void ) prvXfer( TOKEN_INICIO_BLOQUE );

        prvXferBloque( pucBuffer, NULL, DRV_SD_SECTOR_BYTES );

        ( void ) prvXfer( 0xFFU );                  /* CRC16, ignorado */
        ( void ) prvXfer( 0xFFU );

        /*
         * La respuesta de dato dice si el bloque entró. Sólo valen los 5 bits
         * bajos; los de arriba son basura de la línea.
         */
        if( ( prvXfer( 0xFFU ) & 0x1FU ) == RESP_DATO_ACEPTADO )
        {
            /* Y recién cuando suelta la línea el dato está realmente grabado.
               Volver antes daría por buena una escritura que todavía no ocurrió. */
            bOk = prvEsperarLibre();
        }
    }

    prvCsAlto();

    return bOk;
}
//------------------------------------------------------------------------------
