/*
 * tkFlow.c  -  ver tkFlow.h
 */

#include "tkFlow.h"
#include "drv_valvula.h"
#include "wdg.h"
#include "frtos-io.h"

TaskHandle_t xHandle_tkFlow;
StaticTask_t tkFlow_TCB;
StackType_t  tkFlow_Stack[ tkFlow_STACK_SIZE ];

static volatile bool bMatada = false;

//------------------------------------------------------------------------------
void tkFlow_orden( flow_orden_t eOrden )
{
    /*
     * ⚠ SIN LA TAREA CREADA LA ORDEN NO SE PUEDE ATENDER, Y SE DICE.
     *
     * `ARRANCA_TKFLOW` esta en 0 desde el 2026-10-09 porque la valvula interna
     * no se usa todavia (ver `main.c`), asi que `xHandle_tkFlow` queda en NULL.
     * Pero `wan_frame.c` SI parsea los `VOPEN`/`VCLOSE` del servidor y llega
     * hasta aca.
     *
     * Descartarla callado seria el unico lugar del firmware donde una orden
     * remota se pierde sin dejar rastro — contra el criterio de todo lo demas:
     * el centinela `-9999`, el `usInvalidos` del registro, la firma del RTC, el
     * `estado_asumido` de la valvula. Si el servidor pide algo que el equipo no
     * puede hacer, lo que corresponde es **que se vea en el log**.
     */
    if( xHandle_tkFlow == NULL )
    {
        xprintf( "\r\n[!] el servidor pide %s de la valvula interna, "
                 "y tkFlow NO esta corriendo: la orden se DESCARTA.\r\n",
                 ( eOrden == flowORDEN_ABRIR ) ? "ABRIR" : "CERRAR" );
        return;
    }

    ( void ) xTaskNotify( xHandle_tkFlow, ( uint32_t ) eOrden,
                          eSetValueWithOverwrite );
}
//------------------------------------------------------------------------------
void tkFlow_pedir_kill( void )
{
    bMatada = true;
}
//------------------------------------------------------------------------------
bool tkFlow_matada( void )
{
    return bMatada;
}
//------------------------------------------------------------------------------
static void prvMatarse( void )
{
    xprintf( "\r\ntkFlow:: MATADA. La valvula queda libre para 'ev'.\r\n" );
    xprintf( "         Para volver a operacion normal: 'reset'.\r\n" );

    /* Antes de suspender, por lo de siempre: ver `wdg.h`. */
    wdg_stop_task();

    vTaskSuspend( NULL );   /* no retorna */
}
//------------------------------------------------------------------------------
void tkFlow( void *pvParameters )
{
    ( void ) pvParameters;

    wdg_registrar( wdgTK_FLOW );

    vTaskDelay( pdMS_TO_TICKS( 30000 ) );

    xprintf( "\r\ntkFlow arrancando (valvula interna: ordenes del servidor)\r\n" );

    /*
     * ⛔ EL AVR ABRE LA VÁLVULA AL ARRANCAR Y ACÁ NO SE COPIA.
     *
     * Allá, si flowcontrol está deshabilitado, arranca con
     * `VALVE_DEFAULT_ACTION()` = `VALVE_open()`.
     *
     * Acá **no**, y es una decisión tomada el 2026-08-18 al escribir
     * `drv_valvula`: mover la válvula al energizar el equipo son **5 s de motor
     * en cada reset** —incluidos los diez seguidos de una sesión de flasheo y
     * los espurios que meta el watchdog cuando exista—. Y en qué condiciones
     * conviene hacerlo es política de la aplicación, que todavía no está
     * definida.
     *
     * ⚠ La consecuencia hay que tenerla presente: **el firmware no sabe en qué
     * posición está la válvula al arrancar** y por eso `drv_valvula` asume
     * ABIERTA —el estado peligroso— para que una orden de cierre tenga sentido.
     * No adelantar esto sin que Pablo lo pida.
     */

    for( ;; )
    {
        uint32_t ulOrden = 0U;

        if( bMatada )
        {
            prvMatarse();   /* no retorna */
        }

        ( void ) xTaskNotifyWait( 0U, 0xFFFFFFFFU, &ulOrden,
                                  pdMS_TO_TICKS( TKFLOW_MS_PERIODO ) );

        wdg_kick();

        if( bMatada )
        {
            prvMatarse();
        }

        switch( ( flow_orden_t ) ulOrden )
        {
            case flowORDEN_ABRIR:
                xprintf( "\r\ntkFlow:: ABRIR valvula (orden del servidor)\r\n" );
                ( void ) drv_valvula_abrir();
                break;

            case flowORDEN_CERRAR:
                xprintf( "\r\ntkFlow:: CERRAR valvula (orden del servidor)\r\n" );
                ( void ) drv_valvula_cerrar();
                break;

            default:
                break;
        }

        /*
         * ⏳ Acá iría un servicio periódico si alguna vez vuelve la tabla de
         * horarios. Hoy no hay ninguno: la tarea **sólo espera órdenes**, y el
         * timeout del `xTaskNotifyWait()` existe nada más que para poder
         * atender un `kill`.
         *
         * Si vuelve, la forma ya está resuelta en `tkCtlPres`: igualdad exacta
         * de la hora, espera al cambio de minuto después de actuar, y **no
         * actuar con la hora no confiable** — tras un arranque en frío el RTC da
         * `2001-01-01 00:xx` y eso puede coincidir con un slot por casualidad.
         */
    }
}
//------------------------------------------------------------------------------
