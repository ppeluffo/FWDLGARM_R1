/*
 * tkCmd.c  -  ver tkCmd.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tkCmd.h"
#include "drv_uart.h"
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
 * Además de esto, el tickless está anulado en FreeRTOSConfig.h (bloque USER CODE
 * Defines) y el EXTI de TERM_SENSE se apaga más abajo. El micro NO duerme.
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
 * ETAPA 2 - RX. ES LO QUE ESTÁ CORRIENDO AHORA.
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
 * PARA VOLVER A LA CONSOLA: poner TKCMD_MODO_BANCO en 0. Nada de lo de abajo se
 * borró, sólo queda compilado fuera.
 *============================================================================*/
#define TKCMD_MODO_BANCO        1

/* 1 = eco leyendo el RDR por poleo (HAL cruda, sin ISR).
   0 = eco por el camino real: frtos_read() bloqueando en el stream buffer.
   Empezar en 1: prueba menos cosas a la vez. */
#define TKCMD_BANCO_RX_CRUDO    1

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
     *   [B]  -> pasó el HAL_NVIC_DisableIRQ.
     *   [C]  -> frtos_open_all() volvió bien: semáforos y stream buffer creados.
     *   [D]  -> el Receive_IT quedó abortado.
     */
    prvTx( "\r\n\r\n[A] tkCmd arranco\r\n" );

    /* TERM_SENSE afuera. CubeMX habilita EXTI9_5 en MX_GPIO_Init() pase lo que
       pase, así que no alcanza con no llamar a drv_term_sense_init(): hay que
       apagar la línea en el NVIC. */
    HAL_NVIC_DisableIRQ( TERM_SENSE_EXTI_IRQn );
    prvTx( "[B] EXTI de TERM_SENSE apagado\r\n" );

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
static void cmdReset( void );

//------------------------------------------------------------------------------
void tkCmd( void *pvParameters )
{
    ( void ) pvParameters;

    char cChar;

    /* Los drivers se abren desde acá y no desde main(): crear semáforos y stream
       buffers necesita el scheduler corriendo. */
    if( frtos_open_all() == false )
    {
        Error_Handler();
    }

    drv_term_sense_init();

    FRTOS_CMD_init();
    FRTOS_CMD_register( "help",   cmdHelp   );
    FRTOS_CMD_register( "status", cmdStatus );
    FRTOS_CMD_register( "reset",  cmdReset  );

    xprintf( "\r\n\r\nFWDLGARM_R1 - consola TERM\r\n" );
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

static void cmdHelp( void )
{
    xprintf( "Comandos:\r\n" );
    xprintf( "  help    - esta ayuda\r\n" );
    xprintf( "  status  - estado del sistema\r\n" );
    xprintf( "  reset   - reinicia el micro\r\n" );
}
//------------------------------------------------------------------------------
static void cmdStatus( void )
{
    xprintf( "tick        : %lu (%lu Hz)\r\n",
             ( unsigned long ) xTaskGetTickCount(),
             ( unsigned long ) configTICK_RATE_HZ );
    xprintf( "clock        : %lu Hz\r\n", ( unsigned long ) SystemCoreClock );
    xprintf( "terminal     : %s\r\n", drv_term_sense_presente() ? "conectada" : "ausente" );
    xprintf( "pwr locks    : 0x%08lX %s\r\n",
             ( unsigned long ) pwr_lock_estado(),
             pwr_deep_sleep_permitido() ? "(Stop 2 habilitado)" : "(solo Sleep)" );
    xprintf( "heap libre   : %u bytes\r\n", ( unsigned ) xPortGetFreeHeapSize() );
    xprintf( "stack tkCmd  : %u palabras libres\r\n",
             ( unsigned ) uxTaskGetStackHighWaterMark( NULL ) );
}
//------------------------------------------------------------------------------
static void cmdReset( void )
{
    xprintf( "reiniciando...\r\n" );
    vTaskDelay( pdMS_TO_TICKS( 125 ) );   /* que salga el mensaje antes del reset */
    NVIC_SystemReset();
}
//------------------------------------------------------------------------------

#endif  /* TKCMD_MODO_BANCO */
