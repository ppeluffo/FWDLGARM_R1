/*
 * tkCmd.c  -  ver tkCmd.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tkCmd.h"
#include "drv_uart.h"
#include "drv_i2c.h"
#include "drv_eeprom.h"
#include "drv_rtc79410.h"
#include "drv_rs485.h"
#include "drv_ina3221.h"
#include "drv_sd.h"
#include "drv_adc.h"
#include "drv_pulsos.h"
#include "frtos-io.h"
#include "frtos_cmd.h"
#include "drv_term_sense.h"
#include "pwr_lock.h"
#include "main.h"

StaticTask_t tkCmd_TCB;
StackType_t  tkCmd_Stack[ tkCmd_STACK_SIZE ];

/*==============================================================================
 * ⚠ MODO BANCO - TEMPORAL (2026-08-11)
 *
 * La consola no dio señales de vida en el primer intento, así que se reemplaza
 * por una prueba que aísla las capas. Vive acá dentro, y no en un tkTest.c
 * aparte, para NO tocar el sistema de build: un archivo nuevo obliga al baile de
 * Refresh(F5) + regenerar subdir.mk/objects.list en el IDE, y eso es una segunda
 * variable moviéndose justo cuando queremos aislar una.
 *
 * Mientras estuvo activo, el modo banco pedía además que el micro NO durmiera:
 * el tickless anulado en FreeRTOSConfig.h y el EXTI de TERM_SENSE apagado más
 * abajo. Las dos cosas ya se repusieron (2026-08-12); el apagado del EXTI queda
 * acá dentro, compilado fuera, para cuando haya que volver a usar el andamio.
 *
 * ---------------------------------------------------------------------------
 * ETAPA 1 (2026-08-11) - TX. ✅ CERRADA.
 *
 * Mandaba una línea por segundo por tres caminos (literal por HAL, snprintf por
 * HAL, y xprintf por FRTOS-IO/interrupción). Salieron las tres, o sea que quedó
 * validado todo: pines, AF7, baudios, transceiver, el stack de newlib y el
 * camino por ISR con su TxCpltCallback y su semáforo.
 *
 * La causa del silencio inicial era EL CABLE del puerto serial. Vale anotarlo:
 * las tres hipótesis de firmware (MSI descalibrado, desborde de stack, semáforo
 * de TX perdido) eran razonables y ninguna era.
 * ---------------------------------------------------------------------------
 * ETAPA 2 - RX. ✅ CERRADA.
 *
 * Primero por poleo del RDR y después por el camino real (ISR ->
 * RxCpltCallback -> xStreamBufferSendFromISR -> frtos_read). Las dos pasaron.
 * La prueba que valió fue pegar un párrafo de 46 caracteres de un saque: todos
 * llegaron en orden y sin un solo ERR ORE, o sea que el rearme del Receive_IT
 * dentro del callback aguanta el ritmo y el stream buffer no se desborda.
 *
 * Cómo funcionaba:
 *
 * Un solo mensaje al arrancar y después silencio total: todo lo que aparezca en
 * la terminal es consecuencia de un byte recibido. Silencio == no llega nada,
 * sin ambigüedad.
 *
 * Por cada byte imprime  RX 0x41 'A'  — el HEX es el punto. Un eco pelado no
 * distingue "no llega nada" de "llega corrupto"; el HEX sí:
 *
 *   nada                 -> no llega el byte. PB7, AF7, el cable en ese sentido,
 *                           o la terminal que no está mandando (¿eco local?).
 *   HEX equivocado       -> el byte llega pero mal muestreado: baudios/reloj.
 *   líneas ERR FE / NE   -> confirmación de lo anterior por la vía del hardware.
 *   HEX correcto         -> RX crudo cerrado. Se pasa TKCMD_BANCO_RX_CRUDO a 0
 *                           para probar el camino real (ISR + stream buffer).
 *
 * ✅ CERRADO EL 2026-08-11. Las dos etapas pasaron, así que esto queda en 0 y la
 * consola vuelve a ser la de verdad. NO se borra: es el andamio que sirve para
 * el próximo puerto serie (modem LTE, RS485), donde las preguntas van a ser las
 * mismas. Se reactiva poniendo TKCMD_MODO_BANCO en 1.
 *============================================================================*/
#define TKCMD_MODO_BANCO        0

/* 1 = eco leyendo el RDR por poleo (HAL cruda, sin ISR).
   0 = eco por el camino real: frtos_read() bloqueando en el stream buffer.

   El 1 ✅ pasó el 2026-08-11: 'qwertpablo' devolvió 0x71 0x77 0x65 0x72 0x74
   0x70 0x61 0x62 0x6C 0x6F, sin un solo ERR FE/NE. O sea que el pin, el AF, el
   cable y el muestreo del USART están bien, y lo único que queda por validar es
   la cadena ISR -> RxCpltCallback -> xStreamBufferSendFromISR -> frtos_read. */
#define TKCMD_BANCO_RX_CRUDO    0

/*
 * TEST DE LA "U" — mide los baudios REALES con el osciloscopio.
 *
 * 'U' es 0x55 = 0b01010101. Con 8N1 la trama sale
 *
 *     start  b0 b1 b2 b3 b4 b5 b6 b7  stop      start...
 *       0    1  0  1  0  1  0  1  0    1          0
 *
 * o sea 0,1,0,1,... y como el stop es 1 y el start siguiente es 0, mandando 'U'
 * sin parar la alternancia NO se interrumpe en el borde de trama: sale una onda
 * cuadrada perfecta de frecuencia = baudios / 2.
 *
 * Se mide con el contador de frecuencia del osciloscopio, sin cursores:
 *
 *     9600 baudios  ->  4800 Hz  (período 208,3 us)
 *   115200 baudios  -> 57600 Hz  (período  17,4 us)
 *
 * Lo que leas x2 son los baudios reales, con cuatro dígitos. Si no coincide, el
 * cociente contra el nominal ES el error del reloj — no hay nada que interpretar.
 *
 * Mientras está en 1 no se manda otra cosa: el punto es que la onda sea continua.
 *
 * NO hizo falta el 2026-08-11 (el problema era el cable), pero queda acá: es la
 * forma más rápida que hay de medir baudios reales, y va a servir con el modem y
 * con el RS485.
 */
#define TKCMD_BANCO_ONDA_U      0

#if ( TKCMD_MODO_BANCO == 1 )

extern UART_HandleTypeDef huart1;

/* Manda una cadena por el camino más crudo que hay: HAL por poleo. Se usa para
   TODO lo de abajo, incluso para reportar el RX, así que si algo falla no puede
   ser el lado de la transmisión — eso ya quedó validado en la etapa 1. */
static void prvTx( const char *pcTexto )
{
    ( void ) HAL_UART_Transmit( &huart1, ( uint8_t * ) pcTexto,
                                ( uint16_t ) strlen( pcTexto ), 500U );
}

void tkCmd( void *pvParameters )
{
    ( void ) pvParameters;

    char cLinea[ 96 ];

    /*
     * MIGAS DE PAN. Cada letra sale apenas se supera esa etapa, por HAL cruda.
     * Si el arranque se cuelga, la ÚLTIMA letra que aparezca dice exactamente
     * dónde — sin debugger y sin adivinar.
     *
     *   nada -> ni siquiera llegó a correr tkCmd, o el USART no está vivo.
     *           Ahí el sospechoso es el NRST trabado (ver CLAUDE.md), no esto.
     *   [A]  -> la tarea arrancó y la TX anda.
     *   [C]  -> frtos_open_all() volvió bien: semáforos y stream buffer creados.
     *   [D]  -> el Receive_IT quedó abortado.
     *
     * Hubo una miga [B] que apagaba la EXTI de TERM_SENSE en el NVIC. Se sacó el
     * 2026-08-12, cuando el pin pasó a leerse por poleo desde tkCtl y dejó de
     * tener línea EXTI que apagar.
     */
    prvTx( "\r\n\r\n[A] tkCmd arranco\r\n" );

#if ( TKCMD_BANCO_ONDA_U == 1 )
    /*
     * Onda cuadrada continua para medir los baudios reales. Ver el comentario de
     * TKCMD_BANCO_ONDA_U. Nada de FRTOS-IO ni de newlib acá: sólo la HAL por
     * poleo, para que lo único que se esté midiendo sea el reloj.
     */
    static const char cOndaU[] =
        "UUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUU"
        "UUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUU";   /* 64 */

    for( ;; )
    {
        ( void ) HAL_UART_Transmit( &huart1, ( uint8_t * ) cOndaU,
                                    ( uint16_t ) ( sizeof( cOndaU ) - 1U ),
                                    HAL_MAX_DELAY );
    }
#endif

    /* Hace falta igual para la capa 2 (xprintf): crea los semáforos y el mutex
       del driver. Si esto fallara, drv_uart_write() haría xSemaphoreTake(NULL)
       y el configASSERT congelaría todo, incluido el LED. */
    if( frtos_open_all() == false )
    {
        prvTx( "[!] frtos_open_all() FALLO\r\n" );
        Error_Handler();
    }
    prvTx( "[C] drivers abiertos\r\n" );

#if ( TKCMD_BANCO_RX_CRUDO == 1 )
    /* frtos_open_all() dejó armado un Receive_IT que se comería los bytes antes
       de que el poleo los vea. Se aborta. */
    drv_uart_rx_disable( drvUART_TERM );
    prvTx( "[D] Receive_IT abortado\r\n" );
#endif

    /* ---- único mensaje: el de arranque -------------------------------------- */
    ( void ) snprintf( cLinea, sizeof( cLinea ),
                       "== ECO %s == tipea algo\r\n",
                       ( TKCMD_BANCO_RX_CRUDO == 1 ) ? "por poleo del RDR"
                                                     : "por ISR + stream buffer" );
    prvTx( cLinea );

    /*
     * De acá en más NO se manda nada por cuenta propia: todo lo que aparezca en
     * la terminal es consecuencia de un byte recibido. Así, silencio == no llega
     * nada, sin ambigüedad.
     *
     * Y no se hace un eco pelado a propósito: devolver el carácter tal cual no
     * distingue "no llega nada" de "llega corrupto". Mostrando el HEX se ve al
     * toque cuál de las dos es. Si tipeás 'A' y aparece 0x41, perfecto. Si
     * aparece 0x00, 0xFF o cualquier otra cosa, el byte llega pero mal muestreado
     * -> baudios/reloj. Y si además saltan FE/NE, es directamente eso.
     */
    for( ;; )
    {
#if ( TKCMD_BANCO_RX_CRUDO == 1 )
        /*
         * Leer el ISR/RDR desde una tarea viola el principio HAL del proyecto
         * (sólo el driver toca registros). Es a propósito y es temporal: la
         * gracia de esta prueba es justamente saltear el driver.
         */
        uint32_t ulIsr = huart1.Instance->ISR;

        /* Los errores primero: si el baudrate está mal, acá aparecen FE y NE. */
        if( ( ulIsr & ( USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE ) ) != 0U )
        {
            ( void ) snprintf( cLinea, sizeof( cLinea ), "ERR%s%s%s%s\r\n",
                               ( ulIsr & USART_ISR_ORE ) ? " ORE" : "",
                               ( ulIsr & USART_ISR_FE  ) ? " FE"  : "",
                               ( ulIsr & USART_ISR_NE  ) ? " NE"  : "",
                               ( ulIsr & USART_ISR_PE  ) ? " PE"  : "" );

            huart1.Instance->ICR = USART_ICR_ORECF | USART_ICR_FECF
                                 | USART_ICR_NECF  | USART_ICR_PECF;
            prvTx( cLinea );
        }

        if( ( ulIsr & USART_ISR_RXNE ) != 0U )
        {
            uint8_t ucRx = ( uint8_t ) ( huart1.Instance->RDR & 0xFFU );

            ( void ) snprintf( cLinea, sizeof( cLinea ), "RX 0x%02X '%c'\r\n",
                               ucRx,
                               ( ( ucRx >= 32U ) && ( ucRx < 127U ) ) ? ( char ) ucRx : '.' );
            prvTx( cLinea );
        }

        /*
         * Un tick (1,95 ms) entre vueltas, y NO taskYIELD().
         *
         * Con taskYIELD() en lazo apretado esta tarea queda siempre Ready y la
         * Idle (prioridad 0) no corre NUNCA. Eso deja sin liberar la memoria de
         * defaultTask —que se autoelimina al arrancar— y, más importante, hace
         * que el LED sea un testigo poco confiable de si el sistema está vivo.
         * Con el delay, si el LED destella el kernel está sano y el problema es
         * de otro lado.
         *
         * El precio: a 9600 un carácter dura 1,04 ms y este USART no tiene FIFO,
         * así que tipeando rápido o pegando texto se pierden bytes. No importa:
         * el overrun ahora se REPORTA como "ERR ORE", así que es visible y no
         * silencioso, que era el único riesgo real.
         */
        vTaskDelay( 1 );
#else
        /* El camino real: la tarea se BLOQUEA hasta que la ISR le mete un byte
           en el stream buffer. Timeout por defecto = portMAX_DELAY. */
        char cRx;

        if( frtos_read( fdTERM, &cRx, 1U ) == 1 )
        {
            ( void ) snprintf( cLinea, sizeof( cLinea ), "RX 0x%02X '%c'\r\n",
                               ( uint8_t ) cRx,
                               ( ( cRx >= 32 ) && ( cRx < 127 ) ) ? cRx : '.' );
            prvTx( cLinea );
        }
#endif
    }
}

#else   /* ------------------ consola de verdad, TKCMD_MODO_BANCO == 0 -------- */

static void cmdHelp( void );
static void cmdStatus( void );
static void cmdSense( void );
static void cmdI2c( void );
static void cmdEe( void );
static void cmdRtc( void );
static void cmdRs485( void );
static void cmdIna( void );
static void cmdSd( void );
static void cmdVin( void );
static void cmdCnt( void );
static void cmdKeys( void );
static void cmdReset( void );
static void cmdReboot( void );

/* Ayudas detalladas de cada comando. Se definen junto a su comando, más abajo. */
static void prvI2cUso  ( void );
static void prvEeUso   ( void );
static void prvRtcUso  ( void );
static void prvRs485Uso( void );
static void prvInaUso  ( void );
static void prvSdUso   ( void );
static void prvVinUso  ( void );
static void prvCntUso  ( void );

/*
 * Causa del último reset, leída de RCC_CSR antes de limpiarla.
 *
 * Las banderas son ACUMULATIVAS: quedan puestas hasta que alguien escribe RMVF.
 * Por eso se leen y se limpian una sola vez, al arrancar — así el próximo
 * arranque informa su causa y no la de todos los anteriores juntos.
 *
 * No es sólo para depurar: en un datalogger a batería, saber si rebotó por
 * watchdog, por BOR (batería floja) o por software es información de campo.
 */
static uint32_t ulCausaReset;

#define CAUSA_RESET_MASK   ( RCC_CSR_LPWRRSTF | RCC_CSR_WWDGRSTF | RCC_CSR_IWDGRSTF \
                           | RCC_CSR_SFTRSTF  | RCC_CSR_BORRSTF  | RCC_CSR_PINRSTF )

static void prvImprimirCausaReset( void )
{
    if( ( ulCausaReset & CAUSA_RESET_MASK ) == 0U )
    {
        /* Ninguna bandera puesta = no hubo reset de hardware. Es lo que pasa
           después de un 'reboot', que salta al vector sin resetear nada. */
        xprintf( "reset por    : ninguno (reinicio tibio, sin reset de hardware)\r\n" );
        return;
    }

    xprintf( "reset por    :%s%s%s%s%s%s\r\n",
             ( ulCausaReset & RCC_CSR_LPWRRSTF ) ? " LPWR"    : "",
             ( ulCausaReset & RCC_CSR_WWDGRSTF ) ? " WWDG"    : "",
             ( ulCausaReset & RCC_CSR_IWDGRSTF ) ? " IWDG"    : "",
             ( ulCausaReset & RCC_CSR_SFTRSTF  ) ? " SOFT"    : "",
             ( ulCausaReset & RCC_CSR_BORRSTF  ) ? " BOR/POR" : "",
             ( ulCausaReset & RCC_CSR_PINRSTF  ) ? " PIN"     : "" );
}

//------------------------------------------------------------------------------
void tkCmd( void *pvParameters )
{
    ( void ) pvParameters;

    char cChar;

    /*
     * Antes que nada, porque cualquier cosa que resetee de nuevo las pisa.
     *
     * Y hay que BAJAR RMVF después de subirlo: en el L4 no es autolimpiante
     * (__HAL_RCC_CLEAR_RESET_FLAGS() sólo hace SET_BIT). Mientras quede en 1 las
     * banderas se mantienen borradas y el próximo arranque informaría "ninguno"
     * aunque haya habido un reset de verdad. Se salvó de pasar desapercibido
     * porque un reset de hardware resetea RCC_CSR y de paso bajaba RMVF — pero
     * después de un 'reboot', que no resetea nada, el bit quedaba puesto.
     */
    ulCausaReset = RCC->CSR;
    __HAL_RCC_CLEAR_RESET_FLAGS();
    CLEAR_BIT( RCC->CSR, RCC_CSR_RMVF );

    /* Los drivers se abren desde acá y no desde main(): crear semáforos y stream
       buffers necesita el scheduler corriendo. */
    if( frtos_open_all() == false )
    {
        Error_Handler();
    }

    drv_term_sense_init();

    /* Arranca el oscilador y habilita el respaldo por pila si hiciera falta. Es
       idempotente. Si el chip no contesta no se aborta: la consola tiene que
       levantar igual, que es justamente cuando más se la necesita. */
    if( drv_rtc_init() == false )
    {
        xprintf( "\r\n[!] el RTC MCP79410 no contesta\r\n" );
    }

    /* Mismo criterio: deja el riel de sensores apagado y el chip en power-down.
       Si no está poblado, se avisa y se sigue. */
    if( drv_ina_init() == false )
    {
        xprintf( "\r\n[!] el INA3221 no contesta o no se identifico\r\n" );
    }

    /* No toca la tarjeta: sólo deja el riel apagado y los pines del SPI en alta
       impedancia, que es el estado de reposo. */
    ( void ) drv_sd_init();

    /* Calibra el ADC y deja los dos load switches apagados. */
    if( drv_adc_init() == false )
    {
        xprintf( "\r\n[!] fallo la calibracion del ADC1\r\n" );
    }

    /* Pone los contadores en cero. El pin y la EXTI ya los configuró CubeMX, así
       que desde acá en adelante los pulsos se cuentan solos. */
    drv_pulsos_init();

    FRTOS_CMD_init();
    FRTOS_CMD_register( "help",   cmdHelp   );
    FRTOS_CMD_register( "status", cmdStatus );
    FRTOS_CMD_register( "sense",  cmdSense  );
    FRTOS_CMD_register( "i2c",    cmdI2c    );
    FRTOS_CMD_register( "ee",     cmdEe     );
    FRTOS_CMD_register( "rtc",    cmdRtc    );
    FRTOS_CMD_register( "rs485",  cmdRs485  );
    FRTOS_CMD_register( "ina",    cmdIna    );
    FRTOS_CMD_register( "sd",     cmdSd     );
    FRTOS_CMD_register( "vin",    cmdVin    );
    FRTOS_CMD_register( "cnt",    cmdCnt    );
    FRTOS_CMD_register( "keys",   cmdKeys   );
    FRTOS_CMD_register( "reset",  cmdReset  );
    FRTOS_CMD_register( "reboot", cmdReboot );

    /* La versión y la fecha de compilación en el banner, no sólo en 'status':
       es lo primero que uno quiere ver al enchufar la terminal, y contesta sin
       tipear nada la pregunta de si quedó flasheado el binario que se creía. */
    xprintf( "\r\n\r\n%s %s - consola TERM\r\n", FW_NOMBRE, FW_VERSION );
    xprintf( "compilado %s\r\n", FW_FECHA );
    prvImprimirCausaReset();
    xprintf( "cmd>" );

    for( ;; )
    {
        /*
         * Bloqueo indefinido en el kernel: mientras no llegue un carácter esta
         * tarea no consume nada y el micro puede dormir. El timeout por defecto
         * de fdTERM es portMAX_DELAY; se cambia con
         * frtos_ioctl(fdTERM, ioctl_SET_TIMEOUT, &ticks).
         */
        if( frtos_read( fdTERM, &cChar, 1U ) == 1 )
        {
            ( void ) FRTOS_CMD_process( cChar );
        }
    }
}

/*------------------------------------------------------------------------------
 * Comandos
 *----------------------------------------------------------------------------*/

/*==============================================================================
 * Ayuda
 *
 * UNA tabla, dos usos: el resumen de una línea que sale con 'help' pelado, y el
 * puntero a la ayuda detallada que sale con 'help <comando>'.
 *
 * Está en una tabla y no repartido por el archivo porque antes SÍ estaba
 * repartido, y se notó: los comandos tenían opciones que el 'help' no
 * mencionaba, así que existían pero nadie las encontraba. Un resumen escrito
 * lejos del comando que describe envejece mal.
 *============================================================================*/
typedef struct {
    const char *pcNombre;
    const char *pcResumen;      /* una línea, para el listado general */
    void      ( *fnUso )( void ); /* el detalle, o NULL si no toma argumentos */
} cmd_ayuda_t;

static const cmd_ayuda_t xAyuda[] = {
    { "help",   "esta ayuda. 'help <comando>' para el detalle de uno",   NULL          },
    { "status", "estado del sistema",                                    NULL          },
    { "sense",  "nivel de TERM_SENSE (PB5) + monitor de flancos",        NULL          },
    { "i2c",    "bus I2C2 crudo: escaneo y acceso a registros",          prvI2cUso     },
    { "ee",     "EEPROM M24M01 (128 KB): leer, escribir y test",         prvEeUso      },
    { "rtc",    "RTC externo MCP79410: hora, validez y cortes",          prvRtcUso     },
    { "rs485",  "bus RS485 y los 3 rieles de alimentacion",              prvRs485Uso   },
    { "ina",    "INA3221: medida de los lazos de 4-20 mA",               prvInaUso     },
    { "sd",     "tarjeta microSD: energia, arranque y sectores",         prvSdUso      },
    { "vin",    "tension de los rieles: 12 V y VDDA (3V3)",              prvVinUso     },
    { "cnt",    "contador de pulsos CNT0 (PA12): cuenta y estado",       prvCntUso     },
    { "keys",   "muestra el codigo crudo de cada tecla (diagnostico)",   NULL          },
    { "reset",  "reset por NVIC_SystemReset (pulsa NRST)",               NULL          },
    { "reboot", "reinicio tibio, sin tocar NRST (diagnostico)",          NULL          },
};

#define AYUDA_COUNT     ( sizeof( xAyuda ) / sizeof( xAyuda[ 0 ] ) )

static void cmdHelp( void )
{
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    /* ---- 'help' pelado: el listado ---- */
    if( ( ucArgs == 0U ) || ( argv[ 1 ] == NULL ) )
    {
        xprintf( "Comandos:\r\n" );

        for( uint32_t i = 0U; i < AYUDA_COUNT; i++ )
        {
            xprintf( "  %-7s - %s%s\r\n",
                     xAyuda[ i ].pcNombre, xAyuda[ i ].pcResumen,
                     ( xAyuda[ i ].fnUso != NULL ) ? "  [+]" : "" );
        }

        xprintf( "\r\n  [+] tiene mas opciones: 'help <comando>'\r\n" );

        /* El parser matchea por PREFIJO, así que 'r' y 're' caen en 'reset', que
           es el primero registrado, y 's' cae en 'status'. */
        xprintf( "  (matchea por prefijo: 'res'/'reb', 'st'/'se', 'rt'/'rs', 'i2'/'in')\r\n" );
        return;
    }

    /* ---- 'help <comando>' ---- */
    size_t xLargo = strlen( argv[ 1 ] );

    for( uint32_t i = 0U; i < AYUDA_COUNT; i++ )
    {
        /* Por prefijo y no por igualdad, para que 'help rs' funcione igual que
           'rs': una sola regla de matcheo en toda la consola. */
        if( strncmp( xAyuda[ i ].pcNombre, argv[ 1 ], xLargo ) == 0 )
        {
            xprintf( "%s - %s\r\n\r\n", xAyuda[ i ].pcNombre, xAyuda[ i ].pcResumen );

            if( xAyuda[ i ].fnUso != NULL )
            {
                xAyuda[ i ].fnUso();
            }
            else
            {
                xprintf( "no toma argumentos.\r\n" );
            }
            return;
        }
    }

    xprintf( "'%s' no existe. 'help' lista los comandos.\r\n", argv[ 1 ] );
}
//------------------------------------------------------------------------------
static void cmdStatus( void )
{
    xprintf( "version      : %s %s\r\n", FW_NOMBRE, FW_VERSION );
    xprintf( "compilado    : %s\r\n", FW_FECHA );
    xprintf( "tick        : %lu (%lu Hz)\r\n",
             ( unsigned long ) xTaskGetTickCount(),
             ( unsigned long ) configTICK_RATE_HZ );
    xprintf( "clock        : %lu Hz\r\n", ( unsigned long ) SystemCoreClock );
    prvImprimirCausaReset();
    xprintf( "terminal     : %s\r\n", drv_term_sense_presente() ? "conectada" : "ausente" );
    xprintf( "pwr locks    : 0x%08lX %s\r\n",
             ( unsigned long ) pwr_lock_estado(),
             pwr_deep_sleep_permitido() ? "(Stop 2 habilitado)" : "(solo Sleep)" );
    xprintf( "heap libre   : %u bytes\r\n", ( unsigned ) xPortGetFreeHeapSize() );
    xprintf( "stack tkCmd  : %u palabras libres\r\n",
             ( unsigned ) uxTaskGetStackHighWaterMark( NULL ) );
}
//------------------------------------------------------------------------------
/*
 * TERM_SENSE: foto del estado + monitor en vivo.
 *
 * Separa de una pasada las tres causas de un "ausente" con la terminal puesta,
 * que desde afuera se ven iguales:
 *
 *   el nivel NUNCA baja           -> HARDWARE: el pull-up interno lo deja en alto
 *                                    y nada lo tira a masa. Tester y esquemático.
 *   el nivel baja pero el driver
 *   sigue diciendo ausente        -> el poleo de tkCtl no está corriendo.
 *   el driver ve la terminal pero
 *   el candado queda libre        -> FIRMWARE, en prvActualizar().
 *
 * El monitor lee el pin CRUDO cada 100 ms, mucho más seguido que el poleo real de
 * tkCtl: es a propósito, así se ve el atraso del muestreo (la columna "driver"
 * cambia hasta una vuelta de tkCtl después que la columna del pin). Es un
 * instrumento de banco; en producción el pin lo mira tkCtl una vez por vuelta y
 * nada más.
 */
#define SENSE_MONITOR_MS        20000U
#define SENSE_MUESTREO_MS         100U

static void cmdSense( void )
{
    drv_term_sense_cfg_t xCfg;
    drv_term_sense_config( &xCfg );

    bool bNivel = drv_term_sense_nivel_pin();

    xprintf( "TERM_SENSE (PB5), activo en BAJO, leido por poleo desde tkCtl\r\n" );
    xprintf( "  nivel del pin : %d (%s)  -> terminal %s\r\n",
             bNivel ? 1 : 0,
             bNivel ? "alto" : "bajo",
             bNivel ? "AUSENTE" : "PRESENTE" );
    xprintf( "  driver dice   : %s\r\n", drv_term_sense_presente() ? "conectada" : "ausente" );
    xprintf( "  cambios       : %lu\r\n", ( unsigned long ) drv_term_sense_cambios() );
    xprintf( "  candado TERM  : %s\r\n",
             ( pwr_lock_estado() & ( 1UL << pwrLOCK_TERM ) ) ? "tomado" : "libre" );
    xprintf( "  MODER/PUPDR   : %lu / %lu (espera 0 = entrada / 1 = pull-up)\r\n",
             ( unsigned long ) xCfg.ulModer, ( unsigned long ) xCfg.ulPupdr );

    xprintf( "\r\nmonitor %lu s - enchufa y desenchufa la terminal.\r\n",
             ( unsigned long ) ( SENSE_MONITOR_MS / 1000U ) );
    xprintf( "cualquier tecla corta.\r\n" );

    /* Lecturas con timeout corto: sirven a la vez de espera entre muestras y de
       chequeo de tecla, sin un vTaskDelay aparte. */
    TickType_t xEspera = pdMS_TO_TICKS( SENSE_MUESTREO_MS );
    ( void ) frtos_ioctl( fdTERM, ioctl_SET_TIMEOUT, &xEspera );

    bool     bAnterior = bNivel;
    uint32_t ulVueltas = SENSE_MONITOR_MS / SENSE_MUESTREO_MS;
    char     cTecla;

    while( ulVueltas-- > 0U )
    {
        if( frtos_read( fdTERM, &cTecla, 1U ) == 1 )
        {
            break;
        }

        bNivel = drv_term_sense_nivel_pin();

        if( bNivel != bAnterior )
        {
            xprintf( "  t=%lu  pin -> %d (%s)   driver: %s   candado: %s\r\n",
                     ( unsigned long ) xTaskGetTickCount(),
                     bNivel ? 1 : 0,
                     bNivel ? "AUSENTE" : "PRESENTE",
                     drv_term_sense_presente() ? "conectada" : "ausente",
                     ( pwr_lock_estado() & ( 1UL << pwrLOCK_TERM ) ) ? "tomado" : "libre" );
            bAnterior = bNivel;
        }
    }

    /* Devolver el bloqueo indefinido: es lo que espera el lazo de la consola. */
    xEspera = portMAX_DELAY;
    ( void ) frtos_ioctl( fdTERM, ioctl_SET_TIMEOUT, &xEspera );

    xprintf( "monitor terminado. cambios de estado: %lu\r\n",
             ( unsigned long ) drv_term_sense_cambios() );
}
//------------------------------------------------------------------------------
/*
 * Herramienta de bring-up del bus I2C2.
 *
 *   i2c scan
 *   i2c read  <dev> <mem> <largoDir> <n>
 *   i2c write <dev> <mem> <largoDir> <byte> [byte...]
 *
 * Todo en HEXA y con la dirección de dispositivo en FORMATO DE 8 BITS: 0xA0 para
 * la EEPROM, 0xDE para el RTC. Ver drv_i2c.h para el porqué.
 *
 * El escaneo vale incluso con la placa pelada, y es lo primero que hay que correr:
 *
 *   contesta alguien          -> pines, pull-up, reloj y el chip: todo bien.
 *   no contesta nadie, pero
 *   el comando vuelve rapido  -> el bus ELECTRICAMENTE anda. Cada dirección
 *                                terminó en NACK, que es lo correcto cuando no
 *                                hay nadie: el micro generó los clocks y las
 *                                líneas volvieron a alto solas.
 *   el barrido tarda ~un
 *   cuarto de segundo por
 *   dirección, o se cuelga    -> una línea trabada en BAJO. Sospechar pull-up
 *                                ausentes, un esclavo colgado, o un corto.
 */
#define I2C_MAX_BYTES       32U

static uint32_t prvHex( const char *pcTexto )
{
    return ( pcTexto != NULL ) ? strtoul( pcTexto, NULL, 16 ) : 0UL;
}

static void prvI2cUso( void )
{
    xprintf( "uso (todo en HEXA, dev en formato de 8 bits: EEPROM=A0, RTC=DE):\r\n" );
    xprintf( "  i2c scan\r\n" );
    xprintf( "  i2c read  <dev> <mem> <largoDir> <n>\r\n" );
    xprintf( "  i2c write <dev> <mem> <largoDir> <byte> [byte...]\r\n" );
    xprintf( "  largoDir: 1 para el RTC, 2 para la EEPROM\r\n" );
}

static void prvI2cScan( void )
{
    xprintf( "escaneando I2C2 (direcciones de 7 bits)...\r\n" );
    xprintf( "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n" );

    uint32_t ulEncontrados = 0U;

    for( uint8_t ucFila = 0U; ucFila < 8U; ucFila++ )
    {
        xprintf( "%02x: ", ( unsigned ) ( ucFila << 4 ) );

        for( uint8_t ucCol = 0U; ucCol < 16U; ucCol++ )
        {
            uint8_t ucAddr7 = ( uint8_t ) ( ( ucFila << 4 ) | ucCol );

            /* 0x00-0x07 y 0x78-0x7F son direcciones reservadas por el estándar:
               no se sondean para no meter ruido en el bus. */
            if( ( ucAddr7 < 0x08U ) || ( ucAddr7 > 0x77U ) )
            {
                xprintf( "   " );
                continue;
            }

            if( drv_i2c_probe( ( uint8_t ) ( ucAddr7 << 1 ) ) )
            {
                xprintf( "%02x ", ( unsigned ) ucAddr7 );
                ulEncontrados++;
            }
            else
            {
                xprintf( "-- " );
            }
        }
        xprintf( "\r\n" );
    }

    xprintf( "dispositivos: %lu\r\n", ( unsigned long ) ulEncontrados );

    if( ulEncontrados == 0U )
    {
        xprintf( "nadie contesto. Si el barrido fue RAPIDO el bus esta sano y no\r\n" );
        xprintf( "hay chips; si fue LENTO, sospechar una linea trabada en bajo.\r\n" );
    }
    else
    {
        /* Traducir lo conocido, para no tener que ir al datasheet. */
        xprintf( "esperados en R001: 50..53 = EEPROM M24M02 (dev A0..A6)\r\n" );
        xprintf( "                   6f     = MCP79410 RTCC (dev DE)\r\n" );
        xprintf( "                   57     = MCP79410 EEPROM interna (dev AE)\r\n" );
    }
}

static void cmdI2c( void )
{
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    if( ( ucArgs == 0U ) || ( argv[ 1 ] == NULL ) )
    {
        prvI2cUso();
        return;
    }

    if( strcmp( argv[ 1 ], "scan" ) == 0 )
    {
        prvI2cScan();
        return;
    }

    /* Los dos comandos restantes comparten los tres primeros parámetros. */
    if( ucArgs < 5U )
    {
        prvI2cUso();
        return;
    }

    uint8_t  ucDev    = ( uint8_t )  prvHex( argv[ 2 ] );
    uint16_t usMem    = ( uint16_t ) prvHex( argv[ 3 ] );
    uint8_t  ucLargo  = ( uint8_t )  prvHex( argv[ 4 ] );
    char     pcDatos[ I2C_MAX_BYTES ];

    if( strcmp( argv[ 1 ], "read" ) == 0 )
    {
        uint32_t ulN = prvHex( argv[ 5 ] );

        if( ( ulN == 0UL ) || ( ulN > I2C_MAX_BYTES ) )
        {
            xprintf( "n debe estar entre 1 y %u (en hexa)\r\n", ( unsigned ) I2C_MAX_BYTES );
            return;
        }

        int16_t sRet = drv_i2c_read( ucDev, usMem, ucLargo, pcDatos,
                                     ( uint16_t ) ulN, pdMS_TO_TICKS( 1000 ) );

        if( sRet < 0 )
        {
            xprintf( "ERROR: HAL_I2C_ERROR = 0x%08lX%s\r\n",
                     ( unsigned long ) drv_i2c_last_error(),
                     ( drv_i2c_last_error() & HAL_I2C_ERROR_AF ) ? "  (AF = nadie contesto)" : "" );
            return;
        }

        xprintf( "dev %02X mem %04X:", ( unsigned ) ucDev, ( unsigned ) usMem );
        for( int16_t i = 0; i < sRet; i++ )
        {
            xprintf( " %02X", ( unsigned ) ( ( uint8_t ) pcDatos[ i ] ) );
        }
        xprintf( "\r\n" );
        return;
    }

    if( strcmp( argv[ 1 ], "write" ) == 0 )
    {
        uint8_t ucN = 0U;

        /* argv[5] en adelante son los bytes. makeArgv() devuelve la cantidad de
           argumentos sin contar el comando, así que el último válido es argv[ucArgs]. */
        for( uint8_t i = 5U; ( i <= ucArgs ) && ( ucN < I2C_MAX_BYTES ); i++ )
        {
            pcDatos[ ucN++ ] = ( char ) prvHex( argv[ i ] );
        }

        int16_t sRet = drv_i2c_write( ucDev, usMem, ucLargo, pcDatos,
                                      ucN, pdMS_TO_TICKS( 1000 ) );

        if( sRet < 0 )
        {
            xprintf( "ERROR: HAL_I2C_ERROR = 0x%08lX%s\r\n",
                     ( unsigned long ) drv_i2c_last_error(),
                     ( drv_i2c_last_error() & HAL_I2C_ERROR_AF ) ? "  (AF = nadie contesto)" : "" );
            return;
        }

        xprintf( "escritos %d bytes en dev %02X mem %04X\r\n",
                 ( int ) sRet, ( unsigned ) ucDev, ( unsigned ) usMem );
        return;
    }

    prvI2cUso();
}
//------------------------------------------------------------------------------
/*
 * EEPROM M24M01, con dirección PLANA de 17 bits: 00000..1FFFF.
 *
 *   ee rd   <addr> <n>            lee y muestra en hexa + ASCII
 *   ee wr   <addr> <texto>        escribe el texto (sin espacios)
 *   ee test                       las dos trampas de una vez
 *
 * El 'test' es el que vale. Prueba a la vez las dos cosas que rompen en silencio
 * y que ningún read/write suelto detecta:
 *
 *  - CRUCE DE PÁGINA: escribe 300 bytes arrancando 100 antes de un borde de 256.
 *    Sin partir en páginas, los últimos bytes darían la vuelta y pisarían el
 *    principio de la misma página. La relectura lo caza.
 *  - CRUCE DE BLOQUE: escribe a caballo de la frontera de los 64 KB, donde cambia
 *    la dirección de dispositivo (A0 -> A2). Si el bit A16 no se calcula bien,
 *    la segunda mitad aterriza en el bloque equivocado.
 *
 * Escribe de verdad, en dos zonas concretas: 0x00F60..0x0108B y 0x0FFC0..0x1003F.
 */
#define EE_BUF          32U
#define EE_TEST_LARGO   300U

static void prvEeUso( void )
{
    xprintf( "uso (addr y n en HEXA, espacio plano 00000..1FFFF):\r\n" );
    xprintf( "  ee rd   <addr> <n>       n hasta %02X\r\n", ( unsigned ) EE_BUF );
    xprintf( "  ee wr   <addr> <texto>   texto sin espacios\r\n" );
    xprintf( "  ee test                  cruce de pagina y de bloque\r\n" );
}

/*
 * Escribe un patrón, lo relee y compara. El patrón depende de la posición
 * ABSOLUTA, así que si un tramo aterriza donde no va, el byte no coincide y de
 * paso dice a qué posición correspondía.
 */
static bool prvEeProbarTramo( const char *pcNombre, uint32_t ulAddr )
{
    /* Estático y no en el stack: son 300 bytes contra las 512 palabras de tkCmd,
       de las que hoy quedan libres unas 367. Entraría, pero dejar el margen del
       stack colgando de un buffer de banco es pedirle un desborde al futuro. En
       .bss no molesta: sobran 200 KB de RAM. */
    static char pcDatos[ EE_TEST_LARGO ];

    for( uint32_t i = 0U; i < EE_TEST_LARGO; i++ )
    {
        pcDatos[ i ] = ( char ) ( ( ulAddr + i ) & 0xFFU );
    }

    xprintf( "%s: escribiendo %u bytes en %05lX...\r\n",
             pcNombre, ( unsigned ) EE_TEST_LARGO, ( unsigned long ) ulAddr );

    if( drv_eeprom_write( ulAddr, pcDatos, EE_TEST_LARGO ) != ( int32_t ) EE_TEST_LARGO )
    {
        xprintf( "%s: FALLO la escritura (HAL_I2C_ERROR = 0x%08lX)\r\n",
                 pcNombre, ( unsigned long ) drv_i2c_last_error() );
        return false;
    }

    memset( pcDatos, 0, sizeof( pcDatos ) );

    if( drv_eeprom_read( ulAddr, pcDatos, EE_TEST_LARGO ) != ( int32_t ) EE_TEST_LARGO )
    {
        xprintf( "%s: FALLO la lectura (HAL_I2C_ERROR = 0x%08lX)\r\n",
                 pcNombre, ( unsigned long ) drv_i2c_last_error() );
        return false;
    }

    for( uint32_t i = 0U; i < EE_TEST_LARGO; i++ )
    {
        uint8_t ucEsperado = ( uint8_t ) ( ( ulAddr + i ) & 0xFFU );

        if( ( uint8_t ) pcDatos[ i ] != ucEsperado )
        {
            xprintf( "%s: DIFIERE en %05lX: leido %02X, esperado %02X\r\n",
                     pcNombre, ( unsigned long ) ( ulAddr + i ),
                     ( unsigned ) ( uint8_t ) pcDatos[ i ], ( unsigned ) ucEsperado );
            return false;
        }
    }

    xprintf( "%s: OK, %u bytes verificados\r\n", pcNombre, ( unsigned ) EE_TEST_LARGO );
    return true;
}

static void cmdEe( void )
{
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    if( ( ucArgs == 0U ) || ( argv[ 1 ] == NULL ) )
    {
        prvEeUso();
        return;
    }

    if( strcmp( argv[ 1 ], "test" ) == 0 )
    {
        /* 0x00F60: arranca 160 bytes antes del borde de página de 0x01000, así
           que los 300 bytes cruzan DOS bordes de página. */
        bool bPag = prvEeProbarTramo( "pagina", 0x00F60UL );

        /* 0x0FFC0: los 300 bytes cruzan la frontera de los 64 KB, donde cambia
           la dirección de dispositivo. */
        bool bBlq = prvEeProbarTramo( "bloque", 0x0FFC0UL );

        xprintf( "\r\nresultado: %s\r\n",
                 ( bPag && bBlq ) ? "EEPROM VALIDADA" : "HAY UNA FALLA, ver arriba" );
        return;
    }

    if( ucArgs < 3U )
    {
        prvEeUso();
        return;
    }

    uint32_t ulAddr = prvHex( argv[ 2 ] );
    char     pcDatos[ EE_BUF + 1U ];

    if( strcmp( argv[ 1 ], "rd" ) == 0 )
    {
        uint32_t ulN = prvHex( argv[ 3 ] );

        if( ( ulN == 0UL ) || ( ulN > EE_BUF ) )
        {
            xprintf( "n debe estar entre 1 y %02X (en hexa)\r\n", ( unsigned ) EE_BUF );
            return;
        }

        int32_t lRet = drv_eeprom_read( ulAddr, pcDatos, ulN );

        if( lRet < 0 )
        {
            xprintf( "ERROR: fuera de rango, o HAL_I2C_ERROR = 0x%08lX\r\n",
                     ( unsigned long ) drv_i2c_last_error() );
            return;
        }

        xprintf( "%05lX:", ( unsigned long ) ulAddr );
        for( int32_t i = 0; i < lRet; i++ )
        {
            xprintf( " %02X", ( unsigned ) ( ( uint8_t ) pcDatos[ i ] ) );
        }
        xprintf( "  |" );
        for( int32_t i = 0; i < lRet; i++ )
        {
            char c = pcDatos[ i ];
            xputChar( ( ( c >= 0x20 ) && ( c < 0x7F ) ) ? c : '.' );
        }
        xprintf( "|\r\n" );
        return;
    }

    if( strcmp( argv[ 1 ], "wr" ) == 0 )
    {
        uint32_t ulN = ( uint32_t ) strlen( argv[ 3 ] );

        if( ulN > EE_BUF )
        {
            ulN = EE_BUF;
        }

        int32_t lRet = drv_eeprom_write( ulAddr, argv[ 3 ], ulN );

        if( lRet < 0 )
        {
            xprintf( "ERROR: fuera de rango, o HAL_I2C_ERROR = 0x%08lX\r\n",
                     ( unsigned long ) drv_i2c_last_error() );
            return;
        }

        xprintf( "escritos %ld bytes en %05lX\r\n",
                 ( long ) lRet, ( unsigned long ) ulAddr );
        return;
    }

    prvEeUso();
}
//------------------------------------------------------------------------------
/*
 * RTC externo MCP79410.
 *
 *   rtc                                    estado + fecha y hora
 *   rtc set <aa> <mm> <dd> <hh> <mi> <ss>  en DECIMAL
 *   rtc pwrfail                            marcas del corte
 *   rtc clear                              baja PWRFAIL y borra las marcas
 *   rtc invalid                            borra la firma de validez
 *
 * El dia de la semana NO se pide: lo calcula el driver de la fecha. Pedirlo era
 * pedir que alguien se equivocara, y se equivoco en la primera prueba.
 *
 * En decimal y no en hexa, a diferencia de 'ee' e 'i2c': una fecha la tipea una
 * persona, no sale de un mapa de registros.
 */
static const char *pcDiaSemana( uint8_t ucDia )
{
    static const char *pcNombres[ 8 ] = { "?", "dom", "lun", "mar", "mie", "jue", "vie", "sab" };

    return pcNombres[ ( ucDia <= 7U ) ? ucDia : 0U ];
}

static void prvRtcUso( void )
{
    xprintf( "uso (en DECIMAL):\r\n" );
    xprintf( "  rtc\r\n" );
    xprintf( "  rtc set <aa> <mm> <dd> <hh> <mi> <ss>   el dia de semana se calcula\r\n" );
    xprintf( "  rtc pwrfail\r\n" );
    xprintf( "  rtc clear     baja PWRFAIL y borra sus marcas\r\n" );
    xprintf( "  rtc invalid   borra la firma: simula un arranque en frio\r\n" );
}

static void prvRtcMostrarEstado( void )
{
    rtc_estado_t  xEstado;
    RtcTimeType_t xHora;

    if( drv_rtc_estado( &xEstado ) == false )
    {
        xprintf( "el RTC no contesta (HAL_I2C_ERROR = 0x%08lX)\r\n",
                 ( unsigned long ) drv_i2c_last_error() );
        return;
    }

    xprintf( "oscilador : %s\r\n",
             xEstado.bOscilando ? "corriendo" : "DETENIDO - la hora no avanza" );
    xprintf( "pila      : %s\r\n",
             xEstado.bPilaHab ? "respaldo habilitado (VBATEN)" : "VBATEN EN 0 - pierde la hora al cortar" );
    xprintf( "corte     : %s\r\n",
             xEstado.bFalloPower ? "HUBO uno - ver 'rtc pwrfail'" : "ninguno desde el ultimo clear" );

    if( drv_rtc_leer( &xHora ) == false )
    {
        xprintf( "fecha/hora: no se pudo leer\r\n" );
        return;
    }

    xprintf( "fecha/hora: 20%02u-%02u-%02u %02u:%02u:%02u (%s)\r\n",
             ( unsigned ) xHora.year,  ( unsigned ) xHora.month, ( unsigned ) xHora.day,
             ( unsigned ) xHora.hour,  ( unsigned ) xHora.min,   ( unsigned ) xHora.sec,
             pcDiaSemana( xHora.weekDay ) );

    /*
     * Va SIEMPRE y va pegado a la fecha, no en un comando aparte: una hora sin
     * su marca de validez al lado invita a creerle.
     */
    switch( drv_rtc_validez() )
    {
        case rtcHORA_VALIDA:
            xprintf( "validez   : CONFIABLE (firma en SRAM intacta)\r\n" );
            break;

        case rtcHORA_ARRANQUE_FRIO:
            xprintf( "validez   : NO CONFIABLE - arranque en frio.\r\n" );
            xprintf( "            Se perdio el respaldo: la fecha de arriba es basura.\r\n" );
            xprintf( "            Se arregla con 'rtc set'.\r\n" );
            break;

        default:
            xprintf( "validez   : no se pudo determinar (el RTC no contesta)\r\n" );
            break;
    }
}

static void cmdRtc( void )
{
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    if( ( ucArgs == 0U ) || ( argv[ 1 ] == NULL ) )
    {
        prvRtcMostrarEstado();
        return;
    }

    if( strcmp( argv[ 1 ], "set" ) == 0 )
    {
        if( ucArgs < 7U )
        {
            prvRtcUso();
            return;
        }

        RtcTimeType_t xHora;

        xHora.year    = ( uint8_t ) atoi( argv[ 2 ] );
        xHora.month   = ( uint8_t ) atoi( argv[ 3 ] );
        xHora.day     = ( uint8_t ) atoi( argv[ 4 ] );
        xHora.hour    = ( uint8_t ) atoi( argv[ 5 ] );
        xHora.min     = ( uint8_t ) atoi( argv[ 6 ] );
        xHora.sec     = ( uint8_t ) atoi( argv[ 7 ] );
        xHora.weekDay = 0U;   /* se ignora: lo calcula drv_rtc_escribir() */

        if( drv_rtc_escribir( &xHora ) == false )
        {
            xprintf( "ERROR: valores fuera de rango, o el RTC no contesta\r\n" );
            return;
        }

        xprintf( "hora fijada. Estado:\r\n" );
        prvRtcMostrarEstado();
        return;
    }

    if( strcmp( argv[ 1 ], "pwrfail" ) == 0 )
    {
        rtc_estado_t  xEstado;
        RtcTimeType_t xCaida, xVuelta;

        if( ( drv_rtc_estado( &xEstado ) == false ) ||
            ( drv_rtc_leer_falla_power( true,  &xCaida  ) == false ) ||
            ( drv_rtc_leer_falla_power( false, &xVuelta ) == false ) )
        {
            xprintf( "el RTC no contesta\r\n" );
            return;
        }

        if( xEstado.bFalloPower == false )
        {
            xprintf( "no hubo cortes desde el ultimo 'rtc clear'.\r\n" );
            xprintf( "(las marcas de abajo son viejas o no significan nada)\r\n" );
        }

        /* El chip no guarda ni segundos ni año en estos registros. */
        xprintf( "se cayo  : %02u-%02u %02u:%02u (%s)\r\n",
                 ( unsigned ) xCaida.month, ( unsigned ) xCaida.day,
                 ( unsigned ) xCaida.hour,  ( unsigned ) xCaida.min,
                 pcDiaSemana( xCaida.weekDay ) );
        xprintf( "volvio   : %02u-%02u %02u:%02u (%s)\r\n",
                 ( unsigned ) xVuelta.month, ( unsigned ) xVuelta.day,
                 ( unsigned ) xVuelta.hour,  ( unsigned ) xVuelta.min,
                 pcDiaSemana( xVuelta.weekDay ) );

        /*
         * Sin esta aclaración el comando engaña: uno corta la alimentación tres
         * veces, ve siempre la misma hora, y sale a buscar un bug que no existe.
         */
        if( xEstado.bFalloPower )
        {
            xprintf( "\r\nOJO: es el PRIMER corte desde el ultimo 'rtc clear'.\r\n" );
            xprintf( "El chip NO pisa estas marcas mientras PWRFAIL siga en 1.\r\n" );
        }
        return;
    }

    if( strcmp( argv[ 1 ], "clear" ) == 0 )
    {
        xprintf( "%s\r\n", drv_rtc_limpiar_falla_power()
                 ? "PWRFAIL bajado y marcas borradas"
                 : "ERROR: el RTC no contesta" );
        return;
    }

    if( strcmp( argv[ 1 ], "invalid" ) == 0 )
    {
        /* Para probar el mecanismo sin sacar la pila: deja al equipo igual que
           si hubiera arrancado en frio. La hora sigue corriendo; lo que cambia
           es que deja de ser creible, que es exactamente el caso a ejercitar. */
        if( drv_rtc_invalidar() == false )
        {
            xprintf( "ERROR: el RTC no contesta\r\n" );
            return;
        }

        xprintf( "firma borrada. Estado:\r\n" );
        prvRtcMostrarEstado();
        return;
    }

    prvRtcUso();
}
//------------------------------------------------------------------------------
/*
 * Bring-up del RS485.
 *
 *   rs485                          estado de los tres rieles
 *   rs485 on|off  bus|qmbus|cpres  prende y apaga cada riel
 *   rs485 tx <texto>               transmite (sin espacios) y escucha la respuesta
 *   rs485 rx <ms>                  escucha n ms y vuelca lo que llegue
 *
 * Sirve para medir con el tester riel por riel ANTES de que exista Modbus, y
 * para ver la trama en el osciloscopio sin depender de que un esclavo conteste.
 */
/* 256 = el máximo de una trama Modbus RTU, y el mismo tamaño que el buffer de RX
   del driver: así el comando nunca es el que trunca. Estático porque en el stack
   de tkCmd (512 palabras) no entra. */
#define RS485_BUF           256U
#define RS485_ESCUCHA_MS    500U

/*
 * Silencio que da por terminada la trama. El t3.5 de Modbus a 9600 son ~4 ms;
 * con el tick a 512 Hz la resolución es de 1,95 ms, así que 3 ticks (~6 ms) es
 * el valor más chico que se puede pedir con margen. Para el banco sobra.
 */
#define RS485_SILENCIO_MS    10U

static const char *pcNombreRiel( rs485_rail_t eRail )
{
    switch( eRail )
    {
        case rs485RAIL_BUS:   return "bus   (SP3485, PC6) ";
        case rs485RAIL_QMBUS: return "qmbus (caudal,  PC7) ";
        default:              return "cpres (presion, PB15)";
    }
}

static void prvRs485Uso( void )
{
    xprintf( "uso:\r\n" );
    xprintf( "  rs485\r\n" );
    xprintf( "  rs485 on|off  bus|qmbus|cpres\r\n" );
    xprintf( "  rs485 tx <texto>    transmite y escucha %u ms\r\n", ( unsigned ) RS485_ESCUCHA_MS );
    xprintf( "  rs485 rx <ms>       solo escucha\r\n" );
}

static void prvRs485Estado( void )
{
    for( uint32_t i = 0U; i < rs485RAIL_COUNT; i++ )
    {
        xprintf( "  %s : %s\r\n", pcNombreRiel( ( rs485_rail_t ) i ),
                 drv_rs485_power_estado( ( rs485_rail_t ) i ) ? "ENCENDIDO" : "apagado" );
    }

    xprintf( "  pwr locks   : 0x%08lX %s\r\n",
             ( unsigned long ) pwr_lock_estado(),
             pwr_deep_sleep_permitido() ? "(Stop 2 habilitado)" : "(solo Sleep)" );
}

/*
 * Escucha una TRAMA entera y la vuelca en hexa + ASCII.
 *
 * Antes usaba drv_rs485_read(), y salía con el primer byte de la respuesta: el
 * resto quedaba en el buffer y aparecía recién en la llamada siguiente. La culpa
 * era del comentario de drv_uart_read(), que prometía "bloqueante hasta juntar
 * xBytes" cuando en realidad el stream buffer vuelve con uno. Ahora se pide una
 * trama, que además es lo que va a necesitar Modbus.
 */
static int16_t prvRs485Escuchar( uint32_t ulMs )
{
    static char pcDatos[ RS485_BUF ];

    int16_t sRet = drv_rs485_read_frame( pcDatos, RS485_BUF,
                                         pdMS_TO_TICKS( ulMs ),
                                         pdMS_TO_TICKS( RS485_SILENCIO_MS ) );

    if( sRet < 0 )
    {
        xprintf( "ERROR: el bus esta apagado ('rs485 on bus')\r\n" );
        return sRet;
    }

    if( sRet == 0 )
    {
        xprintf( "silencio (%lu ms, nada recibido)\r\n", ( unsigned long ) ulMs );
        return 0;
    }

    xprintf( "recibidos %d:", ( int ) sRet );
    for( int16_t i = 0; i < sRet; i++ )
    {
        xprintf( " %02X", ( unsigned ) ( ( uint8_t ) pcDatos[ i ] ) );
    }
    xprintf( "  |" );
    for( int16_t i = 0; i < sRet; i++ )
    {
        char c = pcDatos[ i ];
        xputChar( ( ( c >= 0x20 ) && ( c < 0x7F ) ) ? c : '.' );
    }
    xprintf( "|\r\n" );

    return sRet;
}

static void cmdRs485( void )
{
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    if( ( ucArgs == 0U ) || ( argv[ 1 ] == NULL ) )
    {
        prvRs485Estado();
        return;
    }

    bool bOn  = ( strcmp( argv[ 1 ], "on"  ) == 0 );
    bool bOff = ( strcmp( argv[ 1 ], "off" ) == 0 );

    if( bOn || bOff )
    {
        if( ( ucArgs < 2U ) || ( argv[ 2 ] == NULL ) )
        {
            prvRs485Uso();
            return;
        }

        rs485_rail_t eRail;

        if     ( strcmp( argv[ 2 ], "bus"   ) == 0 ) { eRail = rs485RAIL_BUS;   }
        else if( strcmp( argv[ 2 ], "qmbus" ) == 0 ) { eRail = rs485RAIL_QMBUS; }
        else if( strcmp( argv[ 2 ], "cpres" ) == 0 ) { eRail = rs485RAIL_CPRES; }
        else { prvRs485Uso(); return; }

        drv_rs485_power( eRail, bOn );

        if( bOn )
        {
            /* Los módulos externos tardan mucho más, pero eso lo decide quien los
               polee: acá sólo se espera al transceiver. */
            vTaskDelay( pdMS_TO_TICKS( DRV_RS485_SETTLE_BUS_MS ) );
        }

        prvRs485Estado();
        return;
    }

    if( ( strcmp( argv[ 1 ], "tx" ) == 0 ) && ( ucArgs >= 2U ) )
    {
        uint16_t usLargo = ( uint16_t ) strlen( argv[ 2 ] );

        /* Se limpia ANTES de transmitir. En un bus half duplex el eco propio y
           cualquier basura previa quedarían mezclados con la respuesta. */
        drv_rs485_rx_flush();

        if( drv_rs485_write( argv[ 2 ], usLargo ) != ( int16_t ) usLargo )
        {
            xprintf( "ERROR: el bus esta apagado ('rs485 on bus')\r\n" );
            return;
        }

        xprintf( "transmitidos %u bytes, escuchando...\r\n", ( unsigned ) usLargo );
        ( void ) prvRs485Escuchar( RS485_ESCUCHA_MS );
        return;
    }

    if( ( strcmp( argv[ 1 ], "rx" ) == 0 ) && ( ucArgs >= 2U ) )
    {
        ( void ) prvRs485Escuchar( ( uint32_t ) atoi( argv[ 2 ] ) );
        return;
    }

    prvRs485Uso();
}
//------------------------------------------------------------------------------
static void prvInaUso( void )
{
    xprintf( "uso:\r\n" );
    xprintf( "  ina                 estado del chip y del riel de sensores\r\n" );
    xprintf( "  ina on|off          fuente lineal de los sensores (EN_PWR_SENS420)\r\n" );
    xprintf( "  ina read            ciclo completo de medida (~%u ms) en mA\r\n",
             ( unsigned ) ( DRV_INA_SETTLE_MS + DRV_INA_BARRIDO_MS ) );
    xprintf( "  ina raw             los 3 canales crudos: cuentas, uV y bus\r\n" );
    xprintf( "  ina wake|sleep      enciende / duerme el INA a mano\r\n" );
    xprintf( "  ina reg <rr>        lee el registro rr (hexa)\r\n" );
    xprintf( "  ina reg <rr> <vvvv> lo escribe\r\n" );
}

/*
 * Imprime un valor en mA con tres decimales.
 *
 * A mano y no con "%.03f" porque el proyecto linkea con --specs=nano.specs y SIN
 * '-u _printf_float': el printf de newlib-nano no trae el soporte de punto
 * flotante, así que un %f no imprime un número mal, no imprime NADA. El síntoma
 * —un campo vacío en medio de una línea que por lo demás sale bien— es de los que
 * hacen perder una tarde buscando el error en el driver.
 *
 * La cuenta en sí se hace en float, que para eso está la FPU; lo único que se
 * evita es el formateo.
 */
static void prvImprimirMa( float fMa )
{
    int32_t  lMicroA = ( int32_t ) ( fMa * 1000.0f );
    uint32_t ulAbs   = ( uint32_t ) ( ( lMicroA < 0 ) ? -lMicroA : lMicroA );

    xprintf( "%s%lu.%03lu mA",
             ( lMicroA < 0 ) ? "-" : "",
             ( unsigned long ) ( ulAbs / 1000UL ),
             ( unsigned long ) ( ulAbs % 1000UL ) );
}

static void prvInaEstado( void )
{
    uint16_t usMfid = 0U, usDieid = 0U, usConf = 0U;

    xprintf( "  chip        : %s\r\n",
             drv_ina_presente() ? "INA3221 identificado" : "NO CONTESTA" );

    if( drv_ina_reg_leer( DRV_INA_REG_MFID,  &usMfid  ) &&
        drv_ina_reg_leer( DRV_INA_REG_DIEID, &usDieid ) &&
        drv_ina_reg_leer( DRV_INA_REG_CONF,  &usConf  ) )
    {
        xprintf( "  MFID / DIEID: 0x%04X / 0x%04X  (esperados 0x%04X / 0x%04X)\r\n",
                 ( unsigned ) usMfid, ( unsigned ) usDieid,
                 ( unsigned ) DRV_INA_MFID_ESPERADO, ( unsigned ) DRV_INA_DIEID_ESPERADO );

        /* Los bits [2:0] de la configuración son el modo: 000 es power-down. */
        xprintf( "  config      : 0x%04X  (%s)\r\n", ( unsigned ) usConf,
                 ( ( usConf & 0x0007U ) == 0U ) ? "power-down, ~2 uA" : "midiendo, ~350 uA" );
    }

    xprintf( "  riel 4-20mA : %s\r\n",
             drv_ina_pwr_sensores_estado() ? "ENCENDIDO" : "apagado" );
    xprintf( "  shunt       : %u.%02u ohm\r\n",
             ( unsigned ) DRV_INA_RSHUNT_OHM,
             ( unsigned ) ( ( DRV_INA_RSHUNT_OHM - ( float ) ( unsigned ) DRV_INA_RSHUNT_OHM )
                            * 100.0f + 0.5f ) );
}

/*
 * Los tres canales crudos, sin ciclo de encendido: lo que digan los registros
 * AHORA. Sirve para ver si el chip está convirtiendo y para mirar el signo, que
 * es lo que delata un lazo abierto o un shunt al revés.
 */
static void prvInaCrudo( void )
{
    xprintf( "  canal   cuentas      shunt        corriente        bus\r\n" );

    for( uint32_t i = 0U; i < ( uint32_t ) inaCH_COUNT; i++ )
    {
        int16_t sRaw    = 0;
        int32_t lMicroV = 0;
        int32_t lMiliV  = 0;
        float   fMa     = 0.0f;

        if( ( drv_ina_shunt_raw( ( ina_canal_t ) i, &sRaw    ) == false ) ||
            ( drv_ina_shunt_uv ( ( ina_canal_t ) i, &lMicroV ) == false ) ||
            ( drv_ina_leer_ma  ( ( ina_canal_t ) i, &fMa     ) == false ) ||
            ( drv_ina_bus_mv   ( ( ina_canal_t ) i, &lMiliV  ) == false ) )
        {
            xprintf( "  CH%lu     ERROR de I2C\r\n", ( unsigned long ) ( i + 1U ) );
            continue;
        }

        xprintf( "  CH%lu     %6d   %8ld uV     ",
                 ( unsigned long ) ( i + 1U ), ( int ) sRaw, ( long ) lMicroV );
        prvImprimirMa( fMa );
        xprintf( "     %ld mV\r\n", ( long ) lMiliV );
    }
}

static void cmdIna( void )
{
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    if( ( ucArgs == 0U ) || ( argv[ 1 ] == NULL ) )
    {
        prvInaEstado();
        return;
    }

    if( strcmp( argv[ 1 ], "on" ) == 0 )
    {
        drv_ina_pwr_sensores( true );
        xprintf( "riel de sensores ENCENDIDO. Esperar %u ms antes de creerle a una medida.\r\n",
                 ( unsigned ) DRV_INA_SETTLE_MS );
        return;
    }

    if( strcmp( argv[ 1 ], "off" ) == 0 )
    {
        drv_ina_pwr_sensores( false );
        xprintf( "riel de sensores apagado\r\n" );
        return;
    }

    if( strcmp( argv[ 1 ], "wake" ) == 0 )
    {
        xprintf( "%s\r\n", drv_ina_awake() ? "INA midiendo (~350 uA)" : "ERROR de I2C" );
        return;
    }

    if( strcmp( argv[ 1 ], "sleep" ) == 0 )
    {
        xprintf( "%s\r\n", drv_ina_sleep() ? "INA en power-down (~2 uA)" : "ERROR de I2C" );
        return;
    }

    if( strcmp( argv[ 1 ], "raw" ) == 0 )
    {
        prvInaCrudo();
        return;
    }

    if( strcmp( argv[ 1 ], "read" ) == 0 )
    {
        float pfMa[ inaCH_COUNT ] = { 0.0f };

        xprintf( "midiendo: riel + %u ms de asentamiento + %u ms de barrido...\r\n",
                 ( unsigned ) DRV_INA_SETTLE_MS, ( unsigned ) DRV_INA_BARRIDO_MS );

        /* Se deja el riel encendido al salir: en banco lo normal es medir varias
           veces seguidas, y así la segunda no vuelve a pagar el asentamiento. Se
           apaga con 'ina off'. */
        bool bOk = drv_ina_medir( pfMa, true );

        for( uint32_t i = 0U; i < ( uint32_t ) inaCH_COUNT; i++ )
        {
            xprintf( "  CH%lu = ", ( unsigned long ) ( i + 1U ) );
            prvImprimirMa( pfMa[ i ] );
            xprintf( "\r\n" );
        }

        if( bOk == false )
        {
            xprintf( "  [!] la medida NO se completo (I2C o timeout de conversion)\r\n" );
        }

        xprintf( "  el riel quedo ENCENDIDO ('ina off' para apagarlo)\r\n" );
        return;
    }

    if( ( strcmp( argv[ 1 ], "reg" ) == 0 ) && ( ucArgs >= 2U ) )
    {
        uint8_t ucReg = ( uint8_t ) strtoul( argv[ 2 ], NULL, 16 );

        if( ( ucArgs >= 3U ) && ( argv[ 3 ] != NULL ) )
        {
            uint16_t usVal = ( uint16_t ) strtoul( argv[ 3 ], NULL, 16 );

            xprintf( "%s\r\n", drv_ina_reg_escribir( ucReg, usVal ) ?
                     "escrito" : "ERROR de I2C" );
            return;
        }

        uint16_t usVal = 0U;

        if( drv_ina_reg_leer( ucReg, &usVal ) )
        {
            xprintf( "reg 0x%02X = 0x%04X\r\n", ( unsigned ) ucReg, ( unsigned ) usVal );
        }
        else
        {
            xprintf( "ERROR de I2C\r\n" );
        }
        return;
    }

    prvInaUso();
}
//------------------------------------------------------------------------------
static void prvSdUso( void )
{
    xprintf( "uso:\r\n" );
    xprintf( "  sd                  estado: presencia, riel, tipo y capacidad\r\n" );
    xprintf( "  sd on|off           energia de la tarjeta (EN_PWR_SD, PB3)\r\n" );
    xprintf( "  sd init             prende e inicializa la tarjeta\r\n" );
    xprintf( "  sd info             CID y CSD crudos\r\n" );
    xprintf( "  sd read <sector>    vuelca un sector en hexa\r\n" );
    xprintf( "  sd test <sector>    escribe un patron y lo relee\r\n" );
    xprintf( "\r\n" );
    xprintf( "  ATENCION: 'sd test' PISA el sector que se le indique.\r\n" );
    xprintf( "  El 0 es el MBR: usar un sector alto en una tarjeta con datos.\r\n" );
}

/* Un sector no entra en el stack de tkCmd (2 KB), así que va estático. */
static uint8_t pucSector[ DRV_SD_SECTOR_BYTES ];

static void prvSdEstado( void )
{
    /* Con el riel apagado la detección no dice nada, y decir "vacia" sería
       inventar: el pin está en alta impedancia justamente para no gastar los
       82 µA del pull-up. Ver drv_sd.h. */
    xprintf( "  ranura      : %s\r\n",
             ( drv_sd_power_estado() == false ) ? "sin saber (riel apagado)" :
             ( drv_sd_presente() ? "TARJETA PRESENTE" : "vacia" ) );
    xprintf( "  riel        : %s\r\n",
             drv_sd_power_estado() ? "ENCENDIDO" : "apagado" );
    xprintf( "  tarjeta     : %s\r\n", drv_sd_tipo_texto() );

    if( drv_sd_tipo() != sdTIPO_NINGUNA )
    {
        uint32_t ulSectores = drv_sd_sectores();

        /* En MB para que el número sea legible; con 512 bytes por sector, cada
           2048 sectores es 1 MB. */
        xprintf( "  capacidad   : %lu sectores (%lu MB)\r\n",
                 ( unsigned long ) ulSectores,
                 ( unsigned long ) ( ulSectores / 2048UL ) );
    }

    xprintf( "  pwr locks   : 0x%08lX %s\r\n",
             ( unsigned long ) pwr_lock_estado(),
             pwr_deep_sleep_permitido() ? "(Stop 2 habilitado)" : "(solo Sleep)" );
}

static void prvSdVolcar( const uint8_t *pucDatos, uint32_t ulLargo )
{
    for( uint32_t i = 0U; i < ulLargo; i += 16U )
    {
        xprintf( "  %04lX: ", ( unsigned long ) i );

        for( uint32_t j = 0U; j < 16U; j++ )
        {
            xprintf( "%02X ", ( unsigned ) pucDatos[ i + j ] );
        }

        xprintf( " |" );

        for( uint32_t j = 0U; j < 16U; j++ )
        {
            char c = ( char ) pucDatos[ i + j ];
            xputChar( ( ( c >= 0x20 ) && ( c < 0x7F ) ) ? c : '.' );
        }

        xprintf( "|\r\n" );
    }
}

/*
 * Prende e inicializa. Se usa desde 'sd init' y desde los comandos que necesitan
 * la tarjeta lista: al cortarle la energía pierde todo su estado, así que esto
 * hay que rehacerlo en cada ciclo.
 */
static bool prvSdListo( void )
{
    if( drv_sd_tipo() != sdTIPO_NINGUNA )
    {
        return true;                    /* ya inicializada */
    }

    /* PRIMERO prender, DESPUÉS preguntar si hay tarjeta: con el riel apagado el
       pin de detección está en alta impedancia y no dice nada. Ver drv_sd.h. */
    if( drv_sd_power_estado() == false )
    {
        drv_sd_power( true );
    }

    if( drv_sd_presente() == false )
    {
        xprintf( "no hay tarjeta en la ranura\r\n" );
        drv_sd_power( false );
        return false;
    }

    if( drv_sd_arrancar() == false )
    {
        xprintf( "ERROR: la tarjeta no inicializo\r\n" );
        return false;
    }

    return true;
}

static void cmdSd( void )
{
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    if( ( ucArgs == 0U ) || ( argv[ 1 ] == NULL ) )
    {
        prvSdEstado();
        return;
    }

    if( strcmp( argv[ 1 ], "on" ) == 0 )
    {
        drv_sd_power( true );
        xprintf( "riel de la microSD ENCENDIDO (sin inicializar: 'sd init')\r\n" );
        return;
    }

    if( strcmp( argv[ 1 ], "off" ) == 0 )
    {
        drv_sd_power( false );
        xprintf( "riel de la microSD apagado\r\n" );
        return;
    }

    if( strcmp( argv[ 1 ], "init" ) == 0 )
    {
        if( prvSdListo() )
        {
            xprintf( "tarjeta inicializada\r\n" );
            prvSdEstado();
        }
        return;
    }

    if( strcmp( argv[ 1 ], "info" ) == 0 )
    {
        uint8_t pucReg[ 16 ];

        if( prvSdListo() == false )
        {
            return;
        }

        if( drv_sd_cid( pucReg ) )
        {
            xprintf( "CID:\r\n" );
            prvSdVolcar( pucReg, 16U );

            /* Los campos legibles del CID: el nombre del producto son 5
               caracteres ASCII, y sirven para saber que se está leyendo bien. */
            xprintf( "  fabricante 0x%02X, producto '%c%c%c%c%c'\r\n",
                     ( unsigned ) pucReg[ 0 ],
                     pucReg[ 3 ], pucReg[ 4 ], pucReg[ 5 ], pucReg[ 6 ], pucReg[ 7 ] );
        }
        else
        {
            xprintf( "ERROR leyendo el CID\r\n" );
        }

        if( drv_sd_csd( pucReg ) )
        {
            xprintf( "CSD (version %u):\r\n", ( unsigned ) ( pucReg[ 0 ] >> 6 ) + 1U );
            prvSdVolcar( pucReg, 16U );
        }
        else
        {
            xprintf( "ERROR leyendo el CSD\r\n" );
        }
        return;
    }

    if( ( strcmp( argv[ 1 ], "read" ) == 0 ) && ( ucArgs >= 2U ) )
    {
        uint32_t ulSector = ( uint32_t ) strtoul( argv[ 2 ], NULL, 0 );

        if( prvSdListo() == false )
        {
            return;
        }

        if( drv_sd_leer_sector( ulSector, pucSector ) == false )
        {
            xprintf( "ERROR leyendo el sector %lu\r\n", ( unsigned long ) ulSector );
            return;
        }

        xprintf( "sector %lu:\r\n", ( unsigned long ) ulSector );
        prvSdVolcar( pucSector, DRV_SD_SECTOR_BYTES );
        return;
    }

    if( ( strcmp( argv[ 1 ], "test" ) == 0 ) && ( ucArgs >= 2U ) )
    {
        uint32_t ulSector = ( uint32_t ) strtoul( argv[ 2 ], NULL, 0 );

        if( prvSdListo() == false )
        {
            return;
        }

        /*
         * El patrón es i*7+sector y no un valor fijo: así un sector que quedó de
         * una prueba anterior no se confunde con uno recién escrito, y si el
         * driver leyera un sector equivocado el contenido lo delata.
         */
        for( uint32_t i = 0U; i < DRV_SD_SECTOR_BYTES; i++ )
        {
            pucSector[ i ] = ( uint8_t ) ( ( i * 7U ) + ulSector );
        }

        xprintf( "escribiendo el sector %lu...\r\n", ( unsigned long ) ulSector );

        if( drv_sd_escribir_sector( ulSector, pucSector ) == false )
        {
            xprintf( "ERROR: la escritura fallo\r\n" );
            return;
        }

        /* Se borra el buffer antes de releer: si no, una lectura que no hiciera
           nada dejaría los datos viejos en RAM y el test pasaría igual. Ese
           falso positivo es el que hay que evitar. */
        memset( pucSector, 0, DRV_SD_SECTOR_BYTES );

        if( drv_sd_leer_sector( ulSector, pucSector ) == false )
        {
            xprintf( "ERROR: la relectura fallo\r\n" );
            return;
        }

        for( uint32_t i = 0U; i < DRV_SD_SECTOR_BYTES; i++ )
        {
            if( pucSector[ i ] != ( uint8_t ) ( ( i * 7U ) + ulSector ) )
            {
                xprintf( "ERROR en el byte %lu: esperaba 0x%02X, leyo 0x%02X\r\n",
                         ( unsigned long ) i,
                         ( unsigned ) ( uint8_t ) ( ( i * 7U ) + ulSector ),
                         ( unsigned ) pucSector[ i ] );
                return;
            }
        }

        xprintf( "sector %lu: escritura y relectura OK, los 512 bytes\r\n",
                 ( unsigned long ) ulSector );
        return;
    }

    prvSdUso();
}
//------------------------------------------------------------------------------
static void prvVinUso( void )
{
    xprintf( "uso:\r\n" );
    xprintf( "  vin              mide los dos rieles\r\n" );
    xprintf( "  vin raw          cuentas crudas del ADC, sin convertir\r\n" );
    xprintf( "  vin on|off       load switch del divisor de 12 V (EN_SENS12V)\r\n" );
    xprintf( "  vin 3v3 on|off   load switch del circuito de 3,3 V\r\n" );
    xprintf( "\r\n" );
    xprintf( "  El circuito de 3,3 V NO se usa: un ADC referenciado al propio\r\n" );
    xprintf( "  riel da 2047 siempre. El riel sale de VREFINT, sin hardware.\r\n" );
}

/* Imprime milivolts como V con tres decimales. A mano y no con %f: ver la nota
   en prvImprimirMa(). */
static void prvImprimirVolts( uint32_t ulMiliV )
{
    xprintf( "%lu.%03lu V",
             ( unsigned long ) ( ulMiliV / 1000UL ),
             ( unsigned long ) ( ulMiliV % 1000UL ) );
}

static void cmdVin( void )
{
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    if( ( ucArgs >= 1U ) && ( argv[ 1 ] != NULL ) )
    {
        if( strcmp( argv[ 1 ], "on" ) == 0 )
        {
            drv_adc_pwr_12v( true );
            xprintf( "divisor de 12 V conectado (consume %lu uA mientras este asi)\r\n",
                     ( unsigned long ) ( 12000UL / 66UL ) );
            return;
        }

        if( strcmp( argv[ 1 ], "off" ) == 0 )
        {
            drv_adc_pwr_12v( false );
            xprintf( "divisor de 12 V desconectado\r\n" );
            return;
        }

        if( ( strcmp( argv[ 1 ], "3v3" ) == 0 ) && ( ucArgs >= 2U ) )
        {
            bool bOn = ( strcmp( argv[ 2 ], "on" ) == 0 );

            drv_adc_pwr_3v3( bOn );
            xprintf( "circuito de 3,3 V %s (no se usa para medir)\r\n",
                     bOn ? "CONECTADO" : "desconectado" );
            return;
        }

        if( strcmp( argv[ 1 ], "raw" ) == 0 )
        {
            uint16_t usVref = 0U;
            uint16_t us12   = 0U;

            /* Se prende el riel para que la cuenta del divisor signifique algo:
               con el load switch abierto la entrada del seguidor queda al aire. */
            bool bYaEstaba = drv_adc_pwr_12v_estado();

            if( bYaEstaba == false )
            {
                drv_adc_pwr_12v( true );
                vTaskDelay( pdMS_TO_TICKS( DRV_ADC_SETTLE_MS ) );
            }

            bool bOk = drv_adc_raw_vrefint( &usVref ) && drv_adc_raw_12v( &us12 );

            if( bYaEstaba == false )
            {
                drv_adc_pwr_12v( false );
            }

            if( bOk == false )
            {
                xprintf( "ERROR: la conversion fallo\r\n" );
                return;
            }

            xprintf( "  VREFINT : %5u cuentas\r\n", ( unsigned ) usVref );
            xprintf( "  IN15    : %5u cuentas  (12 V, divisor 56K/10K)\r\n",
                     ( unsigned ) us12 );
            return;
        }

        prvVinUso();
        return;
    }

    /* ---- 'vin' pelado: la medida ---- */
    uint32_t ulVdda = 0UL;
    uint32_t ulV12  = 0UL;

    if( drv_adc_vdda_mv( &ulVdda ) )
    {
        xprintf( "  VDDA / 3V3 : " );
        prvImprimirVolts( ulVdda );
        xprintf( "   (por VREFINT, sin hardware externo)\r\n" );
    }
    else
    {
        xprintf( "  VDDA / 3V3 : ERROR de conversion\r\n" );
    }

    if( drv_adc_v12_mv( &ulV12, false ) )
    {
        xprintf( "  riel 12 V  : " );
        prvImprimirVolts( ulV12 );
        xprintf( "\r\n" );
    }
    else
    {
        xprintf( "  riel 12 V  : ERROR de conversion\r\n" );
    }

    xprintf( "  switches   : 12V %s / 3V3 %s\r\n",
             drv_adc_pwr_12v_estado() ? "ON" : "off",
             drv_adc_pwr_3v3_estado() ? "ON" : "off" );
}
//------------------------------------------------------------------------------
static void prvCntUso( void )
{
    xprintf( "uso:\r\n" );
    xprintf( "  cnt              cuenta acumulada y estado del pin\r\n" );
    xprintf( "  cnt watch [seg]  cuenta durante N segundos (10 por omision)\r\n" );
    xprintf( "  cnt tomar        devuelve los pulsos pendientes y los descuenta\r\n" );
    xprintf( "  cnt reset        pone los dos contadores en cero\r\n" );
    xprintf( "\r\n" );
    xprintf( "  El pulso se cuenta en el flanco de BAJADA, que es el cierre del\r\n" );
    xprintf( "  contacto. En reposo el contacto esta abierto y el pin en alto.\r\n" );
    xprintf( "  El antirrebote es de hardware (RC de 4K7/1uF + Schmitt): admite\r\n" );
    xprintf( "  hasta unos 30 Hz y filtra todo lo que dure menos de ~5 ms.\r\n" );
}

static void cmdCnt( void )
{
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    if( ( ucArgs >= 1U ) && ( argv[ 1 ] != NULL ) )
    {
        if( strcmp( argv[ 1 ], "reset" ) == 0 )
        {
            drv_pulsos_reset();
            xprintf( "contadores en cero\r\n" );
            return;
        }

        if( strcmp( argv[ 1 ], "tomar" ) == 0 )
        {
            xprintf( "tomados %lu pulsos (quedan 0 pendientes)\r\n",
                     ( unsigned long ) drv_pulsos_tomar() );
            return;
        }

        if( strcmp( argv[ 1 ], "watch" ) == 0 )
        {
            uint32_t ulSeg = 10UL;

            if( ( ucArgs >= 2U ) && ( argv[ 2 ] != NULL ) )
            {
                ulSeg = ( uint32_t ) atoi( argv[ 2 ] );
            }

            if( ( ulSeg == 0UL ) || ( ulSeg > 600UL ) )
            {
                xprintf( "ERROR: la ventana va de 1 a 600 segundos\r\n" );
                return;
            }

            uint32_t ulIni = drv_pulsos_total();

            xprintf( "contando %lu s...\r\n", ( unsigned long ) ulSeg );
            vTaskDelay( pdMS_TO_TICKS( ulSeg * 1000UL ) );

            uint32_t ulN = drv_pulsos_total() - ulIni;

            /* La frecuencia en mHz, con enteros: nada de %f para un comando de
               diagnóstico. Ver la nota en prvImprimirMa(). */
            uint32_t ulMiliHz = ( ulN * 1000UL ) / ulSeg;

            xprintf( "  %lu pulsos en %lu s  ->  %lu.%03lu Hz\r\n",
                     ( unsigned long ) ulN, ( unsigned long ) ulSeg,
                     ( unsigned long ) ( ulMiliHz / 1000UL ),
                     ( unsigned long ) ( ulMiliHz % 1000UL ) );
            return;
        }

        prvCntUso();
        return;
    }

    /* ---- 'cnt' pelado ---- */
    drv_pulsos_cfg_t xCfg = { 0 };

    drv_pulsos_config( &xCfg );

    xprintf( "  total      : %lu pulsos desde el arranque\r\n",
             ( unsigned long ) drv_pulsos_total() );
    xprintf( "  pendientes : %lu (los que se llevaria 'cnt tomar')\r\n",
             ( unsigned long ) drv_pulsos_pendientes() );
    xprintf( "  pin PA12   : %s  ->  contacto %s\r\n",
             drv_pulsos_nivel_pin() ? "alto" : "BAJO",
             drv_pulsos_nivel_pin() ? "abierto (reposo)" : "CERRADO" );

    /* El pull tiene que decir 'flotante'. Un pull-down acá cuesta 82 uA las 24
       horas, y es lo que una regeneración de CubeMX podría meter sin avisar. */
    xprintf( "  config     : modo %lu (0=entrada), pull %lu (%s)\r\n",
             ( unsigned long ) xCfg.ulModer, ( unsigned long ) xCfg.ulPupdr,
             ( xCfg.ulPupdr == 0UL ) ? "flotante, CORRECTO" : "OJO: NO deberia tener pull" );
}
//------------------------------------------------------------------------------
/*
 * Muestra el código CRUDO de cada tecla, sin pasar por el parser.
 *
 * Existe porque "la flecha arriba no hace nada" tiene dos causas que desde este
 * lado se ven iguales: que el historial esté vacío, o que la terminal no mande
 * nada al apretar la flecha —muchos monitores serie simples no mandan las teclas
 * de cursor—. Con esto se ve exactamente qué llega, si es que llega algo.
 *
 * Lo que TIENE que aparecer al apretar flecha arriba son tres bytes:
 *
 *     1B 5B 41    ESC [ A    modo normal (CSI)
 *     1B 4F 41    ESC O A    modo application cursor keys (SS3)
 *
 * Los dos los entiende el parser. Si no aparece ninguno, el problema es la
 * terminal y no el firmware.
 */
#define KEYS_CAPTURA_S      8U
#define KEYS_MAX_BYTES      48U

static void cmdKeys( void )
{
    /*
     * CAPTURA EN SILENCIO Y RECIÉN DESPUÉS IMPRIME. No es un detalle de estilo.
     *
     * La primera versión imprimía cada byte antes de leer el siguiente, y a 9600
     * imprimir " 1B" son 3,1 ms — justo el tiempo en que llegan los otros dos
     * bytes de la flecha. O sea que el instrumento podía estar causando el
     * fenómeno que venía a medir. Capturando primero, la única forma de que
     * falten bytes es que no hayan llegado, o que el driver los pierda; y de eso
     * se encarga el contador de errores de abajo.
     */
    static uint8_t pucCapturado[ KEYS_MAX_BYTES ];
    uint32_t       ulN = 0U;

    drv_uart_errores_reset( drvUART_TERM );

    xprintf( "apreta la tecla que quieras probar TRES veces y espera %u s.\r\n",
             ( unsigned ) KEYS_CAPTURA_S );
    xprintf( "no voy a mostrar nada hasta el final: si imprimo mientras escucho,\r\n" );
    xprintf( "el propio mensaje tapa los bytes que estoy tratando de ver.\r\n\r\n" );

    /* Que salga TODO lo de arriba antes de empezar a escuchar: si no, la cola de
       transmisión sigue saliendo mientras capturamos y volvemos al mismo problema. */
    vTaskDelay( pdMS_TO_TICKS( 500 ) );
    drv_uart_rx_flush( drvUART_TERM );

    TickType_t xEspera = pdMS_TO_TICKS( 100 );
    ( void ) frtos_ioctl( fdTERM, ioctl_SET_TIMEOUT, &xEspera );

    uint32_t ulVueltas = ( KEYS_CAPTURA_S * 1000U ) / 100U;
    char     cTecla;

    while( ( ulVueltas-- > 0U ) && ( ulN < KEYS_MAX_BYTES ) )
    {
        if( frtos_read( fdTERM, &cTecla, 1U ) == 1 )
        {
            pucCapturado[ ulN++ ] = ( uint8_t ) cTecla;
        }
    }

    xEspera = portMAX_DELAY;
    ( void ) frtos_ioctl( fdTERM, ioctl_SET_TIMEOUT, &xEspera );

    /* ---- Recién ahora, el informe ---- */
    xprintf( "capturados %lu bytes:", ( unsigned long ) ulN );

    for( uint32_t i = 0U; i < ulN; i++ )
    {
        xprintf( " %02X", ( unsigned ) pucCapturado[ i ] );
    }
    xprintf( "\r\n" );

    uint32_t ulErr   = drv_uart_errores( drvUART_TERM );
    uint32_t ulIsr   = drv_uart_ultimo_isr( drvUART_TERM );

    xprintf( "errores de UART : %lu\r\n", ( unsigned long ) ulErr );

    if( ulErr > 0U )
    {
        /* El ISR crudo del periférico. ORE es el que importa: dice que llegó un
           byte antes de que se levantara el anterior, o sea que SE PERDIO. */
        xprintf( "ISR acumulado   : 0x%08lX%s%s%s%s\r\n", ( unsigned long ) ulIsr,
                 ( ulIsr & USART_ISR_ORE ) ? "  ORE(se perdieron bytes)" : "",
                 ( ulIsr & USART_ISR_FE  ) ? "  FE(trama)"  : "",
                 ( ulIsr & USART_ISR_NE  ) ? "  NE(ruido)"  : "",
                 ( ulIsr & USART_ISR_PE  ) ? "  PE(paridad)": "" );
        xprintf( "ErrorCode HAL   : 0x%08lX\r\n",
                 ( unsigned long ) drv_uart_ultimo_error( drvUART_TERM ) );
    }
}
//------------------------------------------------------------------------------
static void cmdReset( void )
{
    xprintf( "reiniciando por NVIC_SystemReset (pulsa NRST)...\r\n" );
    vTaskDelay( pdMS_TO_TICKS( 125 ) );   /* que salga el mensaje antes del reset */
    NVIC_SystemReset();
}
//------------------------------------------------------------------------------
/*
 * Reinicio TIBIO: vuelve al vector de reset SIN pasar por NRST.
 *
 * Es un instrumento de diagnóstico, no un reset de verdad. En el STM32L496 no
 * se puede evitar que un reset interno tire NRST a masa (el option byte
 * NRST_MODE existe en G0/G4/L5/U5, no en esta familia), así que esta es la única
 * forma de reiniciar el firmware dejando la línea de NRST quieta.
 *
 * PARA QUÉ: el 2026-08-11 cada "reset" deja la placa muerta hasta que se le corta
 * la alimentación, y al volver informa BOR/POR — o sea que el riel se cae. Este
 * comando separa las dos causas posibles de una vez:
 *
 *   reboot anda y reset mata la placa -> es la LÍNEA DE NRST. Algo colgado de
 *                                        ella (enable del regulador, supervisor)
 *                                        apaga la fuente. Es hardware.
 *   los dos matan la placa            -> no es NRST; el transitorio lo genera
 *                                        otra cosa.
 *
 * ⚠ NO es equivalente a un reset: los periféricos NO se reinicializan, así que
 * quedan configurados de antes y los MX_*_Init() los van a reprogramar en
 * caliente. Sirve para esta prueba; no es un mecanismo para dejar en producción.
 */
static void cmdReboot( void )
{
    xprintf( "reiniciando tibio, sin tocar NRST...\r\n" );
    vTaskDelay( pdMS_TO_TICKS( 125 ) );

    __disable_irq();

    /* Callar todo lo que pueda interrumpir en medio del salto. */
    for( uint32_t i = 0U; i < 8U; i++ )
    {
        NVIC->ICER[ i ] = 0xFFFFFFFFUL;
        NVIC->ICPR[ i ] = 0xFFFFFFFFUL;
    }
    SysTick->CTRL = 0UL;

    uint32_t ulSP = *( volatile uint32_t * )   FLASH_BASE;
    uint32_t ulPC = *( volatile uint32_t * ) ( FLASH_BASE + 4UL );

    /* El scheduler dejó al micro corriendo sobre el PSP; hay que volver al MSP
       ANTES de reemplazar el stack pointer. */
    __set_CONTROL( 0UL );
    __ISB();
    __set_MSP( ulSP );
    __DSB();
    __ISB();

    ( ( void ( * )( void ) ) ulPC )();
}
//------------------------------------------------------------------------------

#endif  /* TKCMD_MODO_BANCO */
