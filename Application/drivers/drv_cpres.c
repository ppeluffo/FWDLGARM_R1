/*
 * drv_cpres.c  -  ver drv_cpres.h
 */

#include "FreeRTOS.h"
#include "task.h"

#include "drv_cpres.h"
#include "drv_rs485.h"
#include "frtos-io.h"

//------------------------------------------------------------------------------
const char *drv_cpres_cmd_str( cpres_cmd_t eCmd )
{
    switch( eCmd )
    {
        case cpresCMD_ABRIR_V0:          return "abrir V0";
        case cpresCMD_CERRAR_V0:         return "cerrar V0";
        case cpresCMD_ABRIR_V1:          return "abrir V1";
        case cpresCMD_CERRAR_V1:         return "cerrar V1";
        case cpresCMD_CONSIGNA_DIURNA:   return "consigna DIURNA";
        case cpresCMD_CONSIGNA_NOCTURNA: return "consigna NOCTURNA";
        default:                         return "?";
    }
}
//------------------------------------------------------------------------------
bool drv_cpres_status_idle( uint16_t usStatus )
{
    return ( ( usStatus & ( 1U << DRV_CPRES_BIT_RUN ) ) == 0U );
}
//------------------------------------------------------------------------------
/*
 * Los bits de posición: b1b0 = V0, b3b2 = V1.
 *
 * ⚠ `2` y `3` son **desconocido**, y no es un caso raro: es lo que informa el
 * dispositivo cada vez que se lo enciende, porque al perder alimentación olvida
 * dónde dejó las válvulas. Sólo sirve para diagnóstico.
 */
static const char *prvPosStr( uint16_t usStatus, uint8_t ucCorrimiento )
{
    switch( ( usStatus >> ucCorrimiento ) & 0x03U )
    {
        case 0U:  return "abierta";
        case 1U:  return "cerrada";
        default:  return "desconocida";
    }
}
//------------------------------------------------------------------------------
const char *drv_cpres_status_v0( uint16_t usStatus )
{
    return prvPosStr( usStatus, 0U );
}
//------------------------------------------------------------------------------
const char *drv_cpres_status_v1( uint16_t usStatus )
{
    return prvPosStr( usStatus, 2U );
}
//------------------------------------------------------------------------------
mb_result_t drv_cpres_leer_status( uint16_t *pusStatus )
{
    uint8_t     pucPayload[ 2 ];
    uint8_t     ucLargo = 0U;
    mb_result_t eRes;

    if( pusStatus == NULL )
    {
        return mbPARAMETRO;
    }

    *pusStatus = 0U;

    eRes = drv_modbus_leer( DRV_CPRES_SLAVE, 3U, DRV_CPRES_REG, 1U,
                            pucPayload, &ucLargo );

    if( ( eRes == mbOK ) && ( ucLargo >= 2U ) )
    {
        /* Big endian, que es como Modbus manda los registros: no hace falta
           codec porque acá no hay nada configurable. */
        *pusStatus = ( ( uint16_t ) pucPayload[ 0 ] << 8 ) | pucPayload[ 1 ];
    }

    return eRes;
}
//------------------------------------------------------------------------------
/*
 * Espera a que el dispositivo esté IDLE, hasta `DRV_CPRES_INTENTOS_IDLE` veces.
 *
 * ⚠ **Un `false` acá NO distingue "está ocupado" de "no contesta"**, y los dos
 * casos importan de forma distinta, así que se dicen por separado en la consola:
 * ocupado es el dispositivo trabajando —quizás una orden anterior todavía en
 * curso— y sin respuesta es el enlace.
 */
static bool prvEsperarIdle( const char *pcCuando )
{
    uint8_t     i;
    uint16_t    usStatus = 0U;
    mb_result_t eRes     = mbSIN_RESPUESTA;

    for( i = 0U; i < DRV_CPRES_INTENTOS_IDLE; i++ )
    {
        if( i > 0U )
        {
            vTaskDelay( pdMS_TO_TICKS( DRV_CPRES_MS_ENTRE_IDLE ) );
        }

        eRes = drv_cpres_leer_status( &usStatus );

        if( eRes != mbOK )
        {
            continue;
        }

        if( drv_cpres_status_idle( usStatus ) )
        {
            return true;
        }

        xprintf( "CPRES:: %s: el dispositivo esta TRABAJANDO (status 0x%02X)\r\n",
                 pcCuando, ( unsigned ) usStatus );
    }

    if( eRes != mbOK )
    {
        xprintf( "CPRES:: %s: no contesta (%s)\r\n", pcCuando,
                 drv_modbus_error_str( eRes ) );
    }
    else
    {
        xprintf( "CPRES:: %s: sigue ocupado despues de %u intentos\r\n", pcCuando,
                 ( unsigned ) DRV_CPRES_INTENTOS_IDLE );
    }

    return false;
}
//------------------------------------------------------------------------------
bool drv_cpres_comando( cpres_cmd_t eCmd )
{
    bool bOk = false;

    if( ( eCmd == cpresCMD_NINGUNO ) || ( eCmd > cpresCMD_CONSIGNA_NOCTURNA ) )
    {
        return false;
    }

    xprintf( "CPRES:: %s\r\n", drv_cpres_cmd_str( eCmd ) );

    /* ---- Energía ---- */
    drv_rs485_power( rs485RAIL_CPRES, true );
    drv_rs485_power( rs485RAIL_BUS,   true );
    /* Un segundo: lo único que hace falta para que conteste. Los 10 s extra que
       espera el AVR acá eran precaución heredada — ver el header. */
    vTaskDelay( pdMS_TO_TICKS( DRV_CPRES_MS_ARRANQUE ) );

    /* ---- 1. ¿Está libre? ---- */
    if( !prvEsperarIdle( "antes de la orden" ) )
    {
        goto salir;
    }

    /* ---- 2. La orden ---- */
    uint16_t    usRta = 0U;
    mb_result_t eRes  = drv_modbus_escribir( DRV_CPRES_SLAVE, DRV_CPRES_REG,
                                             ( uint16_t ) eCmd, &usRta );

    if( eRes == mbOK )
    {
        /*
         * ⭐ La respuesta al FC06 de ESTE dispositivo es su registro de status,
         * no el eco del valor (ver `drv_modbus_escribir()`). O sea que ya dice
         * si el trabajo arrancó, sin pagar una lectura extra.
         */
        xprintf( "CPRES:: orden aceptada, status 0x%02X (%s)\r\n",
                 ( unsigned ) usRta,
                 drv_cpres_status_idle( usRta ) ? "todavia IDLE" : "TRABAJANDO" );
    }

    if( eRes != mbOK )
    {
        /*
         * Lo que se verifica es **la dirección del registro**, no el valor: este
         * dispositivo devuelve su status en vez del eco (ver
         * `drv_modbus_escribir()`). Aun así el chequeo vale: el AVR no mira
         * nada, así que para él **una escritura rechazada se ve idéntica a una
         * exitosa** y el equipo se queda esperando un movimiento que nunca
         * arrancó.
         */
        xprintf( "CPRES:: no se pudo escribir la orden: %s\r\n",
                 drv_modbus_error_str( eRes ) );
        goto salir;
    }

    /* ---- 3. Esperar a que la ejecute ---- */
    vTaskDelay( pdMS_TO_TICKS( DRV_CPRES_MS_EJECUCION ) );

    if( !prvEsperarIdle( "despues de la orden" ) )
    {
        /*
         * ⚠ Que no vuelva a IDLE **no significa que la orden no se haya
         * aplicado**: puede estar todavía moviendo la segunda válvula, que es
         * justo lo que hace lento a este dispositivo. Pero tampoco se puede
         * afirmar que terminó, así que se informa como fallo — un "listo" sobre
         * una consigna a medio aplicar es peor que un error.
         */
        goto salir;
    }

    bOk = true;

salir:

    /* Se apaga SIEMPRE, también por el camino de error. */
    drv_rs485_power( rs485RAIL_BUS,   false );
    drv_rs485_power( rs485RAIL_CPRES, false );
    vTaskDelay( pdMS_TO_TICKS( DRV_CPRES_MS_APAGADO ) );

    xprintf( "CPRES:: %s: %s\r\n", drv_cpres_cmd_str( eCmd ),
             bOk ? "OK" : "FALLO" );

    return bOk;
}
//------------------------------------------------------------------------------
