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
 * QUÉ MANDA, una vez por segundo, TRES líneas, cada una agregando UNA capa:
 *
 *   --- 1   literal const + HAL_UART_Transmit por poleo. Ni newlib, ni ISR, ni
 *           semáforos. Prueba pines, AF7, baudios, reloj del USART, transceiver.
 *   --- 2   lo mismo, pero armando la línea con snprintf. Agrega newlib y su
 *           apetito de stack.
 *   --- 3   xprintf -> frtos_write -> drv_uart_write -> Transmit_IT + semáforo.
 *           Agrega el camino por interrupción y el TxCpltCallback.
 *
 * CÓMO SE LEE EL RESULTADO — esto es todo el punto del ejercicio:
 *
 *   ninguna       -> el problema está DEBAJO del firmware: pines, AF, baudios,
 *                    transceiver, cable, o la config de la terminal en la PC.
 *   sólo 1        -> el hardware anda y newlib se lleva puesto el stack de tkCmd
 *                    (2 KB). Subir tkCmd_STACK_SIZE y volver a probar.
 *   1 y 2, no 3   -> el camino por interrupción: NVIC, TxCpltCallback que no da
 *                    el semáforo, o Transmit_IT devolviendo distinto de HAL_OK.
 *   las tres      -> TX cerrado. Se pasa a probar RX.
 *
 * PARA VOLVER A LA CONSOLA: poner TKCMD_MODO_BANCO en 0. Nada de lo de abajo se
 * borró, sólo queda compilado fuera.
 *============================================================================*/
#define TKCMD_MODO_BANCO        1

/* 1 = eco leyendo el RDR por poleo (HAL cruda, sin ISR).
   0 = eco por el camino real: frtos_read() bloqueando en el stream buffer.
   Empezar en 1: prueba menos cosas a la vez. */
#define TKCMD_BANCO_RX_CRUDO    1

#if ( TKCMD_MODO_BANCO == 1 )

extern UART_HandleTypeDef huart1;

#define BANCO_PERIODO_MS        1000U
#define BANCO_PASO_MS             10U   /* cada cuánto se mira el RX */

void tkCmd( void *pvParameters )
{
    ( void ) pvParameters;

    char     cLinea[ 96 ];
    uint32_t ulCiclo = 0U;

    /* TERM_SENSE afuera. CubeMX habilita EXTI9_5 en MX_GPIO_Init() pase lo que
       pase, así que no alcanza con no llamar a drv_term_sense_init(): hay que
       apagar la línea en el NVIC. */
    HAL_NVIC_DisableIRQ( TERM_SENSE_EXTI_IRQn );

    /* Hace falta igual para la capa 2 (xprintf): crea los semáforos y el mutex
       del driver. Si esto fallara, drv_uart_write() haría xSemaphoreTake(NULL)
       y el configASSERT congelaría todo, incluido el LED. */
    if( frtos_open_all() == false )
    {
        Error_Handler();
    }

#if ( TKCMD_BANCO_RX_CRUDO == 1 )
    /* frtos_open_all() dejó armado un Receive_IT que se comería los bytes antes
       de que el poleo los vea. Se aborta. */
    drv_uart_rx_disable( drvUART_TERM );
#else
    /* frtos_read() no lleva timeout por argumento: se fija por ioctl. Sin esto
       el default es portMAX_DELAY y la línea de TX del segundo no saldría más. */
    TickType_t xTimeout = pdMS_TO_TICKS( BANCO_PASO_MS );
    ( void ) frtos_ioctl( fdTERM, ioctl_SET_TIMEOUT, &xTimeout );
#endif

    for( ;; )
    {
        ulCiclo++;

        /* ---- capa 1: lo más crudo que hay ---------------------------------- */
        /* Literal constante + HAL_UART_Transmit por poleo. Ni newlib, ni ISR, ni
           semáforos, ni stack de stdio. Si esto no sale, no sale nada. */
        static const char cFijo[] = "\r\n--- 1 literal por HAL_UART_Transmit\r\n";
        ( void ) HAL_UART_Transmit( &huart1, ( uint8_t * ) cFijo,
                                    ( uint16_t ) ( sizeof( cFijo ) - 1U ), 500U );

        /* ---- capa 2: igual, pero pasando por newlib ------------------------ */
        /* Agrega una sola variable: snprintf. Es el sospechoso de stack, porque
           configCHECK_FOR_STACK_OVERFLOW está en 0 y un desborde de tkCmd sería
           mudo (el LED de tkCtl seguiría destellando igual). */
        int iLen = snprintf( cLinea, sizeof( cLinea ),
                             "--- 2 [%lu] por snprintf + HAL_UART_Transmit\r\n",
                             ( unsigned long ) ulCiclo );

        ( void ) HAL_UART_Transmit( &huart1, ( uint8_t * ) cLinea,
                                    ( uint16_t ) iLen, 500U );

        /* ---- capa 3: la pila entera ---------------------------------------- */
        /* xprintf -> frtos_write -> drv_uart_write -> Transmit_IT + semáforo. */
        ( void ) xprintf( "--- 3 [%lu] por xprintf/FRTOS-IO (interrupcion)\r\n",
                          ( unsigned long ) ulCiclo );

        /* ---- espera de 1 s, mirando el RX de a poco ------------------------ */
        for( uint32_t i = 0U; i < ( BANCO_PERIODO_MS / BANCO_PASO_MS ); i++ )
        {
#if ( TKCMD_BANCO_RX_CRUDO == 1 )
            /*
             * Leer el RDR desde una tarea viola el principio HAL del proyecto
             * (sólo el driver toca registros). Es a propósito y es temporal:
             * la gracia de esta prueba es justamente saltear el driver.
             */
            if( __HAL_UART_GET_FLAG( &huart1, UART_FLAG_RXNE ) != RESET )
            {
                uint8_t ucRx = ( uint8_t ) ( huart1.Instance->RDR & 0xFFU );
                ( void ) HAL_UART_Transmit( &huart1, &ucRx, 1U, 100U );
            }
#else
            char cRx;
            if( frtos_read( fdTERM, &cRx, 1U ) == 1 )
            {
                ( void ) xprintf( "%c", cRx );
            }
            continue;   /* frtos_read ya bloqueó los BANCO_PASO_MS del paso */
#endif
            vTaskDelay( pdMS_TO_TICKS( BANCO_PASO_MS ) );
        }
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
