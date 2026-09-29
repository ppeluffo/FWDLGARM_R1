/*
 * tkCmd.c  -  ver tkCmd.h
 */

#include <stdio.h>
#include <string.h>

#include "tkCmd.h"
#include "tkCtl.h"
#include "frtos-io.h"
#include "frtos_cmd.h"
#include "drv_uart.h"
#include "drv_term_sense.h"
#include "pwr_lock.h"
#include "main.h"

/* Memoria estática: la tarea no toca el heap. */
StaticTask_t tkCmd_TCB;
StackType_t  tkCmd_Stack[ tkCmd_STACK_SIZE ];

/*------------------------------------------------------------------------------
 * Los comandos
 *----------------------------------------------------------------------------*/

static void cmdHelp( void )
{
    xprintf( "\r\nComandos disponibles:\r\n" );
    xprintf( "  help            esta ayuda\r\n" );
    xprintf( "  status          version, reset, candados, stacks\r\n" );
    xprintf( "  sense           TERM_SENSE: nivel del pin y configuracion\r\n" );
    xprintf( "  reset           reinicia el equipo\r\n" );
    xprintf( "\r\n" );
    xprintf( "  el comando va COMPLETO: 'status', no 'st'\r\n" );
}

/*
 * Traduce las banderas de reset de RCC_CSR.
 *
 * ⚠ Son ACUMULATIVAS hasta que alguien las limpia con RMVF, así que ver varias
 * juntas es normal y no indica una falla. Se limpian al final justo por eso: si
 * no, en campo se vería para siempre el primer reset de la vida del equipo.
 */
static void prvImprimirCausaReset( void )
{
    uint32_t ulCsr = RCC->CSR;

    xprintf( "reset por    :" );

    if( ( ulCsr & RCC_CSR_LPWRRSTF  ) != 0U ) { xprintf( " LOW-POWER" ); }
    if( ( ulCsr & RCC_CSR_WWDGRSTF  ) != 0U ) { xprintf( " WWDG" );      }
    if( ( ulCsr & RCC_CSR_IWDGRSTF  ) != 0U ) { xprintf( " IWDG" );      }
    if( ( ulCsr & RCC_CSR_SFTRSTF   ) != 0U ) { xprintf( " SOFT" );      }
    if( ( ulCsr & RCC_CSR_BORRSTF   ) != 0U ) { xprintf( " BOR/POR" );   }
    if( ( ulCsr & RCC_CSR_PINRSTF   ) != 0U ) { xprintf( " PIN" );       }
    if( ( ulCsr & RCC_CSR_OBLRSTF   ) != 0U ) { xprintf( " OPTION-BYTE" ); }
    if( ( ulCsr & RCC_CSR_FWRSTF    ) != 0U ) { xprintf( " FIREWALL" );  }

    xprintf( "\r\n" );

    __HAL_RCC_CLEAR_RESET_FLAGS();
}

static void cmdStatus( void )
{
    xprintf( "\r\n%s %s\r\n", FW_NOMBRE, FW_VERSION );
    xprintf( "compilado %s\r\n", FW_FECHA );
    prvImprimirCausaReset();

    /*
     * Los candados de energía son lo primero que hay que mirar cuando el consumo
     * no cierra: mientras haya UNO tomado, vPortSuppressTicksAndSleep() vuelve
     * enseguida y el equipo NO baja a Stop 2. Un candado olvidado por un driver
     * se ve acá directamente, en vez de haber que deducirlo del amperimetro.
     */
    uint32_t ulLocks = pwr_lock_estado();

    xprintf( "candados     : 0x%02lX", ( unsigned long ) ulLocks );

    if( ulLocks == 0U )
    {
        xprintf( "  (ninguno: duerme en Stop 2)" );
    }
    else
    {
        if( ( ulLocks & ( 1UL << pwrLOCK_TERM    ) ) != 0U ) { xprintf( " TERM" );    }
        if( ( ulLocks & ( 1UL << pwrLOCK_TERM_TX ) ) != 0U ) { xprintf( " TERM_TX" ); }
    }
    xprintf( "\r\n" );

    xprintf( "terminal     : %s\r\n",
             drv_term_sense_presente() ? "CONECTADA" : "no detectada" );

    /*
     * Stack libre MÍNIMO histórico, en palabras. ⚠ Estos números son de una
     * compilación Debug con -O0, que usa bastante MÁS stack que Release: el
     * cambio va en la dirección segura, pero NO hay que ajustar los tamaños al
     * límite con estos valores.
     */
    xprintf( "heap libre   : %u bytes\r\n",
             ( unsigned ) xPortGetFreeHeapSize() );
    xprintf( "stack libre  : tkCtl %u de %u, tkCmd %u de %u  (palabras)\r\n",
             ( unsigned ) uxTaskGetStackHighWaterMark( xHandle_tkCtl ), tkCtl_STACK_SIZE,
             ( unsigned ) uxTaskGetStackHighWaterMark( NULL ),          tkCmd_STACK_SIZE );

    xprintf( "errores UART : 0x%08lX\r\n",
             ( unsigned long ) drv_uart_errores( drvUART_TERM ) );
}

/*
 * ⭐ Existe para ver el pin CRUDO, no la creencia del driver.
 *
 * Si la consola recibe pero el equipo no baja a Stop 2 —o al revés, si no
 * recibe— la pregunta es siempre la misma: qué nivel tiene PB5 de verdad y con
 * qué pull quedó configurado. Sin este comando hay que ir al tester.
 *
 * ⚠ Y el caso que más cuesta: con el pin flotando, la lectura puede dar 0 y el
 * equipo creería que hay una terminal conectada para siempre, sin bajar nunca a
 * Stop 2. El nivel y el PUPDR juntos lo delatan.
 */
static void cmdSense( void )
{
    drv_term_sense_cfg_t xCfg;

    drv_term_sense_config( &xCfg );

    static const char *pcModer[] = { "entrada", "salida", "alterna", "analogico" };
    static const char *pcPupdr[] = { "sin pull", "pull-up", "pull-down", "reservado" };

    xprintf( "\r\nTERM_SENSE (PB5)\r\n" );
    xprintf( "  nivel del pin : %s   (activo en BAJO: 0 = terminal conectada)\r\n",
             drv_term_sense_nivel_pin() ? "ALTO" : "BAJO" );
    xprintf( "  el driver dice: %s\r\n",
             drv_term_sense_presente() ? "CONECTADA" : "no detectada" );
    xprintf( "  MODER         : %lu (%s)\r\n",
             ( unsigned long ) xCfg.ulModer, pcModer[ xCfg.ulModer & 0x3U ] );
    xprintf( "  PUPDR         : %lu (%s)\r\n",
             ( unsigned long ) xCfg.ulPupdr, pcPupdr[ xCfg.ulPupdr & 0x3U ] );
    xprintf( "  cambios vistos: %lu\r\n",
             ( unsigned long ) drv_term_sense_cambios() );
}

/*
 * Reset por NVIC_SystemReset, que pulsa NRST.
 *
 * ⭐ Está medido que ANDA (2026-09-08) y que el salto tibio al vector —el viejo
 * comando 'reboot'— era el que colgaba: dejaba los periféricos y FreeRTOS
 * corriendo con sus interrupciones pendientes mientras los MX_*_Init los
 * reprogramaban en caliente. Por eso hay un solo comando de reset y es éste.
 */
static void cmdReset( void )
{
    xprintf( "\r\nreiniciando...\r\n" );
    vTaskDelay( pdMS_TO_TICKS( 250 ) );   /* que salga el texto antes del reset */
    NVIC_SystemReset();
}

/*------------------------------------------------------------------------------
 * La tarea
 *----------------------------------------------------------------------------*/
void tkCmd( void *pvParameters )
{
    ( void ) pvParameters;

    char cChar;

    /* Abre los drivers de FRTOS-IO desde ADENTRO de la tarea: crear semáforos y
       stream buffers necesita el scheduler ya corriendo. Si esto fallara,
       drv_uart_write() haría xSemaphoreTake(NULL) y el configASSERT congelaría
       todo, incluido el LED. */
    if( frtos_open_all() == false )
    {
        Error_Handler();
    }

    drv_term_sense_init();

    FRTOS_CMD_init();
    FRTOS_CMD_register( "help",   cmdHelp   );
    FRTOS_CMD_register( "status", cmdStatus );
    FRTOS_CMD_register( "sense",  cmdSense  );
    FRTOS_CMD_register( "reset",  cmdReset  );

    /* La versión y la fecha en el banner, no sólo en 'status': es lo primero que
       uno quiere ver al enchufar la terminal, y contesta sin tipear nada la
       pregunta de si quedó flasheado el binario que se creía. */
    xprintf( "\r\n\r\n%s %s - consola TERM\r\n", FW_NOMBRE, FW_VERSION );
    xprintf( "compilado %s\r\n", FW_FECHA );
    prvImprimirCausaReset();
    xprintf( "cmd>" );

    for( ;; )
    {
        /*
         * Bloquea en el kernel esperando un carácter. ⛔ NADA de polear: un lazo
         * con vTaskDelay(1) despertaría al micro 512 veces por segundo y
         * anularía el tickless, que es de donde salen los µA de reposo.
         *
         * ⏳ La espera es INDEFINIDA porque todavía no hay watchdog. Cuando
         * entre hay que ponerle timeout (~60 s) y reportar en cada vuelta: sin
         * eso la tarea no pasa nunca por ningún lado mientras nadie tipee, y no
         * habría forma de vigilar el único camino por el que un técnico puede
         * diagnosticar el equipo en campo.
         */
        if( frtos_read( fdTERM, &cChar, 1U ) == 1 )
        {
            ( void ) FRTOS_CMD_process( cChar );
        }
    }
}
