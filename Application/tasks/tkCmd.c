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
#include "drv_ina3221.h"
#include "drv_rs485.h"
#include "drv_uart.h"
#include "drv_sd.h"
#include "drv_adc.h"
#include "drv_pulsos.h"
#include "drv_valvula.h"
#include "pwr_lock.h"
#include "main.h"

/*
 * ⛔ ESTE DEFINE VA ACÁ ARRIBA Y NO MÁS ABAJO.
 * Un `#if` sobre un `#define` declarado después en el archivo se lee como 0 y
 * compila el bloque equivocado SIN DECIR NADA. Ya pasó dos veces en el
 * rearranque: con un `#include` y con `prvLeerCNT`.
 *
 * ---------------------------------------------------------------------------
 * INSTRUMENTO DE BISECT DEL CONSUMO (2026-10-02)
 *
 * En 0 NO se llama a `drv_adc_init()`, así que el ADC queda exactamente como lo
 * dejó `MX_ADC1_Init()` de CubeMX: el mismo estado que tenía el `0.0.8`, que
 * medía 5 µA.
 *
 * ⭐ Aísla UNA sola variable, y por eso es mejor que volver al tag `v0.0.8`:
 * aquel difiere además en el `.ioc` —sin PB0 ni PC4— o sea dos cosas a la vez.
 * Acá cambia una línea y el hardware queda idéntico.
 *
 *   mide 5 µA   -> el consumo lo agrega `drv_adc_init()`, y los registros que
 *                  imprime `vin` dicen cuál bit quedó mal
 *   mide 337 µA -> NO es el ADC ni el firmware: es el PCB
 */
/*
 * ⭐ Este bisect YA DIO SU RESPUESTA (2026-10-02): con 0 el consumo siguió en
 * 337 µA, o sea que el ADC quedaba despierto SIN que corriera drv_adc_init().
 * Lo deja así `MX_ADC1_Init()` de CubeMX: llama a `HAL_ADC_Init()`, y ésa sale
 * de deep power-down y enciende el regulador del ADC (stm32l4xx_hal_adc.c:475).
 *
 * ⚠ Por eso `drv_adc_init()` tiene que CORRER: es lo único que vuelve a dormir
 * el ADC. Dejarlo en 0 no es "no tocar el ADC", es dejarlo despierto.
 * El interruptor se conserva porque el experimento sirve para el próximo
 * periférico que entre.
 */
#define TKCMD_ADC_INIT      1     /* 0 = no inicializar el ADC (bisect) */

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
    xprintf( "  ina             INA3221: 4-20 mA y el riel de sensores (on|off)\r\n" );
    xprintf( "  rs485           el SP3485 y los 3 rieles conmutados\r\n" );
    xprintf( "  sd              microSD por SPI3: energia, sectores\r\n" );
    xprintf( "  vin             rieles por ADC1: 12 V y 3V3 (VREFINT)\r\n" );
    xprintf( "  cnt             contador de pulsos CNT0 (PA12): cuenta y pin\r\n" );
    xprintf( "  ev              electrovalvula TOYI: abrir, cerrar y estado\r\n" );
    xprintf( "  cls | clear     limpia la pantalla de la terminal\r\n" );
    xprintf( "  reset           reinicia el equipo\r\n" );
    xprintf( "\r\n" );
    xprintf( "  el comando va COMPLETO: 'status', no 'st'\r\n" );
    /* ⚠ `help <comando>` NO existe, y el 2026-10-02 alguien lo tipeó: salía la
       lista entera sin decir que el argumento se descartaba. Cada comando ya
       imprime su propia ayuda cuando se tipea solo, así que se apunta ahí en
       vez de agregar un mecanismo duplicado. */
    xprintf( "  el detalle de un comando sale tipeandolo SOLO: 'sd', 'rs485', 'ee'\r\n" );
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

/*------------------------------------------------------------------------------
 * INA3221 — las entradas de 4-20 mA
 *----------------------------------------------------------------------------*/

/*
 * ⚠ LO QUE ESTA ETAPA PUEDE Y NO PUEDE VALIDAR.
 *
 * `EN_PWR_SENS420` queda afuera a propósito mientras se mide el consumo de a un
 * integrado por vez, así que **los lazos de 4-20 mA no están alimentados** y las
 * lecturas van a dar cerca de cero. Eso NO es una falla: lo que se valida acá es
 * que el chip se **identifique**, **convierta** y **duerma**.
 *
 * ⭐ Y la identificación importa más que el ACK: en este bus hay siete
 * direcciones ocupadas y ya hubo una sorpresa —la EEPROM resultó ser una M24M01
 * y no la M24M02 que decía el código heredado—. Un ACK prueba que hay algo;
 * `MFID` y `DIEID` prueban **qué**.
 *
 * ⛔ Los valores se imprimen formateados A MANO, sin `%f`, y es a propósito: el
 * `printf` de newlib-nano necesita `-u _printf_float`, que es una opción del
 * `.cproject`. Un comando de diagnóstico no puede depender de algo que una
 * reconfiguración del proyecto puede perder — y el síntoma de que falte no es un
 * número mal, es **un campo VACÍO** en medio de una línea que sale bien.
 */
static void cmdIna( void )
{
    uint16_t usVal = 0U;

    ( void ) FRTOS_CMD_makeArgv();

    /* ⚠ El riel es ACTIVO ALTO (EN=1 prende), como los TPS22810 del RS485 y al
       revés del EN_PWR_SD de la microSD. Los dos criterios conviven en la placa. */
    if( ( argv[ 1 ] != NULL ) && ( strcmp( argv[ 1 ], "on" ) == 0 ) )
    {
        drv_ina_pwr_sensores( true );
        xprintf( "\r\nriel de sensores 4-20 mA ENCENDIDO\r\n" );
        xprintf( "  [!] esperar %u ms antes de creerle a una medida\r\n",
                 ( unsigned ) DRV_INA_SETTLE_MS );
        xprintf( "  [!] ALIMENTA LOS TRANSMISORES DE LAZO: consumen mucho mas que\r\n" );
        xprintf( "      todo el resto del equipo junto. Acordarse de 'ina off'.\r\n" );
        return;
    }

    if( ( argv[ 1 ] != NULL ) && ( strcmp( argv[ 1 ], "off" ) == 0 ) )
    {
        drv_ina_pwr_sensores( false );
        xprintf( "\r\nriel de sensores 4-20 mA apagado\r\n" );
        return;
    }

    if( ( argv[ 2 ] != NULL ) && ( strcmp( argv[ 1 ], "reg" ) == 0 ) )
    {
        uint8_t ucReg = ( uint8_t ) strtoul( argv[ 2 ], NULL, 0 );

        if( drv_ina_reg_leer( ucReg, &usVal ) == false )
        {
            xprintf( "\r\nERROR al leer el registro 0x%02X\r\n", ( unsigned ) ucReg );
            return;
        }

        xprintf( "\r\nreg 0x%02X = 0x%04X\r\n", ( unsigned ) ucReg, ( unsigned ) usVal );
        return;
    }

    xprintf( "\r\nINA3221 (I2C2, direccion de 7 bits 41)\r\n" );

    if( drv_ina_presente() == false )
    {
        xprintf( "  [!] no contesta o no se identifico\r\n" );
        return;
    }

    if( drv_ina_reg_leer( DRV_INA_REG_MFID, &usVal ) )
    {
        xprintf( "  MFID       : 0x%04X\r\n", ( unsigned ) usVal );
    }

    if( drv_ina_reg_leer( DRV_INA_REG_DIEID, &usVal ) )
    {
        xprintf( "  DIEID      : 0x%04X\r\n", ( unsigned ) usVal );
    }

    if( drv_ina_reg_leer( DRV_INA_REG_CONF, &usVal ) )
    {
        /* MODE en los bits 2..0: 000 es power-down, que es el estado de REPOSO
           del chip. Con él despierto consume ~350 µA contra los ~2 µA dormido,
           o sea setenta veces el consumo del micro en Stop 2. */
        xprintf( "  CONFIG     : 0x%04X  (MODE=%u: %s)\r\n",
                 ( unsigned ) usVal, ( unsigned ) ( usVal & 0x7U ),
                 ( ( usVal & 0x7U ) == 0U ) ? "power-down, como debe reposar"
                                            : "[!] CONVIRTIENDO: ~350 uA" );
    }

    /* ⭐ Que el riel haya quedado encendido no tiene NINGUN sintoma salvo la
       autonomia, igual que el MODE del INA: por eso se imprime siempre. */
    xprintf( "  riel 4-20mA: %s\r\n",
             drv_ina_pwr_sensores_estado() ? "[!] ENCENDIDO" : "apagado" );

    xprintf( "\r\nmidiendo los 3 canales (riel + %u ms de asentamiento + ~845 ms\r\n"
             "de barrido)...\r\n", ( unsigned ) DRV_INA_SETTLE_MS );

    float fMa[ inaCH_COUNT ];

    /* Se deja el riel ENCENDIDO al salir: en banco lo normal es medir varias
       veces seguidas, y asi la segunda no vuelve a pagar los 500 ms de
       asentamiento. Se apaga con 'ina off', y el estado de arriba lo recuerda. */
    if( drv_ina_medir( fMa, true ) == false )
    {
        xprintf( "  [!] la medida FALLO (el chip queda dormido igual)\r\n" );
        return;
    }

    for( uint32_t i = 0U; i < ( uint32_t ) inaCH_COUNT; i++ )
    {
        int32_t lRaw = 0;
        int32_t lUa  = ( int32_t ) ( fMa[ i ] * 1000.0f );   /* mA -> µA, entero */
        int16_t sRaw = 0;

        if( drv_ina_shunt_raw( ( ina_canal_t ) i, &sRaw ) )
        {
            lRaw = ( int32_t ) sRaw;
        }

        /* El signo se saca aparte para poder imprimir la parte entera y los
           decimales con %ld sin que un negativo salga como "-0.-123". */
        int32_t lAbs  = ( lUa < 0 ) ? -lUa : lUa;
        const char *pcSigno = ( lUa < 0 ) ? "-" : "";

        xprintf( "  CH%lu: %s%ld.%03ld mA   (shunt raw %ld)\r\n",
                 ( unsigned long ) ( i + 1U ), pcSigno,
                 ( long ) ( lAbs / 1000 ), ( long ) ( lAbs % 1000 ), ( long ) lRaw );
    }

    xprintf( "\r\n  [!] el riel quedo ENCENDIDO ('ina off' para apagarlo)\r\n" );
    xprintf( "\r\n  ina on | off  el riel de la fuente lineal de sensores (PB12)\r\n" );
    xprintf( "  ina reg <n>   lee un registro\r\n" );
}

/*------------------------------------------------------------------------------
 * RS485 — el SP3485 y los tres rieles conmutados
 *----------------------------------------------------------------------------*/

/*
 * ⭐ EL DE LO MANEJA EL HARDWARE, y ésa es la decisión que importa.
 *
 * `HAL_RS485Ex_Init()` pone al USART3 en modo RS485: el silicio asierta el DE
 * antes del primer bit y lo suelta después del último, sin que el firmware toque
 * un pin. Hacerlo por software es la fuente del bug clásico del RS485 —cortar el
 * DE un bit antes de tiempo, con lo que el último byte sale mutilado— y es
 * **intermitente**, porque depende de la latencia de la tarea justo en ese
 * instante. Pasa el banco y falla en campo.
 *
 * ⚠ Los TRES rieles son activo ALTO (TPS22810), al revés que el `EN_PWR_SD` de
 * la microSD, que es un SI2301 de canal P donde 0 prende. Los dos criterios
 * conviven en la misma placa, así que no alcanza con acordarse de "uno".
 */
static void cmdRs485( void )
{
    static const char *pcNombre[] = { "bus (SP3485)", "qmbus (modbus)", "cpres (presion)" };

    ( void ) FRTOS_CMD_makeArgv();

    /* --- los rieles ------------------------------------------------------- */
    if( ( argv[ 2 ] != NULL ) &&
        ( ( strcmp( argv[ 1 ], "on" ) == 0 ) || ( strcmp( argv[ 1 ], "off" ) == 0 ) ) )
    {
        bool bOn = ( strcmp( argv[ 1 ], "on" ) == 0 );
        int  iCual = -1;

        if     ( strcmp( argv[ 2 ], "bus"   ) == 0 ) { iCual = rs485RAIL_BUS;   }
        else if( strcmp( argv[ 2 ], "qmbus" ) == 0 ) { iCual = rs485RAIL_QMBUS; }
        else if( strcmp( argv[ 2 ], "cpres" ) == 0 ) { iCual = rs485RAIL_CPRES; }
        else if( strcmp( argv[ 2 ], "all"   ) != 0 )
        {
            xprintf( "\r\nERROR: bus | qmbus | cpres | all\r\n" );
            return;
        }

        if( iCual < 0 )
        {
            for( uint32_t i = 0U; i < ( uint32_t ) rs485RAIL_COUNT; i++ )
            {
                drv_rs485_power( ( rs485_rail_t ) i, bOn );
            }
            xprintf( "\r\nlos 3 rieles: %s\r\n", bOn ? "ENCENDIDOS" : "apagados" );
        }
        else
        {
            drv_rs485_power( ( rs485_rail_t ) iCual, bOn );
            xprintf( "\r\n%s: %s\r\n", pcNombre[ iCual ], bOn ? "ENCENDIDO" : "apagado" );
        }

        if( bOn )
        {
            /* El transceiver está listo en microsegundos; los dispositivos del
               otro lado tardan SEGUNDOS en arrancar. Lo dice el comando porque
               el olvido más común del banco es hablarle a un caudalímetro que
               todavía no terminó de encender. */
            xprintf( "  (el SP3485 esta listo en us; un dispositivo del bus puede "
                     "tardar segundos)\r\n" );
        }
        return;
    }

    /* --- transmitir y escuchar -------------------------------------------- */
    if( ( argv[ 1 ] != NULL ) && ( strcmp( argv[ 1 ], "tx" ) == 0 ) && ( argv[ 2 ] != NULL ) )
    {
        char     cRta[ 64 ];
        uint16_t usLargo = ( uint16_t ) strlen( argv[ 2 ] );

        if( drv_rs485_power_estado( rs485RAIL_BUS ) == false )
        {
            xprintf( "\r\nERROR: el riel del SP3485 esta apagado ('rs485 on bus')\r\n" );
            return;
        }

        drv_rs485_rx_flush();

        if( drv_rs485_write( argv[ 2 ], usLargo ) != ( int16_t ) usLargo )
        {
            xprintf( "\r\nERROR al transmitir\r\n" );
            return;
        }

        xprintf( "\r\n-> '%s' (%u bytes)\r\n", argv[ 2 ], ( unsigned ) usLargo );

        /* Se lee por TRAMA, no por cantidad: corta al primer silencio en la
           línea, que es literalmente la delimitación que define Modbus RTU. */
        int16_t sLeidos = drv_rs485_read_frame( cRta, ( uint16_t ) ( sizeof( cRta ) - 1U ),
                                                pdMS_TO_TICKS( 1000 ), pdMS_TO_TICKS( 6 ) );

        if( sLeidos <= 0 )
        {
            xprintf( "<- nada en 1000 ms\r\n" );
            return;
        }

        xprintf( "<- %d bytes:", ( int ) sLeidos );

        for( int16_t i = 0; i < sLeidos; i++ )
        {
            xprintf( " %02X", ( unsigned ) ( uint8_t ) cRta[ i ] );
        }

        xprintf( "\r\n" );
        return;
    }

    /* --- el estado -------------------------------------------------------- */
    xprintf( "\r\nRS485 (USART3, 9600 8N1, SP3485)\r\n" );
    xprintf( "  PB10 TX, PB11 RX, PB1 DE  <- el DE lo maneja el HARDWARE\r\n" );

    for( uint32_t i = 0U; i < ( uint32_t ) rs485RAIL_COUNT; i++ )
    {
        xprintf( "  %-16s: %s\r\n", pcNombre[ i ],
                 drv_rs485_power_estado( ( rs485_rail_t ) i ) ? "ENCENDIDO" : "apagado" );
    }

    xprintf( "  errores UART    : 0x%08lX\r\n",
             ( unsigned long ) drv_uart_errores( drvUART_RS485 ) );

    xprintf( "\r\n  rs485 on|off  bus | qmbus | cpres | all\r\n" );
    xprintf( "  rs485 tx <texto>                  transmite y escucha la respuesta\r\n" );
}

/*------------------------------------------------------------------------------
 * vin — los rieles por ADC1
 *
 * ⭐ Dos medidas de naturaleza distinta: la de 12 V tiene hardware —divisor,
 * load switch y seguidor— y la de 3,3 V NO TIENE NINGUNO: sale de VREFINT.
 *----------------------------------------------------------------------------*/

/* A mano y no con %f: así un comando de diagnóstico no depende de la opción
   `-u _printf_float` del .cproject, que una reconfiguración del proyecto puede
   perder. Mismo criterio que el comando 'ina'. */
static void prvImprimirVolts( uint32_t ulMiliV )
{
    xprintf( "%lu.%03lu V",
             ( unsigned long ) ( ulMiliV / 1000UL ),
             ( unsigned long ) ( ulMiliV % 1000UL ) );
}

static void cmdVin( void )
{
    ( void ) FRTOS_CMD_makeArgv();

    if( argv[ 1 ] != NULL )
    {
        if( strcmp( argv[ 1 ], "on" ) == 0 )
        {
            drv_adc_pwr_12v( true );
            xprintf( "\r\ndivisor de 12 V conectado (consume %lu uA mientras este asi)\r\n",
                     ( unsigned long ) ( 12000UL / 66UL ) );
            return;
        }

        if( strcmp( argv[ 1 ], "off" ) == 0 )
        {
            drv_adc_pwr_12v( false );
            xprintf( "\r\ndivisor de 12 V desconectado\r\n" );
            return;
        }

        if( strcmp( argv[ 1 ], "raw" ) == 0 )
        {
            uint16_t usVref = 0U;
            uint16_t us12   = 0U;

            /* Se prende el riel para que la cuenta del divisor signifique algo:
               con el load switch abierto la entrada del seguidor queda al aire
               y lo que se lea no es una medida de nada. */
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

            xprintf( "\r\n" );

            if( bOk == false )
            {
                xprintf( "ERROR: la conversion fallo\r\n" );
                return;
            }

            /* ⭐ Las tres cifras que cierran la cuenta a mano:
                  VDDA = 3000 mV x CAL / leido  */
            xprintf( "  VREFINT : %5u cuentas   (CAL de fabrica: %u)\r\n",
                     ( unsigned ) usVref, ( unsigned ) drv_adc_vrefint_cal() );
            xprintf( "            VDDA = 3000 x %u / %u = %lu mV\r\n",
                     ( unsigned ) drv_adc_vrefint_cal(), ( unsigned ) usVref,
                     ( usVref != 0U ) ?
                        ( unsigned long ) ( ( 3000UL * drv_adc_vrefint_cal() ) / usVref ) : 0UL );
            xprintf( "  IN15    : %5u cuentas  (12 V, divisor 56K/10K)\r\n",
                     ( unsigned ) us12 );
            return;
        }

        if( strcmp( argv[ 1 ], "sleep" ) == 0 )
        {
            /*
             * Escribe la dormida PASO A PASO y lee el registro después de cada
             * escritura. Tres arreglos distintos fallaron igual, así que lo que
             * falta no es otro arreglo: es ver en qué escritura exacta se
             * pierde el efecto, y si el ADC tiene reloj para aceptarla.
             */
            xprintf( "\r\nRCC_AHB2ENR: 0x%08lX  ADCEN=%u%s\r\n",
                     ( unsigned long ) RCC->AHB2ENR,
                     ( unsigned ) ( ( RCC->AHB2ENR >> 13 ) & 1U ),
                     ( ( ( RCC->AHB2ENR >> 13 ) & 1U ) == 0U ) ?
                        "  <- SIN RELOJ: ninguna escritura puede entrar" : "" );

            xprintf( "  CR al entrar        : 0x%08lX\r\n", ( unsigned long ) ADC1->CR );

            ADC1->CR = 0UL;                       /* apaga ADVREGEN, nada mas */
            __DSB();
            xprintf( "  tras CR = 0         : 0x%08lX  (ADVREGEN deberia ser 0)\r\n",
                     ( unsigned long ) ADC1->CR );

            ADC1->CR = ADC_CR_DEEPPWD;            /* y ahora el deep power-down */
            __DSB();
            xprintf( "  tras CR = DEEPPWD   : 0x%08lX  (DEEPPWD deberia ser 1)\r\n",
                     ( unsigned long ) ADC1->CR );

            /* Una escritura a un registro distinto del mismo periférico: si
               ESTA entra y las de CR no, el problema es del registro CR y no
               del reloj ni del bus. */
            uint32_t ulAntes = ADC1->SMPR1;
            ADC1->SMPR1 = ulAntes ^ 0x00000007UL;
            __DSB();
            xprintf( "  SMPR1 %08lX -> %08lX  %s\r\n",
                     ( unsigned long ) ulAntes, ( unsigned long ) ADC1->SMPR1,
                     ( ADC1->SMPR1 != ulAntes ) ? "ESCRIBE OK" : "TAMPOCO ENTRA" );
            ADC1->SMPR1 = ulAntes;
            return;
        }

        xprintf( "\r\nERROR: vin | vin raw | vin on | vin off | vin sleep\r\n" );
        return;
    }

    /* ---- 'vin' pelado: la medida ---- */
    uint32_t ulVdda = 0UL;
    uint32_t ulV12  = 0UL;

    xprintf( "\r\n" );

    /* ⭐ El VDDA va PRIMERO porque la medida de 12 V lo necesita: convertir el
       divisor contra un 3,3 V nominal supuesto trasladaría directo cualquier
       desvío del riel, y el resultado sería un número plausible y mal. */
    if( drv_adc_vdda_mv( &ulVdda ) )
    {
        xprintf( "  VDDA / 3V3 : " );
        prvImprimirVolts( ulVdda );
        xprintf( "   (por VREFINT, SIN hardware externo)\r\n" );
    }
    else
    {
        xprintf( "  VDDA / 3V3 : ERROR de conversion\r\n" );
    }

    if( drv_adc_v12_mv( &ulV12, false ) )
    {
        xprintf( "  riel 12 V  : " );
        prvImprimirVolts( ulV12 );
        xprintf( "   (PB0 = ADC1_IN15, divisor 56K/10K)\r\n" );
    }
    else
    {
        xprintf( "  riel 12 V  : ERROR de conversion\r\n" );
    }

    xprintf( "  EN_SENS12V : %s\r\n", drv_adc_pwr_12v_estado() ? "ON" : "off" );

    /*
     * ⭐ Los registros, SIEMPRE. Mismo criterio que el CONFIG del INA3221: un
     * periférico que quedó despierto no tiene ningún síntoma salvo la
     * autonomía, así que el comando tiene que decirlo sin que nadie pregunte.
     *
     * ADC_CR : DEEPPWD (b31) y ADVREGEN (b29) son el estado de reposo correcto
     *          (1 y 0); ADEN (b0) en 1 significa que el ADC quedó habilitado.
     * ADC_CCR: VREFEN (b22) es el buffer de la referencia interna, TSEN (b23)
     *          el sensor de temperatura y VBATEN (b24) el divisor de VBAT.
     *          ⚠ Este registro es COMÚN y el deep power-down NO lo apaga.
     */
    uint32_t ulCr  = ADC1->CR;
    uint32_t ulCcr = ADC123_COMMON->CCR;

    /*
     * ⛔ Las posiciones son ADVREGEN=28, DEEPPWD=29, ADCALDIF=30, ADCAL=31.
     * La primera versión usaba 31 para DEEPPWD y 29 para ADVREGEN, o sea leía
     * ADCAL y DEEPPWD: **todo el diagnóstico del 2026-10-02 salió de ahí**, con
     * cuatro "arreglos" sobre un dormido que funcionaba desde el principio.
     * Verificar las posiciones en el header del CMSIS, no de memoria.
     */
    xprintf( "\r\n  ADC_CR     : 0x%08lX  DEEPPWD=%u ADVREGEN=%u ADEN=%u%s\r\n",
             ( unsigned long ) ulCr,
             ( unsigned ) ( ( ulCr >> ADC_CR_DEEPPWD_Pos  ) & 1U ),
             ( unsigned ) ( ( ulCr >> ADC_CR_ADVREGEN_Pos ) & 1U ),
             ( unsigned ) ( ulCr & 1U ),
             ( ( ( ulCr >> ADC_CR_DEEPPWD_Pos ) & 1U ) == 1U ) ?
                 "  <- deep power-down" : "  <- despierto" );

    xprintf( "  tras dormir: 0x%08lX  DEEPPWD=%u ADVREGEN=%u\r\n",
             ( unsigned long ) ulCrTrasDormir,
             ( unsigned ) ( ( ulCrTrasDormir >> ADC_CR_DEEPPWD_Pos  ) & 1U ),
             ( unsigned ) ( ( ulCrTrasDormir >> ADC_CR_ADVREGEN_Pos ) & 1U ) );

    xprintf( "  ADC_CCR    : 0x%08lX  VREFEN=%u TSEN=%u VBATEN=%u\r\n",
             ( unsigned long ) ulCcr,
             ( unsigned ) ( ( ulCcr >> 22 ) & 1U ),
             ( unsigned ) ( ( ulCcr >> 23 ) & 1U ),
             ( unsigned ) ( ( ulCcr >> 24 ) & 1U ) );

    xprintf( "\r\n  vin raw      cuentas crudas, sin convertir\r\n" );
    xprintf( "  vin on|off   el load switch del divisor, a mano\r\n" );
}

/*------------------------------------------------------------------------------
 * sd — la tarjeta microSD
 *
 * Buffer de un sector, estático: 512 bytes NO entran en el stack de tkCmd.
 *----------------------------------------------------------------------------*/
static uint8_t pucSector[ DRV_SD_SECTOR_BYTES ];

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

static void prvSdEstado( void )
{
    /* Con el riel apagado la detección no dice nada, y decir "vacia" sería
       inventar: el pin está en alta impedancia justamente para no gastar los
       82 µA del pull-up. Ver drv_sd.h. */
    xprintf( "  ranura      : %s\r\n",
             ( drv_sd_power_estado() == false ) ? "sin saber (riel apagado)" :
             ( drv_sd_presente() ? "TARJETA PRESENTE" : "vacia" ) );
    xprintf( "  riel        : %s  (EN_PWR_SD = PB3, 0 = PRENDE)\r\n",
             drv_sd_power_estado() ? "ENCENDIDO" : "apagado" );
    xprintf( "  tarjeta     : %s\r\n", drv_sd_tipo_texto() );

    if( drv_sd_tipo() != sdTIPO_NINGUNA )
    {
        uint32_t ulSectores = drv_sd_sectores();

        /* En MB para que el número sea legible: con 512 bytes por sector, cada
           2048 sectores es 1 MB. */
        xprintf( "  capacidad   : %lu sectores (%lu MB)\r\n",
                 ( unsigned long ) ulSectores,
                 ( unsigned long ) ( ulSectores / 2048UL ) );
    }

    xprintf( "  pwr locks   : 0x%08lX %s\r\n",
             ( unsigned long ) pwr_lock_estado(),
             pwr_deep_sleep_permitido() ? "(Stop 2 habilitado)" : "(solo Sleep)" );
}

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
        xprintf( "no hay tarjeta en la ranura (SD_DET en alto)\r\n" );
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
    /* ⚠ El criterio es `argv[N] != NULL` y NO el retorno de makeArgv(): esa
       función devuelve ARGUMENTOS, no tokens, y compararla contra un número ya
       hizo que tres subcomandos cayeran en su propia ayuda. */
    ( void ) FRTOS_CMD_makeArgv();

    if( argv[ 1 ] == NULL )
    {
        xprintf( "\r\nmicroSD (SPI3, CS por software en PA15)\r\n" );
        prvSdEstado();
        xprintf( "\r\n  sd on|off           energia de la tarjeta\r\n" );
        xprintf( "  sd init             prende e inicializa\r\n" );
        xprintf( "  sd info             CID y CSD crudos\r\n" );
        xprintf( "  sd read <sector>    vuelca un sector en hexa\r\n" );
        xprintf( "  sd test <sector>    ESCRIBE un patron y lo relee\r\n" );
        return;
    }

    if( strcmp( argv[ 1 ], "on" ) == 0 )
    {
        drv_sd_power( true );
        xprintf( "\r\nriel de la microSD ENCENDIDO (sin inicializar: 'sd init')\r\n" );
        return;
    }

    if( strcmp( argv[ 1 ], "off" ) == 0 )
    {
        drv_sd_power( false );
        xprintf( "\r\nriel de la microSD apagado\r\n" );
        return;
    }

    if( strcmp( argv[ 1 ], "init" ) == 0 )
    {
        xprintf( "\r\n" );

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

        xprintf( "\r\n" );

        if( prvSdListo() == false )
        {
            return;
        }

        if( drv_sd_cid( pucReg ) )
        {
            xprintf( "CID:\r\n" );
            prvSdVolcar( pucReg, 16U );

            /* Los campos legibles del CID: el nombre del producto son 5
               caracteres ASCII, y sirven para saber que se está leyendo bien —
               si sale basura, el problema es el enlace y no el parseo. */
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

    if( ( strcmp( argv[ 1 ], "read" ) == 0 ) && ( argv[ 2 ] != NULL ) )
    {
        uint32_t ulSector = ( uint32_t ) strtoul( argv[ 2 ], NULL, 0 );

        xprintf( "\r\n" );

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

    if( ( strcmp( argv[ 1 ], "test" ) == 0 ) && ( argv[ 2 ] != NULL ) )
    {
        uint32_t ulSector = ( uint32_t ) strtoul( argv[ 2 ], NULL, 0 );

        xprintf( "\r\n" );

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
           falso positivo es justo el que hay que evitar. */
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

    xprintf( "\r\nERROR: on | off | init | info | read <sector> | test <sector>\r\n" );
    xprintf( "  ATENCION: 'sd test' PISA el sector que se le indique.\r\n" );
    xprintf( "  El 0 es el MBR: usar un sector alto en una tarjeta con datos.\r\n" );
}

/*==============================================================================
 * cnt  -  el contador de pulsos CNT0 (PA12)
 *
 * ⚠ REDUCIDO respecto de la referencia a propósito: allá el comando trae además
 * `cnt log` y un bloque de caudal, que dependen de `caudal.{h,c}`,
 * `caudal_log.{h,c}`, `fs_sd` y la configuración del contador — o sea de la capa
 * de aplicación, que en este firmware todavía no existe. Lo que queda es
 * exactamente lo que se puede validar contra el hardware poblado.
 *============================================================================*/

static void prvCntEstado( void )
{
    drv_pulsos_cfg_t xCfg = { 0 };

    drv_pulsos_config( &xCfg );

    xprintf( "  total      : %lu pulsos desde el arranque\r\n",
             ( unsigned long ) drv_pulsos_total() );
    xprintf( "  pendientes : %lu (los que se llevaria 'cnt tomar')\r\n",
             ( unsigned long ) drv_pulsos_pendientes() );
    xprintf( "  pin PA12   : %s  ->  contacto %s\r\n",
             drv_pulsos_nivel_pin() ? "alto" : "BAJO",
             drv_pulsos_nivel_pin() ? "abierto (reposo)" : "CERRADO" );

    /* ⭐ El pull TIENE que decir flotante, y por eso se imprime siempre: un
       pull-down acá cuesta 82 µA las 24 horas —el reposo del pin es el nivel
       alto— y es lo que una regeneración de CubeMX podría meter sin avisar. Es
       la misma trampa que costó 89 µA con SD_DET, y no tiene ningún otro
       síntoma salvo la autonomía. */
    xprintf( "  config     : modo %lu (0=entrada), pull %lu (%s)\r\n",
             ( unsigned long ) xCfg.ulModer, ( unsigned long ) xCfg.ulPupdr,
             ( xCfg.ulPupdr == 0UL ) ? "flotante, CORRECTO"
                                     : "OJO: NO deberia tener pull" );
}
//------------------------------------------------------------------------------
static void cmdCnt( void )
{
    /* El criterio es `argv[N] != NULL` y no el retorno de makeArgv() — ver la
       nota en cmdSd(). */
    ( void ) FRTOS_CMD_makeArgv();

    if( argv[ 1 ] == NULL )
    {
        xprintf( "\r\ncontador de pulsos CNT0 (PA12, flanco de BAJADA)\r\n" );
        prvCntEstado();
        xprintf( "\r\n  cnt watch [seg]  cuenta durante N segundos (10 por omision)\r\n" );
        xprintf( "  cnt tomar        devuelve los pendientes y los DESCUENTA\r\n" );
        xprintf( "  cnt reset        pone los dos contadores en cero\r\n" );
        xprintf( "\r\n" );
        xprintf( "  El pulso se cuenta al CERRAR el contacto. En reposo esta\r\n" );
        xprintf( "  abierto y el pin en alto. El antirrebote es de hardware\r\n" );
        xprintf( "  (RC de 4K7/1uF + Schmitt): admite ~30 Hz y filtra todo lo\r\n" );
        xprintf( "  que dure menos de ~5 ms.\r\n" );
        return;
    }

    if( strcmp( argv[ 1 ], "reset" ) == 0 )
    {
        drv_pulsos_reset();
        xprintf( "\r\ncontadores en cero\r\n" );
        return;
    }

    if( strcmp( argv[ 1 ], "tomar" ) == 0 )
    {
        xprintf( "\r\ntomados %lu pulsos (quedan 0 pendientes)\r\n",
                 ( unsigned long ) drv_pulsos_tomar() );
        return;
    }

    if( strcmp( argv[ 1 ], "watch" ) == 0 )
    {
        uint32_t ulSeg = 10UL;
        uint32_t ulIni;
        uint32_t ulN;
        uint32_t ulMiliHz;

        if( argv[ 2 ] != NULL )
        {
            ulSeg = ( uint32_t ) atoi( argv[ 2 ] );
        }

        if( ( ulSeg == 0UL ) || ( ulSeg > 600UL ) )
        {
            xprintf( "\r\nERROR: la ventana va de 1 a 600 segundos\r\n" );
            return;
        }

        ulIni = drv_pulsos_total();

        xprintf( "\r\ncontando %lu s...\r\n", ( unsigned long ) ulSeg );
        vTaskDelay( pdMS_TO_TICKS( ulSeg * 1000UL ) );

        ulN = drv_pulsos_total() - ulIni;

        /* La frecuencia en mHz y con enteros: un comando de diagnóstico no
           depende del `-u _printf_float` del .cproject, que una reconfiguración
           del proyecto puede perder. Mismo criterio que el comando `ina`. */
        ulMiliHz = ( ulN * 1000UL ) / ulSeg;

        xprintf( "  %lu pulsos en %lu s  ->  %lu.%03lu Hz\r\n",
                 ( unsigned long ) ulN, ( unsigned long ) ulSeg,
                 ( unsigned long ) ( ulMiliHz / 1000UL ),
                 ( unsigned long ) ( ulMiliHz % 1000UL ) );
        return;
    }

    xprintf( "\r\ncnt: subcomando desconocido '%s'\r\n", argv[ 1 ] );
}

/*==============================================================================
 * ev  -  la electrovalvula TOYI (PA6 = energia, PA7 = direccion)
 *
 * ⚠ Es un SERVO, no una biestable: mientras esta alimentada se mueve hacia donde
 * diga CTL y al llegar al tope se queda ahi. El firmware solo le da tiempo.
 *
 * ⚠ Y el estado es una CREENCIA, no una medicion: no hay realimentacion de
 * posicion en la placa. Lo que informa el driver es el ultimo comando que
 * ejecuto, y por eso 'ev' dice ASUMIDO mientras no se haya movido nunca.
 *============================================================================*/

static void prvEvEstado( void )
{
    xprintf( "  estado     : %s%s\r\n",
             ( drv_valvula_estado() == valvulaABIERTA ) ? "ABIERTA" : "CERRADA",
             drv_valvula_estado_asumido() ? "  (ASUMIDO: todavia no se movio)" : "" );
    xprintf( "  movimientos: %lu desde el arranque\r\n",
             ( unsigned long ) drv_valvula_movimientos() );
    xprintf( "  PA6 pwr    : %s\r\n",
             drv_valvula_pin_pwr_estado() ? "[!] 1 - servo ALIMENTADO"
                                          : "0 - apagado (reposo)" );
    xprintf( "  PA7 ctl    : %s\r\n",
             drv_valvula_pin_ctl_estado() ? "1 - abrir" : "0 - cerrar (reposo)" );
}
//------------------------------------------------------------------------------
static void cmdEv( void )
{
    ( void ) FRTOS_CMD_makeArgv();

    if( argv[ 1 ] == NULL )
    {
        xprintf( "\r\nelectrovalvula TOYI (servo, sin realimentacion de posicion)\r\n" );
        prvEvEstado();
        xprintf( "\r\n  ev abrir | cerrar   mueve la valvula (%u s con el motor energizado)\r\n",
                 ( unsigned ) ( DRV_VALVULA_MS_RECORRIDO / 1000U ) );
        xprintf( "  ev pwr on|off       EN_EV_TOYI (PA6) a mano\r\n" );
        xprintf( "  ev ctl on|off       CTL_EV_TOYI (PA7) a mano: 1=abrir, 0=cerrar\r\n" );
        xprintf( "\r\n" );
        xprintf( "  [!] 'pwr' y 'ctl' saltean la secuencia y el mutex: son para\r\n" );
        xprintf( "      medir con el tester, NO para mover la valvula.\r\n" );
        return;
    }

    if( ( strcmp( argv[ 1 ], "abrir" ) == 0 ) ||
        ( strcmp( argv[ 1 ], "cerrar" ) == 0 ) )
    {
        bool bAbrir = ( argv[ 1 ][ 0 ] == 'a' );

        xprintf( "\r\n%s la valvula, %u s...\r\n",
                 bAbrir ? "abriendo" : "cerrando",
                 ( unsigned ) ( DRV_VALVULA_MS_RECORRIDO / 1000U ) );

        if( bAbrir ? drv_valvula_abrir() : drv_valvula_cerrar() )
        {
            xprintf( "  hecho: valvula %s\r\n", bAbrir ? "ABIERTA" : "CERRADA" );
        }
        else
        {
            /* El segundo en llegar recibe false en vez de encolarse: encolar
               movimientos de una valvula no significa nada, y dos solapados
               serian CTL cambiando con el motor energizado. */
            xprintf( "  ERROR: hay otro movimiento en curso\r\n" );
        }
        return;
    }

    if( ( strcmp( argv[ 1 ], "pwr" ) == 0 ) && ( argv[ 2 ] != NULL ) )
    {
        bool bOn = ( strcmp( argv[ 2 ], "on" ) == 0 );

        drv_valvula_pin_pwr( bOn );
        xprintf( "\r\nEN_EV_TOYI (PA6) = %s\r\n",
                 bOn ? "1 (servo ALIMENTADO)" : "0 (apagado)" );
        return;
    }

    if( ( strcmp( argv[ 1 ], "ctl" ) == 0 ) && ( argv[ 2 ] != NULL ) )
    {
        bool bOn = ( strcmp( argv[ 2 ], "on" ) == 0 );

        drv_valvula_pin_ctl( bOn );
        xprintf( "\r\nCTL_EV_TOYI (PA7) = %s\r\n", bOn ? "1 (abrir)" : "0 (cerrar)" );
        return;
    }

    xprintf( "\r\nev: subcomando desconocido '%s'\r\n", argv[ 1 ] );
}

/*------------------------------------------------------------------------------
 * cls / clear  -  limpia la pantalla de la terminal
 *
 * Son dos secuencias ANSI, las mismas que usaba FWDLGX:
 *
 *   ESC [ 2 J   borra toda la pantalla
 *   ESC [ H     manda el cursor a 1,1
 *
 * ⚠ El orden importa: `[2J` borra pero NO mueve el cursor, así que sin el `[H`
 * el prompt saldría en la fila donde hubiera quedado y la pantalla se vería
 * vacía por arriba.
 *
 * ⚠ Esto lo interpreta la TERMINAL, no el equipo. Con minicom, picocom o cualquier
 * emulador ANSI anda; si alguna vez se engancha un capturador que no interprete
 * escapes, va a ver los cinco caracteres crudos en el log. Es inofensivo y es el
 * precio de que sea sólo texto.
 *----------------------------------------------------------------------------*/
static void cmdCls( void )
{
    xputChar( 0x1B ); xprintf( "[2J" );     /* borrar toda la pantalla */
    xputChar( 0x1B ); xprintf( "[H"  );     /* cursor a 1,1            */
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

    /* Deja los tres rieles apagados y la recepción del 485 armada. Va después
       de drv_uart_init() —que lo hace frtos_open_all()— porque necesita que la
       instancia del USART3 ya exista. */
    if( drv_rs485_init() == false )
    {
        xprintf( "\r\n[!] el RS485 no se pudo inicializar\r\n" );
    }

    /* ⭐ Deja el riel apagado Y el pull-up de SD_DET fuera. Eso segundo es lo que
       importa: con la tarjeta puesta, el pull-up interno contra el contacto
       cerrado a GND son 82 µA las 24 horas — ver drv_sd.h. */
    ( void ) drv_sd_init();

    /* Pone los contadores en cero y descarta un flanco que hubiera quedado
       latcheado antes de que el NVIC se habilitara: sin esto el primer pulso del
       equipo sería uno que nunca ocurrió. El pin y la EXTI los configura
       MX_GPIO_Init(), como todo el resto. */
    drv_pulsos_init();

    /* Deja los dos pines en reposo —sin alimentar y con la direccion en
       "cerrar"— y crea el mutex. ⚠ NO mueve la valvula: en que condiciones
       conviene moverla al energizar el equipo es politica de la capa de
       aplicacion, y un cierre automatico serian 5 s de motor en CADA reset,
       incluidos los diez seguidos de una sesion de flasheo. */
    drv_valvula_init();

#if ( TKCMD_ADC_INIT == 1 )
    /* ⚠ Calibra el ADC y lo deja en deep power-down. La calibración es
       OBLIGATORIA en el STM32L4: sin ella el offset de varias cuentas se
       multiplica por 6,6 al volver a la tensión del riel de 12 V. */
    if( drv_adc_init() == false )
    {
        prvTxPoleo( "ADC: ERROR de calibracion\r\n" );
    }
#else
    prvTxPoleo( "ADC: SIN INICIALIZAR (TKCMD_ADC_INIT=0, bisect de consumo)\r\n" );
#endif
#if ( TKCMD_ADC_INIT == 1 )
    /*
     * ⭐ Los tres momentos del init, por POLEO y en el arranque: es el único
     * estado que importa para el reposo, porque en campo nadie corre `vin`.
     * Sale acá y no en `status` para que se lea ANTES de tipear nada.
     */
    {
        char cBuf[ 72 ];

        for( uint32_t i = 0U; i < 3U; i++ )
        {
            static const char *pcMomento[ 3 ] =
                { "tras MX_ADC1_Init ", "tras CALIBRAR     ", "tras dormirlo     " };

            ( void ) snprintf( cBuf, sizeof( cBuf ),
                               "ADC %s CR=0x%08lX CCR=0x%08lX\r\n",
                               pcMomento[ i ],
                               ( unsigned long ) ulDiagCr[ i ],
                               ( unsigned long ) ulDiagCcr[ i ] );
            prvTxPoleo( cBuf );
        }
    }
#endif

    if( drv_ina_init() == false )
    {
        xprintf( "\r\n[!] el INA3221 no contesta o no se identifico\r\n" );
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
    FRTOS_CMD_register( "ina",    cmdIna    );
    FRTOS_CMD_register( "rs485",  cmdRs485  );
    FRTOS_CMD_register( "sd",     cmdSd     );
    FRTOS_CMD_register( "vin",    cmdVin    );
    FRTOS_CMD_register( "cnt",    cmdCnt    );
    FRTOS_CMD_register( "ev",     cmdEv     );
    /* Los dos nombres a la misma función: el parser exige el comando COMPLETO,
       así que no se puede abreviar uno en el otro y cuesta un slot de los 32. */
    FRTOS_CMD_register( "cls",    cmdCls    );
    FRTOS_CMD_register( "clear",  cmdCls    );
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
