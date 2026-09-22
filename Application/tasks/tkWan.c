/*
 * tkWan.c  -  ver tkWan.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tkWan.h"
#include "tkSys.h"
#include "tkCmd.h"
#include "wan_frame.h"
#include "fs_datos.h"
#include "fs_sd.h"
#include "cfg_nvm.h"
#include "drv_lte.h"
#include "drv_rtc79410.h"
#include "wdg.h"
#include "frtos-io.h"
#include "main.h"

TaskHandle_t xHandle_tkWan = NULL;

StaticTask_t tkWan_TCB;
StackType_t  tkWan_Stack[ tkWan_STACK_SIZE ];

static wan_estado_t eEstado    = wanAPAGADO;
static bool         bSesionFallo = false;   /* la última sesión terminó mal */
static bool         bHayEnlace = false;
static bool         bMatada    = false;

/* ⭐ Al arrancar se disca ENSEGUIDA, sin esperar el timer: es el `f_dial_now`
   del AVR, y existe para que un equipo recién energizado se configure y vacíe
   la memoria en vez de quedarse callado hasta el primer `timerdial`. */
static bool bDiscarYa = true;

/*------------------------------------------------------------------------------
 * Los tiempos de la sesión. ⏳ Ninguno está en el manual del módulo: son los
 * del AVR o puntos de partida generosos, **a ajustar en banco**.
 *----------------------------------------------------------------------------*/
#define WAN_MS_ESPERA_ARRANQUE   10000U  /* antes de la 1.ª sesión: que el resto
                                            del equipo termine de arrancar     */
#define WAN_MS_ARRANQUE_MODEM     8000U  /* de dar energía a que conteste AT   */
#define WAN_INTENTOS_AT               3U
#define WAN_MS_ENTRE_INTENTOS     5000U

/* ⚠ Los reintentos de CSQ/CIP **son** el poll de registro a la red: no es que
   el comando falle, es que el módulo todavía no se registró. Ver el comentario
   de `prvEstadoOffline()`. */
#define WAN_INTENTOS_RED              3U
#define WAN_MS_ENTRE_RED          5000U

/* Los intentos de PING. Es el `PING_TRYES` del AVR, y con los 15 s de timeout
   de cada uno dan **75 s de ventana de atache**. Ver `prvPingConReintentos()`. */
#define WAN_INTENTOS_PING             5U

/*
 * ⚠ Cuánto esperar cuando una sesión FALLÓ, aun en modo continuo.
 *
 * Sin esto el equipo **martilla**: en `CONTINUO` la espera de `APAGADO` es de
 * 1 s, así que un módulo que todavía no se atachó se prende y se apaga una vez
 * por minuto sin ninguna chance de converger — y encima cada ciclo le corta la
 * alimentación a un módulo que estaba arrancando, que es justo lo que puede
 * corromperle la flash. Se vio en banco el 2026-09-21.
 */
#define WAN_SEG_TRAS_FALLO          120UL

/* A cuánto se espacia el equipo cuando el servidor no lo reconoce. Es el valor
   del AVR: una hora. */
#define WAN_SEG_ESPACIADO          3600U

//------------------------------------------------------------------------------
/*------------------------------------------------------------------------------
 * Modem LTE
 *----------------------------------------------------------------------------*/

/* Acá adentro entran el `ftime` del módulo, la petición HTTP por LTE y la
   respuesta del servidor. Por eso es mucho más largo que un AT. */
#define LTE_PING_TIMEOUT_MS 15000U

/*
 * Cuántos frames se mandan sin esperar respuesta antes de pedir confirmación.
 * Es el valor del AVR y se mantiene: cuanto más grande, más rápido el vaciado,
 * pero más registros se retransmiten si la sesión se corta.
 */
#define LTE_DATA_VENTANA    10U

/*
 * ⚠ La pausa entre frames, y **no es opcional**: el DTU delimita cada trama por
 * SILENCIO en la serie (su `ftime`, configurado en 250 ms). Sin esta espera dos
 * frames se le juntan en un solo GET. Va con margen sobre el `ftime`.
 */
#define LTE_DATA_MS_ENTRE_FRAMES    500U

/*
 * Cuánto espera CADA lectura mientras se busca la respuesta buena. No es el
 * timeout total —ése lo pone el lazo de `prvEsperarRespuestaConClase()`— sino el
 * tiempo que se le da a una trama para aparecer antes de volver a mirar.
 */
#define LTE_DATA_MS_POR_LECTURA     3000U
//------------------------------------------------------------------------------
/*
 * Deja el módulo en modo comando, venga de donde venga.
 *
 * ⚠ Existe porque `lte esc` **falla si el módulo YA está en modo comando**: el
 * `+++` se lee como texto cualquiera, no contesta la `a`, y el resultado es
 * `SIN_A` — el mismo que cuando no escucha nada. Ese error es el que confunde
 * cuando uno va y viene entre consultas.
 *
 * El orden importa: **primero el escape** y sólo si falla se prueba un `AT`. Al
 * revés, un `AT` mandado en modo transparente **se iría a la red como datos**;
 * es inocuo pero ensucia, y no hay razón para pagarlo en el caso normal.
 */
/*
 * ⚠ LOS COMANDOS DE CONFIGURACIÓN NO ENTRAN SOLOS EN MODO AT. SI NO ESTÁ, FALLAN.
 *
 * Es el modelo del AVR y lo pidió Pablo explícitamente (2026-09-09): *"con un
 * comando lo pongo en modo AT y con otro lo saco. Luego tengo comandos que
 * ASUMIENDO que está en modo AT le mandan la configuración o leen. Estos
 * comandos NO intentan ponerlo. Si no está, fallan."*
 *
 * O sea: `lte esc` entra, `lte exit` sale, y en el medio `info`/`set`/`save`
 * sólo hablan. El estado del módulo lo maneja el técnico, que sabe en cuál está
 * porque lo puso él.
 *
 * **Y elimina de raíz el bug del 2026-09-09**: intentar el escape "por las
 * dudas" mandaba `+++` cuando el módulo YA estaba en modo comando. Ese `+++` va
 * sin CR —es una contraseña, no un comando— así que quedaba colgado en el buffer
 * del módulo y el `AT` siguiente se le concatenaba: leía `+++AT`, contestaba
 * ERROR, y desde afuera se veía como "no se pudo entrar en modo comando"
 * **estando adentro**. Un estado explícito no tiene ese problema.
 */
bool wan_modem_listo( void )
{
    if( !drv_lte_power_estado() )
    {
        xprintf( "el modem esta APAGADO: 'lte on' primero\r\n" );
        return false;
    }

    return true;
}
//------------------------------------------------------------------------------
/*
 * Una vuelta completa contra el servidor: escribe el frame y espera la
 * respuesta. La usan CONF_ALL y los cinco bloques, que hacen exactamente lo
 * mismo, y por eso está acá afuera: con seis copias, un arreglo en una se
 * olvida en las otras cinco.
 *
 * ⚠ **ASUME modo TRANSPARENTE.** Ver `prvLtePing()`.
 */
/*
 * ⛔ Avisa si se está por transmitir con el IMEI FALSO.
 *
 * El 2026-09-21 se transmitió una sesión entera con los 15 ceros y nada lo
 * dijo: el IMEI se cachea recién cuando alguien corre `lte info`, y esa corrida
 * empezó con `lte clock set`. **El servidor los aceptó**, así que esos frames
 * quedaron en la base atribuidos a un equipo que no existe.
 *
 * ⚠ **Avisa pero no impide**, que es el criterio de este firmware (igual que
 * `cfg_nvm_chequear_nombres()`): en banco a veces se quiere transmitir sin
 * haber leído el IMEI, y un comando que se niega en medio de una prueba es peor
 * que uno que advierte.
 */
static void prvAvisarImeiFalso( void )
{
    if( wan_imei_es_falso() )
    {
        xprintf( "[!] el IMEI es el FALSO (15 ceros): nadie se lo pregunto al modulo.\r\n" );
        xprintf( "    los frames van a quedar en el servidor sin equipo que los reclame.\r\n" );
        xprintf( "    correr 'lte esc' + 'lte info' para leerlo, y despues 'lte exit'.\r\n" );
    }
}
//------------------------------------------------------------------------------
static bool prvLteTxRx( const char *pcFrame, uint16_t usLargo,
                        char *pcRta, uint16_t usRtaSize )
{
    xprintf( "-> " );
    ( void ) frtos_write( fdTERM, pcFrame, usLargo );
    xprintf( "\r\n" );

    drv_lte_flush();

    if( drv_lte_write( pcFrame, usLargo ) != ( int16_t ) usLargo )
    {
        xprintf( "ERROR: no se pudo transmitir\r\n" );
        return false;
    }

    int16_t sRet = drv_lte_read( pcRta, usRtaSize - 1U, LTE_PING_TIMEOUT_MS );

    if( sRet <= 0 )
    {
        xprintf( "sin respuesta en %u ms.\r\n", ( unsigned ) LTE_PING_TIMEOUT_MS );
        xprintf( "  el modulo esta en modo TRANSPARENTE? (en modo comando NO transmite)\r\n" );
        return false;
    }

    pcRta[ sRet ] = '\0';

    xprintf( "<- " );
    ( void ) frtos_write( fdTERM, pcRta, ( uint16_t ) sRet );
    xprintf( "\r\n" );

    return true;
}
//------------------------------------------------------------------------------
/*
 * Los frames `CONF_*`: por cada bloque que el servidor pidió, se le manda su
 * hash y él contesta con la configuración que quiere que tenga el equipo.
 *
 * Tres cosas que no son obvias:
 *
 *  - **Se sigue con los demás aunque uno falle.** Es lo que hace el AVR
 *    (`wan_state_online_config()`: sólo `conf_base` aborta la secuencia) y es
 *    lo que permite que un `FLOWC` que nunca vamos a configurar no deje al
 *    equipo sin transmitir una sola muestra.
 *  - **Se verifica que la respuesta sea de ESTE bloque** antes de aplicarla. Es
 *    el `wan_check_response()` del AVR, y no es paranoia: con el módulo en
 *    transparente una respuesta demorada del frame anterior llega igual, y
 *    aplicar la configuración de un bloque leyendo la respuesta de otro
 *    escribiría basura con toda naturalidad.
 *  - **Se graba UNA sola vez al final**, y sólo si algo cambió. Ver
 *    `wan_conf_aplicar()`.
 */
static void prvLteConfBloques( const wan_conf_flags_t *pxFlags )
{
    static char pcFrame[ WAN_FRAME_BUFFER_SIZE ];
    static char pcRta  [ WAN_FRAME_BUFFER_SIZE ];
    char        pcEsperado[ 24 ];
    bool        bAlgo = false;
    uint8_t     i;

    for( i = 0U; i < ( uint8_t ) wanBLOQUE_NRO; i++ )
    {
        wan_bloque_t eBloque = ( wan_bloque_t ) i;

        /* Un bloque por vuelta, cada uno con su ida y vuelta al servidor: seis
           timeouts seguidos superan el plazo del watchdog sin que nada esté
           colgado. Y como `wdg_kick()` resuelve por la tarea que corre, esto
           sirve igual cuando la sesión la lanza `lte conf` desde la consola. */
        wdg_kick();

        if( !wan_conf_pedido( pxFlags, eBloque ) )
        {
            continue;
        }

        xprintf( "\r\n--- %s ---\r\n", wan_conf_clase( eBloque ) );

        uint16_t usLargo = wan_frame_conf_bloque( pcFrame, sizeof( pcFrame ), eBloque );

        if( usLargo == 0U )
        {
            continue;
        }

        if( !prvLteTxRx( pcFrame, usLargo, pcRta, sizeof( pcRta ) ) )
        {
            continue;
        }

        snprintf( pcEsperado, sizeof( pcEsperado ), "CLASS=%s", wan_conf_clase( eBloque ) );

        if( strstr( pcRta, pcEsperado ) == NULL )
        {
            xprintf( "[!] la respuesta no es de %s: se descarta\r\n", wan_conf_clase( eBloque ) );
            continue;
        }

        switch( wan_conf_aplicar( eBloque, pcRta ) )
        {
            case wanCONF_OK:
                xprintf( "CONFIG=OK: este bloque ya coincide\r\n" );
                break;

            case wanCONF_RECONFIGURAR:
                bAlgo = true;
                break;

            case wanCONF_DESCONOCIDO:
                xprintf( "[!] el servidor NO RECONOCE al equipo\r\n" );
                break;

            default:
                xprintf( "[!] no se aplico nada de este bloque\r\n" );
                break;
        }
    }

    xprintf( "\r\n" );

    if( !bAlgo )
    {
        xprintf( "no cambio nada: no se graba la EEPROM\r\n" );
        return;
    }

    /* Con la configuración nueva puede aparecer lo de siempre: dos canales con
       el mismo nombre. Se avisa, no se impide — igual que en `config save`. */
    ( void ) cfg_nvm_chequear_nombres();

    if( cfg_nvm_save_all() )
    {
        xprintf( "configuracion nueva GRABADA en la EEPROM\r\n" );
        xprintf( "verificar los hashes con 'config' y repetir 'lte conf'\r\n" );
    }
    else
    {
        xprintf( "ERROR: no se pudo grabar la configuracion en la EEPROM !!\r\n" );
    }
}
//------------------------------------------------------------------------------
/*
 * El servidor no nos reconoce: **el equipo se espacia solo a una sesión por
 * hora**. Es lo que hace el AVR (`PWR_DISCRETO` con `timerdial` y `timerpoll`
 * en 3600) y es la política correcta: insistir cada minuto contra un servidor
 * que no te va a contestar sólo gasta batería y tráfico.
 *
 * ⚠ **Esto NO estaba mientras la sesión era sólo comandos de banco**, y a
 * propósito: que un comando tipeado a mano cambiara la configuración del equipo
 * por lo bajo habría sido peor que el problema. Con la FSM sí corresponde —nadie
 * está mirando— y por eso entra recién ahora (acordado, 2026-09-11).
 *
 * ⚠ **Se graba en la EEPROM**: si no, un reset volvería a la configuración
 * anterior y el equipo retomaría el ciclo corto contra un servidor que sigue sin
 * reconocerlo.
 */
static void prvEspaciarPorDesconocido( void )
{
    if( xCfgBase.ePwrModo == PWR_DISCRETO )
    {
        if( ( xCfgBase.usTimerDial >= WAN_SEG_ESPACIADO ) &&
            ( xCfgBase.usTimerPoll >= WAN_SEG_ESPACIADO ) )
        {
            return;     /* ya está espaciado: no hay nada que hacer */
        }
    }

    xprintf( "tkWan:: reconfigurando para reintentar en 1 H (DISCRETO, %u s)\r\n",
             ( unsigned ) WAN_SEG_ESPACIADO );

    xCfgBase.ePwrModo    = PWR_DISCRETO;
    xCfgBase.usTimerDial = WAN_SEG_ESPACIADO;
    xCfgBase.usTimerPoll = WAN_SEG_ESPACIADO;

    if( !cfg_nvm_save_all() )
    {
        xprintf( "tkWan:: [!] no se pudo grabar: el espaciado se pierde en el proximo reset\r\n" );
    }
}
//------------------------------------------------------------------------------
/*
 * `CONF_ALL`: manda un hash por bloque y el servidor contesta cuáles quiere
 * reconfigurar. Es el paso donde el contrato del hash se prueba de verdad.
 *
 * ⚠ **ASUME modo TRANSPARENTE**, igual que `lte ping`.
 *
 * Y si pide algo, sigue con los `CONF_*`: le pregunta bloque por bloque y
 * **aplica lo que conteste**, grabando una sola vez al final. Es la secuencia
 * completa de configuración, la misma que va a correr sola la FSM del paso 5c.
 */
void wan_sesion_config( void )
{
    static char pcFrame[ WAN_FRAME_BUFFER_SIZE ];
    static char pcRta  [ WAN_FRAME_BUFFER_SIZE ];

    if( !wan_modem_listo() )
    {
        return;
    }

    if( !wan_csq_valido() )
    {
        /*
         * Se avisa pero NO se aborta: el frame sale igual con CSQ=0, y que el
         * servidor conteste prueba que el enlace está aunque la señal no se haya
         * podido medir. Abortar acá escondería el resultado que se vino a ver.
         */
        xprintf( "[!] el CSQ leido no es una medida (99 = desconocido, >=31 = todavia\r\n" );
        xprintf( "    no campo en la red). Correr 'lte esc' + 'lte info' para refrescarlo.\r\n" );
    }

    prvAvisarImeiFalso();

    uint16_t usLargo = wan_frame_conf_all( pcFrame, sizeof( pcFrame ) );

    if( usLargo == 0U )
    {
        return;
    }

    if( !prvLteTxRx( pcFrame, usLargo, pcRta, sizeof( pcRta ) ) )
    {
        return;
    }

    wan_conf_flags_t xFlags;

    switch( wan_frame_conf_all_rta( pcRta, &xFlags ) )
    {
        case wanCONF_OK:
            xprintf( "CONFIG=OK: la configuracion del equipo coincide con la del servidor\r\n" );
            break;

        case wanCONF_DESCONOCIDO:
            xprintf( "[!] el servidor NO RECONOCE al equipo (CONFIG=ERROR / FAIL).\r\n" );
            xprintf( "    hay que dar de alta el IMEI %s en el servidor.\r\n", wan_imei() );
            prvEspaciarPorDesconocido();
            break;

        case wanCONF_RECONFIGURAR:
            xprintf( "el servidor pide reconfigurar:%s%s%s%s%s%s\r\n",
                     xFlags.bBase        ? " BASE"     : "",
                     xFlags.bAinputs     ? " AINPUT"   : "",
                     xFlags.bCounter     ? " COUNTER"  : "",
                     xFlags.bModbus      ? " MODBUS"   : "",
                     xFlags.bConsigna    ? " PRESION"  : "",
                     xFlags.bFlowcontrol ? " FLOWC"    : "" );

            /*
             * ⛔ Acá había un aviso diciendo que `FLOWC` se pedía siempre porque
             * no mandábamos su hash. **Dejó de ser cierto el 2026-09-22**, con
             * el sexto hash: ahora el bloque existe y se configura como
             * cualquier otro. Un mensaje que explica un comportamiento que ya no
             * ocurre es peor que no tener mensaje — manda a no investigar algo
             * que sí puede estar fallando.
             */
            prvLteConfBloques( &xFlags );
            break;

        default:
            xprintf( "respuesta no reconocida\r\n" );
            break;
    }
}
//------------------------------------------------------------------------------
/*
 * Espera la respuesta a un frame **descartando las que no son de él**.
 *
 * ⛔ EL PROBLEMA QUE RESUELVE, encontrado en banco el 2026-09-21 con el log del
 * servidor al lado:
 *
 * **El servidor contesta a TODOS los GET, también a los `DATANR`.** El
 * `NR` —"no response"— es una convención de la capa de aplicación: dice que el
 * equipo no va a *esperar* la respuesta, no que el servidor no la mande. Por
 * HTTP siempre hay respuesta, y para un `DATANR` es un cuerpo **vacío**, que
 * envuelto en HTML llega como `<html></html>`.
 *
 * Esas respuestas viajan por LTE con cientos de ms de latencia, así que **llegan
 * cuando ya estamos mandando el frame siguiente**. Los tiempos del banco:
 *
 *     14:41:46,455  el servidor procesa el DATANR #9   -> responde vacio
 *     14:41:47,445  el servidor procesa el DATA        -> responde CLASS=DATA&CLOCK=...
 *
 * Con una pausa de 500 ms entre frames, el `drv_lte_flush()` de antes del `DATA`
 * corre **justo antes** de que llegue la respuesta del `DATANR` anterior: la
 * limpiamos cuando todavía venía en camino, y después leímos **esa** en lugar de
 * la nuestra. El síntoma era un `<html></html>` vacío que parecía un rechazo del
 * servidor, cuando en su log figuraba `raw_response->CLASS=DATA&CLOCK=2609211441`.
 *
 * ⭐ **El AVR no tiene este problema y vale la pena entender por qué**: aquel
 * ACUMULA todo lo que llega en un buffer y busca el patrón con `strstr`
 * (`wan_check_response`), así que una respuesta demorada simplemente queda
 * delante y no estorba. Nosotros leemos **una trama delimitada por silencio** y
 * la evaluamos sola — que es mejor para todo lo demás, pero necesita esto.
 *
 * ⚠ Descartar es lo correcto y no un parche: una respuesta sin `CLASS=` **no
 * lleva información** —es el acuse vacío de un `DATANR`— así que perderla no
 * pierde nada. Lo que no se puede es tomarla por la respuesta de otro frame.
 */
static bool prvEsperarRespuestaConClase( char *pcRta, uint16_t usSize,
                                         uint32_t ulTimeoutMs )
{
    TickType_t xInicio  = xTaskGetTickCount();
    TickType_t xLimite  = pdMS_TO_TICKS( ulTimeoutMs );
    uint8_t    ucVacias = 0U;

    while( ( xTaskGetTickCount() - xInicio ) < xLimite )
    {
        int16_t sRet = drv_lte_read( pcRta, usSize - 1U, LTE_DATA_MS_POR_LECTURA );

        if( sRet <= 0 )
        {
            continue;   /* nada todavía; el lazo decide cuándo rendirse */
        }

        pcRta[ sRet ] = '\0';

        if( strstr( pcRta, "CLASS=" ) != NULL )
        {
            if( ucVacias > 0U )
            {
                xprintf( "   (se descartaron %u acuses vacios de DATANR anteriores)\r\n",
                         ( unsigned ) ucVacias );
            }

            return true;
        }

        /* Sin `CLASS=` no hay nada que interpretar: es el acuse de un DATANR que
           venía atrasado. Se cuenta para poder decirlo, y se sigue esperando. */
        ucVacias++;
    }

    if( ucVacias > 0U )
    {
        xprintf( "   (llegaron %u acuses vacios, pero ninguna respuesta con CLASS=)\r\n",
                 ( unsigned ) ucVacias );
    }

    return false;
}
//------------------------------------------------------------------------------
/*
 * Arma el frame que se va a transmitir a partir de **una línea del lote**, que
 * trae sólo la parte de datos (`DATE=…`).
 *
 *     prefijo construido AHORA          +  '&'  +  la línea del archivo
 *     ID=..&HW=..&TYPE=..&VER=..&CLASS=..        DATE=..&TIME=..&…
 *
 * ⭐ Construir el prefijo en el momento de transmitir es todo el punto del
 * formato: el `ID` es el **IMEI del módulo que hay hoy**, no el que había cuando
 * se guardó el lote — si no, cambiar el módulo LTE haría que los lotes
 * pendientes salieran con el IMEI viejo. Y el `CLASS` sale directamente como
 * corresponde, sin tener que reescribir nada.
 *
 * ⚠ **Tolera los lotes del formato VIEJO**, que guardaban el frame entero: si la
 * línea ya empieza con `ID=`, se manda tal cual. Sin esto, una tarjeta con lotes
 * de antes del cambio transmitiría frames con el prefijo duplicado. Cuando no
 * queden lotes viejos en ninguna tarjeta, esta rama se puede sacar.
 */
static uint16_t prvLineaArmarFrame( char *pcDestino, uint16_t usSize,
                                    const char *pcLinea, bool bConRespuesta )
{
    if( ( pcDestino == NULL ) || ( pcLinea == NULL ) || ( usSize < 2U ) )
    {
        return 0U;
    }

    /* Formato viejo: la línea YA es un frame completo. */
    if( strncmp( pcLinea, "ID=", 3U ) == 0 )
    {
        int iN = snprintf( pcDestino, usSize, "%s", pcLinea );

        return ( ( iN > 0 ) && ( ( uint16_t ) iN < usSize ) ) ? ( uint16_t ) iN : 0U;
    }

    uint16_t usIdx = wan_frame_prefijo( pcDestino, usSize, bConRespuesta );

    if( usIdx == 0U )
    {
        return 0U;
    }

    int iN = snprintf( &pcDestino[ usIdx ], ( size_t ) ( usSize - usIdx ), "&%s", pcLinea );

    if( ( iN < 0 ) || ( ( uint16_t ) iN >= ( uint16_t ) ( usSize - usIdx ) ) )
    {
        return 0U;      /* no entra: mejor no transmitir que transmitir cortado */
    }

    return ( uint16_t ) ( usIdx + ( uint16_t ) iN );
}
//------------------------------------------------------------------------------
/*
 * La segunda mitad del vaciado: **los lotes de la microSD**.
 *
 * Van después de la ventana (criterio de Pablo, 2026-09-08): no es el orden
 * cronológico y está bien, porque el servidor indexa por la fecha que viaja
 * adentro de cada frame. De paso, vaciar la ventana primero la libera justo
 * antes de la parte larga.
 *
 * ⚠ **No se imprime cada frame**, a diferencia de la ventana: un lote son hasta
 * 1984 líneas de ~153 bytes, y sacarlas por la consola a 9600 son **más de
 * cinco minutos por lote** de puro log. Se informa por bloque confirmado.
 *
 * ⚠ **El lote se borra sólo si se transmitió ENTERO.** Si se corta en el medio
 * queda y se retransmite completo la próxima vez — con duplicados de lo que ya
 * había llegado, que son inofensivos. Ver `fs_sd.h`.
 */
static bool prvLteLotes( void )
{
    /*
     * Dos buffers porque hace falta **mirar una línea adelante**: el frame que
     * cierra el lote tiene que ir como `CLASS=DATA` para que el servidor
     * confirme, y no hay forma de saber que una línea es la última hasta
     * intentar leer la siguiente.
     */
    static char pcActual   [ WAN_FRAME_BUFFER_SIZE ];
    static char pcSiguiente[ WAN_FRAME_BUFFER_SIZE ];
    static char pcFrame    [ WAN_FRAME_BUFFER_SIZE ];   /* prefijo + la línea */
    static char pcRta      [ WAN_FRAME_BUFFER_SIZE ];

    char     pcNombre[ FS_SD_NOMBRE_LARGO ];
    uint16_t usLotes = 0U;

    while( fs_sd_lote_abrir( pcNombre, sizeof( pcNombre ) ) )
    {
        uint32_t ulLineas       = 0UL;
        uint32_t ulConfirmadas  = 0UL;
        uint16_t usSinConfirmar = 0U;
        bool     bCompleto      = true;

        xprintf( "\r\n--- %s ---\r\n", pcNombre );

        bool bHayActual = fs_sd_lote_leer( pcActual, sizeof( pcActual ) );

        while( bHayActual )
        {
            bool bHaySiguiente = fs_sd_lote_leer( pcSiguiente, sizeof( pcSiguiente ) );

            /* La última del lote SIEMPRE pide confirmación: si no, lo que queda
               del último bloque nunca se daría por bueno y el lote no se podría
               borrar. */
            bool bConfirmar = ( !bHaySiguiente ) ||
                              ( ( usSinConfirmar + 1U ) >= LTE_DATA_VENTANA );

            uint16_t usLargo = prvLineaArmarFrame( pcFrame, sizeof( pcFrame ),
                                                   pcActual, bConfirmar );

            if( usLargo == 0U )
            {
                xprintf( "[!] no se pudo armar el frame de la linea %lu: se salta\r\n",
                         ( unsigned long ) ( ulLineas + 1UL ) );
            }
            else
            {
                if( bConfirmar )
                {
                    drv_lte_flush();
                }

                if( drv_lte_write( pcFrame, usLargo ) != ( int16_t ) usLargo )
                {
                    xprintf( "ERROR: no se pudo transmitir\r\n" );
                    bCompleto = false;
                    break;
                }

                ulLineas++;
                usSinConfirmar++;

                if( bConfirmar )
                {
                    if( !prvEsperarRespuestaConClase( pcRta, sizeof( pcRta ),
                                                      LTE_PING_TIMEOUT_MS ) )
                    {
                        xprintf( "sin respuesta: el lote queda para la proxima\r\n" );
                        bCompleto = false;
                        break;
                    }

                    wan_data_ordenes_t xOrdenes;

                    if( wan_frame_data_rta( pcRta, &xOrdenes ) != wanDATA_ACEPTADO )
                    {
                        xprintf( "[!] respuesta inesperada: el lote queda para la proxima\r\n" );
                        bCompleto = false;
                        break;
                    }

                    ulConfirmadas += usSinConfirmar;
                    usSinConfirmar = 0U;

                    xprintf( "  %lu lineas confirmadas\r\n",
                             ( unsigned long ) ulConfirmadas );

                    if( xOrdenes.bReset )
                    {
                        /* Igual que en la ventana: el RESET se atiende, pero acá
                           **sin borrar el lote** — se retransmite entero al
                           volver, que es lo correcto porque no se confirmó todo. */
                        xprintf( "el servidor pide RESET: reiniciando...\r\n" );
                        fs_sd_lote_cerrar( false );
                        vTaskDelay( pdMS_TO_TICKS( 2000 ) );
                        NVIC_SystemReset();
                    }
                }
            }

            if( bHaySiguiente )
            {
                memcpy( pcActual, pcSiguiente, sizeof( pcActual ) );
                vTaskDelay( pdMS_TO_TICKS( LTE_DATA_MS_ENTRE_FRAMES ) );
            }

            /* ⭐ Un lote son hasta 1984 líneas y varios minutos de transmisión.
               Reportar por línea —en vez de pedir una prórroga por el lote
               entero— deja al watchdog vigilando durante toda la operación, que
               es justo la más larga que hace el equipo. */
            wdg_kick();

            bHayActual = bHaySiguiente;
        }

        /*
         * ⭐ Se borra SÓLO si se transmitió entero y no quedó nada sin
         * confirmar. Las dos condiciones: `bCompleto` dice que no se cortó, y
         * `usSinConfirmar == 0` que el último bloque se dio por bueno.
         */
        bool bBorrar = bCompleto && ( usSinConfirmar == 0U ) && ( ulLineas > 0UL );

        fs_sd_lote_cerrar( bBorrar );

        if( !bBorrar )
        {
            xprintf( "[!] %s NO se borro: se retransmite entero la proxima vez\r\n",
                     pcNombre );
            return false;   /* si uno falló, los demás también van a fallar */
        }

        usLotes++;
    }

    if( usLotes > 0U )
    {
        xprintf( "\r\n%u lote(s) transmitidos y borrados\r\n", ( unsigned ) usLotes );
    }

    return true;
}
//------------------------------------------------------------------------------
/*
 * El vaciado de la VENTANA (la EEPROM): transmite los registros guardados y los
 * borra **recién cuando el servidor confirmó**.
 *
 * ⚠ **ASUME modo TRANSPARENTE**, igual que `lte ping` y `lte conf`.
 *
 * La estructura es la del AVR (`wan_send_from_memory`): se mandan bloques de
 * `LTE_DATA_VENTANA` frames con `CLASS=DATANR` —sin esperar respuesta— y el que
 * cierra el bloque va como `CLASS=DATA`, que sí espera. Confirmado ése, se dan
 * por buenos todos los del bloque.
 *
 * ⛔ **Pero el borrado NO es el del AVR, y la diferencia importa.** Aquel usa
 * `FS_readRcd()`, que **consume el registro antes de transmitirlo**: si la
 * sesión se corta, esos datos ya se perdieron. Acá se lee con
 * `fs_datos_peek( dr, offset )` y se llama `fs_datos_pop( n )` **sólo tras la
 * confirmación** — que es para lo que esas dos funciones se separaron en el
 * paso 4. Si se corta, no se pierde nada; a lo sumo se retransmiten hasta
 * `LTE_DATA_VENTANA` registros, y como el servidor los indexa por la fecha que
 * viaja adentro del frame, un duplicado es inofensivo.
 *
 * ⚠ **El `count` se congela al entrar**, como en el AVR: los registros que
 * `tkSys` grabe durante el vaciado quedan para el ciclo siguiente. Sin eso, con
 * un `timerpoll` corto el vaciado no terminaría nunca.
 */
bool wan_sesion_datos( void )
{
    static char pcFrame[ WAN_FRAME_BUFFER_SIZE ];
    static char pcRta  [ WAN_FRAME_BUFFER_SIZE ];
    dataRcd_t   xDr;

    fs_datos_stats_t xStats;
    uint16_t         usPendientes;
    uint16_t         usSinConfirmar = 0U;
    uint16_t         usEnviados     = 0U;

    if( !wan_modem_listo() )
    {
        return false;
    }

    fs_datos_stats( &xStats );
    usPendientes = xStats.usCount;

    /* El total se congela acá, igual que `usPendientes`: es contra este número
       que se informa el progreso, y no contra el `count` de la ventana —que
       sigue creciendo mientras `tkSys` polea—. Si se informara contra el count
       real, el porcentaje iría para atrás en medio del vaciado. */
    const uint16_t usTotal = usPendientes;

    if( usPendientes == 0U )
    {
        xprintf( "la ventana esta vacia\r\n" );

        /* Vacía no es un fallo: igual hay que mirar si quedaron lotes. */
        return prvLteLotes();
    }

    prvAvisarImeiFalso();

    xprintf( "vaciando la ventana: %u registros\r\n", ( unsigned ) usPendientes );

    while( usPendientes > 0U )
    {
        /*
         * El offset es lo que ya se mandó y todavía no se confirmó: el registro
         * 0 sigue siendo el más viejo hasta que el `pop()` lo saque.
         */
        if( !fs_datos_peek( &xDr, usSinConfirmar ) )
        {
            xprintf( "ERROR: no se pudo leer el registro %u\r\n",
                     ( unsigned ) usSinConfirmar );
            break;
        }

        /* El último del bloque —y el último de todos— piden confirmación. */
        bool bConfirmar = ( ( usSinConfirmar + 1U ) >= LTE_DATA_VENTANA ) ||
                          ( usPendientes == 1U );

        uint16_t usLargo = wan_frame_data( pcFrame, sizeof( pcFrame ), &xDr, bConfirmar );

        if( usLargo == 0U )
        {
            break;
        }

        xprintf( "-> " );
        ( void ) frtos_write( fdTERM, pcFrame, usLargo );
        xprintf( "\r\n" );

        if( bConfirmar )
        {
            drv_lte_flush();
        }

        if( drv_lte_write( pcFrame, usLargo ) != ( int16_t ) usLargo )
        {
            xprintf( "ERROR: no se pudo transmitir\r\n" );
            break;
        }

        usSinConfirmar++;
        usPendientes--;
        usEnviados++;

        /* Un vaciado son hasta 1984 registros de a 10 con confirmación: por
           frame, igual que en los lotes. */
        wdg_kick();

        if( !bConfirmar )
        {
            /*
             * ⚠ **La pausa entre frames es OBLIGATORIA y tiene que ser
             * explícita.** El DTU delimita cada trama por SILENCIO en la serie
             * (su `ftime`, 250 ms acá): dos frames seguidos sin pausa se le
             * juntan en un solo GET y del otro lado llega un frame corrupto.
             *
             * ⛔ El AVR no tiene ninguna espera acá — le funciona porque imprime
             * el frame por la consola a 9600 antes de mandarlo, y eso son ~150 ms
             * de pausa ACCIDENTAL. Depender de eso es depender de que el log
             * esté encendido y de la velocidad de la terminal; en campo, con el
             * log apagado, los frames se pegarían.
             */
            vTaskDelay( pdMS_TO_TICKS( LTE_DATA_MS_ENTRE_FRAMES ) );
            continue;
        }

        /* ---- Toca confirmar: se espera la respuesta del servidor ---- */

        if( !prvEsperarRespuestaConClase( pcRta, sizeof( pcRta ), LTE_PING_TIMEOUT_MS ) )
        {
            xprintf( "sin respuesta en %u ms: quedan %u sin confirmar\r\n",
                     ( unsigned ) LTE_PING_TIMEOUT_MS, ( unsigned ) usSinConfirmar );
            break;
        }

        xprintf( "<- " );
        ( void ) frtos_write( fdTERM, pcRta, ( uint16_t ) strlen( pcRta ) );
        xprintf( "\r\n" );

        wan_data_ordenes_t xOrdenes;
        wan_data_rta_t     eRta = wan_frame_data_rta( pcRta, &xOrdenes );

        if( eRta != wanDATA_ACEPTADO )
        {
            /*
             * Se corta el vaciado en los tres casos: si el servidor no confirma,
             * seguir mandando cientos de registros que no se van a poder borrar
             * sólo gasta batería y tráfico. **Los datos quedan intactos.**
             *
             * Pero el mensaje distingue las causas, porque mandan a mirar lugares
             * distintos.
             */
            if( eRta == wanDATA_VACIA )
            {
                /* Con `prvEsperarRespuestaConClase()` delante esto ya no
                   debería poder pasar: el lazo sólo devuelve tramas que traen
                   `CLASS=`. Si aparece, es que algo cambió ahí. */
                xprintf( "[!] respuesta sin CLASS= (no deberia llegar aca)\r\n" );
            }
            else if( eRta == wanDATA_OTRA_CLASE )
            {
                xprintf( "[!] llego una respuesta de OTRA clase: se descarta\r\n" );
            }
            else
            {
                xprintf( "[!] sin respuesta del servidor\r\n" );
            }

            break;
        }

        /* ⭐ Recién acá se borran: el servidor los tiene. */
        uint16_t usBorrados = fs_datos_pop( usSinConfirmar );

        xprintf( "OK: %u de %u confirmados y borrados; quedan %u\r\n",
                 ( unsigned ) usEnviados, ( unsigned ) usTotal,
                 ( unsigned ) usPendientes );

        if( usBorrados != usSinConfirmar )
        {
            /* No debería pasar nunca: se borra exactamente lo que se confirmó.
               Si el almacén descarta menos de lo pedido, algo no cierra entre lo
               que se leyó con `peek` y lo que hay — mejor verlo que suponerlo. */
            xprintf( "[!] se pidio borrar %u y se borraron %u\r\n",
                     ( unsigned ) usSinConfirmar, ( unsigned ) usBorrados );
        }

        usSinConfirmar = 0U;

        if( xOrdenes.bReset )
        {
            /*
             * Se atiende DESPUÉS del `pop()`: reiniciar antes dejaría los
             * registros confirmados sin borrar, y el equipo los retransmitiría
             * enteros al volver.
             */
            xprintf( "el servidor pide RESET: reiniciando...\r\n" );
            vTaskDelay( pdMS_TO_TICKS( 2000 ) );
            NVIC_SystemReset();
        }

        if( usPendientes > 0U )
        {
            vTaskDelay( pdMS_TO_TICKS( LTE_DATA_MS_ENTRE_FRAMES ) );
        }
    }

    fs_datos_stats( &xStats );

    xprintf( "\r\ntransmitidos %u, quedan %u en la ventana\r\n",
             ( unsigned ) usEnviados, ( unsigned ) xStats.usCount );

    if( usSinConfirmar > 0U )
    {
        xprintf( "[!] %u quedaron SIN confirmar: no se borraron, se reintentan\r\n",
                 ( unsigned ) usSinConfirmar );

        /* Si la ventana no se pudo vaciar, el enlace está mal y los lotes van a
           fallar igual: no tiene sentido encender la SD para descubrirlo. */
        return false;
    }

    /* ---- Y recién ahora los lotes de la microSD ---- */
    return prvLteLotes();
}
//------------------------------------------------------------------------------
/*
 * El PING: la primera pregunta de toda sesión, "¿estás ahí?".
 *
 * ⚠ **ASUME el modo TRANSPARENTE**, al revés que `info` y `set`. Es coherente
 * con el mismo criterio: cada comando asume un estado y no lo cambia. En modo
 * comando el módulo **no transmite nada**, así que un PING desde ahí no sale —
 * por eso, si no hay respuesta, el mensaje pregunta justo por eso.
 *
 * Transmitir es sólo **escribir el payload**: el módulo arma el GET entero con
 * la IP, el puerto y la URL que tiene grabados, y delimita la trama por
 * SILENCIO en la serie (el `ftime` de su configuración). No hay terminador que
 * mandar; es lo mismo que hace `MODEM_txmit()` en el AVR.
 */
void wan_sesion_ping( void )
{
    static char pcFrame[ WAN_FRAME_BUFFER_SIZE ];
    static char pcRta  [ WAN_FRAME_BUFFER_SIZE ];

    if( !wan_modem_listo() )
    {
        return;
    }

    prvAvisarImeiFalso();

    uint16_t usLargo = wan_frame_ping( pcFrame, sizeof( pcFrame ) );

    if( usLargo == 0U )
    {
        return;
    }

    xprintf( "-> " );
    ( void ) frtos_write( fdTERM, pcFrame, usLargo );
    xprintf( "\r\n" );

    drv_lte_flush();

    if( drv_lte_write( pcFrame, usLargo ) != ( int16_t ) usLargo )
    {
        xprintf( "ERROR: no se pudo transmitir\r\n" );
        return;
    }

    /*
     * El timeout es generoso a propósito: acá adentro entran el `ftime` del
     * módulo, la petición HTTP por LTE y la respuesta del servidor. El AVR
     * reintenta 5 veces por esta misma razón.
     */
    int16_t sRet = drv_lte_read( pcRta, sizeof( pcRta ) - 1U, LTE_PING_TIMEOUT_MS );

    if( sRet <= 0 )
    {
        xprintf( "sin respuesta en %u ms.\r\n", ( unsigned ) LTE_PING_TIMEOUT_MS );
        xprintf( "  el modulo esta en modo TRANSPARENTE? (en modo comando NO transmite)\r\n" );
        xprintf( "  y verificar la IP con 'lte esc' + 'lte info'\r\n" );
        return;
    }

    pcRta[ sRet ] = '\0';

    xprintf( "<- " );
    ( void ) frtos_write( fdTERM, pcRta, ( uint16_t ) sRet );
    xprintf( "\r\n" );

    /*
     * La respuesta del servidor viene envuelta en HTML — el log del AVR del
     * 2026-09-09 la muestra como `<html>CLASS=PONG</html>` — así que se busca el
     * patrón adentro en vez de comparar la respuesta entera.
     */
    if( strstr( pcRta, "CLASS=PONG" ) != NULL )
    {
        xprintf( "PONG: el servidor contesta\r\n" );
    }
    else
    {
        xprintf( "[!] contesto algo, pero sin CLASS=PONG\r\n" );
    }
}
//------------------------------------------------------------------------------
/*
 * Pregunta al módulo quiénes somos, si hay red y qué hora es. **Asume modo AT.**
 *
 * Devuelve false si tras los reintentos no hay IP, o sea si el módulo no llegó a
 * registrarse. En ese caso no tiene sentido seguir: el `PING` fallaría igual y
 * se gastaría el ciclo.
 *
 * ⚠ El orden no es casual, y es el del AVR: **`ICCID` e `IMEI` primero** porque
 * son del módulo y salen enseguida; **`CSQ` y `CIP` después** porque dependen
 * del registro a la red, que tarda. Reintentar esos dos con espera **es** el
 * poll de registro.
 */
static bool wan_sesion_identificar( void )
{
    static char pcRta[ 128 ];
    char       *pcVal;
    uint8_t     i;

    /* ---- La identidad: no depende de la red ---- */
    if( drv_lte_at( "AT+IMEI?", pcRta, sizeof( pcRta ), 2000U ) > 0 )
    {
        pcVal = strstr( pcRta, "+IMEI:" );

        if( pcVal != NULL )
        {
            wan_imei_set( pcVal + 6 );
        }
    }

    if( wan_imei_es_falso() )
    {
        /* Sin IMEI real los frames quedarían en el servidor sin equipo que los
           reclame — pasó en banco el 2026-09-21 y nada lo dijo. Acá sí se
           aborta: la FSM transmite sola, así que nadie va a estar mirando. */
        xprintf( "tkWan:: no se pudo leer el IMEI: sin el, los frames quedan huerfanos\r\n" );
        return false;
    }

    if( drv_lte_at( "AT+ICCID?", pcRta, sizeof( pcRta ), 2000U ) > 0 )
    {
        pcVal = strstr( pcRta, "+ICCID:" );

        if( pcVal != NULL )
        {
            wan_iccid_set( pcVal + 7 );
        }
    }

    /* ---- La red: acá es donde hay que insistir ---- */
    bool bHayIp = false;

    for( i = 0U; ( i < WAN_INTENTOS_RED ) && !bHayIp; i++ )
    {
        /* ⭐ Estos reintentos SON la espera del registro a la red (ver el
           comentario del estado OFFLINE): esperar no es colgarse. */
        wdg_kick();

        if( i > 0U )
        {
            vTaskDelay( pdMS_TO_TICKS( WAN_MS_ENTRE_RED ) );
        }

        if( drv_lte_at( "AT+CSQ", pcRta, sizeof( pcRta ), 2000U ) > 0 )
        {
            pcVal = strstr( pcRta, "+CSQ:" );

            if( pcVal != NULL )
            {
                wan_csq_set( ( uint8_t ) atoi( pcVal + 5 ) );
            }
        }

        /*
         * ⭐ **`AT+CIP?` es el que decide.** Tener señal no es tener conexión:
         * con `CSQ 31` —que parece excelente y es el centinela de "todavía no
         * campó"— el módulo se traga el payload sin enviarlo. Eso costó una
         * tarde el 2026-09-09.
         */
        if( drv_lte_at( "AT+CIP?", pcRta, sizeof( pcRta ), 2000U ) > 0 )
        {
            bHayIp = ( strstr( pcRta, "+CIP:" ) != NULL ) &&
                     ( strstr( pcRta, "ERROR" ) == NULL );
        }
    }

    if( !bHayIp )
    {
        return false;
    }

    xprintf( "tkWan:: IMEI %s, senal %u dBm negativos%s\r\n",
             wan_imei(), ( unsigned ) wan_csq(),
             wan_csq_valido() ? "" : " (NO es una medida)" );

    /*
     * ---- Y la hora, ANTES de transmitir y antes de que tkSys mida ----
     *
     * ⭐ Acá es donde tiene que ir, y no en la respuesta a un frame de datos: el
     * `CLOCK=` del servidor llega **después** de que los registros de esta
     * sesión ya se grabaron. Con el reloj del módulo el equipo sale de un
     * arranque en frío antes de estampar una sola muestra mal.
     */
    if( drv_lte_at( "AT+CCLK?", pcRta, sizeof( pcRta ), 2000U ) > 0 )
    {
        RtcTimeType_t xHora;

        if( wan_cclk_parsear( pcRta, &xHora ) )
        {
            ( void ) wan_rtc_sincronizar( &xHora, "el modulo (NTP)", false );
        }
    }

    return true;
}
//------------------------------------------------------------------------------
/*
 * El `PING` de la FSM: igual que `wan_sesion_ping()` pero **devuelve si hubo
 * PONG**, que es lo que la máquina de estados necesita para decidir.
 */
static bool wan_sesion_ping_ok( void )
{
    static char pcFrame[ WAN_FRAME_BUFFER_SIZE ];
    static char pcRta  [ WAN_FRAME_BUFFER_SIZE ];

    uint16_t usLargo = wan_frame_ping( pcFrame, sizeof( pcFrame ) );

    if( usLargo == 0U )
    {
        return false;
    }

    if( !prvLteTxRx( pcFrame, usLargo, pcRta, sizeof( pcRta ) ) )
    {
        return false;
    }

    return ( strstr( pcRta, "CLASS=PONG" ) != NULL );
}
/*==============================================================================
 * LA MÁQUINA DE ESTADOS (paso 5d)
 *============================================================================*/

const char *wan_estado_str( void )
{
    switch( eEstado )
    {
        case wanAPAGADO:       return "APAGADO";
        case wanOFFLINE:       return "OFFLINE";
        case wanONLINE_CONFIG: return "ONLINE_CONFIG";
        case wanONLINE_DATA:   return "ONLINE_DATA";
        default:               return "?";
    }
}
//------------------------------------------------------------------------------
bool wan_hay_enlace( void )
{
    return bHayEnlace;
}
//------------------------------------------------------------------------------
bool wan_matada( void )
{
    return bMatada;
}
//------------------------------------------------------------------------------
void wan_pedir_kill( void )
{
    /*
     * ⚠ Sólo levanta la bandera: **quien se suspende es la propia tkWan**, en su
     * siguiente vuelta. Suspenderla desde otra tarea podría congelarla en medio
     * de una transacción del modem o de FatFs — con el candado de energía
     * tomado y el archivo del lote abierto, que es justo el estado del que no se
     * puede salir sin resetear.
     */
    bMatada = true;
}
//------------------------------------------------------------------------------
static void prvMatarse( void )
{

    /*
     * ⭐ EL DESREGISTRO VA ANTES DE SUSPENDER, y el orden importa: una tarea
     * suspendida deja de reportar, así que si el watchdog la siguiera vigilando
     * la daría por colgada y **resetearía el equipo justo mientras el operador
     * está trabajando el módulo a mano** — el síntoma más desconcertante
     * posible. Es lo que hace `WD_stop_task()` del AVR, y es lo que vuelve
     * correcto a su `kill`.
     */
    wdg_stop_task();

    xprintf( "tkWan MATADA. El modem queda como esta, para trabajarlo a mano.\r\n" );
    xprintf( "Para volver a operacion normal: 'reset' (no hay como revivirla,\r\n" );
    xprintf( "y es a proposito: el reset garantiza un arranque limpio).\r\n" );

    vTaskSuspend( NULL );   /* no retorna: la llama la propia tkWan */
}
//------------------------------------------------------------------------------
/*
 * Cuánto hay que quedarse apagado, en segundos. 0 = no apagarse (continuo).
 *
 * Es `u_get_sleep_time()` del AVR. ⚠ `RTU` **no tiene caso propio allá** —cae en
 * el default— y su `base_print` lo llama *"(RTU) continuo"*: o sea que es
 * continuo, y lo único que lo distingue es que descarta lo que no puede
 * transmitir. Acá se hace explícito en vez de depender de un default.
 */
uint32_t wan_segundos_apagado( void )
{
    switch( xCfgBase.ePwrModo )
    {
        case PWR_CONTINUO:
        case PWR_RTU:
            return 0UL;

        case PWR_DISCRETO:
            return ( uint32_t ) xCfgBase.usTimerDial;

        case PWR_MIXTO:
            break;      /* abajo */

        case PWR_SILENT:
            /*
             * ⛔ SILENT tiene que devolver algo DISTINTO DE CERO, y esto salió
             * recién al enganchar el Modbus (2026-09-21).
             *
             * `tkWan` nunca consulta esta función en SILENT —se queda en su lazo
             * de `APAGADO`— así que el `0` de antes no le hacía nada. Pero
             * `tkSys` la usa para otra pregunta: **"¿el equipo va a dormir hasta
             * el próximo ciclo?"**, y con un 0 dejaba el riel del caudalímetro
             * **encendido para siempre** en el único modo donde el equipo está
             * a batería y no transmite nunca. Justo al revés de lo que hace
             * falta.
             *
             * Devolver `timerdial` es literalmente cierto —en SILENT el modem va
             * a seguir apagado ese tiempo y todos los que vengan— y es lo que
             * las dos preguntas necesitan.
             */
            return ( uint32_t ) xCfgBase.usTimerDial;

        default:
            return 0UL;
    }

    /* ---- MIXTO: depende de la hora ---- */
    RtcTimeType_t xHora;

    if( !drv_rtc_leer( &xHora ) )
    {
        /* Sin hora no se puede decidir una ventana horaria. Se elige CONTINUO
           —transmitir de más— antes que quedarse callado: un equipo que no
           reporta es indistinguible de uno roto. */
        return 0UL;
    }

    int32_t lAhora = ( ( int32_t ) xHora.hour * 60 ) + ( int32_t ) xHora.min;
    int32_t lOn    = ( ( ( int32_t ) xCfgBase.usPwrHhmmOn  / 100 ) * 60 ) +
                     ( ( int32_t ) xCfgBase.usPwrHhmmOn  % 100 );
    int32_t lOff   = ( ( ( int32_t ) xCfgBase.usPwrHhmmOff / 100 ) * 60 ) +
                     ( ( int32_t ) xCfgBase.usPwrHhmmOff % 100 );

    bool bEnVentana;

    if( lOn < lOff )
    {
        /* |--apagado--|--continuo--|--apagado--|  */
        bEnVentana = ( lAhora >= lOn ) && ( lAhora < lOff );
    }
    else
    {
        /* La ventana cruza la medianoche: |--continuo--|--apagado--|--continuo--| */
        bEnVentana = ( lAhora >= lOn ) || ( lAhora < lOff );
    }

    if( bEnVentana )
    {
        return 0UL;     /* dentro de la ventana: continuo */
    }

    /* Fuera de la ventana: dormir hasta que empiece. */
    int32_t lFaltan = lOn - lAhora;

    if( lFaltan <= 0 )
    {
        lFaltan += 24 * 60;     /* es mañana */
    }

    return ( uint32_t ) lFaltan * 60UL;
}
//------------------------------------------------------------------------------
/*
 * Espera troceada. **No es un `vTaskDelay()` largo y hay dos razones:**
 *
 *  1. ⏳ El watchdog (paso 8) va a necesitar un kick periódico, y una espera de
 *     una hora entera no le daría lugar a ninguno.
 *  2. Con el tickless, `pdMS_TO_TICKS( segundos * 1000 )` desborda un `uint32_t`
 *     a partir de ~2,3 h a 512 Hz. Troceando, cada espera es chica y el
 *     problema no existe. **El AVR tiene exactamente el mismo comentario**, con
 *     su propio número.
 */
static void prvEsperar( uint32_t ulSegundos )
{
    const uint32_t ulTrozo = 60UL;

    while( ( ulSegundos > 0UL ) && !bMatada )
    {
        uint32_t ulEste = ( ulSegundos > ulTrozo ) ? ulTrozo : ulSegundos;

        vTaskDelay( pdMS_TO_TICKS( ulEste * 1000UL ) );
        ulSegundos -= ulEste;

        /* ⭐ Acá está la mitad del watchdog que le toca a esta tarea: en modo
           DISCRETO puede dormir seis horas, y sin este reporte un cuelgue suyo
           tardaría todo eso en detectarse. Con el trozo de 60 s, minuto y
           medio. */
        wdg_kick();
    }
}
//------------------------------------------------------------------------------
static void prvEstadoApagado( void )
{
    xprintf( "\r\ntkWan:: APAGADO\r\n" );

    bHayEnlace = false;
    drv_lte_power( false );

    /*
     * ⛔ En SILENT el modem **no se enciende nunca**. El AVR se queda en un lazo
     * infinito en este mismo estado; acá se hace lo mismo pero mirando la
     * configuración en cada vuelta, así un `config pwrmodo continuo` desde la
     * consola se aplica sin reiniciar.
     */
    while( cfg_base_modo_sin_modem() )
    {
        xprintf( "tkWan:: modo SILENT: el modem NO se enciende. Los datos van a la microSD.\r\n" );
        prvEsperar( 60UL );

        if( bMatada )
        {
            return;
        }
    }

    if( !bDiscarYa )
    {
        uint32_t ulEspera = wan_segundos_apagado();

        if( ulEspera > 0UL )
        {
            xprintf( "tkWan:: esperando %lu s hasta la proxima sesion\r\n",
                     ( unsigned long ) ulEspera );
            prvEsperar( ulEspera );
        }
        else if( bSesionFallo )
        {
            /* Continuo, PERO la sesión anterior falló: ver WAN_SEG_TRAS_FALLO. */
            xprintf( "tkWan:: la sesion anterior fallo: espero %lu s antes de reintentar\r\n",
                     ( unsigned long ) WAN_SEG_TRAS_FALLO );
            prvEsperar( WAN_SEG_TRAS_FALLO );
        }
        else
        {
            /* Modo continuo: una pausa corta para no girar. */
            vTaskDelay( pdMS_TO_TICKS( 1000 ) );
        }
    }

    bDiscarYa    = false;
    bSesionFallo = false;
    eEstado      = wanOFFLINE;
}
//------------------------------------------------------------------------------
/*
 * Vuelve a `APAGADO` **marcando que fue por un fallo**, para que la espera de
 * allá sea la larga y no la de un ciclo normal. Ver `WAN_SEG_TRAS_FALLO`.
 */
static void prvFalloVolverAApagado( const char *pcMotivo )
{
    xprintf( "tkWan:: %s: apago y reintento despues\r\n", pcMotivo );
    bSesionFallo = true;
    bHayEnlace   = false;
    eEstado      = wanAPAGADO;
}
//------------------------------------------------------------------------------
/*
 * Saca al módulo del modo comando y **verifica que salió**.
 *
 * ⚠ Devuelve false sólo si el módulo contestó algo que no es `OK`, o no
 * contestó: en los dos casos no se puede afirmar que esté en transparente, y
 * mandar un frame ahí es tirar la sesión (ver el comentario del llamador).
 */
static bool prvSalirDeModoAt( void )
{
    static char pcRta[ 64 ];

    int16_t sRet = drv_lte_at( "AT+ENTM", pcRta, sizeof( pcRta ), 2000U );

    if( ( sRet > 0 ) && ( strstr( pcRta, "OK" ) != NULL ) )
    {
        vTaskDelay( pdMS_TO_TICKS( 500 ) );     /* que el cambio se asiente */
        return true;
    }

    xprintf( "tkWan:: el modulo NO confirmo el paso a transparente (AT+ENTM sin OK)\r\n" );
    xprintf( "        en modo comando no transmite: apago y reintento despues\r\n" );
    return false;
}
//------------------------------------------------------------------------------
/*
 * ⭐ EL PING SE REINTENTA, Y LOS REINTENTOS SON LA ESPERA DEL ATACHE
 *
 * Criterio de Pablo (2026-09-21), que es el del AVR: *"se reintentan PINGS con
 * un espacio entre ellos. Esto hace que los primeros puedan fallar pero luego
 * el modem se atachea a la red y el ultimo conecta"*.
 *
 * ⚠ **No hace falta una espera entre intentos, y por eso no la hay:** el
 * espaciado lo da el propio timeout. Cada intento se queda hasta
 * `LTE_PING_TIMEOUT_MS` (15 s) esperando el PONG, así que cinco intentos son
 * **75 segundos de ventana** para que el módulo termine de campar en la red.
 * Es exactamente la estructura de `wan_process_frame_ping()` del AVR
 * —`PING_TRYES = 5`, con su lazo de 15 esperas de 1 s adentro—, que documenta
 * la intención como *"intento durante 2 minutos mandando un ping cada 10 s"*.
 *
 * ⚠ Esto NO reemplaza al chequeo de `AT+CIP?`: aquél evita gastar el ciclo
 * entero cuando el módulo ni siquiera tiene IP, y éste cubre el tramo en que ya
 * la tiene pero la red todavía no lo deja salir. Son dos esperas distintas
 * sobre el mismo fenómeno, y el AVR también tiene las dos.
 */
static bool prvPingConReintentos( void )
{
    uint8_t i;

    for( i = 0U; i < WAN_INTENTOS_PING; i++ )
    {
        if( i > 0U )
        {
            xprintf( "tkWan:: PING, intento %u de %u\r\n",
                     ( unsigned ) ( i + 1U ), ( unsigned ) WAN_INTENTOS_PING );
        }

        if( wan_sesion_ping_ok() )
        {
            return true;
        }

        /* ⚠ Sin esto el lazo entero son 5 intentos x 15 s de timeout = **75 s**,
           que contra un plazo de 90 deja apenas 15 de margen para todo lo demás
           que hace el estado OFFLINE. Un PING lento no es un cuelgue. */
        wdg_kick();

        if( bMatada )
        {
            return false;
        }
    }

    return false;
}
//------------------------------------------------------------------------------
/*
 * OFFLINE: encender, entrar en modo AT, averiguar quiénes somos y si hay red,
 * poner el reloj en hora, salir de AT y probar el enlace con un `PING`.
 *
 * ⭐ **El reintento de las lecturas AT ES el poll de registro a la red.** Lo dice
 * el AVR y es la parte no obvia: `ICCID` e `IMEI` salen enseguida porque son del
 * módulo, pero `CSQ` y `CIP` **dependen de que se haya registrado**, y eso tarda.
 * Reintentar con espera es esperar el registro; si tras los intentos no hay IP,
 * no tiene sentido gastar el ciclo de PING que va a fallar igual.
 */
static void prvEstadoOffline( void )
{
    xprintf( "\r\ntkWan:: OFFLINE\r\n" );

    drv_lte_power( true );
    vTaskDelay( pdMS_TO_TICKS( WAN_MS_ARRANQUE_MODEM ) );

    /* ---- modo AT ---- */
    uint8_t i;
    bool    bAt = false;

    for( i = 0U; ( i < WAN_INTENTOS_AT ) && !bAt; i++ )
    {
        wdg_kick();

        if( drv_lte_escape() == lteESC_OK )
        {
            bAt = true;
        }
        else
        {
            vTaskDelay( pdMS_TO_TICKS( WAN_MS_ENTRE_INTENTOS ) );
        }
    }

    if( !bAt )
    {
        prvFalloVolverAApagado( "no se pudo entrar en modo AT" );
        return;
    }

    /* ---- quiénes somos, si hay red, y qué hora es ---- */
    if( !wan_sesion_identificar() )
    {
        prvFalloVolverAApagado( "el modem no llego a registrarse" );
        return;
    }

    /*
     * ---- De vuelta a TRANSPARENTE, y VERIFICADO ----
     *
     * ⛔ Acá había un bug que costó la primera corrida en banco (2026-09-21):
     * esta llamada pasaba `NULL` como buffer de respuesta, y `drv_lte_at()`
     * **rechaza `pcRta == NULL` devolviendo -1 sin escribir un byte**. O sea
     * que el `AT+ENTM` NUNCA SE MANDÓ, el `(void)` se comía el error y el
     * módulo seguía en modo comando.
     *
     * El síntoma es engañoso y conviene saber reconocerlo: el frame del PING
     * **vuelve ecoado** —con `AT+E` activo el módulo ecoa lo que recibe en modo
     * comando— seguido de `+CME ERROR:58`, o sea *comando no soportado*, porque
     * `ID=...&CLASS=PING` no es un AT válido. Desde afuera parece un problema
     * de red, y no lo es: **en modo transparente el módulo NO ecoa nada**, manda
     * los bytes a la red. El eco es la firma de que seguimos en modo comando.
     *
     * Por eso ahora se verifica el `OK` y se aborta si no está: mandar el PING
     * sin haber salido de modo AT no puede funcionar nunca, y se llevaría los
     * cinco intentos puestos.
     */
    if( !prvSalirDeModoAt() )
    {
        prvFalloVolverAApagado( "el modulo sigue en modo comando" );
        return;
    }

    /* ---- ¿está el servidor del otro lado? ---- */
    if( !prvPingConReintentos() )
    {
        prvFalloVolverAApagado( "sin PONG en los 5 intentos" );
        return;
    }

    bHayEnlace = true;
    eEstado    = wanONLINE_CONFIG;
}
//------------------------------------------------------------------------------
static void prvEstadoOnlineConfig( void )
{
    xprintf( "\r\ntkWan:: ONLINE_CONFIG\r\n" );

    wan_sesion_config();

    /*
     * ⚠ **Se pasa a DATA pase lo que pase con la configuración**, y es
     * obligatorio: no mandamos el hash `FH` de flowcontrol, así que el servidor
     * va a pedir `FLOWC` en TODAS las sesiones y `CONF_ALL` **nunca** va a
     * contestar `CONFIG=OK`. Si la FSM esperara ese OK, el equipo no mandaría
     * una sola muestra.
     *
     * La estructura del AVR ya lo tolera —sólo `conf_base` aborta su secuencia—
     * y acá se copia el criterio.
     */
    eEstado = wanONLINE_DATA;
}
//------------------------------------------------------------------------------
static void prvEstadoOnlineData( void )
{
    xprintf( "\r\ntkWan:: ONLINE_DATA\r\n" );

    if( !wan_sesion_datos() )
    {
        /*
         * ⛔ **Detectar la caída no sirve si nadie actúa.** Si el vaciado falló
         * —típicamente el `DATA` que se fue por timeout— el enlace está caído, y
         * quedarse en este estado reintentando cada `timerpoll` dejaría el modem
         * encendido para siempre contra un servidor que no contesta.
         *
         * Se vuelve a `APAGADO`, que es lo único que reestablece: apaga, prende,
         * vuelve a entrar en modo AT, reespera el registro a la red y rehace el
         * `PING`. Es el mismo criterio que el resto de la FSM —cualquier fallo
         * vuelve al principio— porque un módulo a mitad de una sesión fallida
         * está en un estado que desde afuera no se puede saber.
         */
        prvFalloVolverAApagado( "el vaciado fallo: el enlace se cayo" );
        return;
    }

    if( wan_segundos_apagado() > 0UL )
    {
        /* DISCRETO, o MIXTO fuera de su ventana: se apaga hasta la próxima. */
        eEstado = wanAPAGADO;
        return;
    }

    /*
     * CONTINUO (y RTU): el modem queda encendido esperando datos nuevos. Se
     * despierta cada `timerpoll` para vaciar lo que `tkSys` haya guardado.
     *
     * ⚠ Se vuelve a `ONLINE_DATA`, no a `OFFLINE`: reabrir la sesión entera
     * —modo AT, lecturas, PING— en cada vuelta sería pagar ~50 s de setup para
     * mandar un frame.
     */
    prvEsperar( ( uint32_t ) xCfgBase.usTimerPoll );
}
//------------------------------------------------------------------------------
void tkWan( void *pvParameters )
{
    ( void ) pvParameters;

    /* Antes de la espera de arranque: el plazo corre desde que la tarea existe. */
    wdg_registrar( wdgTK_WAN );

    vTaskDelay( pdMS_TO_TICKS( WAN_MS_ESPERA_ARRANQUE ) );

    xprintf( "\r\ntkWan arrancando (modo %s)\r\n", cfg_base_pwrmodo_str() );

    for( ;; )
    {
        if( bMatada )
        {
            prvMatarse();   /* no retorna */
        }

        wdg_kick();

        switch( eEstado )
        {
            case wanAPAGADO:       prvEstadoApagado();      break;
            case wanOFFLINE:       prvEstadoOffline();      break;
            case wanONLINE_CONFIG: prvEstadoOnlineConfig(); break;
            case wanONLINE_DATA:   prvEstadoOnlineData();   break;

            default:
                eEstado = wanAPAGADO;
                break;
        }
    }
}
//------------------------------------------------------------------------------
