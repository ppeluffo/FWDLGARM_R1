/*
 * modbus.c
 *
 * De bytes crudos a una magnitud. El porqué está en el header.
 */

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "modbus.h"
#include "frtos-io.h"

//------------------------------------------------------------------------------
/*
 * La unión de siempre: los bytes entran por `raw` y salen interpretados por el
 * campo que corresponda. **`raw[0]` es el byte MENOS significativo**, porque el
 * Cortex-M4 es little endian — igual que el AVR, así que las tablas de codec se
 * portan sin tocar un índice.
 */
typedef union {
    uint8_t  raw[ 4 ];
    uint16_t u16;
    int16_t  i16;
    uint32_t u32;
    int32_t  i32;
    float    f;
} mb_valor_t;

//------------------------------------------------------------------------------
/*
 * ⭐ LOS CUATRO CODECS, COMO TABLA
 *
 * El AVR tiene cuatro funciones de ~20 líneas cada una que hacen asignaciones
 * byte a byte. Son exactamente una permutación, y el propio nombre del codec
 * **es** la permutación: `C3210` manda el byte 0 del payload a `raw[3]`, el 1 a
 * `raw[2]`, y así.
 *
 * `raw[ pucDest[ codec ][ i ] ] = payload[ i ]`
 *
 * ⚠ **Las dos tablas son distintas y no se pueden deducir una de la otra.**
 * Con 16 bits sólo hay dos permutaciones posibles, así que los cuatro codecs
 * colapsan de a pares — y el AVR elige `C3210 == C1032` (invertido) y
 * `C2301 == C0123` (directo). Eso **es contrato con los dispositivos en campo**:
 * si acá se "arregla" para que el 16 siga la lógica del 32, un caudalímetro
 * configurado con `C3210` empieza a leer al revés. Copiado literal de
 * `pv_decoder_f3_c*()`.
 */
static const uint8_t pucDest16[ 4 ][ 2 ] = {
    /* CFG_MB_C0123 */ { 0U, 1U },
    /* CFG_MB_C1032 */ { 1U, 0U },
    /* CFG_MB_C3210 */ { 1U, 0U },
    /* CFG_MB_C2301 */ { 0U, 1U },
};

static const uint8_t pucDest32[ 4 ][ 4 ] = {
    /* CFG_MB_C0123 */ { 0U, 1U, 2U, 3U },
    /* CFG_MB_C1032 */ { 1U, 0U, 3U, 2U },
    /* CFG_MB_C3210 */ { 3U, 2U, 1U, 0U },
    /* CFG_MB_C2301 */ { 2U, 3U, 0U, 1U },
};

/*
 * ⭐ `pow(10, n)` era una llamada de DOBLE precisión para elegir entre diez
 * números conocidos: arrastra la librería matemática entera y tarda cientos de
 * ciclos. La tabla cubre el rango que admite la configuración.
 */
static const float pfPot10[ 10 ] = {
    1.0f, 10.0f, 100.0f, 1000.0f, 10000.0f,
    100000.0f, 1000000.0f, 10000000.0f, 100000000.0f, 1000000000.0f
};

static bool prvEsDe32( cfg_modbus_tipo_t eTipo )
{
    return ( eTipo == CFG_MB_U32 ) || ( eTipo == CFG_MB_I32 ) ||
           ( eTipo == CFG_MB_FLOAT );
}
//------------------------------------------------------------------------------
bool modbus_leer_canal( const cfg_modbus_canal_t *pxCanal, float *pfValor,
                        mb_result_t *peRes )
{
    uint8_t     pucPayload[ DRV_MODBUS_MAX_PAYLOAD ];
    uint8_t     ucLargo = 0U;
    mb_result_t eRes    = mbPARAMETRO;
    mb_valor_t  xValor;
    uint8_t     i;

    if( peRes != NULL )
    {
        *peRes = mbPARAMETRO;
    }

    if( ( pxCanal == NULL ) || ( pfValor == NULL ) )
    {
        return false;
    }

    *pfValor = 0.0f;

    /*
     * ⚠ Cuántos bytes hacen falta lo dice el TIPO, no `nro_regs`.
     *
     * Los dos campos son configurables por separado y pueden contradecirse: un
     * canal `float` con `nro_regs = 1` trae 2 bytes donde el decodificador
     * espera 4. El AVR leería los dos bytes que siguen en su buffer —que son el
     * CRC— y devolvería un float perfectamente plausible. Acá se rechaza.
     */
    uint8_t ucNecesarios = prvEsDe32( pxCanal->eTipo ) ? 4U : 2U;

    if( ( pxCanal->ucDivisorP10 >= 10U ) ||
        ( ( uint16_t ) pxCanal->ucNroRegs * 2U < ucNecesarios ) )
    {
        return false;
    }

    /* ---- La transacción, con sus reintentos ---- */
    for( i = 0U; i < MODBUS_INTENTOS; i++ )
    {
        if( i > 0U )
        {
            vTaskDelay( pdMS_TO_TICKS( MODBUS_MS_ENTRE_INTENTOS ) );
        }

        eRes = drv_modbus_leer( pxCanal->ucSlaveAddress, pxCanal->ucFcode,
                                pxCanal->usRegAddress, pxCanal->ucNroRegs,
                                pucPayload, &ucLargo );

        if( eRes == mbOK )
        {
            break;
        }

        /*
         * ⚠ Hay dos fallas que NO se reintentan, porque reintentarlas es perder
         * el tiempo con toda seguridad: una **excepción** es el esclavo diciendo
         * que ese registro no existe —va a contestar lo mismo las tres veces— y
         * un **parámetro inválido** ni siquiera llegó a transmitirse. Lo que sí
         * vale reintentar es lo que depende del bus: timeout, CRC, colisión.
         */
        if( ( eRes == mbEXCEPCION ) || ( eRes == mbPARAMETRO ) ||
            ( eRes == mbBUS_APAGADO ) )
        {
            break;
        }
    }

    if( peRes != NULL )
    {
        *peRes = eRes;
    }

    if( eRes != mbOK )
    {
        return false;
    }

    if( ucLargo < ucNecesarios )
    {
        if( peRes != NULL )
        {
            *peRes = mbLARGO_INESPERADO;
        }
        return false;
    }

    /* ---- El codec: reordenar los bytes ---- */
    memset( xValor.raw, 0, sizeof( xValor.raw ) );

    if( prvEsDe32( pxCanal->eTipo ) )
    {
        for( i = 0U; i < 4U; i++ )
        {
            xValor.raw[ pucDest32[ pxCanal->eCodec ][ i ] ] = pucPayload[ i ];
        }
    }
    else
    {
        for( i = 0U; i < 2U; i++ )
        {
            xValor.raw[ pucDest16[ pxCanal->eCodec ][ i ] ] = pucPayload[ i ];
        }
    }

    /* ---- El tipo y el divisor ---- */
    float fMag;

    switch( pxCanal->eTipo )
    {
        case CFG_MB_FLOAT:
            /*
             * ⚠ El float NO se divide, y es así en el AVR: un dispositivo que
             * entrega punto flotante ya entrega la magnitud. `divisor_p10`
             * existe para convertir un entero en magnitud.
             */
            *pfValor = xValor.f;
            return true;

        case CFG_MB_I16: fMag = ( float ) xValor.i16; break;
        case CFG_MB_U16: fMag = ( float ) xValor.u16; break;
        case CFG_MB_I32: fMag = ( float ) xValor.i32; break;
        case CFG_MB_U32: fMag = ( float ) xValor.u32; break;

        default:
            return false;
    }

    *pfValor = fMag / pfPot10[ pxCanal->ucDivisorP10 ];

    return true;
}
//------------------------------------------------------------------------------
