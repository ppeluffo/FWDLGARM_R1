/*
 * tkCtlPres.c  -  ver tkCtlPres.h
 */

#include <string.h>

#include "tkCtlPres.h"
#include "tkSys.h"
#include "cfg_consigna.h"
#include "drv_rs485.h"
#include "drv_rtc79410.h"
#include "wdg.h"
#include "frtos-io.h"

TaskHandle_t xHandle_tkCtlPres;
StaticTask_t tkCtlPres_TCB;
StackType_t  tkCtlPres_Stack[ tkCtlPres_STACK_SIZE ];

static volatile bool bMatada = false;

/* La última consigna aplicada: sólo para informar. Ver el header — NO se
   persiste, y el motivo es que recordarla mal sería peor que no recordarla. */
static cpres_cmd_t eUltimaConsigna = cpresCMD_NINGUNO;
static bool        bUltimaOk       = false;
static uint16_t    usUltimaHhmm    = 0U;

//------------------------------------------------------------------------------
void tkCtlPres_orden( cpres_cmd_t eCmd )
{
    if( xHandle_tkCtlPres != NULL )
    {
        ( void ) xTaskNotify( xHandle_tkCtlPres, ( uint32_t ) eCmd,
                              eSetValueWithOverwrite );
    }
}
//------------------------------------------------------------------------------
cpres_cmd_t tkCtlPres_ultima_consigna( bool *pbOk, uint16_t *pusHhmm )
{
    if( pbOk != NULL )
    {
        *pbOk = bUltimaOk;
    }

    if( pusHhmm != NULL )
    {
        *pusHhmm = usUltimaHhmm;
    }

    return eUltimaConsigna;
}
//------------------------------------------------------------------------------
/* Anota lo que se acaba de hacer. Sólo para las CONSIGNAS: una orden puntual de
   válvula no cambia en qué consigna está el equipo. */
static void prvAnotarConsigna( cpres_cmd_t eCmd, bool bOk, uint16_t usHhmm )
{
    eUltimaConsigna = eCmd;
    bUltimaOk       = bOk;
    usUltimaHhmm    = usHhmm;
}
//------------------------------------------------------------------------------
void tkCtlPres_pedir_kill( void )
{
    bMatada = true;
}
//------------------------------------------------------------------------------
bool tkCtlPres_matada( void )
{
    return bMatada;
}
//------------------------------------------------------------------------------
static void prvMatarse( void )
{
    xprintf( "\r\ntkCtlPres:: MATADA. El control de presion queda libre.\r\n" );
    xprintf( "            Para volver a operacion normal: 'reset'.\r\n" );

    /* Antes de suspender: una tarea suspendida deja de reportar, y el watchdog
       la daría por colgada. Ver `wdg.h`. */
    wdg_stop_task();

    vTaskSuspend( NULL );   /* no retorna */
}
//------------------------------------------------------------------------------
/*
 * Ejecuta una orden tomando el bus.
 *
 * ⚠ **`portMAX_DELAY` y no un timeout, a propósito.** Si acá se perdiera la
 * vuelta por no conseguir el bus, se perdería la consigna justo en el minuto en
 * que había que aplicarla — que es lo único que este diseño no puede permitirse.
 * El otro usuario del bus es el poleo Modbus, que dura ~20 s en el peor caso.
 *
 * Es el `rs485_ENTER_CRITICAL()` del AVR, que también espera indefinidamente.
 */
static bool prvEjecutar( cpres_cmd_t eCmd )
{
    bool bOk;

    if( !drv_rs485_tomar_bus( portMAX_DELAY ) )
    {
        return false;
    }

    bOk = drv_cpres_comando( eCmd );

    drv_rs485_soltar_bus();

    return bOk;
}
//------------------------------------------------------------------------------
static uint16_t prvHhmmAMinutos( uint16_t usHhmm )
{
    return ( uint16_t ) ( ( ( usHhmm / 100U ) * 60U ) + ( usHhmm % 100U ) );
}
//------------------------------------------------------------------------------
/*
 * Qué consigna corresponde a esta hora. Es `pv_consigna_initService()` del AVR,
 * con sus dos casos.
 *
 * El día queda partido en dos tramos por las dos horas configuradas, y hay que
 * mirar cuál de las dos viene primero:
 *
 *   diurna < nocturna :  |--nocturna--| diurna |--nocturna--|
 *   nocturna < diurna :  |--diurna--| nocturna |--diurna--|
 *
 * ⚠ Con las dos horas IGUALES no hay tramo que decidir y se devuelve
 * `cpresCMD_NINGUNO`. El AVR cae en el mismo caso —sus dos `if` fallan y sale
 * sin hacer nada— pero por omisión; acá es explícito.
 */
static cpres_cmd_t prvConsignaQueCorresponde( uint16_t usAhoraHhmm )
{
    uint16_t usAhora = prvHhmmAMinutos( usAhoraHhmm );
    uint16_t usDia   = prvHhmmAMinutos( xCfgConsigna.usDiurna );
    uint16_t usNoche = prvHhmmAMinutos( xCfgConsigna.usNocturna );

    if( usDia < usNoche )
    {
        return ( ( usAhora <= usDia ) || ( usAhora >= usNoche ) )
               ? cpresCMD_CONSIGNA_NOCTURNA : cpresCMD_CONSIGNA_DIURNA;
    }

    if( usNoche < usDia )
    {
        return ( ( usAhora <= usNoche ) || ( usAhora >= usDia ) )
               ? cpresCMD_CONSIGNA_DIURNA : cpresCMD_CONSIGNA_NOCTURNA;
    }

    return cpresCMD_NINGUNO;
}
//------------------------------------------------------------------------------
/*
 * Lee la hora y dice si es utilizable.
 *
 * ⛔ **Con la hora no confiable NO se aplica ninguna consigna**, y es una
 * diferencia deliberada con el AVR, que no lo chequea.
 *
 * El motivo: tras un arranque en frío el MCP79410 devuelve `2001-01-01 00:xx`, y
 * ese `hhmm` puede coincidir con una consigna configurada **por casualidad**.
 * Aplicar la consigna nocturna a las diez de la mañana es peor que no aplicar
 * nada: el equipo de presión queda operando mal y nadie se entera.
 *
 * ⚠ El costo es que tras un arranque en frío la consigna queda sin aplicar hasta
 * que el reloj se ponga en hora — lo que pasa en la primera sesión, con el
 * `AT+CCLK?` del módulo o el `CLOCK=` del servidor.
 */
static bool prvHoraUtilizable( RtcTimeType_t *pxRtc )
{
    if( !drv_rtc_leer( pxRtc ) )
    {
        return false;
    }

    if( ( drv_rtc_validez() != rtcHORA_VALIDA ) ||
        ( pxRtc->year < TKSYS_ANIO_COMPILACION ) )
    {
        return false;
    }

    return true;
}
//------------------------------------------------------------------------------
/*
 * ⭐ Espera a que cambie el minuto. Ver el header: sin esto, la segunda muestra
 * del mismo minuto volvería a mandar la orden.
 *
 * ⚠ Tiene tope. Si el RTC dejara de contestar, un lazo sin salida dejaría a la
 * tarea colgada para siempre — y con el watchdog, reiniciaría el equipo. Dos
 * minutos de tope alcanzan de sobra para que un reloj sano cambie de minuto.
 */
static void prvEsperarCambioDeMinuto( uint16_t usHhmmDeLaOrden )
{
    uint32_t      ulEsperado = ( 2U * 60U * 1000U ) / TKCTLPRES_MS_CHEQUEO_MIN;
    RtcTimeType_t xRtc;

    while( ulEsperado-- > 0U )
    {
        vTaskDelay( pdMS_TO_TICKS( TKCTLPRES_MS_CHEQUEO_MIN ) );

        /* ⚠ Este lazo llega a 2 minutos de tope, más que el plazo del watchdog.
           Se reporta acá adentro en vez de pedir una prórroga: esperar a que el
           reloj cambie de minuto es progreso, no un cuelgue. */
        wdg_kick();

        if( bMatada )
        {
            return;
        }

        if( drv_rtc_leer( &xRtc ) )
        {
            uint16_t usAhora = ( uint16_t ) ( ( xRtc.hour * 100 ) + xRtc.min );

            if( usAhora != usHhmmDeLaOrden )
            {
                return;
            }
        }
    }

    xprintf( "tkCtlPres:: el minuto no cambio en 2 min: el RTC no contesta?\r\n" );
}
//------------------------------------------------------------------------------
/*
 * El servicio de cada vuelta: ¿es la hora de alguna consigna?
 *
 * **Igualdad exacta de `hhmm`**, que es lo correcto porque se muestrea dos veces
 * por minuto. Ver el header.
 */
static void prvServicioConsigna( void )
{
    RtcTimeType_t xRtc;

    if( !prvHoraUtilizable( &xRtc ) )
    {
        return;
    }

    uint16_t    usAhora = ( uint16_t ) ( ( xRtc.hour * 100 ) + xRtc.min );
    cpres_cmd_t eCmd    = cpresCMD_NINGUNO;

    if( usAhora == xCfgConsigna.usDiurna )
    {
        eCmd = cpresCMD_CONSIGNA_DIURNA;
    }
    else if( usAhora == xCfgConsigna.usNocturna )
    {
        eCmd = cpresCMD_CONSIGNA_NOCTURNA;
    }
    else
    {
        return;
    }

    xprintf( "\r\ntkCtlPres:: son las %02d:%02d -> %s\r\n",
             xRtc.hour, xRtc.min, drv_cpres_cmd_str( eCmd ) );

    prvAnotarConsigna( eCmd, prvEjecutar( eCmd ), usAhora );

    /* Pase lo que pase con la orden, hay que salir del minuto: si falló, no
       tiene sentido reintentarla 45 s después contra el mismo dispositivo. */
    prvEsperarCambioDeMinuto( usAhora );
}
//------------------------------------------------------------------------------
/*
 * Al arrancar: la consigna que corresponda al tramo del día. Autocorrectivo —
 * ver el header.
 */
static void prvConsignaDeArranque( void )
{
    RtcTimeType_t xRtc;

    if( !xCfgConsigna.bEnabled )
    {
        return;
    }

    if( !prvHoraUtilizable( &xRtc ) )
    {
        xprintf( "tkCtlPres:: la hora NO es confiable: no se aplica ninguna consigna\r\n" );
        return;
    }

    uint16_t    usAhora = ( uint16_t ) ( ( xRtc.hour * 100 ) + xRtc.min );
    cpres_cmd_t eCmd    = prvConsignaQueCorresponde( usAhora );

    if( eCmd == cpresCMD_NINGUNO )
    {
        xprintf( "tkCtlPres:: diurna y nocturna son la MISMA hora (%04d): no se puede decidir\r\n",
                 ( int ) xCfgConsigna.usDiurna );
        return;
    }

    xprintf( "tkCtlPres:: arranque a las %02d:%02d -> %s\r\n",
             xRtc.hour, xRtc.min, drv_cpres_cmd_str( eCmd ) );

    prvAnotarConsigna( eCmd, prvEjecutar( eCmd ), usAhora );
}
//------------------------------------------------------------------------------
void tkCtlPres( void *pvParameters )
{
    ( void ) pvParameters;

    wdg_registrar( wdgTK_CTLPRES );

    /* Que el resto del equipo termine de arrancar: el RTC, la configuración y el
       primer poleo. Una consigna al arrancar puede esperar medio minuto. */
    vTaskDelay( pdMS_TO_TICKS( 30000 ) );

    xprintf( "\r\ntkCtlPres arrancando (consignas %s)\r\n",
             xCfgConsigna.bEnabled ? "HABILITADAS" : "deshabilitadas" );

    prvConsignaDeArranque();

    for( ;; )
    {
        uint32_t ulOrden = 0U;

        if( bMatada )
        {
            prvMatarse();   /* no retorna */
        }

        /*
         * El período y la espera de órdenes son lo mismo: si llega una orden del
         * servidor se atiende enseguida, y si no, se despierta a chequear la
         * hora. Igual que el AVR.
         */
        ( void ) xTaskNotifyWait( 0U, 0xFFFFFFFFU, &ulOrden,
                                  pdMS_TO_TICKS( TKCTLPRES_MS_PERIODO ) );

        wdg_kick();

        if( bMatada )
        {
            prvMatarse();
        }

        /* ---- Una orden puntual del servidor (EXT_V0/V1) ---- */
        if( ( ulOrden != 0U ) && ( ulOrden <= ( uint32_t ) cpresCMD_CERRAR_V1 ) )
        {
            xprintf( "\r\ntkCtlPres:: orden recibida: %s\r\n",
                     drv_cpres_cmd_str( ( cpres_cmd_t ) ulOrden ) );

            ( void ) prvEjecutar( ( cpres_cmd_t ) ulOrden );
        }

        /* ---- Y el servicio de las consignas ---- */
        if( xCfgConsigna.bEnabled )
        {
            prvServicioConsigna();
        }
    }
}
//------------------------------------------------------------------------------
