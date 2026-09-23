/*
 * caudal_log.c  -  ver caudal_log.h
 */

#include <stddef.h>

#include "caudal_log.h"

#include "FreeRTOS.h"
#include "task.h"

static caudal_log_reg_t xBuf[ CAUDAL_LOG_REGISTROS ];

static volatile uint16_t usEscribe = 0U;
static volatile uint16_t usLee     = 0U;
static volatile uint16_t usCuenta  = 0U;
static volatile uint32_t ulPisados = 0UL;
static volatile bool     bActivo   = false;

//------------------------------------------------------------------------------
void caudal_log_habilitar( bool bOn )
{
    taskENTER_CRITICAL();

    bActivo = bOn;

    if( bOn )
    {
        /* Arrancar limpio: mezclar trazas de dos sesiones haría que los dT
           entre ellas no signifiquen nada. */
        usEscribe = 0U;
        usLee     = 0U;
        usCuenta  = 0U;
        ulPisados = 0UL;
    }

    taskEXIT_CRITICAL();
}
//------------------------------------------------------------------------------
bool caudal_log_activo( void )
{
    return bActivo;
}
//------------------------------------------------------------------------------
void caudal_log_agregar_desde_isr( uint32_t ulTicks, uint32_t ulDtMs,
                                   float fQInst, float fQEma,
                                   caudal_ev_t eEvento )
{
    if( !bActivo )
    {
        return;
    }

    xBuf[ usEscribe ].ulTicks  = ulTicks;
    xBuf[ usEscribe ].ulDtMs   = ulDtMs;
    xBuf[ usEscribe ].fQInst   = fQInst;
    xBuf[ usEscribe ].fQEma    = fQEma;
    xBuf[ usEscribe ].ucEvento = ( uint8_t ) eEvento;

    usEscribe = ( uint16_t ) ( ( usEscribe + 1U ) % CAUDAL_LOG_REGISTROS );

    if( usCuenta < CAUDAL_LOG_REGISTROS )
    {
        usCuenta++;
    }
    else
    {
        /*
         * ⚠ Lleno: se pisa el MÁS VIEJO y se cuenta.
         *
         * Para depurar un evento puntual suele ser lo correcto —interesa lo
         * último— pero si el evento ocurrió al principio de una corrida larga,
         * se perdió. Con el volcado al 90 % esto sólo pasa si la tarjeta no
         * está, y por eso el contador se informa en vez de callarse.
         */
        usLee = ( uint16_t ) ( ( usLee + 1U ) % CAUDAL_LOG_REGISTROS );
        ulPisados++;
    }
}
//------------------------------------------------------------------------------
uint16_t caudal_log_pendientes( void )
{
    return usCuenta;
}
//------------------------------------------------------------------------------
uint32_t caudal_log_pisados( void )
{
    return ulPisados;
}
//------------------------------------------------------------------------------
bool caudal_log_lleno( void )
{
    return ( usCuenta >= ( ( CAUDAL_LOG_REGISTROS * CAUDAL_LOG_UMBRAL_PCT ) / 100U ) );
}
//------------------------------------------------------------------------------
bool caudal_log_sacar( caudal_log_reg_t *pxOut )
{
    bool bHay = false;

    if( pxOut == NULL )
    {
        return false;
    }

    taskENTER_CRITICAL();

    if( usCuenta > 0U )
    {
        *pxOut = xBuf[ usLee ];
        usLee  = ( uint16_t ) ( ( usLee + 1U ) % CAUDAL_LOG_REGISTROS );
        usCuenta--;
        bHay = true;
    }

    taskEXIT_CRITICAL();

    return bHay;
}
//------------------------------------------------------------------------------
void caudal_log_vaciar( void )
{
    taskENTER_CRITICAL();
    usEscribe = 0U;
    usLee     = 0U;
    usCuenta  = 0U;
    taskEXIT_CRITICAL();
}
//------------------------------------------------------------------------------
const char *caudal_log_evento_str( uint8_t ucEvento )
{
    switch( ( caudal_ev_t ) ucEvento )
    {
        case cauEV_OK:    return "OK";
        case cauEV_CORTO: return "CORTO";
        case cauEV_QMAX:  return "QMAX";
        default:          return "?";
    }
}
//------------------------------------------------------------------------------
