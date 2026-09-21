/*
 * tkSys.c  -  ver tkSys.h
 */

#include <string.h>

#include "tkSys.h"
#include "cfg_nvm.h"
#include "fs_datos.h"
#include "fs_sd.h"
#include "tkWan.h"
#include "drv_adc.h"
#include "drv_ina3221.h"
#include "drv_pulsos.h"
#include "drv_valvula.h"
#include "drv_rtc79410.h"
#include "frtos-io.h"
#include "main.h"

TaskHandle_t xHandle_tkSys;
StaticTask_t tkSys_TCB;
StackType_t  tkSys_Stack[ tkSys_STACK_SIZE ];

static dataRcd_t xUltimo;

/* Cuándo toca el próximo poleo, en ticks. Lo lee `status`. */
static TickType_t xTicksProximoPoll;

/*
 * Espera inicial antes del primer poleo, igual que el AVR: le da tiempo a la
 * consola y a los periféricos a asentarse, y ese primer registro **no se
 * almacena** — es de arranque y no representa un intervalo completo.
 */
#define TKSYS_ARRANQUE_MS       10000U

/* A qué porcentaje de la ventana se vuelca a la microSD. Ver el comentario del
   volcado, más abajo: el margen que queda es para poder reintentar. */
#define TKSYS_UMBRAL_VOLCADO_PCT   90U

/* Cuántos registros se descartaron por estar en modo RTU sin enlace. Ver el
   comentario en el lazo de la tarea: es la única pérdida deliberada de datos
   del equipo, y por eso se cuenta. */
static uint32_t ulDescartadosRtu = 0UL;

//------------------------------------------------------------------------------
/*
 * ⚠ EL CANAL DEL INA NO ES EL NÚMERO DE LA ENTRADA: EL MAPEO ESTÁ INVERTIDO.
 *
 * La entrada `a0` del datalogger está cableada al canal 3 del chip, y `a2` al 1.
 * Sale de `ainputs_read_channel_raw()` de FWDLGX, que hace el mismo switch, y
 * **depende del cableado de la placa, no del chip**: es un dato de R001, no una
 * propiedad del INA3221. Por eso vive acá, en la capa de aplicación, y no adentro
 * del driver — que habla de `inaCH1..3`, que son los del integrado.
 *
 * ✅ **Confirmado por Pablo el 2026-09-09**: en R001 la asignación es la misma que
 * en el AVR, así que la tabla queda como está. No "corregirla" por parecer al
 * revés — lo está, y a propósito.
 */
static const ina_canal_t xMapaCanales[ CFG_AINPUTS_NRO_CANALES ] = {
    inaCH3,     /* a0 */
    inaCH2,     /* a1 */
    inaCH1,     /* a2 */
};
//------------------------------------------------------------------------------
static void prvPolearAnalogicas( dataRcd_t *pxDr )
{
    float   fMa[ inaCH_COUNT ];
    uint8_t i;
    bool    bAlguno = false;

    for( i = 0U; i < CFG_AINPUTS_NRO_CANALES; i++ )
    {
        pxDr->fAinputs[ i ] = 0.0f;
        bAlguno |= xCfgAinputs.xCanal[ i ].bEnabled;
    }

    /* Sin canales habilitados no se enciende la fuente lineal ni se despierta el
       chip: son ~1,4 s y 350 µA que no le sirven a nadie. */
    if( !bAlguno )
    {
        return;
    }

    /*
     * El `sensors_pwr_settle_time` de la configuración es un asentamiento EXTRA,
     * no un reemplazo del que hace el driver: existe para los sensores lentos
     * —los de ultrasonido de Dica— que necesitan bastante más que los 500 ms por
     * omisión.
     *
     * Se aplica encendiendo el riel acá y esperando; `drv_ina_medir()` ve que ya
     * está encendido y **no vuelve a pagar su propio asentamiento**, que es
     * exactamente para lo que ese camino existe en el driver. Así el tiempo
     * configurable sale sin tocar código validado.
     */
    if( xCfgAinputs.ucSensorsPwrSettleTime > 0U )
    {
        drv_ina_pwr_sensores( true );
        vTaskDelay( pdMS_TO_TICKS( ( uint32_t ) xCfgAinputs.ucSensorsPwrSettleTime * 1000UL ) );
    }

    /* `false` = apagar el riel al terminar. El INA se duerme solo, ande o no. */
    bool bOk = drv_ina_medir( fMa, false );

    for( i = 0U; i < CFG_AINPUTS_NRO_CANALES; i++ )
    {
        if( !xCfgAinputs.xCanal[ i ].bEnabled )
        {
            continue;
        }

        if( !bOk )
        {
            /* Una falla del I2C invalida los TRES canales, no uno: `drv_ina_medir()`
               informa un solo resultado para el barrido completo, así que no hay
               forma de saber cuál se leyó bien. Marcar sólo alguno sería inventar. */
            pxDr->usInvalidos |= ( uint16_t ) ( dataINVALIDO_AIN0 << i );
            continue;
        }

        pxDr->fAinputs[ i ] = cfg_ainputs_convertir( i, fMa[ xMapaCanales[ i ] ] );
    }
}
//------------------------------------------------------------------------------
static void prvPolearRieles( dataRcd_t *pxDr )
{
    uint32_t ulMv;

    /*
     * El de 3,3 V sale de VREFINT, sin hardware: medirlo con un ADC referenciado
     * a él mismo daría 2047 siempre, midiera lo que midiera. Ver drv_adc.h.
     */
    if( drv_adc_vdda_mv( &ulMv ) )
    {
        pxDr->fBt3v3 = ( float ) ulMv / 1000.0f;
    }
    else
    {
        pxDr->fBt3v3      = 0.0f;
        pxDr->usInvalidos |= dataINVALIDO_BT3V3;
    }

    /* El de 12 V enciende su load switch, mide y lo vuelve a apagar: el divisor
       consume 182 µA mientras está conectado. */
    if( drv_adc_v12_mv( &ulMv, false ) )
    {
        pxDr->fBt12v = ( float ) ulMv / 1000.0f;
    }
    else
    {
        pxDr->fBt12v      = 0.0f;
        pxDr->usInvalidos |= dataINVALIDO_BT12V;
    }
}
//------------------------------------------------------------------------------
bool tkSys_poll( dataRcd_t *pxDr )
{
    if( pxDr == NULL )
    {
        return false;
    }

    memset( pxDr, 0, sizeof( dataRcd_t ) );

    /*
     * ---------------------------------------------------------------------
     * EL ORDEN NO ES ARBITRARIO, y se conserva del AVR:
     *
     *   1. el contador PRIMERO, antes de encender ninguna fuente auxiliar,
     *      para que el ruido de las fuentes conmutadas no contamine la medida;
     *   2. después los canales que necesitan energía, cada uno con su riel;
     *   3. la hora al final, que es cuando el registro ya está armado.
     * ---------------------------------------------------------------------
     */

    /* ---- 1. Contador ------------------------------------------------- */
    if( xCfgCounter.bEnabled )
    {
        /*
         * ⏳ PASO 2b. El dato que va al servidor es el CAUDAL, no los pulsos: el
         * `modo_medida` sólo cambia cómo se imprime en consola. Ese caudal sale
         * de un EMA por pulso calculado en la ISR, con decay por silencio y
         * clamp de slew-rate, y portarlo toca `drv_pulsos` —que hoy sólo cuenta
         * y no lleva timestamps—.
         *
         * Hasta entonces se marca inválido en vez de mandar un cero: un caudal
         * de 0.000 es un valor perfectamente creíble para el servidor.
         */
        pxDr->fContador    = 0.0f;
        pxDr->usInvalidos |= dataINVALIDO_CONTADOR;
    }

    /* ---- 2. Analógicas de 4-20 mA ------------------------------------ */
    prvPolearAnalogicas( pxDr );

    /* ---- 3. Modbus --------------------------------------------------- */
    /* Paso 6. Los canales quedan en cero y NO se marcan inválidos: sin Modbus
       implementado, marcarlos sólo agregaría ruido a cada registro. */

    /* ---- 4. Rieles de alimentación ----------------------------------- */
    prvPolearRieles( pxDr );

    /* ---- 5. Válvula -------------------------------------------------- */
    /* ⚠ 0 = abierta, 1 = cerrada. Es al revés de lo intuitivo, y es como viaja
       en el frame (`&V0=%d`). No "corregirlo". */
    pxDr->ucValvula = ( drv_valvula_estado() == valvulaABIERTA ) ? 0U : 1U;

    /* ---- 6. La hora -------------------------------------------------- */
    if( !drv_rtc_leer( &pxDr->xRtc ) )
    {
        memset( &pxDr->xRtc, 0, sizeof( pxDr->xRtc ) );
        pxDr->usInvalidos |= dataINVALIDO_RTC;
    }
    else if( ( drv_rtc_validez() != rtcHORA_VALIDA ) ||
             ( pxDr->xRtc.year < TKSYS_ANIO_COMPILACION ) )
    {
        /*
         * ⚠ QUE EL CHIP CONTESTE NO SIGNIFICA QUE LA HORA SIRVA.
         *
         * Si el respaldo por pila falló, el MCP79410 arranca en `2001-01-01` y
         * la devuelve con toda naturalidad — es lo que se vio en banco el
         * 2026-09-08. Un datalogger que estampa esa fecha **sin marcarla** es
         * peor que uno que no estampa nada: los datos malos se mezclan con los
         * buenos y después no hay forma de separarlos.
         *
         * `drv_rtc_validez()` existe exactamente para esto y no es una
         * heurística sobre la fecha: mira la firma en la SRAM del propio chip,
         * que se alimenta de la misma pila que el contador de tiempo. Firma
         * intacta <=> el reloj nunca se detuvo.
         *
         * ⚠ **La firma certifica CONTINUIDAD, no CORRECCIÓN**, y eso apareció
         * en banco el 2026-09-08: el equipo informó `01/01/01` con la firma
         * intacta y el oscilador corriendo, o sea "hora válida". Pasa cuando
         * alguna vez se fijó una hora equivocada —la firma se escribe al fijar
         * la hora, y no puede saber si el que la fijó se equivocó— y desde
         * entonces el reloj viene contando sin interrupciones desde 2001.
         *
         * De ahí el segundo chequeo, que **no es una heurística arbitraria**
         * tipo "el año parece viejo": una muestra no puede ser ANTERIOR a la
         * compilación del firmware que la tomó. Si `year` es menor que el año
         * en que se compiló este binario, la hora es imposible, no improbable.
         *
         * La hora se conserva en el registro —sirve para diagnóstico— pero
         * queda marcada como no confiable.
         */
        pxDr->usInvalidos |= dataINVALIDO_RTC;
    }

    return ( pxDr->usInvalidos == 0U );
}
//------------------------------------------------------------------------------
void tkSys_print( const dataRcd_t *pxDr )
{
    uint8_t i;

    if( pxDr == NULL )
    {
        return;
    }

    /* Mismo formato que la consola del AVR: `campo=valor;` en una línea. La
       hora se imprime igual aunque no sea confiable —sirve para diagnóstico—
       pero marcada, para que nadie la copie a un informe sin darse cuenta. */
    xprintf( "%02d/%02d/%02d %02d:%02d:%02d%s;",
             pxDr->xRtc.day, pxDr->xRtc.month, pxDr->xRtc.year,
             pxDr->xRtc.hour, pxDr->xRtc.min, pxDr->xRtc.sec,
             ( pxDr->usInvalidos & dataINVALIDO_RTC ) ? "(HORA NO CONFIABLE)" : "" );

    for( i = 0U; i < CFG_AINPUTS_NRO_CANALES; i++ )
    {
        if( xCfgAinputs.xCanal[ i ].bEnabled )
        {
            if( pxDr->usInvalidos & ( uint16_t ) ( dataINVALIDO_AIN0 << i ) )
            {
                xprintf( "%s=SIN_DATO;", xCfgAinputs.xCanal[ i ].pcName );
            }
            else
            {
                xprintf( "%s=%0.2f;", xCfgAinputs.xCanal[ i ].pcName, pxDr->fAinputs[ i ] );
            }
        }
    }

    if( xCfgCounter.bEnabled )
    {
        if( pxDr->usInvalidos & dataINVALIDO_CONTADOR )
        {
            xprintf( "%s=SIN_DATO;", xCfgCounter.pcName );
        }
        else if( xCfgCounter.eModoMedida == CFG_CNT_PULSOS )
        {
            /* En PULSOS el AVR lo imprime como entero. Ojo: es sólo la
               impresión — al servidor viaja el mismo float. */
            xprintf( "%s=%d;", xCfgCounter.pcName, ( int ) pxDr->fContador );
        }
        else
        {
            xprintf( "%s=%0.3f;", xCfgCounter.pcName, pxDr->fContador );
        }
    }

    if( xCfgModbus.bEnabled )
    {
        for( i = 0U; i < CFG_MODBUS_NRO_CANALES; i++ )
        {
            if( xCfgModbus.xCanal[ i ].bEnabled )
            {
                xprintf( "%s=%0.3f;", xCfgModbus.xCanal[ i ].pcName, pxDr->fModbus[ i ] );
            }
        }
    }

    xprintf( "V0=%d;", ( int ) pxDr->ucValvula );

    if( pxDr->usInvalidos & dataINVALIDO_BT3V3 )
    {
        xprintf( "bt3v3=SIN_DATO;" );
    }
    else
    {
        xprintf( "bt3v3=%0.3f;", pxDr->fBt3v3 );
    }

    if( pxDr->usInvalidos & dataINVALIDO_BT12V )
    {
        xprintf( "bt12v=SIN_DATO;" );
    }
    else
    {
        xprintf( "bt12v=%0.3f;", pxDr->fBt12v );
    }

    xprintf( "\r\n" );

    /* El detalle sólo cuando hay algo que contar, para no ensuciar cada línea. */
    if( pxDr->usInvalidos != 0U )
    {
        xprintf( "  [!] campos sin dato (0x%04X):%s%s%s%s%s%s%s\r\n",
                 ( unsigned ) pxDr->usInvalidos,
                 ( pxDr->usInvalidos & dataINVALIDO_AIN0     ) ? " a0"       : "",
                 ( pxDr->usInvalidos & dataINVALIDO_AIN1     ) ? " a1"       : "",
                 ( pxDr->usInvalidos & dataINVALIDO_AIN2     ) ? " a2"       : "",
                 ( pxDr->usInvalidos & dataINVALIDO_CONTADOR ) ? " contador" : "",
                 ( pxDr->usInvalidos & dataINVALIDO_BT12V    ) ? " bt12v"    : "",
                 ( pxDr->usInvalidos & dataINVALIDO_BT3V3    ) ? " bt3v3"    : "",
                 ( pxDr->usInvalidos & dataINVALIDO_RTC      ) ? " rtc"      : "" );
    }
}
//------------------------------------------------------------------------------
const dataRcd_t *tkSys_ultimo( void )
{
    return &xUltimo;
}
//------------------------------------------------------------------------------
uint32_t tkSys_segundos_al_proximo( void )
{
    TickType_t xAhora = xTaskGetTickCount();

    if( xTicksProximoPoll <= xAhora )
    {
        return 0U;
    }

    return ( uint32_t ) ( ( xTicksProximoPoll - xAhora ) / configTICK_RATE_HZ );
}
//------------------------------------------------------------------------------
void tkSys( void *pvParameters )
{
    ( void ) pvParameters;

    TickType_t xLastWakeTime;

    /*
     * Espera de arranque. El primer registro sale tarde a propósito: los
     * periféricos acaban de inicializarse y la consola todavía está escupiendo
     * el banner.
     */
    vTaskDelay( pdMS_TO_TICKS( TKSYS_ARRANQUE_MS ) );

    xprintf( "\r\ntkSys arrancando, timerpoll = %u s\r\n",
             ( unsigned ) xCfgBase.usTimerPoll );

    xLastWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        ( void ) tkSys_poll( &xUltimo );
        tkSys_print( &xUltimo );

        /*
         * Al almacén. **Se guarda SIEMPRE**, incluso con campos inválidos: un
         * registro con -9999 es información —dice que a esa hora el equipo no
         * pudo medir— y descartarlo dejaría un hueco en la serie que del lado
         * del servidor es indistinguible de un equipo apagado.
         *
         * ⏳ En el paso 5, tkWan va a leer de acá: en modo CONTINUO transmite y
         * borra enseguida, en DISCRETO acumula hasta que toque discar.
         *
         * ⛔ **La excepción es el modo RTU**, que descarta lo que no puede
         * transmitir (Pablo, 2026-09-12): es una unidad remota, no un
         * datalogger. Ver `cfg_base.h`.
         */
        if( cfg_base_modo_sin_memoria() && !wan_hay_enlace() )
        {
            /*
             * ✅ Desde el paso 5d se consulta **el enlace real**: `wan_hay_enlace()`
             * dice si la FSM tiene sesión abierta con el servidor. Hasta que esa
             * tarea existió, `RTU` descartaba siempre porque no había a quién
             * preguntarle.
             *
             * ⚠ El contador NO es cosmético: descartar es la única situación en
             * la que este equipo pierde datos a propósito, y un RTU con el
             * enlace caído se ve exactamente igual que uno funcionando salvo por
             * este número. Es el mismo criterio que `ulPisados` de la ventana y
             * que el centinela -9999: **hacer visible lo que se perdió**.
             */
            ulDescartadosRtu++;

            xprintf( "tkSys:: modo RTU sin enlace: registro DESCARTADO (van %lu)\r\n",
                     ( unsigned long ) ulDescartadosRtu );
        }
        else if( !fs_datos_write( &xUltimo ) )
        {
            xprintf( "tkSys:: [!] no se pudo guardar el registro\r\n" );
        }
        else
        {
            /* guardado */
        }

        /*
         * ¿La ventana se está por llenar? Entonces se vuelca a la microSD.
         *
         * ⚠ **Al UMBRAL y no al llenarse del todo**, que es la diferencia entre
         * un margen y un borde: si se esperara a `count == length`, un volcado
         * fallido —tarjeta ausente, error de escritura— dejaría al equipo
         * pisando registros desde el intento siguiente. Con el umbral al 90 %
         * quedan ~198 registros de aire para reintentar, que a una muestra por
         * minuto son más de 3 horas.
         *
         * Si el equipo transmite con normalidad la ventana nunca llega acá y la
         * tarjeta no se toca nunca: la SD es una EXTENSIÓN para cuando el enlace
         * no está, no un histórico paralelo.
         */
        fs_datos_stats_t xStFs;

        fs_datos_stats( &xStFs );

        if( xStFs.usCount >= ( ( xStFs.usLength * TKSYS_UMBRAL_VOLCADO_PCT ) / 100U ) )
        {
            /*
             * ⛔ En modo SILENT la microSD **deja de ser una extensión y pasa a
             * ser el destino final**: no hay transmisión que vacíe la ventana,
             * así que siempre se llega acá. Si el volcado falla —no hay tarjeta—
             * los datos se van a perder cuando la ventana dé la vuelta, y eso
             * hay que decirlo fuerte y en el momento, no descubrirlo después.
             */
            if( !fs_sd_volcar_ventana() && cfg_base_modo_sin_modem() )
            {
                xprintf( "tkSys:: ⛔ SILENT sin microSD: los datos SE VAN A PERDER\r\n" );
            }
        }

        /*
         * `timerpoll` se lee EN CADA VUELTA, no una sola vez al arrancar: así un
         * cambio de configuración por consola —o del servidor, cuando exista—
         * tiene efecto sin reiniciar el equipo.
         *
         * ⚠ `pdMS_TO_TICKS` sobre `timerpoll * 1000` puede desbordar un
         * `uint32_t` recién a las 1193 horas, y el máximo configurable son 24 h,
         * así que no hay problema; pero la cuenta va en 32 bits a propósito.
         */
        TickType_t xPeriodo = pdMS_TO_TICKS( ( uint32_t ) xCfgBase.usTimerPoll * 1000UL );

        xTicksProximoPoll = xLastWakeTime + xPeriodo;

        /*
         * `vTaskDelayUntil` y no `vTaskDelay`: el período se cuenta desde el
         * despertar anterior, así que **lo que tarde el poleo no se acumula**.
         * Con `vTaskDelay` el equipo se iría atrasando un poco en cada vuelta y
         * al cabo de un día los registros no caerían en los minutos redondos.
         */
        vTaskDelayUntil( &xLastWakeTime, xPeriodo );
    }
}
//------------------------------------------------------------------------------
