/*
 * tkCmd.c  -  ver tkCmd.h
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "tkCmd.h"
#include "tkCtl.h"
#include "frtos-io.h"
#include "frtos_cmd.h"
#include "drv_term_sense.h"
#include "drv_i2c.h"
#include "drv_eeprom.h"
#include "drv_rtc79410.h"
#include "drv_uart.h"
#include "pwr_lock.h"
#include "main.h"

/* Memoria estática: la tarea no toca el heap. */
StaticTask_t tkCmd_TCB;
StackType_t  tkCmd_Stack[ tkCmd_STACK_SIZE ];

extern UART_HandleTypeDef huart1;

/*------------------------------------------------------------------------------
 * ⭐ DIAGNÓSTICO DEL TX: emitir por POLEO, sin driver
 *
 * Esto NO pasa por drv_uart ni por FRTOS-IO: llama a HAL_UART_Transmit() directo
 * y espera con el periférico. O sea que no usa la interrupción, ni el semáforo
 * xTxDone, ni el mutex, ni el candado de energía — nada de lo que el camino
 * normal necesita para funcionar.
 *
 * Por eso parte el problema en dos con una sola bajada:
 *
 *   sale texto por poleo, pero NO el banner  -> el TX físico ANDA. El problema
 *                                               está en el driver por
 *                                               interrupción o en su ISR.
 *   no sale NADA                             -> el problema es anterior al
 *                                               firmware: PB6, el conector, el
 *                                               cable o el adaptador.
 *
 * El firmware de referencia traía esta misma herramienta, y no por casualidad:
 * en esta placa ya hubo una vez un componente malo en el camino serial, con este
 * mismo síntoma.
 *----------------------------------------------------------------------------*/
static void prvTxPoleo( const char *pcTexto )
{
    ( void ) HAL_UART_Transmit( &huart1,
                                ( const uint8_t * ) pcTexto,
                                ( uint16_t ) strlen( pcTexto ),
                                HAL_MAX_DELAY );
}

/*
 * En 1, tkCmd no hace NADA más que emitir 'U' para siempre, por poleo.
 *
 * ⭐ La 'U' es 0x55, o sea 01010101: a 9600 baudios da una onda cuadrada
 * perfecta de 4800 Hz en PB6, que se ve con cualquier osciloscopio y permite
 * MEDIR el baudrate real en vez de suponerlo. Si el micro emite y en la terminal
 * no llega nada, lo que falla está entre el pin y la PC.
 */
#define TKCMD_ONDA_U        0

/*------------------------------------------------------------------------------
 * Los comandos
 *----------------------------------------------------------------------------*/

static void cmdHelp( void )
{
    xprintf( "\r\nComandos disponibles:\r\n" );
    xprintf( "  help            esta ayuda\r\n" );
    xprintf( "  status          version, reset, candados, stacks\r\n" );
    xprintf( "  sense           TERM_SENSE: nivel del pin y configuracion\r\n" );
    xprintf( "  i2c [scan]      el bus I2C2\r\n" );
    xprintf( "  ee              EEPROM M24M01: rd, wr, test\r\n" );
    xprintf( "  rtc             RTC MCP79410: hora, validez, estado\r\n" );
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

    /*
     * Stack libre MÍNIMO histórico, en palabras. ⚠ Estos números son de una
     * compilación Debug con -O0, que usa bastante MÁS stack que Release: el
     * cambio va en la dirección segura, pero NO hay que ajustar los tamaños al
     * límite con estos valores.
     */
    xprintf( "terminal     : %s   (PB5 en %s)\r\n",
             drv_term_sense_presente() ? "CONECTADA: NO duerme" : "no detectada: duerme",
             drv_term_sense_nivel_pin() ? "ALTO" : "BAJO" );

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
 * Si la consola no recibe, la pregunta es siempre la misma: qué nivel tiene PB5
 * de verdad y con qué pull quedó configurado. ⚠ En esta placa el pin ya se vio
 * SUBIR SOLO con la terminal enchufada (medido el 2026-09-30 con un latido por
 * consola: PB5=0 a los 5 s y PB5=1 a los 10), y cuando eso pasa el equipo se
 * duerme y la consola muere sin ninguna otra señal. El nivel y el PUPDR juntos
 * lo delatan.
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

/*------------------------------------------------------------------------------
 * I2C2 — el bus
 *----------------------------------------------------------------------------*/

/*
 * ⭐ El scan es la herramienta que corrige la documentación, no sólo la que
 * confirma que el bus anda.
 *
 * Fue un `i2c scan` el que descubrió el 2026-08-12 que la EEPROM es una
 * **M24M01 de 128 KB y no una M24M02 de 256**: el chip contesta en `50` y `51`
 * y no en `50..53`. Los bits A17/A16 los decodifica adentro y no son patas que
 * se puedan atar, así que dos bloques en vez de cuatro son 1 Mbit y no 2.
 *
 * ⚠ Y hay una dirección donde NO hay que escribir NUNCA: `58`/`59`, la
 * *Identification Page* de la EEPROM. Se bloquea en sólo-lectura de forma
 * **permanente e irreversible**, y el bloqueo lo dispara una escritura. Leerla
 * es gratis; por eso el scan usa `drv_i2c_probe()`, que sólo direcciona.
 */
/*
 * ⛔ CÓMO SE LEEN LOS ARGUMENTOS, y la trampa que costó una bajada (2026-09-30)
 *
 * `FRTOS_CMD_makeArgv()` devuelve la cantidad de **ARGUMENTOS, no de tokens**:
 * termina en `return i - 1`, donde `i` cuenta también `argv[0]`. O sea que es
 * **uno menos que el `argc` de C**, con el que es natural confundirlo.
 *
 * La primera versión de estos comandos comparaba contra ese valor como si fuera
 * `argc`, así que `i2c scan` daba 1 donde se esperaba 2 y **los tres comandos
 * caían en su propia ayuda**. El síntoma engaña: parece que el subcomando "no
 * existe" cuando en realidad la condición nunca se cumplió.
 *
 * ⭐ Por eso acá NO se usa el contador: se pregunta por **`argv[N] != NULL`**,
 * que es inequívoco —`makeArgv()` hace `memset(argv, 0, ...)` antes de
 * tokenizar, así que los no usados quedan en NULL— y de paso verifica
 * exactamente lo que se va a leer, no una cuenta que lo aproxima.
 */
static void cmdI2c( void )
{
    ( void ) FRTOS_CMD_makeArgv();

    if( ( argv[ 1 ] != NULL ) && ( strcmp( argv[ 1 ], "scan" ) == 0 ) )
    {
        uint32_t ulEncontrados = 0U;

        xprintf( "\r\nbarriendo el I2C2 (direcciones de 7 bits)...\r\n" );

        for( uint8_t uc7 = 0x08U; uc7 <= 0x77U; uc7++ )
        {
            if( drv_i2c_probe( ( uint8_t ) ( uc7 << 1 ) ) )
            {
                const char *pcQuien = "";

                switch( uc7 )
                {
                    case 0x50: case 0x51: pcQuien = "  <- EEPROM M24M01";        break;
                    case 0x58: case 0x59: pcQuien = "  <- EEPROM: ID page (NO ESCRIBIR)"; break;
                    case 0x6F:            pcQuien = "  <- RTC MCP79410";         break;
                    case 0x57:            pcQuien = "  <- MCP79410: su EEPROM";  break;
                    case 0x41:            pcQuien = "  <- INA3221";              break;
                    default:                                                     break;
                }

                xprintf( "  %02X%s\r\n", ( unsigned ) uc7, pcQuien );
                ulEncontrados++;
            }
        }

        xprintf( "%lu dispositivo(s)\r\n", ( unsigned long ) ulEncontrados );
        return;
    }

    xprintf( "\r\nI2C2 (PB13 SCL, PB14 SDA, 100 kHz, pull-up de 10K externos)\r\n" );
    xprintf( "  ultimo error : 0x%08lX\r\n", ( unsigned long ) drv_i2c_last_error() );
    xprintf( "  EEPROM lista : %s\r\n", drv_eeprom_lista() ? "SI" : "no contesta" );
    xprintf( "\r\n  i2c scan     barre el bus\r\n" );
}

/*------------------------------------------------------------------------------
 * EEPROM M24M01
 *----------------------------------------------------------------------------*/

/*
 * ⭐ `ee test` ejercita las DOS trampas de escribir en una EEPROM I2C, que son
 * mudas las dos y por eso hay que provocarlas a propósito:
 *
 *   1. **La escritura de página no desborda: DA LA VUELTA.** Pasarse del borde
 *      de 256 bytes no sigue en la página siguiente — pisa el principio de ESA
 *      misma página, sin error y sin aviso.
 *   2. **Después de escribir, el chip queda sordo ~5 ms** y NACKea todo,
 *      incluso su propia dirección. Ese NACK NO es un error: es *acknowledge
 *      polling* y hay que reintentar. Sin eso, escribir dos páginas seguidas
 *      falla siempre en la segunda.
 *
 * Por eso el test escribe **cruzando un borde de página** (0x000F0) y otro de
 * **bloque de 64 KB** (0x0FFF0), que es donde el driver tiene que cambiar el
 * byte de dispositivo de 0xA0 a 0xA2 — ahí es donde se vería que el
 * direccionamiento de 17 bits está mal.
 */
static void cmdEe( void )
{
    char cBuf[ 40 ];

    ( void ) FRTOS_CMD_makeArgv();

    if( ( argv[ 1 ] != NULL ) && ( strcmp( argv[ 1 ], "test" ) == 0 ) )
    {
        static const uint32_t pulDir[] = { 0x000F0UL, 0x0FFF0UL };
        static const char     cPatron[] = "SPQ-ARM-0123456789abcdef";
        uint32_t              ulFallas  = 0U;

        xprintf( "\r\nee test: escribe cruzando bordes de pagina y de bloque\r\n" );

        for( uint32_t i = 0U; i < ( sizeof( pulDir ) / sizeof( pulDir[ 0 ] ) ); i++ )
        {
            uint32_t ulLargo = ( uint32_t ) strlen( cPatron );

            if( drv_eeprom_write( pulDir[ i ], cPatron, ulLargo ) != ( int32_t ) ulLargo )
            {
                xprintf( "  0x%05lX: FALLO la escritura\r\n", ( unsigned long ) pulDir[ i ] );
                ulFallas++;
                continue;
            }

            memset( cBuf, 0, sizeof( cBuf ) );

            if( drv_eeprom_read( pulDir[ i ], cBuf, ulLargo ) != ( int32_t ) ulLargo )
            {
                xprintf( "  0x%05lX: FALLO la lectura\r\n", ( unsigned long ) pulDir[ i ] );
                ulFallas++;
                continue;
            }

            if( memcmp( cBuf, cPatron, ulLargo ) != 0 )
            {
                xprintf( "  0x%05lX: los datos NO coinciden -> '%s'\r\n",
                         ( unsigned long ) pulDir[ i ], cBuf );
                ulFallas++;
                continue;
            }

            xprintf( "  0x%05lX: OK (%lu bytes)\r\n",
                     ( unsigned long ) pulDir[ i ], ( unsigned long ) ulLargo );
        }

        xprintf( ( ulFallas == 0U ) ? "ee test: OK\r\n" : "ee test: %lu FALLA(S)\r\n",
                 ( unsigned long ) ulFallas );
        return;
    }

    if( ( argv[ 3 ] != NULL ) && ( strcmp( argv[ 1 ], "rd" ) == 0 ) )
    {
        uint32_t ulAddr  = strtoul( argv[ 2 ], NULL, 0 );
        uint32_t ulBytes = strtoul( argv[ 3 ], NULL, 0 );

        if( ulBytes > ( sizeof( cBuf ) - 1U ) )
        {
            ulBytes = sizeof( cBuf ) - 1U;
        }

        memset( cBuf, 0, sizeof( cBuf ) );

        if( drv_eeprom_read( ulAddr, cBuf, ulBytes ) != ( int32_t ) ulBytes )
        {
            xprintf( "\r\nERROR de lectura\r\n" );
            return;
        }

        xprintf( "\r\n0x%05lX:", ( unsigned long ) ulAddr );

        for( uint32_t i = 0U; i < ulBytes; i++ )
        {
            xprintf( " %02X", ( unsigned ) ( uint8_t ) cBuf[ i ] );
        }

        /* Los bytes no imprimibles se reemplazan: un 0x07 en el medio haría
           sonar la terminal y un 0x08 borraría lo ya escrito. */
        for( uint32_t i = 0U; i < ulBytes; i++ )
        {
            if( ( cBuf[ i ] < 0x20 ) || ( cBuf[ i ] > 0x7E ) )
            {
                cBuf[ i ] = '.';
            }
        }

        xprintf( "  |%s|\r\n", cBuf );
        return;
    }

    if( ( argv[ 3 ] != NULL ) && ( strcmp( argv[ 1 ], "wr" ) == 0 ) )
    {
        uint32_t ulAddr  = strtoul( argv[ 2 ], NULL, 0 );
        uint32_t ulLargo = ( uint32_t ) strlen( argv[ 3 ] );

        if( drv_eeprom_write( ulAddr, argv[ 3 ], ulLargo ) != ( int32_t ) ulLargo )
        {
            xprintf( "\r\nERROR de escritura\r\n" );
            return;
        }

        xprintf( "\r\n0x%05lX <- '%s' (%lu bytes)\r\n",
                 ( unsigned long ) ulAddr, argv[ 3 ], ( unsigned long ) ulLargo );
        return;
    }

    xprintf( "\r\nEEPROM M24M01: 128 KB, direccion plana 0x00000..0x1FFFF\r\n" );
    xprintf( "  lista: %s\r\n", drv_eeprom_lista() ? "SI" : "no contesta" );
    xprintf( "\r\n  ee rd <addr> <n>     lee n bytes\r\n" );
    xprintf( "  ee wr <addr> <texto> escribe\r\n" );
    xprintf( "  ee test              cruza bordes de pagina y de bloque\r\n" );
}

/*------------------------------------------------------------------------------
 * RTC MCP79410
 *----------------------------------------------------------------------------*/

/*
 * ⭐ Lo que informa este comando no es sólo la hora: es si se le puede CREER.
 *
 * `drv_rtc_validez()` no adivina por la fecha —¿qué año es "demasiado
 * viejo"?—: mira una **firma de 5 bytes en la SRAM del propio chip**, que se
 * alimenta de la misma pila que el contador de tiempo. Eso hace que sea una
 * equivalencia física y no una heurística:
 *
 *   firma intacta  <=>  el respaldo sostuvo  <=>  el reloj nunca se detuvo
 *   firma perdida  <=>  el respaldo falló    <=>  el chip arrancó frío
 *
 * ⚠ Pero certifica CONTINUIDAD, no CORRECCIÓN: si alguna vez se fijó una hora
 * equivocada, la firma la certifica igual. Por eso las capas de arriba agregan
 * el chequeo de que el año no sea anterior al de compilación del firmware —una
 * muestra no puede ser anterior al binario que la tomó—.
 */
static void cmdRtc( void )
{
    RtcTimeType_t xHora;
    rtc_estado_t  xEstado;

    ( void ) FRTOS_CMD_makeArgv();

    if( ( argv[ 1 ] != NULL ) && ( strcmp( argv[ 1 ], "invalid" ) == 0 ) )
    {
        /* Borra la firma para poder ejercitar el camino de arranque en frío SIN
           sacar la pila, que es lo que lo hacía imposible de probar. */
        xprintf( "\r\nfirma borrada: %s\r\n",
                 drv_rtc_invalidar() ? "la hora queda marcada NO CONFIABLE" : "ERROR" );
        return;
    }

    if( argv[ 6 ] != NULL )
    {
        xHora.year  = ( uint8_t ) strtoul( argv[ 1 ], NULL, 10 );
        xHora.month = ( uint8_t ) strtoul( argv[ 2 ], NULL, 10 );
        xHora.day   = ( uint8_t ) strtoul( argv[ 3 ], NULL, 10 );
        xHora.hour  = ( uint8_t ) strtoul( argv[ 4 ], NULL, 10 );
        xHora.min   = ( uint8_t ) strtoul( argv[ 5 ], NULL, 10 );
        xHora.sec   = ( uint8_t ) strtoul( argv[ 6 ], NULL, 10 );

        /* El día de la semana NO se pide: es un dato DERIVADO de la fecha y el
           driver lo calcula con Sakamoto. Pedirlo sería dejar que alguien
           escriba una combinación imposible. */
        xHora.weekDay = 0U;

        if( drv_rtc_escribir( &xHora ) == false )
        {
            xprintf( "\r\nERROR al escribir la hora\r\n" );
            return;
        }

        xprintf( "\r\nhora fijada, y la firma de validez escrita DESPUES\r\n" );
        return;
    }

    if( drv_rtc_leer( &xHora ) == false )
    {
        xprintf( "\r\nel RTC MCP79410 no contesta\r\n" );
        return;
    }

    static const char *pcDia[] = { "?", "dom", "lun", "mar", "mie", "jue", "vie", "sab" };

    xprintf( "\r\nRTC MCP79410\r\n" );
    xprintf( "  fecha/hora : 20%02u-%02u-%02u %02u:%02u:%02u (%s)\r\n",
             ( unsigned ) xHora.year,  ( unsigned ) xHora.month, ( unsigned ) xHora.day,
             ( unsigned ) xHora.hour,  ( unsigned ) xHora.min,   ( unsigned ) xHora.sec,
             pcDia[ ( xHora.weekDay <= 7U ) ? xHora.weekDay : 0U ] );

    switch( drv_rtc_validez() )
    {
        case rtcHORA_VALIDA:
            xprintf( "  validez    : CONFIABLE (la firma esta y el oscilador corre)\r\n" );
            break;
        case rtcHORA_ARRANQUE_FRIO:
            xprintf( "  validez    : [!] ARRANQUE EN FRIO: se perdio el respaldo\r\n" );
            break;
        default:
            xprintf( "  validez    : [!] el chip no contesta\r\n" );
            break;
    }

    if( drv_rtc_estado( &xEstado ) )
    {
        xprintf( "  oscilador  : %s\r\n", xEstado.bOscilando ? "corriendo" : "[!] PARADO" );
        xprintf( "  pila       : %s\r\n", xEstado.bPilaHab   ? "habilitada" : "[!] deshabilitada" );
        xprintf( "  PWRFAIL    : %s\r\n", xEstado.bFalloPower ? "SI (hubo corte)" : "no" );
    }

    xprintf( "\r\n  rtc <aa> <mm> <dd> <hh> <mm> <ss>   fija la hora\r\n" );
    xprintf( "  rtc invalid                         borra la firma (prueba en frio)\r\n" );
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

    /*
     * Lo PRIMERO, y por poleo: si esto no aparece en la terminal, el problema no
     * está en el driver ni en FreeRTOS. Ver el comentario de prvTxPoleo().
     */
    prvTxPoleo( "\r\n\r\n[A] tkCmd arranco (esto sale por POLEO)\r\n" );

#if ( TKCMD_ONDA_U == 1 )
    for( ;; )
    {
        prvTxPoleo( "UUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUU" );
    }
#endif

    /* Abre los drivers de FRTOS-IO desde ADENTRO de la tarea: crear semáforos y
       stream buffers necesita el scheduler ya corriendo. Si esto fallara,
       drv_uart_write() haría xSemaphoreTake(NULL) y el configASSERT congelaría
       todo, incluido el LED. */
    if( frtos_open_all() == false )
    {
        prvTxPoleo( "[!] frtos_open_all() FALLO\r\n" );
        Error_Handler();
    }

    prvTxPoleo( "[B] drivers abiertos; lo que sigue va por INTERRUPCION\r\n" );

    /* El bus primero: los dos chips cuelgan de él. Un fallo acá no impide
       arrancar —un equipo desatendido es mejor que mida algo que nada— así que
       se avisa y se sigue. */
    if( drv_i2c_init() == false )
    {
        xprintf( "\r\n[!] el bus I2C2 no se pudo inicializar\r\n" );
    }

    if( drv_rtc_init() == false )
    {
        xprintf( "\r\n[!] el RTC MCP79410 no contesta\r\n" );
    }

    if( drv_eeprom_lista() == false )
    {
        xprintf( "\r\n[!] la EEPROM M24M01 no contesta\r\n" );
    }

    FRTOS_CMD_init();
    FRTOS_CMD_register( "help",   cmdHelp   );
    FRTOS_CMD_register( "status", cmdStatus );
    FRTOS_CMD_register( "sense",  cmdSense  );
    FRTOS_CMD_register( "i2c",    cmdI2c    );
    FRTOS_CMD_register( "ee",     cmdEe     );
    FRTOS_CMD_register( "rtc",    cmdRtc    );
    FRTOS_CMD_register( "reset",  cmdReset  );

    /* La versión y la fecha en el banner, no sólo en 'status': es lo primero que
       uno quiere ver al enchufar la terminal, y contesta sin tipear nada la
       pregunta de si quedó flasheado el binario que se creía. */
    xprintf( "\r\n\r\n%s %s - consola TERM\r\n", FW_NOMBRE, FW_VERSION );
    xprintf( "compilado %s\r\n", FW_FECHA );
    prvImprimirCausaReset();

    /* ⚠ Que el equipo no duerma no es obvio desde afuera y cambia el consumo por
       tres órdenes de magnitud, así que lo dice el banner. */
    xprintf( "[!] la consola vive mientras TERM_SENSE vea terminal (se polea cada 5 s)\r\n" );

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
