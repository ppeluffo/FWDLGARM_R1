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
#include "caudal.h"
#include "caudal_log.h"
#include "drv_valvula.h"
#include "drv_rs485.h"
#include "modbus.h"
#include "drv_rtc79410.h"
#include "wdg.h"
#include "eventos.h"
#include "tkCmd.h"
#include "tkCtl.h"
#include "tkCtlPres.h"
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

/*
 * Cada cuántas horas sale un `RESUMEN` al registro de eventos, además del que
 * sale al cambiar el día. Sólo se usa con el registro ACTIVO, así que un equipo
 * de producción —sin `EVENTOS.ON`— no paga nada por esto.
 *
 * ⚠ Entra en `pdMS_TO_TICKS( 3600 * 1000 ) * TKSYS_RESUMEN_HORAS`: con el tick a
 * 512 Hz cada hora son 1 843 200 ticks, así que el techo antes de desbordar un
 * `uint32_t` son ~2330 h. No apretarlo igual: 6 h es la resolución que hace
 * falta para ver la curva de batería y stacks en una corrida de 72 h.
 */
#define TKSYS_RESUMEN_HORAS         6UL

/* Cuántos registros se descartaron por estar en modo RTU sin enlace. Ver el
   comentario en el lazo de la tarea: es la única pérdida deliberada de datos
   del equipo, y por eso se cuenta. */
static uint32_t ulDescartadosRtu = 0UL;

static float    fCauMagppActual  = -1.0f;   /* -1 fuerza la 1.ª configuración */

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
/*
 * ⭐ EL RIEL DEL MÓDULO SE PRENDE TEMPRANO, Y ESO NO ES UN DETALLE
 *
 * Un caudalímetro tarda segundos en arrancar. El AVR prende `EN_PWR_QMBUS`
 * **antes** de medir las analógicas, así ese arranque transcurre durante el
 * barrido de 1,4 s del INA3221: el tiempo total es el mismo pero **no se paga**,
 * porque se solapa con trabajo que había que hacer igual.
 *
 * Acá se hace lo mismo, con una mejora: en vez de esperar "2 s más" como el AVR
 * —un número que deja de valer si alguien cambia el `sensors_pwr_settle_time`—
 * se **mide** cuánto pasó desde que se prendió y se espera sólo lo que falte.
 */
static TickType_t xTickRielModbus;

static void prvPrenderModuloModbus( void )
{
    if( !xCfgModbus.bEnabled )
    {
        return;
    }

    drv_rs485_power( rs485RAIL_QMBUS, true );
    xTickRielModbus = xTaskGetTickCount();
}
//------------------------------------------------------------------------------
static void prvPolearModbus( dataRcd_t *pxDr )
{
    uint8_t i;

    if( !xCfgModbus.bEnabled )
    {
        return;
    }

    /*
     * ⚠ El BUS es compartido con `tkCtlPres`, que le habla al control de presión
     * por el mismo transceiver. Se toma antes de encenderlo y se suelta al
     * final: si el otro está en medio de una consigna, acá se espera.
     *
     * El timeout es generoso pero finito: una consigna completa son 30-45 s. Si
     * ni así se libera, hay algo trabado y **es mejor perder este poleo de
     * Modbus que colgar a `tkSys`**, que además mide las analógicas, el RTC y
     * guarda el registro.
     */
    if( !drv_rs485_tomar_bus( pdMS_TO_TICKS( 60000 ) ) )
    {
        uint8_t j;

        xprintf( "MODBUS:: el bus RS485 esta ocupado: se saltea el poleo\r\n" );

        for( j = 0U; j < CFG_MODBUS_NRO_CANALES; j++ )
        {
            if( xCfgModbus.xCanal[ j ].bEnabled )
            {
                pxDr->usInvalidos |= ( uint16_t ) ( dataINVALIDO_MODBUS0 << j );
            }
        }

        drv_rs485_power( rs485RAIL_QMBUS, false );
        return;
    }

    /* El transceiver se prende recién ahora: está listo en microsegundos y
       mientras tanto no tiene sentido tenerlo consumiendo. Prenderlo toma
       `pwrLOCK_RS485`, que es lo que evita que el tickless se coma bytes de las
       tramas — Modbus es todo ráfagas de bytes pegados. */
    drv_rs485_power( rs485RAIL_BUS, true );

    /* Lo que falte del arranque del módulo, descontando lo que ya tardaron las
       analógicas. Si tardaron más que eso, no se espera nada. */
    TickType_t xTranscurrido = xTaskGetTickCount() - xTickRielModbus;
    TickType_t xNecesario    = pdMS_TO_TICKS( MODBUS_MS_ARRANQUE_MODULO );

    if( xTranscurrido < xNecesario )
    {
        vTaskDelay( xNecesario - xTranscurrido );
    }

    for( i = 0U; i < CFG_MODBUS_NRO_CANALES; i++ )
    {
        if( !xCfgModbus.xCanal[ i ].bEnabled )
        {
            continue;
        }

        float       fValor = 0.0f;
        mb_result_t eRes   = mbOK;

        /* La MISMA función que usa el comando `modbus ch`: codecs, tipo,
           divisor y los 3 reintentos. */
        if( modbus_leer_canal( &xCfgModbus.xCanal[ i ], &fValor, &eRes ) )
        {
            pxDr->fModbus[ i ] = fValor;
        }
        else
        {
            /*
             * ⛔ Un canal que no se pudo leer se MARCA, no se rellena con cero.
             * Un caudal de 0.000 es un valor perfectamente creíble, así que un
             * cero inventado se mezcla con los buenos y después no hay forma de
             * separarlos. Es el mismo criterio que las analógicas, el contador y
             * la firma del RTC: hacer visible lo que no se sabe.
             */
            pxDr->fModbus[ i ]  = 0.0f;
            pxDr->usInvalidos  |= ( uint16_t ) ( dataINVALIDO_MODBUS0 << i );

            xprintf( "MODBUS:: ch%u [%s] SIN DATO: %s\r\n", ( unsigned ) i,
                     xCfgModbus.xCanal[ i ].pcName, drv_modbus_error_str( eRes ) );
        }
    }

    drv_rs485_power( rs485RAIL_BUS, false );

    /*
     * ⚠ El riel del MÓDULO sólo se apaga si el equipo va a dormir, igual que el
     * AVR (`if ( u_get_sleep_time(false) > 0 )`). En continuo el poleo vuelve en
     * `timerpoll` segundos, y apagarlo obligaría a pagar otra vez los 5 s de
     * arranque en cada vuelta — además de ciclar la alimentación del
     * caudalímetro una vez por minuto, para siempre.
     */
    if( wan_segundos_apagado() > 0UL )
    {
        drv_rs485_power( rs485RAIL_QMBUS, false );
    }

    drv_rs485_soltar_bus();
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
         * ✅ PASO 2b. **El dato que va al servidor es el CAUDAL, no los pulsos**:
         * el `modo_medida` sólo cambia cómo se imprime en consola, y al frame
         * viaja el mismo float en los dos casos. El caudal sale de un EMA que
         * la ISR calcula pulso a pulso (`caudal.c`), y acá sólo se lee lo que
         * esa ventana acumuló.
         *
         * ⚠ **Un caudal de 0 es un valor legítimo**, no un "sin dato": significa
         * que el flujo paró, y lo dice el decay por silencio. Por eso este canal
         * NO se marca inválido salvo que el contador esté deshabilitado.
         */
        caudal_t xCau;

        /*
         * `magpp` puede cambiar en caliente —por consola o por el servidor— y
         * el EMA acumulado con el valor viejo no significa nada con el nuevo.
         * Por eso al cambiar se reconfigura, y eso resetea el estado a
         * propósito: es preferible perder una ventana a reportar un caudal
         * calculado con dos constantes distintas.
         */
        if( xCfgCounter.fMagPP != fCauMagppActual )
        {
            fCauMagppActual = xCfgCounter.fMagPP;
            caudal_config( fCauMagppActual );
        }

        caudal_leer( &xCau );

        pxDr->fContador = xCau.fCaudal;

        /* Para la consola: cuántos pulsos entraron en esta ventana y cuántos se
           descartaron. ⭐ Los descartes son el dato que dice si el filtro
           pasa-bajos del hardware alcanza — sin ellos, simplificar el algoritmo
           habría sido a ciegas. */
        /* ⚠ Ya no se cachea nada para la consola: `cnt` usa `caudal_peek()` y
           lee el estado ACTUAL. Cachear acá hacía que el comando mostrara lo
           del último poleo, que con alguien puenteando el borne a mano es
           exactamente lo que no sirve. */
    }

    /* ---- 1b. El riel del módulo Modbus, TEMPRANO --------------------- */
    /* Se prende antes de las analógicas para que arranque mientras el INA3221
       hace su barrido. Ver `prvPrenderModuloModbus()`. */
    prvPrenderModuloModbus();

    /* ---- 2. Analógicas de 4-20 mA ------------------------------------ */
    prvPolearAnalogicas( pxDr );

    /* ---- 3. Modbus --------------------------------------------------- */
    prvPolearModbus( pxDr );

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

    /* ⚠ Los canales Modbus van DESPUÉS del contador, igual que en el frame: la
       consola y lo que viaja tienen que leerse en el mismo orden, o comparar
       una traza contra un frame se vuelve un ejercicio de paciencia. */
    if( xCfgModbus.bEnabled )
    {
        for( i = 0U; i < CFG_MODBUS_NRO_CANALES; i++ )
        {
            if( !xCfgModbus.xCanal[ i ].bEnabled )
            {
                continue;
            }

            if( pxDr->usInvalidos & ( uint16_t ) ( dataINVALIDO_MODBUS0 << i ) )
            {
                xprintf( "%s=SIN_DATO;", xCfgModbus.xCanal[ i ].pcName );
            }
            else
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
        xprintf( "  [!] campos sin dato (0x%04X):%s%s%s%s%s%s%s%s\r\n",
                 ( unsigned ) pxDr->usInvalidos,
                 ( pxDr->usInvalidos & dataINVALIDO_AIN0     ) ? " a0"       : "",
                 ( pxDr->usInvalidos & dataINVALIDO_AIN1     ) ? " a1"       : "",
                 ( pxDr->usInvalidos & dataINVALIDO_AIN2     ) ? " a2"       : "",
                 ( pxDr->usInvalidos & dataINVALIDO_CONTADOR ) ? " contador" : "",
                 ( pxDr->usInvalidos & dataINVALIDO_MODBUS_TODOS ) ? " modbus" : "",
                 ( pxDr->usInvalidos & dataINVALIDO_BT12V    ) ? " bt12v"    : "",
                 ( pxDr->usInvalidos & dataINVALIDO_BT3V3    ) ? " bt3v3"    : "",
                 ( pxDr->usInvalidos & dataINVALIDO_RTC      ) ? " rtc"      : "" );
    }

    /*==========================================================================
     * ⭐ EL LATIDO DEL CONTADOR: el acumulado de pulsos, por TX, sin que nadie
     * tipee nada.
     *========================================================================*/
    /*
     * Existe para que un log PASIVO pueda verificar el conteo. El total
     * acumulado sólo salía al tipear `cnt`, así que una captura de 48 h —donde
     * nadie manda comandos— no podía contestar la única pregunta que importa en
     * un test de contador: **¿se perdió algún pulso?** Del caudal no se puede
     * reconstruir, porque es un EMA promediado.
     *
     * Es la lección #1 del rearranque aplicada al contador: *sin RX no hay
     * diagnóstico, así que el TX tiene que hablar solo*. Con `timerpoll` de 60 s
     * son 2.880 checkpoints en 48 h y ~60 bytes cada uno.
     *
     * ⭐⭐ Y LOS DOS CONTADORES DE DESCARTE SON LA MITAD DEL VALOR. Si al cruzar
     * contra el generador faltan 5 pulsos y esta línea dice `cortos=5`, el caso
     * está cerrado en el acto —el contacto rebotó y el filtro los comió— en vez
     * de mandar a buscar un bug en el firmware. Y acusan a lugares OPUESTOS:
     * `cortos` al circuito de entrada, `imposibles` a la cadencia (ver
     * `caudal.h`).
     *
     * ⚠ VA EN SU PROPIA LÍNEA, no como un campo más del registro. Si fuera un
     * campo, se leería como parte del frame —y NO viaja al servidor—; con el
     * modo tag encima queda envuelta aparte y sale trivial de parsear.
     */
    if( xCfgCounter.bEnabled )
    {
        caudal_t xC;

        caudal_peek( &xC );

        xprintf( "CNT:: total=%lu pendientes=%lu cortos=%lu imposibles=%lu\r\n",
                 ( unsigned long ) drv_pulsos_total(),
                 ( unsigned long ) drv_pulsos_pendientes(),
                 ( unsigned long ) xC.ulCortos,
                 ( unsigned long ) xC.ulImposibles );
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
/*==============================================================================
 * EL RESUMEN DIARIO del registro de eventos (`eventos.h`, 2026-10-19)
 *
 * Es el control de estabilidad del banco —stacks, heap, ventana, contador,
 * rieles— hecho en campo, una vez por día. Sólo existe si el registro está
 * activo: con EVENTOS.ON ausente no se calcula nada.
 *============================================================================*/
static float      fBt3v3Min, fBt3v3Max, fBt12vMin, fBt12vMax;
static uint32_t   ulPoleosResumen;
static uint8_t    ucDiaResumen = 0xFFU;      /* 0xFF: el primero sale enseguida */
static TickType_t xTickResumen;

/*
 * Palabras de stack que una tarea NUNCA usó. Es lo mismo que
 * `uxTaskGetStackHighWaterMark()`, pero mirando el arreglo estático: así no
 * hacen falta los handles, y `tkCmd` no exporta el suyo. El stack crece hacia
 * abajo y FreeRTOS lo llena con 0xA5 al crear la tarea.
 */
static uint32_t prvStackLibre( const StackType_t *pxStack, uint32_t ulPalabras )
{
    uint32_t i = 0UL;

    while( ( i < ulPalabras ) && ( pxStack[ i ] == ( StackType_t ) 0xA5A5A5A5UL ) )
    {
        i++;
    }

    return i;
}

static void prvAcumularRieles( const dataRcd_t *pxDr )
{
    if( !( pxDr->usInvalidos & dataINVALIDO_BT3V3 ) )
    {
        if( ( ulPoleosResumen == 0UL ) || ( pxDr->fBt3v3 < fBt3v3Min ) ) { fBt3v3Min = pxDr->fBt3v3; }
        if( ( ulPoleosResumen == 0UL ) || ( pxDr->fBt3v3 > fBt3v3Max ) ) { fBt3v3Max = pxDr->fBt3v3; }
    }

    if( !( pxDr->usInvalidos & dataINVALIDO_BT12V ) )
    {
        if( ( ulPoleosResumen == 0UL ) || ( pxDr->fBt12v < fBt12vMin ) ) { fBt12vMin = pxDr->fBt12v; }
        if( ( ulPoleosResumen == 0UL ) || ( pxDr->fBt12v > fBt12vMax ) ) { fBt12vMax = pxDr->fBt12v; }
    }

    ulPoleosResumen++;
}

/*
 * ¿Toca resumen? Al cambiar el día **o** cada `TKSYS_RESUMEN_HORAS`; sin hora
 * confiable, sólo por tiempo. El primero sale con el primer poleo: es la línea
 * de base.
 *
 * ⭐ **Por qué hay un período además del cambio de día** (2026-10-20): el
 * resumen es la única fuente de la CURVA de batería, heap y stacks, y con una
 * línea por día una corrida de 72 h deja **tres puntos**. A 6 h son 12 resúmenes
 * —48 líneas contra el techo de 1 MB del archivo, nada— y eso ya es una curva.
 *
 * ⚠ Y el cambio de día **se conserva**: alinea un resumen a medianoche, que es
 * lo que hace legible el archivo cuando se lo lee a mano. Los dos criterios
 * pueden caer juntos (un resumen a las 23:xx y otro a las 00:0x) y está bien:
 * son cuatro líneas.
 */
static bool prvTocaResumen( const dataRcd_t *pxDr )
{
    bool bPorTiempo;

    if( ucDiaResumen == 0xFFU )
    {
        return true;
    }

    /* ⚠ En ticks, y troceado por la misma razón que las esperas de `tkWan`:
       `pdMS_TO_TICKS( horas * 3600 * 1000 )` desbordaría un `uint32_t` a partir
       de ~2,3 h con el tick a 512 Hz. Multiplicando DESPUÉS, 6 h son 11 059 200
       ticks y hay margen de sobra. */
    bPorTiempo = ( ( xTaskGetTickCount() - xTickResumen ) >=
                   ( pdMS_TO_TICKS( 3600UL * 1000UL ) * TKSYS_RESUMEN_HORAS ) );

    if( !( pxDr->usInvalidos & dataINVALIDO_RTC ) )
    {
        return ( ( pxDr->xRtc.day != ucDiaResumen ) || bPorTiempo );
    }

    return bPorTiempo;
}

/*
 * ⭐ LOS DESCARTES DEL CONTADOR, CUANDO CRECEN (2026-10-20)
 *
 * `cortos` e `imposibles` ya salen en el `RESUMEN`, pero ahí son **acumulados**:
 * si en 72 h aparecen 50 cortos, se sabe cuántos y no **cuándo**. Y en una
 * primera instalación eso es justamente el diagnóstico del circuito de entrada.
 *
 * ⚠ Los dos acusan a lugares OPUESTOS y por eso van separados: `cortos` al
 * contacto (rebotes que el pasa-bajos no filtró), `imposibles` a la cadencia
 * (pulsos más rápidos que el caudal máximo).
 *
 * ⭐ Se emite una línea **por crecimiento, con el delta**, no una por descarte:
 * un reed que rebota puede dar cientos seguidos y llenaría el archivo con lo
 * mismo. El llamador ya garantiza que esto sólo corre con el registro activo.
 */
static void prvVigilarDescartes( void )
{
    static uint32_t ulCortosPrev     = 0UL;
    static uint32_t ulImposiblesPrev = 0UL;
    static bool     bInicializado    = false;

    caudal_t xC;

    if( !xCfgCounter.bEnabled )
    {
        return;
    }

    caudal_peek( &xC );

    /* La línea de base no se informa: al arrancar los contadores pueden venir
       de una corrida anterior, y eso ya lo dice el primer `RESUMEN`. */
    if( !bInicializado )
    {
        bInicializado    = true;
        ulCortosPrev     = xC.ulCortos;
        ulImposiblesPrev = xC.ulImposibles;
        return;
    }

    if( ( xC.ulCortos > ulCortosPrev ) || ( xC.ulImposibles > ulImposiblesPrev ) )
    {
        eventos_log( "CNT descartes: +%lu cortos (total %lu), +%lu imposibles (total %lu)",
                     ( unsigned long ) ( xC.ulCortos - ulCortosPrev ),
                     ( unsigned long ) xC.ulCortos,
                     ( unsigned long ) ( xC.ulImposibles - ulImposiblesPrev ),
                     ( unsigned long ) xC.ulImposibles );

        ulCortosPrev     = xC.ulCortos;
        ulImposiblesPrev = xC.ulImposibles;
    }
}

static void prvResumen( const dataRcd_t *pxDr )
{
    fs_datos_stats_t xFs;
    uint32_t         ulOk, ulFallos, ulEnviados;

    fs_datos_stats( &xFs );
    wan_stats( &ulOk, &ulFallos, &ulEnviados );

    eventos_log( "RESUMEN: up %lu h | heap %u B | stack libre (palabras) sys %lu/%u wan %lu/%u cmd %lu/%u ctl %lu/%u cpres %lu/%u",
                 ( unsigned long ) ( xTaskGetTickCount() / configTICK_RATE_HZ / 3600UL ),
                 ( unsigned ) xPortGetFreeHeapSize(),
                 ( unsigned long ) prvStackLibre( tkSys_Stack, tkSys_STACK_SIZE ),         ( unsigned ) tkSys_STACK_SIZE,
                 ( unsigned long ) prvStackLibre( tkWan_Stack, tkWan_STACK_SIZE ),         ( unsigned ) tkWan_STACK_SIZE,
                 ( unsigned long ) prvStackLibre( tkCmd_Stack, tkCmd_STACK_SIZE ),         ( unsigned ) tkCmd_STACK_SIZE,
                 ( unsigned long ) prvStackLibre( tkCtl_Stack, tkCtl_STACK_SIZE ),         ( unsigned ) tkCtl_STACK_SIZE,
                 ( unsigned long ) prvStackLibre( tkCtlPres_Stack, tkCtlPres_STACK_SIZE ), ( unsigned ) tkCtlPres_STACK_SIZE );

    eventos_log( "RESUMEN: ventana %u/%u, pisados %lu | wan: vaciados ok %lu, sesiones fallidas %lu, registros enviados %lu | sd postergadas %lu | rtu descartados %lu",
                 ( unsigned ) xFs.usCount, ( unsigned ) xFs.usLength,
                 ( unsigned long ) xFs.ulPisados,
                 ( unsigned long ) ulOk, ( unsigned long ) ulFallos, ( unsigned long ) ulEnviados,
                 ( unsigned long ) fs_sd_postergadas(),
                 ( unsigned long ) ulDescartadosRtu );

    if( xCfgCounter.bEnabled )
    {
        caudal_t xC;

        caudal_peek( &xC );
        eventos_log( "RESUMEN: cnt total %lu, cortos %lu, imposibles %lu",
                     ( unsigned long ) drv_pulsos_total(),
                     ( unsigned long ) xC.ulCortos, ( unsigned long ) xC.ulImposibles );
    }

    eventos_log( "RESUMEN: bt3v3 %.3f..%.3f V | bt12v %.3f..%.3f V (en %lu poleos)",
                 ( double ) fBt3v3Min, ( double ) fBt3v3Max,
                 ( double ) fBt12vMin, ( double ) fBt12vMax,
                 ( unsigned long ) ulPoleosResumen );

    ulPoleosResumen = 0UL;
    xTickResumen    = xTaskGetTickCount();
    ucDiaResumen    = ( pxDr->usInvalidos & dataINVALIDO_RTC ) ? 0xFEU : pxDr->xRtc.day;
}
//------------------------------------------------------------------------------
void tkSys( void *pvParameters )
{
    ( void ) pvParameters;

    TickType_t xLastWakeTime;

    /* Antes de la espera de arranque: el plazo empieza a correr desde que la
       tarea existe, no desde que hace su primer trabajo. */
    wdg_registrar( wdgTK_SYS );

    /*
     * Espera de arranque. El primer registro sale tarde a propósito: los
     * periféricos acaban de inicializarse y la consola todavía está escupiendo
     * el banner.
     */
    vTaskDelay( pdMS_TO_TICKS( TKSYS_ARRANQUE_MS ) );

    xprintf( "\r\ntkSys arrancando, timerpoll = %u s\r\n",
             ( unsigned ) xCfgBase.usTimerPoll );

    /*
     * ⭐ ¿Este equipo está "a estudio"? Se decide UNA vez, acá: si la microSD
     * tiene EVENTOS.ON se registra. Va en esta tarea porque usa FatFs, y antes
     * del primer poleo para que el resumen de base ya caiga en el archivo.
     */
    wdg_kick_largo( 120000UL );
    eventos_init();
    wdg_kick();

    xLastWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        ( void ) tkSys_poll( &xUltimo );
        tkSys_print( &xUltimo );

        /* El poleo puede llevarse decenas de segundos —el barrido del INA son
           1,4 s y los 5 canales Modbus con reintentos hasta 15—, así que se
           renueva el plazo antes de encarar el almacenamiento. */
        wdg_kick();

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
            eventos_log( "[!] no se pudo guardar el registro en la EEPROM" );
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

        /*
         * La traza de pulsos, si está activa. Se chequea acá y no en la ISR
         * porque volcar enciende la microSD: tiene que pasar en el hilo de una
         * tarea, y este poleo es el momento natural.
         *
         * ⚠ Con 1000 registros y 500 pulsos/hora el umbral del 90 % llega cada
         * ~2 horas, o sea una vez cada 24 poleos: chequear en cada vuelta es de
         * sobra y no cuesta nada.
         */
        if( caudal_log_activo() && caudal_log_lleno() )
        {
            ( void ) fs_sd_volcar_pulsos();
        }

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
            /*
             * ⚠ PRÓRROGA, uno de los dos únicos casos del firmware.
             *
             * `fs_sd_volcar_ventana()` no vuelve hasta terminar: enciende la
             * tarjeta, la monta, escribe hasta 1984 líneas y desmonta, todo en
             * una llamada. No tiene dónde reportar sin que el watchdog se meta
             * dentro de `fs_sd`, que es una capa que no tiene por qué conocerlo.
             *
             * Los 120 s son generosos a propósito: una tarjeta lenta escribiendo
             * ~100 KB tarda segundos, no minutos, pero pasarse de plazo acá
             * resetearía el equipo **en medio de una escritura a FAT**, que es
             * exactamente el momento en que un corte hace daño de verdad.
             */
            wdg_kick_largo( 120000UL );

            if( !fs_sd_volcar_ventana() )
            {
                eventos_log( "[!] la ventana esta al %u %% y NO se pudo volcar a la microSD",
                             ( unsigned ) TKSYS_UMBRAL_VOLCADO_PCT );

                if( cfg_base_modo_sin_modem() )
                {
                    xprintf( "tkSys:: ⛔ SILENT sin microSD: los datos SE VAN A PERDER\r\n" );
                }
            }
            else
            {
                eventos_log( "la ventana llego al %u %%: volcada a un lote de la microSD",
                             ( unsigned ) TKSYS_UMBRAL_VOLCADO_PCT );
            }

            wdg_kick();    /* de vuelta al plazo normal */
        }

        /*
         * El registro de eventos: el resumen diario, y volcar si hace falta. En
         * DISCRETO lo vuelca `tkWan` al final de cada sesión; esto cubre el
         * CONTINUO —que no tiene fin de sesión— y el buffer que se llena.
         */
        if( eventos_estado() == eventosACTIVO )
        {
            bool bResumen;

            prvAcumularRieles( &xUltimo );
            prvVigilarDescartes();
            bResumen = prvTocaResumen( &xUltimo );

            if( bResumen )
            {
                prvResumen( &xUltimo );
            }

            if( bResumen || eventos_conviene_volcar() )
            {
                wdg_kick_largo( 120000UL );
                ( void ) eventos_volcar();
                wdg_kick();
            }
        }

        /*
         * `timerpoll` se lee EN CADA VUELTA, no una sola vez al arrancar: así un
         * cambio de configuración por consola —o del servidor— tiene efecto sin
         * reiniciar el equipo.
         */
        uint32_t ulSegundos = ( uint32_t ) xCfgBase.usTimerPoll;

        xTicksProximoPoll = xLastWakeTime
                            + pdMS_TO_TICKS( ulSegundos * 1000UL );

        /*
         * ⭐ LA ESPERA VA TROCEADA, y no es sólo por el watchdog.
         *
         * 1. **El watchdog** (paso 8) necesita que esta tarea dé señales de vida
         *    cada 90 s, y `timerpoll` llega hasta 24 h. Troceando a 60 s la
         *    tarea reporta durante toda la espera, así que un cuelgue suyo se
         *    detecta en un minuto y medio y no al día siguiente.
         *
         * 2. ⛔ **Y de paso se arregla un desborde que estaba vivo.** El
         *    comentario que había acá decía que `pdMS_TO_TICKS` sobre
         *    `timerpoll * 1000` desbordaba recién a las 1193 horas, y es FALSO:
         *    la macro **multiplica por `configTICK_RATE_HZ` antes de dividir por
         *    1000**, así que a 512 Hz desborda un `uint32_t` a partir de
         *    ~8388 s = **2,3 h**. Con `timerpoll` configurable hasta 86400 s, un
         *    equipo puesto a polear cada 3 horas esperaba cualquier cosa. En
         *    trozos de 60 s la cuenta ni se acerca al límite.
         *    (`tkWan::prvEsperar()` ya tenía el número bien.)
         *
         * ⭐ `vTaskDelayUntil` repetido **conserva la no-deriva**: cada trozo se
         * cuenta desde el despertar anterior, así que los trozos suman exacto y
         * lo que tarde el poleo no se acumula. Con `vTaskDelay` los registros se
         * irían corriendo de los minutos redondos a lo largo del día.
         */
        while( ulSegundos > 0UL )
        {
            uint32_t ulEste = ( ulSegundos > WDG_TROZO_ESPERA_S )
                              ? WDG_TROZO_ESPERA_S : ulSegundos;

            vTaskDelayUntil( &xLastWakeTime, pdMS_TO_TICKS( ulEste * 1000UL ) );
            ulSegundos -= ulEste;

            wdg_kick();
        }
    }
}
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
