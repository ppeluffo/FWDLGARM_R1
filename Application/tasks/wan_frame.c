/*
 * wan_frame.c  -  ver wan_frame.h
 *
 * ⭐ Lo de acá es el CONTRATO con el servidor. No se "mejora": se reproduce.
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "wan_frame.h"
#include "cfg_nvm.h"
#include "tkCmd.h"
#include "tkSys.h"
/* Las órdenes de válvula del servidor se despachan a estas dos tareas: cada una
   mueve un dispositivo distinto. Ver `prvOrdenesDeValvula()`. */
#include "tkFlow.h"
#include "tkCtlPres.h"
#include "drv_rtc79410.h"
#include "frtos-io.h"
#include "main.h"

/*
 * ⏳ PROVISORIO de esta etapa. Ver wan_frame.h: lo definitivo lo lee tkWAN del
 * modem con `AT+IMEI?` en el paso 5, y queda fijado para toda la corrida.
 */
#define WAN_IMEI_FALSO      "000000000000000"

static char pcImei [ 16 ] = WAN_IMEI_FALSO;
static char pcIccid[ 24 ] = "";

/* `rssi` crudo tal como lo devuelve el módulo. 99 mientras no se leyó. */
static uint8_t ucRssiCrudo = 99U;

//------------------------------------------------------------------------------
const char *wan_imei( void )
{
    return pcImei;
}
//------------------------------------------------------------------------------
bool wan_imei_es_falso( void )
{
    return ( strcmp( pcImei, WAN_IMEI_FALSO ) == 0 );
}
//------------------------------------------------------------------------------
void wan_imei_set( const char *pcNuevo )
{
    if( pcNuevo == NULL )
    {
        return;
    }

    /*
     * Sólo se acepta si tiene pinta de IMEI: 15 dígitos. Un parseo que falle a
     * medias —porque el módulo contestó otra cosa, o llegó cortado— dejaría al
     * equipo presentándose ante el servidor con basura, y eso es peor que
     * seguir con los ceros, que al menos son reconocibles.
     */
    size_t i;

    for( i = 0U; i < 15U; i++ )
    {
        if( ( pcNuevo[ i ] < '0' ) || ( pcNuevo[ i ] > '9' ) )
        {
            return;
        }
    }

    memcpy( pcImei, pcNuevo, 15U );
    pcImei[ 15 ] = '\0';
}
//------------------------------------------------------------------------------
uint16_t wan_frame_ping( char *pcBuf, uint16_t usSize )
{
    if( ( pcBuf == NULL ) || ( usSize == 0U ) )
    {
        return 0U;
    }

    int iN = snprintf( pcBuf, usSize, "ID=%s&HW=%s&TYPE=%s&VER=%s&CLASS=PING",
                       wan_imei(), FW_HW, FW_TYPE, FW_VERSION );

    if( ( iN < 0 ) || ( ( uint16_t ) iN >= usSize ) )
    {
        return 0U;
    }

    return ( uint16_t ) iN;
}
//------------------------------------------------------------------------------
void wan_iccid_set( const char *pcNuevo )
{
    if( pcNuevo == NULL )
    {
        return;
    }

    /* Igual que el IMEI: sólo se acepta si empieza con un dígito. Un parseo a
       medias dejaría al equipo informando basura como identidad de la SIM. */
    if( ( pcNuevo[ 0 ] < '0' ) || ( pcNuevo[ 0 ] > '9' ) )
    {
        return;
    }

    uint32_t i = 0U;

    while( ( i < ( sizeof( pcIccid ) - 1U ) ) &&
           ( ( ( pcNuevo[ i ] >= '0' ) && ( pcNuevo[ i ] <= '9' ) ) ||
             ( ( pcNuevo[ i ] >= 'A' ) && ( pcNuevo[ i ] <= 'F' ) ) ) )
    {
        pcIccid[ i ] = pcNuevo[ i ];
        i++;
    }

    pcIccid[ i ] = '\0';
}
//------------------------------------------------------------------------------
const char *wan_iccid( void )
{
    return pcIccid;
}
//------------------------------------------------------------------------------
void wan_csq_set( uint8_t ucRssi )
{
    ucRssiCrudo = ucRssi;
}
//------------------------------------------------------------------------------
bool wan_csq_valido( void )
{
    /* Ver wan_frame.h: 99 es "desconocido" y >= 31 es el centinela de "todavía
       no campó en la red". Ninguno de los dos es una medida. */
    return ( ucRssiCrudo < 31U );
}
//------------------------------------------------------------------------------
uint8_t wan_csq( void )
{
    /*
     * El contrato lleva el valor absoluto de los dBm, no el `rssi`:
     *     dBm = -113 + 2 * rssi   ->   |dBm| = 113 - 2 * rssi
     * Con `rssi = 20` da 73, que es lo que manda el AVR.
     */
    if( !wan_csq_valido() )
    {
        return 0U;      /* sin medida; el servidor lo ve como señal nula */
    }

    return ( uint8_t ) ( 113U - ( 2U * ucRssiCrudo ) );
}
//------------------------------------------------------------------------------
/*
 * El identificador único del micro, en hexadecimal.
 *
 * ⚠ **Son 96 bits = 24 caracteres, contra los 32 que manda el AVR** (su
 * `NVM_signature2str()`). Es una diferencia de silicio, no una decisión: el
 * STM32L4 tiene un UID de 96 bits y el AVR uno más largo. Si el servidor
 * validara el largo, acá habría que rellenar — pero el equipo se identifica por
 * el **IMEI**, así que esto es informativo.
 */
static void prvUidStr( char *pcBuf, uint16_t usSize )
{
    const uint32_t *pulUid = ( const uint32_t * ) UID_BASE;

    snprintf( pcBuf, usSize, "%08lX%08lX%08lX",
              ( unsigned long ) pulUid[ 0 ],
              ( unsigned long ) pulUid[ 1 ],
              ( unsigned long ) pulUid[ 2 ] );
}
//------------------------------------------------------------------------------
uint16_t wan_frame_conf_all( char *pcBuf, uint16_t usSize )
{
    char pcUid[ 32 ];

    if( ( pcBuf == NULL ) || ( usSize == 0U ) )
    {
        return 0U;
    }

    prvUidStr( pcUid, sizeof( pcUid ) );

    /* ⛔ CINCO hashes: el `FH` de flowcontrol NO va, y el servidor tampoco lo
       espera desde el 2026-09-22. Ver wan_frame.h. */
    int iN = snprintf( pcBuf, usSize,
                       "ID=%s&HW=%s&TYPE=%s&VER=%s&CLASS=CONF_ALL"
                       "&UID=%s&ICCID=%s&CSQ=%u&WDG=%u"
                       "&BH=0x%02X&AH=0x%02X&CH=0x%02X&MH=0x%02X&PH=0x%02X",
                       wan_imei(), FW_HW, FW_TYPE, FW_VERSION,
                       pcUid, wan_iccid(),
                       ( unsigned ) wan_csq(),
                       ( unsigned ) wan_causa_reset(),
                       ( unsigned ) cfg_base_hash(),
                       ( unsigned ) cfg_ainputs_hash(),
                       ( unsigned ) cfg_counter_hash(),
                       ( unsigned ) cfg_modbus_hash(),
                       ( unsigned ) cfg_consigna_hash() );

    if( ( iN < 0 ) || ( ( uint16_t ) iN >= usSize ) )
    {
        xprintf( "WAN:: ERROR: el frame CONF_ALL no entra en %u bytes\r\n",
                 ( unsigned ) usSize );
        return 0U;
    }

    return ( uint16_t ) iN;
}
//------------------------------------------------------------------------------
wan_conf_rta_t wan_frame_conf_all_rta( const char *pcRta, wan_conf_flags_t *pxFlags )
{
    if( ( pcRta == NULL ) || ( pxFlags == NULL ) )
    {
        return wanCONF_SIN_RESPUESTA;
    }

    memset( pxFlags, 0, sizeof( wan_conf_flags_t ) );

    /*
     * El orden de los chequeos importa: `CONFIG=OK` primero, porque una
     * respuesta que lista bloques también contiene la palabra `CONFIG`.
     */
    if( strstr( pcRta, "CONFIG=OK" ) != NULL )
    {
        return wanCONF_OK;
    }

    /*
     * `CONFIG=ERROR` = el servidor no reconoce al datalogger (no está dado de
     * alta). `FAIL` = no reconoce el frame. Los dos significan lo mismo para el
     * equipo: no hay nada que hacer hasta que alguien toque el servidor.
     */
    if( ( strstr( pcRta, "CONFIG=ERROR" ) != NULL ) ||
        ( strstr( pcRta, "FAIL" ) != NULL ) )
    {
        return wanCONF_DESCONOCIDO;
    }

    /* Los nombres son los del AVR, y `FLOWC` se parsea aunque no se use. */
    pxFlags->bBase        = ( strstr( pcRta, "BASE"    ) != NULL );
    pxFlags->bAinputs     = ( strstr( pcRta, "AINPUT"  ) != NULL );
    pxFlags->bCounter     = ( strstr( pcRta, "COUNTER" ) != NULL );
    pxFlags->bModbus      = ( strstr( pcRta, "MODBUS"  ) != NULL );
    pxFlags->bConsigna    = ( strstr( pcRta, "PRESION" ) != NULL );
    pxFlags->bFlowcontrol = ( strstr( pcRta, "FLOWC"   ) != NULL );

    return wanCONF_RECONFIGURAR;
}
//------------------------------------------------------------------------------
uint16_t wan_frame_datos( char *pcBuf, uint16_t usSize, const dataRcd_t *pxDr )
{
    uint16_t usIdx = 0U;
    bool     bOvf  = false;
    uint8_t  i;

    if( ( pcBuf == NULL ) || ( pxDr == NULL ) || ( usSize == 0U ) )
    {
        return 0U;
    }

    memset( pcBuf, 0, usSize );

    /*
     * Append acotado. El AVR llegó a este mismo patrón después de que el frame
     * desbordara `modem_tx_buffer` y corrompiera variables globales: con 9
     * canales habilitados y nombres normales pasa de 255 bytes. Acá el buffer es
     * de 512, pero la cota se mantiene igual — un frame truncado del otro lado
     * es un registro con campos perdidos, y eso hay que saberlo, no descubrirlo.
     */
    #define FRAME_APPEND( ... )                                                     \
        do {                                                                        \
            if( !bOvf )                                                             \
            {                                                                       \
                int _n = snprintf( &pcBuf[ usIdx ], ( size_t ) ( usSize - usIdx ),  \
                                   __VA_ARGS__ );                                   \
                if( ( _n < 0 ) || ( ( uint16_t ) _n >= ( uint16_t ) ( usSize - usIdx ) ) ) \
                    bOvf = true;                                                    \
                else                                                                \
                    usIdx += ( uint16_t ) _n;                                       \
            }                                                                       \
        } while( 0 )

    /* ---- Fecha y hora ------------------------------------------------ */
    /* ⚠ DATE es YYMMDD, en ese orden. El año son los dos últimos dígitos: el
       MCP79410 no tiene siglo. */
    FRAME_APPEND( "DATE=%02d%02d%02d",
                  pxDr->xRtc.year, pxDr->xRtc.month, pxDr->xRtc.day );
    FRAME_APPEND( "&TIME=%02d%02d%02d",
                  pxDr->xRtc.hour, pxDr->xRtc.min, pxDr->xRtc.sec );

    /* ---- Analógicas: 2 decimales ------------------------------------- */
    for( i = 0U; i < CFG_AINPUTS_NRO_CANALES; i++ )
    {
        if( xCfgAinputs.xCanal[ i ].bEnabled )
        {
            float fValor = ( pxDr->usInvalidos & ( uint16_t ) ( dataINVALIDO_AIN0 << i ) )
                           ? WAN_CENTINELA_SIN_DATO
                           : pxDr->fAinputs[ i ];

            FRAME_APPEND( "&%s=%0.2f", xCfgAinputs.xCanal[ i ].pcName, fValor );
        }
    }

    /* ---- Contador: 3 decimales --------------------------------------- */
    if( xCfgCounter.bEnabled )
    {
        float fValor = ( pxDr->usInvalidos & dataINVALIDO_CONTADOR )
                       ? WAN_CENTINELA_SIN_DATO
                       : pxDr->fContador;

        FRAME_APPEND( "&%s=%0.3f", xCfgCounter.pcName, fValor );
    }

    /* ---- Modbus: 3 decimales ----------------------------------------- */
    if( xCfgModbus.bEnabled )
    {
        for( i = 0U; i < CFG_MODBUS_NRO_CANALES; i++ )
        {
            if( xCfgModbus.xCanal[ i ].bEnabled )
            {
                float fValor = ( pxDr->usInvalidos & ( uint16_t ) ( dataINVALIDO_MODBUS0 << i ) )
                               ? WAN_CENTINELA_SIN_DATO
                               : pxDr->fModbus[ i ];

                FRAME_APPEND( "&%s=%0.3f", xCfgModbus.xCanal[ i ].pcName, fValor );
            }
        }
    }

    /* ---- Válvula ------------------------------------------------------ */
    /* ⚠ 0 = abierta, 1 = cerrada. Al revés de lo intuitivo, y así lo espera el
       servidor desde la V1.2.5 del AVR. No "corregirlo". */
    FRAME_APPEND( "&V0=%d", ( int ) pxDr->ucValvula );

    /* ---- Rieles ------------------------------------------------------- */
    FRAME_APPEND( "&bt3v3=%0.3f", ( pxDr->usInvalidos & dataINVALIDO_BT3V3 )
                                  ? WAN_CENTINELA_SIN_DATO : pxDr->fBt3v3 );
    FRAME_APPEND( "&bt12v=%0.3f", ( pxDr->usInvalidos & dataINVALIDO_BT12V )
                                  ? WAN_CENTINELA_SIN_DATO : pxDr->fBt12v );

    #undef FRAME_APPEND

    if( bOvf )
    {
        xprintf( "WAN:: ERROR: los datos no entran en %u bytes, DESCARTADOS !!\r\n",
                 ( unsigned ) usSize );
        return 0U;
    }

    return usIdx;
}
//------------------------------------------------------------------------------
uint16_t wan_frame_prefijo( char *pcBuf, uint16_t usSize, bool bConRespuesta )
{
    if( ( pcBuf == NULL ) || ( usSize == 0U ) )
    {
        return 0U;
    }

    int iN = snprintf( pcBuf, usSize, "ID=%s&HW=%s&TYPE=%s&VER=%s&CLASS=%s",
                       wan_imei(), FW_HW, FW_TYPE, FW_VERSION,
                       bConRespuesta ? "DATA" : "DATANR" );

    if( ( iN < 0 ) || ( ( uint16_t ) iN >= usSize ) )
    {
        return 0U;
    }

    return ( uint16_t ) iN;
}
//------------------------------------------------------------------------------
uint16_t wan_frame_data( char *pcBuf, uint16_t usSize, const dataRcd_t *pxDr,
                         bool bConRespuesta )
{
    if( ( pcBuf == NULL ) || ( pxDr == NULL ) || ( usSize < 2U ) )
    {
        return 0U;
    }

    uint16_t usIdx = wan_frame_prefijo( pcBuf, usSize, bConRespuesta );

    if( usIdx == 0U )
    {
        return 0U;
    }

    pcBuf[ usIdx++ ] = '&';

    uint16_t usDatos = wan_frame_datos( &pcBuf[ usIdx ], ( uint16_t ) ( usSize - usIdx ), pxDr );

    if( usDatos == 0U )
    {
        return 0U;
    }

    return ( uint16_t ) ( usIdx + usDatos );
}
//------------------------------------------------------------------------------

/*==============================================================================
 * LOS BLOQUES DE CONFIGURACIÓN (paso 5b-2)
 *
 * El servidor manda la configuración que quiere que tenga el equipo y acá se
 * parsea y se aplica. Portado de `wan_process_rsp_config*()` de FWDLGX.
 *============================================================================*/

/*
 * Los delimitadores del AVR, tal cual: `&` separa campos, `,` separa los
 * tokens de un campo, `=` separa clave de valor, y **`<` y `>` están porque la
 * respuesta viene envuelta en HTML** (`<html>…</html>`) — sin ellos el último
 * valor se llevaría puesto el cierre de la etiqueta.
 */
static const char pcDelim[] = "&,;:=><";

//------------------------------------------------------------------------------
static bool prvEsDelim( char cCh )
{
    /* Los caracteres de control terminan el token igual que un delimitador. El
       AVR no lo hace —su buffer siempre venía cerrado por el `<` del HTML— pero
       una respuesta con CRLF al final dejaría el `\r` pegado al valor, y el
       setter lo rechazaría por una razón que desde afuera no se entiende. */
    return ( ( cCh == '\0' ) || ( cCh < ' ' ) || ( strchr( pcDelim, cCh ) != NULL ) );
}
//------------------------------------------------------------------------------
/*
 * Extrae en `pcVal` el token número `ucToken` (0 = el primero) que sigue a
 * `pcClave` en la respuesta. `pcClave` incluye el `=`: "TPOLL=", "A0=".
 *
 * Reemplaza al par `strlcpy` + `strsep` repetido del AVR, y a propósito:
 *
 *  - **`strsep()` no está en newlib** (es de BSD, como `strlcpy`).
 *  - Aquel copiaba 64 bytes desde el campo a un buffer global y tokenizaba ahí,
 *    así que **un campo largo se truncaba y los últimos tokens salían NULL**:
 *    un canal modbus con nombres largos perdía el `pow10` en silencio. Acá se
 *    parsea en el lugar y cada token se acota por separado. **Eso no toca el
 *    contrato**: el contrato es lo que se MANDA (el hash sobre el string
 *    formateado), no cómo se lee la respuesta.
 *
 * Devuelve false si la clave no está o el token está vacío — y en los dos casos
 * el campo se deja como estaba, que es lo correcto: el servidor no lo mandó.
 */
static bool prvCampo( const char *pcRta, const char *pcClave, uint8_t ucToken,
                      char *pcVal, uint16_t usSize )
{
    const char *p;
    uint16_t    i;

    if( ( pcRta == NULL ) || ( pcClave == NULL ) || ( pcVal == NULL ) || ( usSize < 2U ) )
    {
        return false;
    }

    pcVal[ 0 ] = '\0';

    p = strstr( pcRta, pcClave );

    if( p == NULL )
    {
        return false;
    }

    p += strlen( pcClave );

    /* Saltear los tokens anteriores al pedido. */
    while( ucToken > 0U )
    {
        while( !prvEsDelim( *p ) )
        {
            p++;
        }

        /* Sólo `&,;:=` continúan la lista; un '\0', un '<' o un control
           significan que la respuesta se acabó antes del token pedido. */
        if( ( *p == '\0' ) || ( strchr( "&,;:=", *p ) == NULL ) )
        {
            return false;
        }

        p++;
        ucToken--;
    }

    for( i = 0U; ( i < ( uint16_t ) ( usSize - 1U ) ) && !prvEsDelim( p[ i ] ); i++ )
    {
        pcVal[ i ] = p[ i ];
    }

    pcVal[ i ] = '\0';

    return ( i > 0U );
}
//------------------------------------------------------------------------------
/*
 * Los dos veredictos que TODA respuesta de configuración puede traer, y que se
 * chequean antes de buscar un solo campo.
 *
 * ⚠ El orden importa: una respuesta que trae configuración también contiene la
 * palabra `CONFIG` si el servidor la incluyera, así que `CONFIG=OK` va primero
 * y se compara entero.
 */
static bool prvVeredicto( const char *pcRta, wan_conf_rta_t *peRta )
{
    if( ( pcRta == NULL ) || ( pcRta[ 0 ] == '\0' ) )
    {
        *peRta = wanCONF_SIN_RESPUESTA;
        return true;
    }

    if( strstr( pcRta, "CONFIG=OK" ) != NULL )
    {
        *peRta = wanCONF_OK;
        return true;
    }

    if( ( strstr( pcRta, "CONFIG=ERROR" ) != NULL ) ||
        ( strstr( pcRta, "CONFIG=FAIL"  ) != NULL ) )
    {
        *peRta = wanCONF_DESCONOCIDO;
        return true;
    }

    return false;
}

//------------------------------------------------------------------------------
/*
 * Lee hasta `ucNro` tokens de `pcClave` y deja en `ppcOut` un puntero por cada
 * uno — **NULL el que el servidor no haya mandado**. Devuelve cuántos vinieron.
 *
 * ⚠ **Que falten campos es NORMAL, no un error**, y costó una vuelta el
 * 2026-09-11: el servidor contestó `C0=FALSE,X,1.0,CAUDAL` —cuatro de los seis
 * que parsea el AVR— y la primera versión, que los exigía todos, descartó el
 * bloque entero. Los setters tratan el NULL como "dejá este campo como está",
 * que es exactamente lo que hace el AVR (`if ( s_qmax != NULL ) …`).
 *
 * El token 1 es el NOMBRE en los tres bloques, y es el único imprescindible:
 * sin él no hay nada que configurar.
 */
static uint8_t prvTokens( const char *pcRta, const char *pcClave, uint8_t ucNro,
                          char pcBuf[][ 16 ], const char *ppcOut[] )
{
    uint8_t i;
    uint8_t ucVinieron = 0U;

    for( i = 0U; i < ucNro; i++ )
    {
        if( prvCampo( pcRta, pcClave, i, pcBuf[ i ], 16U ) )
        {
            ppcOut[ i ] = pcBuf[ i ];
            ucVinieron++;
        }
        else
        {
            ppcOut[ i ] = NULL;
        }
    }

    return ucVinieron;
}
//------------------------------------------------------------------------------
/*
 * El aviso de que un bloque vino corto. **No es cosmético**: si el servidor no
 * manda un campo que sí entra en el hash, el equipo se queda con SU valor y el
 * hash no va a coincidir nunca — o sea que va a pedir reconfigurar ese bloque
 * en todas las sesiones. Verlo en la consola es lo único que separa ese caso de
 * un error del parseo.
 */
static void prvAvisarIncompleto( const char *pcQue, uint8_t ucVinieron, uint8_t ucNro )
{
    if( ucVinieron < ucNro )
    {
        xprintf( "WAN:: [!] %s: vinieron %u de %u campos; el resto queda como estaba\r\n",
                 pcQue, ( unsigned ) ucVinieron, ( unsigned ) ucNro );
    }
}
/*------------------------------------------------------------------------------
 * CONF_BASE
 *----------------------------------------------------------------------------*/
static wan_conf_rta_t prvAplicarBase( const char *pcRta )
{
    char pcVal[ 24 ];
    bool bAlgo = false;

    if( prvCampo( pcRta, "TPOLL=", 0U, pcVal, sizeof( pcVal ) ) )
    {
        if( cfg_base_set_timerpoll( pcVal ) )
        {
            xprintf( "WAN:: reconfig TIMERPOLL = %s\r\n", pcVal );
            bAlgo = true;
        }
        else
        {
            xprintf( "WAN:: ERROR: TIMERPOLL rechazado (%s)\r\n", pcVal );
        }
    }

    if( prvCampo( pcRta, "TDIAL=", 0U, pcVal, sizeof( pcVal ) ) )
    {
        if( cfg_base_set_timerdial( pcVal ) )
        {
            xprintf( "WAN:: reconfig TIMERDIAL = %s\r\n", pcVal );
            bAlgo = true;
        }
        else
        {
            xprintf( "WAN:: ERROR: TIMERDIAL rechazado (%s)\r\n", pcVal );
        }
    }

    if( prvCampo( pcRta, "PWRMODO=", 0U, pcVal, sizeof( pcVal ) ) )
    {
        if( cfg_base_set_pwrmodo( pcVal ) )
        {
            xprintf( "WAN:: reconfig PWRMODO = %s\r\n", pcVal );
            bAlgo = true;
        }
        else
        {
            /*
             * El equipo queda con el modo que tenía, que es lo seguro: adoptar
             * un modo que no sabe ejecutar sería peor.
             *
             * ℹ Los cinco modos del AVR están soportados desde el 2026-09-12
             * (`RTU` y `SILENT` entraron a pedido de Pablo), así que acá ya sólo
             * caen valores realmente desconocidos.
             */
            xprintf( "WAN:: ERROR: PWRMODO desconocido (%s), queda el anterior\r\n", pcVal );
        }
    }

    if( prvCampo( pcRta, "PWRON=", 0U, pcVal, sizeof( pcVal ) ) )
    {
        if( cfg_base_set_pwron( pcVal ) )
        {
            xprintf( "WAN:: reconfig PWRON = %s\r\n", pcVal );
            bAlgo = true;
        }
        else
        {
            xprintf( "WAN:: ERROR: PWRON rechazado (%s)\r\n", pcVal );
        }
    }

    if( prvCampo( pcRta, "PWROFF=", 0U, pcVal, sizeof( pcVal ) ) )
    {
        if( cfg_base_set_pwroff( pcVal ) )
        {
            xprintf( "WAN:: reconfig PWROFF = %s\r\n", pcVal );
            bAlgo = true;
        }
        else
        {
            xprintf( "WAN:: ERROR: PWROFF rechazado (%s)\r\n", pcVal );
        }
    }

    return bAlgo ? wanCONF_RECONFIGURAR : wanCONF_SIN_RESPUESTA;
}

/*------------------------------------------------------------------------------
 * CONF_AINPUTS
 *----------------------------------------------------------------------------*/
static wan_conf_rta_t prvAplicarAinputs( const char *pcRta )
{
    /* Los siete tokens de un canal: enable, name, imin, imax, mmin, mmax, offset. */
    char        pcTk[ 7 ][ 16 ];
    const char *ppcTk[ 7 ];
    char        pcClave[ 8 ];
    char        pcVal[ 16 ];
    bool        bAlgo = false;
    uint8_t     ucCh;

    if( prvCampo( pcRta, "PST=", 0U, pcVal, sizeof( pcVal ) ) )
    {
        if( cfg_ainputs_set_settle_time( pcVal ) )
        {
            xprintf( "WAN:: reconfig PST = %s\r\n", pcVal );
            bAlgo = true;
        }
        else
        {
            xprintf( "WAN:: ERROR: PST rechazado (%s)\r\n", pcVal );
        }
    }

    for( ucCh = 0U; ucCh < CFG_AINPUTS_NRO_CANALES; ucCh++ )
    {
        snprintf( pcClave, sizeof( pcClave ), "A%u=", ( unsigned ) ucCh );

        uint8_t ucVinieron = prvTokens( pcRta, pcClave, 7U, pcTk, ppcTk );

        /* Sin el nombre no hay canal: o el servidor no lo mandó, o vino roto. */
        if( ppcTk[ 1 ] == NULL )
        {
            continue;
        }

        prvAvisarIncompleto( pcClave, ucVinieron, 7U );

        if( cfg_ainputs_set_canal( ucCh, ppcTk[0], ppcTk[1], ppcTk[2], ppcTk[3],
                                          ppcTk[4], ppcTk[5], ppcTk[6] ) )
        {
            xprintf( "WAN:: reconfig A%u\r\n", ( unsigned ) ucCh );
            bAlgo = true;
        }
        else
        {
            xprintf( "WAN:: ERROR: A%u rechazado\r\n", ( unsigned ) ucCh );
        }
    }

    return bAlgo ? wanCONF_RECONFIGURAR : wanCONF_SIN_RESPUESTA;
}

/*------------------------------------------------------------------------------
 * CONF_COUNTERS
 *
 * ⚠ Son SEIS tokens: enable, name, magpp, **modo**, qmax, alpha. El modo va
 * tercero y `alpha` es configurable — ver el comentario de `cfg_counter_set()`.
 *----------------------------------------------------------------------------*/
static wan_conf_rta_t prvAplicarCounter( const char *pcRta )
{
    char        pcTk[ 6 ][ 16 ];
    const char *ppcTk[ 6 ];

    uint8_t ucVinieron = prvTokens( pcRta, "C0=", 6U, pcTk, ppcTk );

    if( ppcTk[ 1 ] == NULL )
    {
        return wanCONF_SIN_RESPUESTA;
    }

    prvAvisarIncompleto( "C0", ucVinieron, 6U );

    if( !cfg_counter_set( ppcTk[0], ppcTk[1], ppcTk[2], ppcTk[3], ppcTk[4], ppcTk[5] ) )
    {
        xprintf( "WAN:: ERROR: C0 rechazado\r\n" );
        return wanCONF_SIN_RESPUESTA;
    }

    xprintf( "WAN:: reconfig C0\r\n" );

    return wanCONF_RECONFIGURAR;
}

/*------------------------------------------------------------------------------
 * CONF_MODBUS
 *----------------------------------------------------------------------------*/
static wan_conf_rta_t prvAplicarModbus( const char *pcRta )
{
    /* Nueve: enable, name, sla, regaddr, nroregs, fcode, tipo, codec, pow10. */
    char        pcTk[ 9 ][ 16 ];
    const char *ppcTk[ 9 ];
    char        pcClave[ 8 ];
    char        pcVal[ 16 ];
    bool        bAlgo = false;
    uint8_t     ucCh;

    if( prvCampo( pcRta, "ENABLE=", 0U, pcVal, sizeof( pcVal ) ) )
    {
        if( cfg_modbus_set_enable( pcVal ) )
        {
            xprintf( "WAN:: reconfig MODBUS ENABLE = %s\r\n", pcVal );
            bAlgo = true;
        }
        else
        {
            xprintf( "WAN:: ERROR: MODBUS ENABLE rechazado (%s)\r\n", pcVal );
        }
    }

    if( prvCampo( pcRta, "LOCALADDR=", 0U, pcVal, sizeof( pcVal ) ) )
    {
        if( cfg_modbus_set_localaddr( pcVal ) )
        {
            xprintf( "WAN:: reconfig MODBUS LOCALADDR = %s\r\n", pcVal );
            bAlgo = true;
        }
        else
        {
            xprintf( "WAN:: ERROR: MODBUS LOCALADDR rechazado (%s)\r\n", pcVal );
        }
    }

    for( ucCh = 0U; ucCh < CFG_MODBUS_NRO_CANALES; ucCh++ )
    {
        snprintf( pcClave, sizeof( pcClave ), "M%u=", ( unsigned ) ucCh );

        uint8_t ucVinieron = prvTokens( pcRta, pcClave, 9U, pcTk, ppcTk );

        if( ppcTk[ 1 ] == NULL )
        {
            continue;
        }

        prvAvisarIncompleto( pcClave, ucVinieron, 9U );

        if( cfg_modbus_set_canal( ucCh, ppcTk[0], ppcTk[1], ppcTk[2], ppcTk[3], ppcTk[4],
                                         ppcTk[5], ppcTk[6], ppcTk[7], ppcTk[8] ) )
        {
            xprintf( "WAN:: reconfig M%u\r\n", ( unsigned ) ucCh );
            bAlgo = true;
        }
        else
        {
            xprintf( "WAN:: ERROR: M%u rechazado\r\n", ( unsigned ) ucCh );
        }
    }

    return bAlgo ? wanCONF_RECONFIGURAR : wanCONF_SIN_RESPUESTA;
}

/*------------------------------------------------------------------------------
 * CONF_CONSIGNA
 *
 * Los tres campos van juntos a un solo setter, así que se exigen los tres: con
 * dos de tres habría que inventar el que falta con el valor actual, y eso es
 * indistinguible de que el servidor lo haya mandado así.
 *----------------------------------------------------------------------------*/
static wan_conf_rta_t prvAplicarConsigna( const char *pcRta )
{
    char pcEnable[ 8 ], pcDiurna[ 8 ], pcNocturna[ 8 ];

    if( !prvCampo( pcRta, "ENABLE=",   0U, pcEnable,   sizeof( pcEnable   ) ) ||
        !prvCampo( pcRta, "DIURNA=",   0U, pcDiurna,   sizeof( pcDiurna   ) ) ||
        !prvCampo( pcRta, "NOCTURNA=", 0U, pcNocturna, sizeof( pcNocturna ) ) )
    {
        xprintf( "WAN:: ERROR: CONSIGNA incompleta, se ignora\r\n" );
        return wanCONF_SIN_RESPUESTA;
    }

    if( !cfg_consigna_set( pcEnable, pcDiurna, pcNocturna ) )
    {
        xprintf( "WAN:: ERROR: CONSIGNA rechazada\r\n" );
        return wanCONF_SIN_RESPUESTA;
    }

    xprintf( "WAN:: reconfig CONSIGNA\r\n" );

    return wanCONF_RECONFIGURAR;
}

/*==============================================================================
 * La tabla de bloques: nombre, hash y parser. Tener los tres juntos es lo que
 * deja que la máquina de estados del paso 5c recorra los cinco en un lazo en
 * vez de escribir cinco veces la misma secuencia.
 *============================================================================*/

typedef struct {
    const char     *pcClase;
    uint8_t       (*pfHash)( void );
    wan_conf_rta_t (*pfAplicar)( const char *pcRta );
} wan_bloque_desc_t;

//------------------------------------------------------------------------------
static const wan_bloque_desc_t xBloques[ wanBLOQUE_NRO ] = {
    [ wanBLOQUE_BASE     ] = { "CONF_BASE",     cfg_base_hash,     prvAplicarBase     },
    [ wanBLOQUE_AINPUTS  ] = { "CONF_AINPUTS",  cfg_ainputs_hash,  prvAplicarAinputs  },
    [ wanBLOQUE_COUNTER  ] = { "CONF_COUNTERS", cfg_counter_hash,  prvAplicarCounter  },
    [ wanBLOQUE_MODBUS   ] = { "CONF_MODBUS",   cfg_modbus_hash,   prvAplicarModbus   },
    [ wanBLOQUE_CONSIGNA ] = { "CONF_CONSIGNA", cfg_consigna_hash, prvAplicarConsigna },
};

//------------------------------------------------------------------------------
const char *wan_conf_clase( wan_bloque_t eBloque )
{
    return ( eBloque < wanBLOQUE_NRO ) ? xBloques[ eBloque ].pcClase : "?";
}
//------------------------------------------------------------------------------
bool wan_conf_pedido( const wan_conf_flags_t *pxFlags, wan_bloque_t eBloque )
{
    if( pxFlags == NULL )
    {
        return false;
    }

    switch( eBloque )
    {
        case wanBLOQUE_BASE:     return pxFlags->bBase;
        case wanBLOQUE_AINPUTS:  return pxFlags->bAinputs;
        case wanBLOQUE_COUNTER:  return pxFlags->bCounter;
        case wanBLOQUE_MODBUS:   return pxFlags->bModbus;
        case wanBLOQUE_CONSIGNA: return pxFlags->bConsigna;
        default:                 return false;
    }
}
//------------------------------------------------------------------------------
uint16_t wan_frame_conf_bloque( char *pcBuf, uint16_t usSize, wan_bloque_t eBloque )
{
    int iN;

    if( ( pcBuf == NULL ) || ( usSize == 0U ) || ( eBloque >= wanBLOQUE_NRO ) )
    {
        return 0U;
    }

    if( eBloque == wanBLOQUE_BASE )
    {
        /*
         * ⚠ Sólo CONF_BASE lleva la identidad del equipo (UID, ICCID, CSQ, WDG).
         * Los otros cuatro mandan nada más que el hash. Es así en el AVR y por
         * lo tanto es lo que espera el servidor.
         */
        char pcUid[ 32 ];

        prvUidStr( pcUid, sizeof( pcUid ) );

        iN = snprintf( pcBuf, usSize,
                       "ID=%s&HW=%s&TYPE=%s&VER=%s&CLASS=%s"
                       "&UID=%s&ICCID=%s&CSQ=%u&WDG=%u&HASH=0x%02X",
                       wan_imei(), FW_HW, FW_TYPE, FW_VERSION,
                       xBloques[ eBloque ].pcClase,
                       pcUid, wan_iccid(),
                       ( unsigned ) wan_csq(),
                       ( unsigned ) wan_causa_reset(),
                       ( unsigned ) xBloques[ eBloque ].pfHash() );
    }
    else
    {
        iN = snprintf( pcBuf, usSize,
                       "ID=%s&HW=%s&TYPE=%s&VER=%s&CLASS=%s&HASH=0x%02X",
                       wan_imei(), FW_HW, FW_TYPE, FW_VERSION,
                       xBloques[ eBloque ].pcClase,
                       ( unsigned ) xBloques[ eBloque ].pfHash() );
    }

    if( ( iN < 0 ) || ( ( uint16_t ) iN >= usSize ) )
    {
        xprintf( "WAN:: ERROR: el frame %s no entra en %u bytes\r\n",
                 xBloques[ eBloque ].pcClase, ( unsigned ) usSize );
        return 0U;
    }

    return ( uint16_t ) iN;
}
//------------------------------------------------------------------------------
wan_conf_rta_t wan_conf_aplicar( wan_bloque_t eBloque, const char *pcRta )
{
    wan_conf_rta_t eRta;

    if( eBloque >= wanBLOQUE_NRO )
    {
        return wanCONF_SIN_RESPUESTA;
    }

    if( prvVeredicto( pcRta, &eRta ) )
    {
        return eRta;
    }

    return xBloques[ eBloque ].pfAplicar( pcRta );
}
//------------------------------------------------------------------------------

/*==============================================================================
 * LA RESPUESTA A UN FRAME DE DATOS (paso 5c)
 *============================================================================*/

/*==============================================================================
 * LA DERIVA DEL RTC: que la corrección MIDA en vez de esconder
 *
 * ⚠ Una vez que el equipo se pone en hora solo en cada sesión, **la deriva del
 * cristal deja de verse**: el reloj siempre está bien porque se corrige, y nunca
 * nos enteraríamos de que hay que cambiar un componente de la placa.
 *
 * ⛔ **Y ojo con CUÁL cristal**: la hora de las muestras la lleva el **MCP79410
 * con su propio cristal `Y1`** (X1/X2 de ese chip), no el de PC14/PC15 del
 * micro — ése es el LSE, que alimenta el tick del kernel y el RTC interno. Son
 * dos cristales distintos y la nota de "pendiente de hardware" del bring-up
 * hablaba del segundo, que **no tiene nada que ver con la hora que se estampa**.
 *
 * Cargar de menos hace oscilar **rápido**, siempre, y cuanto menos carga más
 * adelanta — que es justo el signo que se midió en banco.
 *
 * ⭐ Con la marca de la última sincronización guardada, **cada corrección se
 * convierte en una medición**: el desvío acumulado dividido por el tiempo
 * transcurrido da los ppm directamente. No cuesta nada y es el dato que decide
 * si hay que tocar la placa.
 *============================================================================*/

/* Ver el mapa de la SRAM en `drv_rtc79410.h` antes de mover esto. */
#define TKCMD_SYNC_SRAM_ADDR    32U

typedef struct {
    RtcTimeType_t xHora;        /* cuándo fue la última sincronización */
    uint8_t       ucChecksum;   /* suma de lo anterior, para no creerle a basura */
} __attribute__( ( packed ) ) tkcmd_sync_t;

//------------------------------------------------------------------------------
/*
 * Días transcurridos desde una época arbitraria. Sólo sirve para restar dos
 * fechas, así que el origen da igual mientras sea el mismo en los dos.
 *
 * El truco es correr el año para que **marzo sea el mes 0**: así el día
 * bisiesto cae al final y no hay que tratarlo como caso especial en el medio.
 *
 * ⚠ `y / 4` asume que todo múltiplo de 4 es bisiesto. Eso es cierto **entre
 * 2000 y 2099** —2000 lo es y 2100 no, pero queda fuera del rango de un RTC que
 * guarda el año en dos dígitos—, así que acá es exacto y no una aproximación.
 */
static uint32_t prvDiasAbsolutos( const RtcTimeType_t *pxHora )
{
    uint32_t y = ( uint32_t ) pxHora->year;
    uint32_t m = ( uint32_t ) pxHora->month;

    /* El +100/+99 es para que el año corrido nunca quede negativo; se cancela
       al restar dos fechas. */
    if( m <= 2U )
    {
        y += 99U;
        m += 12U;
    }
    else
    {
        y += 100U;
    }

    return ( 365U * y ) + ( y / 4U ) + ( ( 153U * ( m - 3U ) + 2U ) / 5U )
           + ( uint32_t ) pxHora->day;
}
//------------------------------------------------------------------------------
static int32_t prvSegundosDelDia( const RtcTimeType_t *pxHora )
{
    return ( ( int32_t ) pxHora->hour * 3600 ) + ( ( int32_t ) pxHora->min * 60 )
           + ( int32_t ) pxHora->sec;
}
//------------------------------------------------------------------------------
/* Diferencia con signo, en segundos: positiva si `a` va ADELANTADO respecto de `b`. */
static int32_t prvDiferenciaSeg( const RtcTimeType_t *pxA, const RtcTimeType_t *pxB )
{
    int32_t lDias = ( int32_t ) prvDiasAbsolutos( pxA ) - ( int32_t ) prvDiasAbsolutos( pxB );

    return ( lDias * 86400 ) + prvSegundosDelDia( pxA ) - prvSegundosDelDia( pxB );
}
//------------------------------------------------------------------------------
static uint8_t prvSyncChecksum( const tkcmd_sync_t *pxSync )
{
    const uint8_t *p = ( const uint8_t * ) pxSync;
    uint8_t        ucSuma = 0U;
    uint8_t        i;

    /* ⚠ `offsetof` y NO `sizeof - 1`: el checksum cubre lo que hay ANTES del
       campo checksum, y con `sizeof` entraría el relleno. Es el bug que en el
       paso 4 hacía que la FAT se declarara inválida en cada arranque. */
    for( i = 0U; i < ( uint8_t ) offsetof( tkcmd_sync_t, ucChecksum ); i++ )
    {
        ucSuma = ( uint8_t ) ( ucSuma + p[ i ] );
    }

    return ucSuma;
}
//------------------------------------------------------------------------------
/* Informa la deriva contra la última sincronización, y guarda la marca nueva.
   `pxAntes` es la hora que tenía el equipo; `pxNueva`, la que se le acaba de
   poner. */
static void prvDerivaInformarYGuardar( const RtcTimeType_t *pxAntes,
                                       const RtcTimeType_t *pxNueva,
                                       bool bHabiaHoraConfiable )
{
    tkcmd_sync_t xSync;

    /* ---- Lo primero: ¿cuánto se había desviado? ---- */
    if( bHabiaHoraConfiable &&
        ( drv_rtc_sram_leer( TKCMD_SYNC_SRAM_ADDR, ( char * ) &xSync,
                             ( uint8_t ) sizeof( xSync ) ) == ( int16_t ) sizeof( xSync ) ) &&
        ( xSync.ucChecksum == prvSyncChecksum( &xSync ) ) &&
        ( xSync.xHora.year >= TKSYS_ANIO_COMPILACION ) )
    {
        int32_t lDesvio     = prvDiferenciaSeg( pxAntes, pxNueva );
        int32_t lTranscurrido = prvDiferenciaSeg( pxNueva, &xSync.xHora );

        xprintf( "\r\nDERIVA DEL RTC desde la ultima sincronizacion:\r\n" );
        xprintf( "  desde        : %02u/%02u/%02u %02u:%02u\r\n",
                 ( unsigned ) xSync.xHora.day,  ( unsigned ) xSync.xHora.month,
                 ( unsigned ) xSync.xHora.year, ( unsigned ) xSync.xHora.hour,
                 ( unsigned ) xSync.xHora.min );

        if( lTranscurrido >= 3600 )
        {
            xprintf( "  transcurrido : %ld h\r\n", ( long ) ( lTranscurrido / 3600 ) );
            xprintf( "  desvio       : %+ld s (%s)\r\n", ( long ) lDesvio,
                     ( lDesvio > 0 ) ? "ADELANTADO" : "atrasado" );

            /*
             * ppm = desvio / transcurrido * 1e6. Se hace en enteros y con el
             * numerador primero para no perder la parte que interesa: con
             * desvíos de segundos sobre días, dividir antes daría cero.
             */
            long lPpm     = ( long ) ( ( lDesvio * 1000000L ) / lTranscurrido );
            long lSegDia  = ( long ) ( ( lDesvio * 86400L ) / lTranscurrido );

            xprintf( "  ==> %+ld ppm  (%+ld s/dia)\r\n", lPpm, lSegDia );

            /*
             * ⚠ El umbral no es arbitrario: **±20 ppm es la tolerancia típica de
             * un cristal de reloj** (±1,7 s/día). Por encima de eso el cristal
             * ya no está andando dentro de especificación.
             *
             * ⛔ **Y el cristal que hay que mirar es `Y1`, el del MCP79410** — el
             * que está en X1/X2 de ese chip, NO el de PC14/PC15 del micro. Son
             * dos cristales distintos y es fácil confundirlos: el del STM32 es
             * el LSE, que alimenta el tick del kernel y el RTC interno; **la
             * hora de las muestras la lleva el MCP79410 con el suyo**, que es el
             * que esta medición mide.
             */
            if( ( lPpm > 20L ) || ( lPpm < -20L ) )
            {
                xprintf( "  [!] fuera de la tolerancia tipica de un cristal (+-20 ppm).\r\n" );
                xprintf( "      El cristal es Y1, el del MCP79410 (X1/X2 de ese chip),\r\n" );
                xprintf( "      NO el de PC14/PC15 del micro.\r\n" );

                if( lPpm > 0L )
                {
                    /* Cargar de MENOS siempre adelanta, y cuanto menos carga,
                       más adelanta: es la primera cosa a verificar. */
                    xprintf( "      ADELANTA -> le falta CARGA capacitiva: verificar que\r\n" );
                    xprintf( "      Y1 tenga sus condensadores y que correspondan a su CL.\r\n" );
                }
            }
        }
        else
        {
            /* Con poco tiempo transcurrido el ppm no significa nada: la
               resolución del RTC es 1 s, así que el error relativo es enorme. */
            xprintf( "  transcurrido : %ld s -- muy poco para calcular ppm\r\n",
                     ( long ) lTranscurrido );
            xprintf( "  desvio       : %+ld s\r\n", ( long ) lDesvio );
        }
    }

    /* ---- Y se deja la marca para la próxima ---- */
    memset( &xSync, 0, sizeof( xSync ) );
    xSync.xHora      = *pxNueva;
    xSync.ucChecksum = prvSyncChecksum( &xSync );

    ( void ) drv_rtc_sram_escribir( TKCMD_SYNC_SRAM_ADDR, ( const char * ) &xSync,
                                    ( uint8_t ) sizeof( xSync ) );
}

/*------------------------------------------------------------------------------
 * `CLOCK=YYMMDDhhmm`: el servidor pone en hora al equipo.
 *
 * ⭐ **No es un adorno: es cómo el equipo se pone en hora solo en campo.** Y
 * como `drv_rtc_escribir()` escribe además la firma de la SRAM, esto **saca al
 * MCP79410 de un arranque en frío sin que nadie vaya al sitio** — que con el
 * porta pila fallando de forma intermitente no es un caso hipotético.
 *
 * ⚠ **El umbral de 90 segundos es del AVR y hay que conservarlo** (su comentario
 * lo fecha en 2021-12-14): sin él, con `timerpoll` corto el reloj se reajusta en
 * cada poleo y la hora del equipo se mueve todo el tiempo.
 *
 * ⛔ **Pero el AVR sólo compara la HORA DEL DÍA, y ése es un agujero que acá sí
 * importa.** Su cuenta es `hour*3600 + min*60 + sec` de los dos lados: un equipo
 * que arrancó frío en `2001-01-01 10:30` contra un servidor en
 * `2026-09-11 10:30` da diferencia CERO, así que **no ajustaría nunca** y el
 * equipo quedaría estampando 2001 para siempre. Es exactamente el escenario que
 * este equipo tiene abierto por el porta pila.
 *
 * Por eso acá se ajusta **siempre** en dos casos más, antes de mirar los 90 s:
 *
 *   - la firma del RTC dice que la hora NO es confiable (arranque en frío), o
 *   - la FECHA difiere — y ahí no hay nada que dosificar: una fecha distinta no
 *     es deriva del cristal, es que uno de los dos está equivocado.
 *----------------------------------------------------------------------------*/
static bool prvAplicarClock( const char *pcValor )
{
    RtcTimeType_t xNueva;
    uint8_t       i;

    /* Diez dígitos, ni uno menos: con el string corto se leería basura de los
       campos siguientes y quedaría una hora plausible e inventada. */
    for( i = 0U; i < 10U; i++ )
    {
        if( ( pcValor[ i ] < '0' ) || ( pcValor[ i ] > '9' ) )
        {
            xprintf( "WAN:: ERROR: CLOCK mal formado (%s)\r\n", pcValor );
            return false;
        }
    }

    #define DOS_DIGITOS( n )  ( ( uint8_t ) ( ( pcValor[ n ] - '0' ) * 10 + \
                                              ( pcValor[ n + 1 ] - '0' ) ) )

    memset( &xNueva, 0, sizeof( xNueva ) );
    xNueva.year  = DOS_DIGITOS( 0 );
    xNueva.month = DOS_DIGITOS( 2 );
    xNueva.day   = DOS_DIGITOS( 4 );
    xNueva.hour  = DOS_DIGITOS( 6 );
    xNueva.min   = DOS_DIGITOS( 8 );
    xNueva.sec   = 0U;      /* el servidor no los manda; el AVR hace lo mismo */

    #undef DOS_DIGITOS

    /*
     * El AVR toma además un 11.º carácter como día de la semana —y lee un byte
     * de más para hacerlo—. Acá no hace falta: `drv_rtc_escribir()` lo calcula
     * con Sakamoto, que es un dato DERIVADO de la fecha y no algo que convenga
     * recibir de afuera.
     */

    if( ( xNueva.month < 1U ) || ( xNueva.month > 12U ) ||
        ( xNueva.day   < 1U ) || ( xNueva.day   > 31U ) ||
        ( xNueva.hour > 23U ) || ( xNueva.min > 59U ) )
    {
        xprintf( "WAN:: ERROR: CLOCK fuera de rango (%s)\r\n", pcValor );
        return false;
    }

    return wan_rtc_sincronizar( &xNueva, "el servidor", false );
}
//------------------------------------------------------------------------------
bool wan_cclk_parsear( const char *pcRta, RtcTimeType_t *pxHora )
{
    if( ( pcRta == NULL ) || ( pxHora == NULL ) )
    {
        return false;
    }

    /*
     * Formato: `+CCLK: "26/09/21,11:04:08-12"`.
     *
     * Se busca la comilla y no una posición fija porque el eco del comando y el
     * CRLF corren todo; si no hay comillas, se cae al `+CCLK:` y se saltean los
     * espacios.
     *
     * ⭐ **La hora viene en LOCAL, con el huso ya aplicado** —el `-12` son
     * cuartos de hora, UTC-3— confirmado en banco el 2026-09-21 contra el reloj
     * de la PC. Si viniera en UTC, estampar eso donde el AVR estampa local
     * correría todos los registros 3 horas: plausibles y mal.
     */
    const char *p = strchr( pcRta, '"' );

    if( p == NULL )
    {
        p = strstr( pcRta, "+CCLK:" );
        p = ( p != NULL ) ? ( p + 6 ) : NULL;

        while( ( p != NULL ) && ( *p == ' ' ) )
        {
            p++;
        }
    }
    else
    {
        p++;
    }

    if( ( p == NULL ) || ( strlen( p ) < 17U ) )
    {
        return false;
    }

    /* Se validan los separadores antes de creerle a los dígitos. */
    if( ( p[ 2 ] != '/' ) || ( p[ 5 ] != '/' ) || ( p[ 8 ] != ',' ) ||
        ( p[ 11 ] != ':' ) || ( p[ 14 ] != ':' ) )
    {
        return false;
    }

    #define DOSD( n )   ( ( uint8_t ) ( ( p[ n ] - '0' ) * 10 + ( p[ n + 1 ] - '0' ) ) )

    memset( pxHora, 0, sizeof( RtcTimeType_t ) );
    pxHora->year  = DOSD( 0 );
    pxHora->month = DOSD( 3 );
    pxHora->day   = DOSD( 6 );
    pxHora->hour  = DOSD( 9 );
    pxHora->min   = DOSD( 12 );
    pxHora->sec   = DOSD( 15 );

    #undef DOSD

    if( ( pxHora->month < 1U ) || ( pxHora->month > 12U ) ||
        ( pxHora->day   < 1U ) || ( pxHora->day   > 31U ) ||
        ( pxHora->hour > 23U ) || ( pxHora->min > 59U ) || ( pxHora->sec > 59U ) )
    {
        return false;
    }

    /* Un año anterior al de compilación es imposible: el módulo todavía no
       sincronizó con la red. Mismo criterio que `tkSys`. */
    return ( pxHora->year >= TKSYS_ANIO_COMPILACION );
}
//------------------------------------------------------------------------------
bool wan_rtc_sincronizar( const RtcTimeType_t *pxNueva, const char *pcOrigen,
                          bool bSiempre )
{
    RtcTimeType_t xActual;
    bool          bHabiaHora = ( drv_rtc_validez() == rtcHORA_VALIDA );
    bool          bLeida     = drv_rtc_leer( &xActual );
    bool          bForzar    = bSiempre || !bHabiaHora;

    if( ( pxNueva == NULL ) || ( pcOrigen == NULL ) )
    {
        return false;
    }

    if( !bLeida )
    {
        /* Sin poder leer la hora actual no hay con qué comparar, así que se
           escribe: tener una hora de afuera es mejor que no tener ninguna. */
        bForzar    = true;
        bHabiaHora = false;
    }
    else if( ( xActual.year  != pxNueva->year  ) ||
             ( xActual.month != pxNueva->month ) ||
             ( xActual.day   != pxNueva->day   ) )
    {
        /* ⛔ La FECHA distinta fuerza el ajuste, y acá está el agujero del AVR:
           su cuenta es sólo `hour*3600 + min*60 + sec`, así que un equipo que
           arrancó frío en 2001-01-01 10:30 contra un servidor en 2026-09-11
           10:30 da diferencia CERO y no ajustaría nunca. */
        bForzar = true;
    }

    if( !bForzar )
    {
        /*
         * ⚠ El umbral de 90 segundos es del AVR y hay que conservarlo (su
         * comentario lo fecha en 2021-12-14): sin él, con `timerpoll` corto el
         * reloj se reajusta en cada poleo y la hora del equipo se mueve todo el
         * tiempo.
         */
        int32_t lDiff = prvDiferenciaSeg( &xActual, pxNueva );

        if( ( lDiff <= 90 ) && ( lDiff >= -90 ) )
        {
            return false;   /* dentro de la tolerancia: no se toca */
        }
    }

    if( !drv_rtc_escribir( pxNueva ) )
    {
        xprintf( "WAN:: ERROR: no se pudo poner en hora el RTC\r\n" );
        return false;
    }

    xprintf( "WAN:: RTC en hora desde %s: %02u/%02u/%02u %02u:%02u:%02u\r\n",
             pcOrigen,
             ( unsigned ) pxNueva->day,  ( unsigned ) pxNueva->month,
             ( unsigned ) pxNueva->year, ( unsigned ) pxNueva->hour,
             ( unsigned ) pxNueva->min,  ( unsigned ) pxNueva->sec );

    /* ⭐ Y acá la corrección se vuelve una MEDICIÓN. Ver el bloque de arriba. */
    prvDerivaInformarYGuardar( &xActual, pxNueva, bHabiaHora && bLeida );

    return true;
}
//------------------------------------------------------------------------------
/*
 * Las órdenes de válvula que puede traer la respuesta a un `DATA`.
 *
 * ⚠ **Se despachan por notificación y NO se ejecutan acá.** Mover la TOYI son
 * 5 s y una orden al control de presión ~14; hacerlo dentro del parseo dejaría
 * congelada la sesión con el servidor en medio de un vaciado de la ventana. Es
 * lo que hace el AVR y por lo que existen `tkFlow` y `tkCtlPres`.
 *
 * ⚠ **El orden de los chequeos importa, aunque hoy no colisione.** Se buscan las
 * `EXT_*` PRIMERO porque son las más específicas: si mañana apareciera una orden
 * que contenga a otra como subcadena, el `strstr` de la corta se la llevaría. El
 * AVR las busca al revés y se salva por casualidad —`EXT_V0_OPEN` no contiene
 * `VOPEN`, porque entre la `V` y la `O` hay un `0`—, que es una coincidencia
 * demasiado frágil para copiarla.
 *
 * Devuelve cuántas se despacharon, para que el llamador lo informe.
 */
static uint8_t prvOrdenesDeValvula( const char *pcRta )
{
    uint8_t ucN = 0U;

    /* ---- Las del control de presión (Modbus) ---- */
    if( strstr( pcRta, "EXT_V0_OPEN" ) != NULL )
    {
        xprintf( "WAN:: orden del servidor: ABRIR V0 externa\r\n" );
        tkCtlPres_orden( cpresCMD_ABRIR_V0 );
        ucN++;
    }
    else if( strstr( pcRta, "EXT_V0_CLOSE" ) != NULL )
    {
        xprintf( "WAN:: orden del servidor: CERRAR V0 externa\r\n" );
        tkCtlPres_orden( cpresCMD_CERRAR_V0 );
        ucN++;
    }

    if( strstr( pcRta, "EXT_V1_OPEN" ) != NULL )
    {
        xprintf( "WAN:: orden del servidor: ABRIR V1 externa\r\n" );
        tkCtlPres_orden( cpresCMD_ABRIR_V1 );
        ucN++;
    }
    else if( strstr( pcRta, "EXT_V1_CLOSE" ) != NULL )
    {
        xprintf( "WAN:: orden del servidor: CERRAR V1 externa\r\n" );
        tkCtlPres_orden( cpresCMD_CERRAR_V1 );
        ucN++;
    }

    /*
     * ---- Y la válvula TOYI interna ----
     *
     * ⚠ Van DESPUÉS y con `else if` contra las de arriba no serviría —son otra
     * tarea—, así que se acota distinto: sólo se miran si no hubo una `EXT_*`
     * que las contenga. Hoy no puede pasar, pero cuesta una línea.
     */
    if( strstr( pcRta, "VOPEN" ) != NULL )
    {
        xprintf( "WAN:: orden del servidor: ABRIR la valvula interna\r\n" );
        tkFlow_orden( flowORDEN_ABRIR );
        ucN++;
    }
    else if( strstr( pcRta, "VCLOSE" ) != NULL )
    {
        xprintf( "WAN:: orden del servidor: CERRAR la valvula interna\r\n" );
        tkFlow_orden( flowORDEN_CERRAR );
        ucN++;
    }

    return ucN;
}
//------------------------------------------------------------------------------
wan_data_rta_t wan_frame_data_rta( const char *pcRta, wan_data_ordenes_t *pxOrdenes )
{
    char pcVal[ 16 ];

    if( pxOrdenes != NULL )
    {
        memset( pxOrdenes, 0, sizeof( wan_data_ordenes_t ) );
    }

    if( ( pcRta == NULL ) || ( pcRta[ 0 ] == '\0' ) )
    {
        return wanDATA_SIN_RESPUESTA;
    }

    if( strstr( pcRta, "CLASS=DATA" ) == NULL )
    {
        /* Sin ningún `CLASS=` el servidor contestó pero no dijo nada: ver el
           comentario del enum, porque la causa es otra. */
        return ( strstr( pcRta, "CLASS=" ) == NULL ) ? wanDATA_VACIA
                                                     : wanDATA_OTRA_CLASE;
    }

    if( pxOrdenes == NULL )
    {
        return wanDATA_ACEPTADO;
    }

    if( prvCampo( pcRta, "CLOCK=", 0U, pcVal, sizeof( pcVal ) ) )
    {
        pxOrdenes->bClock = prvAplicarClock( pcVal );
    }

    /*
     * ⚠ `RESET` se busca como palabra suelta y eso alcanza porque el servidor la
     * manda así. No se usa `prvCampo()` porque no lleva `=`.
     */
    if( strstr( pcRta, "RESET" ) != NULL )
    {
        pxOrdenes->bReset = true;
    }

    /* ---- Las órdenes de válvula ---- */
    pxOrdenes->ucValvulas = prvOrdenesDeValvula( pcRta );

    return wanDATA_ACEPTADO;
}
//------------------------------------------------------------------------------
