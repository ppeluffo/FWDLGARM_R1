/*
 * eventos.c  -  ver eventos.h
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "eventos.h"

#include "FreeRTOS.h"
#include "task.h"
#include "fs_sd.h"
#include "tkCmd.h"          /* wan_causa_reset() */
#include "tkSys.h"          /* TKSYS_ANIO_COMPILACION */
#include "cfg_base.h"
#include "drv_rtc79410.h"
#include "frtos-io.h"
#include "main.h"           /* FW_VERSION, FW_FECHA */

/* Una línea: la hora (~30 bytes) más el mensaje. */
#define EVENTOS_LINEA_BYTES     192U

/*
 * El buffer, en la SRAM2 sin inicializar. Ver eventos.h.
 *
 * ⚠ La magia va DOS veces —el valor y su complemento— porque una SRAM recién
 * alimentada puede traer cualquier patrón, y uno que coincidiera por casualidad
 * con un solo `uint32_t` haría escribir 8 KB de basura en la tarjeta.
 */
#define EVENTOS_MAGIA           0x45564E54UL     /* "EVNT" */

typedef struct {
    uint32_t ulMagia;
    uint32_t ulMagiaNeg;
    uint32_t ulLargo;
    uint32_t ulPerdidos;
    char     pcBuf[ EVENTOS_BUFFER_BYTES ];
} eventos_ram_t;

static eventos_ram_t xRam __attribute__( ( section( ".ram2_noinit" ) ) );

static volatile eventos_estado_t eEstado = eventosDECIDIENDO;
static volatile bool             bValidado;
static volatile bool             bVolcando;
static uint32_t                  ulRecuperados;   /* bytes que venían de antes del reset */

//------------------------------------------------------------------------------
/* Se llama DENTRO de la sección crítica. */
static void prvValidar( void )
{
    if( bValidado )
    {
        return;
    }

    if( ( xRam.ulMagia    != EVENTOS_MAGIA ) ||
        ( xRam.ulMagiaNeg != ~EVENTOS_MAGIA ) ||
        ( xRam.ulLargo    >  EVENTOS_BUFFER_BYTES ) )
    {
        xRam.ulMagia    = EVENTOS_MAGIA;
        xRam.ulMagiaNeg = ~EVENTOS_MAGIA;
        xRam.ulLargo    = 0UL;
        xRam.ulPerdidos = 0UL;
    }

    ulRecuperados = xRam.ulLargo;
    bValidado     = true;
}
//------------------------------------------------------------------------------
static void prvAgregar( const char *pcLinea, uint32_t ulLargo )
{
    taskENTER_CRITICAL();

    prvValidar();

    if( ( xRam.ulLargo + ulLargo ) <= EVENTOS_BUFFER_BYTES )
    {
        memcpy( &xRam.pcBuf[ xRam.ulLargo ], pcLinea, ulLargo );
        xRam.ulLargo += ulLargo;
    }
    else
    {
        xRam.ulPerdidos++;
    }

    taskEXIT_CRITICAL();
}
//------------------------------------------------------------------------------
static void prvLog( bool bConHora, const char *pcFmt, va_list xArgs )
{
    char     pcLinea[ EVENTOS_LINEA_BYTES ];
    int      iN;
    uint32_t ulSeg;

    if( eEstado == eventosINACTIVO )
    {
        return;
    }

    ulSeg = ( uint32_t ) ( xTaskGetTickCount() / configTICK_RATE_HZ );

    if( bConHora )
    {
        RtcTimeType_t xHora;

        if( drv_rtc_leer( &xHora ) )
        {
            /* El '?' marca una hora imposible —anterior a la compilación—, el
               mismo criterio que `tkSys` para el registro. */
            iN = snprintf( pcLinea, sizeof( pcLinea ),
                           "%02u/%02u/%02u %02u:%02u:%02u%s t=%lu ",
                           ( unsigned ) xHora.year, ( unsigned ) xHora.month,
                           ( unsigned ) xHora.day,  ( unsigned ) xHora.hour,
                           ( unsigned ) xHora.min,  ( unsigned ) xHora.sec,
                           ( xHora.year < TKSYS_ANIO_COMPILACION ) ? "?" : "",
                           ( unsigned long ) ulSeg );
        }
        else
        {
            iN = snprintf( pcLinea, sizeof( pcLinea ), "--/--/-- --:--:-- t=%lu ",
                           ( unsigned long ) ulSeg );
        }
    }
    else
    {
        iN = snprintf( pcLinea, sizeof( pcLinea ), "(sin hora)        t=%lu ",
                       ( unsigned long ) ulSeg );
    }

    if( ( iN < 0 ) || ( ( uint32_t ) iN >= sizeof( pcLinea ) ) )
    {
        return;
    }

    /* Se deja lugar para el "\r\n". Un mensaje largo se trunca, no se pierde. */
    int iM = vsnprintf( &pcLinea[ iN ], sizeof( pcLinea ) - ( uint32_t ) iN - 2U, pcFmt, xArgs );

    if( iM < 0 )
    {
        return;
    }

    uint32_t ulLargo = strlen( pcLinea );

    pcLinea[ ulLargo++ ] = '\r';
    pcLinea[ ulLargo++ ] = '\n';

    prvAgregar( pcLinea, ulLargo );
}
//------------------------------------------------------------------------------
void eventos_log( const char *pcFmt, ... )
{
    va_list xArgs;

    va_start( xArgs, pcFmt );
    prvLog( true, pcFmt, xArgs );
    va_end( xArgs );
}
//------------------------------------------------------------------------------
void eventos_log_sin_hora( const char *pcFmt, ... )
{
    va_list xArgs;

    va_start( xArgs, pcFmt );
    prvLog( false, pcFmt, xArgs );
    va_end( xArgs );
}
//------------------------------------------------------------------------------
bool eventos_volcar( void )
{
    uint32_t ulN;
    uint32_t ulPerdidos;

    if( eEstado != eventosACTIVO )
    {
        return true;
    }

    /*
     * ⚠ Un solo volcado a la vez. Mientras éste escribe `pcBuf[0..ulN)`, las
     * demás tareas pueden seguir agregando DETRÁS —no tocan lo que se está
     * escribiendo—, pero un segundo volcado escribiría lo mismo dos veces.
     */
    taskENTER_CRITICAL();

    prvValidar();

    if( bVolcando || ( xRam.ulLargo == 0UL ) )
    {
        bool bVacio = ( xRam.ulLargo == 0UL );

        taskEXIT_CRITICAL();
        return bVacio;
    }

    bVolcando = true;
    ulN       = xRam.ulLargo;

    taskEXIT_CRITICAL();

    bool bOk = fs_sd_agregar( EVENTOS_ARCHIVO, EVENTOS_ARCHIVO_VIEJO,
                              xRam.pcBuf, ulN, EVENTOS_ARCHIVO_MAX );

    taskENTER_CRITICAL();

    if( bOk )
    {
        /* Lo que llegó durante la escritura pasa al principio. */
        memmove( xRam.pcBuf, &xRam.pcBuf[ ulN ], xRam.ulLargo - ulN );
        xRam.ulLargo -= ulN;
    }

    ulPerdidos = xRam.ulPerdidos;

    if( bOk )
    {
        xRam.ulPerdidos = 0UL;
    }

    bVolcando = false;

    taskEXIT_CRITICAL();

    if( !bOk )
    {
        /* Queda todo en RAM: se reintenta en el próximo volcado. */
        xprintf( "EVENTOS:: no se pudo escribir en la microSD; quedan %lu bytes en RAM\r\n",
                 ( unsigned long ) ulN );
        return false;
    }

    if( ulPerdidos > 0UL )
    {
        /* Va al buffer y sale en el volcado siguiente. */
        eventos_log( "[!] se PERDIERON %lu eventos: el buffer de RAM se lleno",
                     ( unsigned long ) ulPerdidos );
    }

    return true;
}
//------------------------------------------------------------------------------
bool eventos_conviene_volcar( void )
{
    return ( eEstado == eventosACTIVO ) &&
           ( xRam.ulLargo >= ( EVENTOS_BUFFER_BYTES / 2U ) );
}
//------------------------------------------------------------------------------
static const char *prvCausaReset( void )
{
    switch( ( wan_causa_reset_t ) wan_causa_reset() )
    {
        case wanRESET_IWDG: return "IWDG (watchdog)";
        case wanRESET_WWDG: return "WWDG";
        case wanRESET_LPWR: return "LPWR";
        case wanRESET_SOFT: return "SOFT (consola o servidor)";
        case wanRESET_BOR:  return "BOR/POR (alimentacion)";
        case wanRESET_PIN:  return "PIN";
        default:            return "ninguno";
    }
}
//------------------------------------------------------------------------------
void eventos_init( void )
{
    bool bOn = fs_sd_existe( EVENTOS_ACTIVADOR );

    if( !bOn )
    {
        taskENTER_CRITICAL();
        eEstado = eventosINACTIVO;
        prvValidar();
        xRam.ulLargo    = 0UL;       /* lo de antes del reset no le importa a nadie */
        xRam.ulPerdidos = 0UL;
        taskEXIT_CRITICAL();

        xprintf( "EVENTOS:: registro APAGADO (no hay '%s' en la microSD)\r\n",
                 EVENTOS_ACTIVADOR );
        return;
    }

    eEstado = eventosACTIVO;

    taskENTER_CRITICAL();
    prvValidar();
    taskEXIT_CRITICAL();

    xprintf( "EVENTOS:: registro ACTIVO: '%s' en la microSD -> %s\r\n",
             EVENTOS_ACTIVADOR, EVENTOS_ARCHIVO );

    if( ulRecuperados > 0UL )
    {
        xprintf( "EVENTOS:: %lu bytes de eventos de ANTES del reset, recuperados de la SRAM2\r\n",
                 ( unsigned long ) ulRecuperados );
    }

    eventos_log( "==== ARRANQUE fw %s (%s) | reset por %s | modo %s | timerpoll %u s, timerdial %u s",
                 FW_VERSION, FW_FECHA, prvCausaReset(), cfg_base_pwrmodo_str(),
                 ( unsigned ) xCfgBase.usTimerPoll, ( unsigned ) xCfgBase.usTimerDial );

    ( void ) eventos_volcar();
}
//------------------------------------------------------------------------------
eventos_estado_t eventos_estado( void )
{
    return eEstado;
}
//------------------------------------------------------------------------------
const char *eventos_estado_str( void )
{
    switch( eEstado )
    {
        case eventosACTIVO:   return "ACTIVO";
        case eventosINACTIVO: return "apagado";
        default:              return "decidiendo (todavia no se miro la tarjeta)";
    }
}
//------------------------------------------------------------------------------
uint32_t eventos_pendientes_bytes( void )
{
    return bValidado ? xRam.ulLargo : 0UL;
}
//------------------------------------------------------------------------------
uint32_t eventos_perdidos( void )
{
    return bValidado ? xRam.ulPerdidos : 0UL;
}
//------------------------------------------------------------------------------
