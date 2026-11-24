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

/*------------------------------------------------------------------------------
 * EL ESTADO DE LA RECONCILIACIÓN
 *
 * ⭐ `eAplicada` arranca en `cpresCMD_NINGUNO`, que en este rol significa
 * **AMBIGUA**: no se sabe qué consigna tiene el dispositivo. Vale 0, así que es
 * el estado de arranque sin inicializar nada — y como nunca coincide con la
 * deseada, la primera vuelta reconcilia. Ver el header.
 *
 * ⚠ **NO se persiste**, y el motivo no es pereza: recordarla mal sería peor que
 * no recordarla, porque el equipo dejaría de corregir.
 *----------------------------------------------------------------------------*/
static cpres_cmd_t eAplicada  = cpresCMD_NINGUNO;   /* == AMBIGUA */
static uint16_t    usUltimaHhmm = 0U;               /* del último cambio OK */

/* La serie de reintentos en curso: a qué consigna apunta y cuántos van. */
static cpres_cmd_t eObjetivo  = cpresCMD_NINGUNO;
static uint8_t     ucIntentos = 0U;
static TickType_t  xTickUltimoIntento = 0U;

/* Para no repetir el aviso de configuración inválida en cada vuelta. */
static bool bConfInvalidaInformada = false;

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
const char *tkCtlPres_consigna_str( cpres_cmd_t eCmd )
{
    switch( eCmd )
    {
        case cpresCMD_CONSIGNA_DIURNA:   return "DIURNA";
        case cpresCMD_CONSIGNA_NOCTURNA: return "NOCTURNA";
        default:                         return "AMBIGUA";
    }
}
//------------------------------------------------------------------------------
static uint16_t prvHhmmAMinutos( uint16_t usHhmm )
{
    return ( uint16_t ) ( ( ( usHhmm / 100U ) * 60U ) + ( usHhmm % 100U ) );
}
//------------------------------------------------------------------------------
/*
 * ⭐⭐ **La consigna que DEBE estar aplicada a la hora `usAhoraHhmm`.** Es el
 * corazón de la reconciliación: `consigna_deseada()` del AVR 2.0.14.
 *
 * El día queda partido en dos franjas **semiabiertas** por las dos horas
 * configuradas, y hay que mirar cuál viene primero:
 *
 *   diurna < nocturna :  |--nocturna--| [diurna) |--nocturna--|
 *   nocturna < diurna :  |--diurna--| [nocturna) |--diurna--|
 *
 * **En el minuto exacto de un horario rige la consigna que empieza**, y las
 * comparaciones son `>=` del inicio y `<` del fin. Eso cubre las 1440 horas del
 * día sin huecos ni solapes, incluso cuando una franja cruza la medianoche.
 *
 * ⛔ **El `<=` que había acá era un bug**, heredado del
 * `pv_consigna_initService()` del AVR: con `now <= diurna` devolvía la
 * **NOCTURNA** en el minuto exacto de la diurna, al revés que el servicio
 * periódico, que en ese mismo minuto aplicaba la diurna. Dos partes del mismo
 * firmware discrepaban sobre qué consigna va a las 06:30.
 *
 * ⚠ Con la configuración inválida —horas fuera de HHMM, o las dos iguales—
 * devuelve `cpresCMD_NINGUNO` y **no se aplica nada**. Las dos iguales no dejan
 * franja que decidir; y la configuración puede venir de la EEPROM, por eso se
 * valida acá y no sólo en el setter (ver `cfg_consigna_valida()`).
 */
static cpres_cmd_t prvConsignaDeseada( uint16_t usAhoraHhmm )
{
    if( !cfg_consigna_valida() )
    {
        return cpresCMD_NINGUNO;
    }

    uint16_t usAhora = prvHhmmAMinutos( usAhoraHhmm );
    uint16_t usDia   = prvHhmmAMinutos( xCfgConsigna.usDiurna );
    uint16_t usNoche = prvHhmmAMinutos( xCfgConsigna.usNocturna );

    if( usDia < usNoche )
    {
        return ( ( usAhora >= usDia ) && ( usAhora < usNoche ) )
               ? cpresCMD_CONSIGNA_DIURNA : cpresCMD_CONSIGNA_NOCTURNA;
    }

    /* usNoche < usDia  (iguales ya quedó descartado por cfg_consigna_valida) */
    return ( ( usAhora >= usNoche ) && ( usAhora < usDia ) )
           ? cpresCMD_CONSIGNA_NOCTURNA : cpresCMD_CONSIGNA_DIURNA;
}
//------------------------------------------------------------------------------
void tkCtlPres_consigna_estado( tkctlpres_consigna_t *pxEstado )
{
    if( pxEstado == NULL )
    {
        return;
    }

    pxEstado->eAplicada  = eAplicada;
    pxEstado->usHhmm     = usUltimaHhmm;
    pxEstado->ucIntentos = ucIntentos;
    pxEstado->eDeseada   = cpresCMD_NINGUNO;

    /*
     * La deseada se calcula acá, con la hora de ahora: guardarla en una variable
     * la dejaría vieja entre vueltas, y `status` informaría una franja que ya
     * pasó. ⚠ Si la hora no es confiable queda en NINGUNO, que es lo correcto —
     * sin hora no hay franja que decidir.
     */
    RtcTimeType_t xRtc;

    if( drv_rtc_leer( &xRtc ) && ( drv_rtc_validez() == rtcHORA_VALIDA ) )
    {
        pxEstado->eDeseada =
            prvConsignaDeseada( ( uint16_t ) ( ( xRtc.hour * 100 ) + xRtc.min ) );
    }
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
 * que había que aplicarla. El otro usuario del bus es el poleo Modbus, que dura
 * ~20 s en el peor caso.
 *
 * Es el `rs485_ENTER_CRITICAL()` del AVR, que también espera indefinidamente.
 *
 * ⭐ **Y se patea el watchdog al terminar**, que es lo que hizo el AVR 2.0.14:
 * una orden tarda ~14 s y la vuelta puede llevar dos —una puntual y una
 * consigna—. Con el período de 45 s eso da 73 s contra un plazo de 90, o sea
 * 17 s de margen; reportando acá, la vuelta nunca acumula más de una espera.
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

    wdg_kick();

    return bOk;
}
//------------------------------------------------------------------------------
/*
 * Lee la hora y dice si es utilizable.
 *
 * ⛔ **Con la hora no confiable NO se aplica ninguna consigna**, y es una
 * diferencia deliberada con el AVR, que no lo chequea.
 *
 * El motivo: tras un arranque en frío el MCP79410 devuelve `2001-01-01 00:xx`, y
 * ese `hhmm` puede caer en cualquier franja. Aplicar la consigna nocturna a las
 * diez de la mañana es peor que no aplicar nada: el equipo de presión queda
 * operando mal y nadie se entera.
 *
 * ⚠ El costo es que tras un arranque en frío la consigna queda sin aplicar hasta
 * que el reloj se ponga en hora — lo que pasa en la primera sesión, con el
 * `AT+CCLK?` del módulo o el `CLOCK=` del servidor. ⭐ Y con la reconciliación
 * eso se arregla **solo**: en cuanto la hora es confiable, la vuelta siguiente
 * aplica lo que corresponda. Antes había que esperar al minuto exacto.
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
 * ⭐⭐ EL SERVICIO DE CADA VUELTA: LA RECONCILIACIÓN.
 *
 *   1. deseada = la consigna que corresponde a la hora de ahora.
 *   2. Si deseada == aplicada: nada que hacer.
 *   3. Si no: aplicar. Si falla, la aplicada queda AMBIGUA y se reintenta,
 *      hasta `TKCTLPRES_CONSIGNA_MAX_INTENTOS` separados
 *      `TKCTLPRES_MS_REINTENTO`.
 *   4. Un cambio de franja reinicia la serie de intentos: es otra orden.
 *
 * **No depende del período del lazo**, que es todo el punto. Ver el header.
 */
static void prvServicioConsigna( void )
{
    RtcTimeType_t xRtc;

    if( !prvHoraUtilizable( &xRtc ) )
    {
        return;
    }

    uint16_t    usAhora = ( uint16_t ) ( ( xRtc.hour * 100 ) + xRtc.min );
    cpres_cmd_t eDeseada = prvConsignaDeseada( usAhora );

    /*
     * Configuración inválida: no se aplica nada. ⚠ Se informa **una sola vez**
     * para no llenar la consola —y el registro de eventos— con una línea cada
     * 45 s por algo que no va a cambiar solo.
     */
    if( eDeseada == cpresCMD_NINGUNO )
    {
        if( !bConfInvalidaInformada )
        {
            xprintf( "\r\ntkCtlPres:: ERROR: configuracion de consigna INVALIDA "
                     "(diurna=%04d, nocturna=%04d): no se aplica nada\r\n",
                     ( int ) xCfgConsigna.usDiurna, ( int ) xCfgConsigna.usNocturna );
            bConfInvalidaInformada = true;
        }
        return;
    }

    bConfInvalidaInformada = false;

    /* ---- Ya está aplicada la que corresponde: el caso normal ---- */
    if( eDeseada == eAplicada )
    {
        eObjetivo  = eDeseada;
        ucIntentos = 0U;
        return;
    }

    /* ---- Cambió la franja: serie de intentos nueva ---- */
    if( eDeseada != eObjetivo )
    {
        eObjetivo  = eDeseada;
        ucIntentos = 0U;
    }

    /* ---- Intentos agotados: se espera al próximo cambio de franja ---- */
    if( ucIntentos >= TKCTLPRES_CONSIGNA_MAX_INTENTOS )
    {
        return;
    }

    /* ---- Separación entre reintentos ---- */
    if( ( ucIntentos > 0U ) &&
        ( ( xTaskGetTickCount() - xTickUltimoIntento ) <
          pdMS_TO_TICKS( TKCTLPRES_MS_REINTENTO ) ) )
    {
        return;
    }

    ucIntentos++;
    xTickUltimoIntento = xTaskGetTickCount();

    xprintf( "\r\ntkCtlPres:: %02d:%02d aplicar consigna %s (estaba %s) intento %u/%u\r\n",
             xRtc.hour, xRtc.min,
             tkCtlPres_consigna_str( eDeseada ),
             tkCtlPres_consigna_str( eAplicada ),
             ( unsigned ) ucIntentos,
             ( unsigned ) TKCTLPRES_CONSIGNA_MAX_INTENTOS );

    if( prvEjecutar( eDeseada ) )
    {
        eAplicada    = eDeseada;
        usUltimaHhmm = usAhora;
        ucIntentos   = 0U;

        xprintf( "tkCtlPres:: consigna %s OK\r\n",
                 tkCtlPres_consigna_str( eDeseada ) );
        return;
    }

    /*
     * ⭐ Falló: **la aplicada queda AMBIGUA**, no se deja la anterior. Si el
     * Modbus se cortó a mitad del diálogo, el dispositivo pudo haber aplicado la
     * orden o no — y afirmar lo que no se sabe es justo lo que este diseño
     * evita.
     */
    eAplicada = cpresCMD_NINGUNO;

    if( ucIntentos < TKCTLPRES_CONSIGNA_MAX_INTENTOS )
    {
        xprintf( "tkCtlPres:: consigna %s FALLO (intento %u/%u), reintento en %u min\r\n",
                 tkCtlPres_consigna_str( eDeseada ),
                 ( unsigned ) ucIntentos,
                 ( unsigned ) TKCTLPRES_CONSIGNA_MAX_INTENTOS,
                 ( unsigned ) ( TKCTLPRES_MS_REINTENTO / 60000U ) );
    }
    else
    {
        xprintf( "tkCtlPres:: consigna %s FALLO: %u intentos agotados, queda AMBIGUA "
                 "hasta el proximo cambio de franja\r\n",
                 tkCtlPres_consigna_str( eDeseada ),
                 ( unsigned ) TKCTLPRES_CONSIGNA_MAX_INTENTOS );
    }
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

    /* El delay de arriba consume un tercio del plazo del watchdog antes de la
       primera vuelta: se reporta acá para no arrancar con 75 s gastados. */
    wdg_kick();

    /*
     * ⭐ **No hay "consigna de arranque" aparte.** `eAplicada` arranca en
     * AMBIGUA, así que esta primera reconciliación aplica la que corresponda al
     * tramo del día — que es lo que hacía `prvConsignaDeArranque()`, eliminada
     * junto con su bug de borde. Es exactamente lo que hizo el AVR 2.0.14:
     * llamar al servicio en lugar del `initService`. Ver el header.
     */
    if( xCfgConsigna.bEnabled )
    {
        prvServicioConsigna();
    }

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

            /* ⭐ NO toca `eAplicada`: mover una válvula a mano no cambia en qué
               consigna está el equipo, así que la reconciliación no la pisa. */
            ( void ) prvEjecutar( ( cpres_cmd_t ) ulOrden );
        }

        /* ---- Y la reconciliación de la consigna ---- */
        if( xCfgConsigna.bEnabled )
        {
            prvServicioConsigna();
        }
    }
}
//------------------------------------------------------------------------------
