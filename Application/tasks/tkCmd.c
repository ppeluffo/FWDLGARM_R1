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
#include "drv_modbus.h"
#include "drv_cpres.h"
#include "tkCtlPres.h"
#include "modbus.h"
#include "drv_ina3221.h"
#include "drv_sd.h"
#include "drv_adc.h"
#include "drv_pulsos.h"
#include "drv_valvula.h"
#include "drv_lte.h"
#include "cfg_nvm.h"
#include "cfg_hash.h"
#include "tkCtl.h"
#include "tkSys.h"
#include "wan_frame.h"
#include "fs_datos.h"
#include "fs_sd.h"
#include "tkWan.h"
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
static void cmdModbus( void );
static void cmdCpres( void );
static void cmdIna( void );
static void cmdSd( void );
static void cmdVin( void );
static void cmdCnt( void );
static void cmdEv( void );
static void cmdLte( void );
static void cmdConfig( void );
static void cmdKill( void );
static void cmdPoll( void );
static void cmdFrame( void );
static void cmdCls( void );
static void cmdFs( void );
static void prvFsUso( void );
static void cmdKeys( void );
static void cmdReset( void );

/* Ayudas detalladas de cada comando. Se definen junto a su comando, más abajo. */
static void prvI2cUso  ( void );
static void prvEeUso   ( void );
static void prvRtcUso  ( void );
static void prvRs485Uso( void );
static void prvModbusUso( void );
static void prvCpresUso( void );
static void prvEstadoTarea( const char *pcNombre, TaskHandle_t xHandle,
                            uint16_t usStack, bool bKillPedido,
                            const char *pcExtra );
static void prvInaUso  ( void );
static void prvSdUso   ( void );
static void prvVinUso  ( void );
static void prvCntUso  ( void );
static void prvEvUso   ( void );
static void prvLteUso  ( void );
static void prvConfigUso( void );
static void prvKillUso( void );

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

uint8_t wan_causa_reset( void )
{
    /*
     * Las banderas son ACUMULATIVAS hasta que alguien escribe `RMVF`, así que
     * puede haber varias puestas a la vez. Se informa **la más específica**, en
     * orden de menos a más común: un reset por watchdog dice mucho más que el
     * `PIN` que suele venir junto con él.
     */
    if( ulCausaReset & RCC_CSR_IWDGRSTF ) { return ( uint8_t ) wanRESET_IWDG; }
    if( ulCausaReset & RCC_CSR_WWDGRSTF ) { return ( uint8_t ) wanRESET_WWDG; }
    if( ulCausaReset & RCC_CSR_LPWRRSTF ) { return ( uint8_t ) wanRESET_LPWR; }
    if( ulCausaReset & RCC_CSR_SFTRSTF  ) { return ( uint8_t ) wanRESET_SOFT; }
    if( ulCausaReset & RCC_CSR_BORRSTF  ) { return ( uint8_t ) wanRESET_BOR;  }
    if( ulCausaReset & RCC_CSR_PINRSTF  ) { return ( uint8_t ) wanRESET_PIN;  }

    return ( uint8_t ) wanRESET_NINGUNO;
}
//------------------------------------------------------------------------------
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

    /* Deja el servo sin alimentar y la dirección en "cerrar". NO la mueve: el
       cierre de arranque va más abajo, cuando ya hay consola para contarlo. */
    drv_valvula_init();

    /* Deja los dos rieles del modem cortados y el PWRKEY suelto. */
    drv_lte_init();

    /*
     * La configuración, de la EEPROM. Va acá y no antes porque necesita el bus
     * I2C, que se levanta más arriba en esta misma función.
     *
     * Un bloque con checksum malo NO impide arrancar: cae a sus valores por
     * defecto y lo dice por consola. En un equipo desatendido es preferible
     * medir con la configuración de fábrica que no arrancar — y el aviso queda
     * en el log para quien lo lea.
     */
    if( !cfg_nvm_load_all() )
    {
        xprintf( "CFG:: [!] hubo bloques con defaults, revisar con 'config'\r\n" );
    }

    /* El almacén de registros. Va después de la configuración porque comparte
       la EEPROM con ella, y necesita el RTC para la FAT. */
    ( void ) fs_datos_init();

    FRTOS_CMD_init();
    FRTOS_CMD_register( "help",   cmdHelp   );
    FRTOS_CMD_register( "status", cmdStatus );
    FRTOS_CMD_register( "sense",  cmdSense  );
    FRTOS_CMD_register( "i2c",    cmdI2c    );
    FRTOS_CMD_register( "ee",     cmdEe     );
    FRTOS_CMD_register( "rtc",    cmdRtc    );
    FRTOS_CMD_register( "rs485",  cmdRs485  );
    FRTOS_CMD_register( "modbus", cmdModbus );
    FRTOS_CMD_register( "cpres",  cmdCpres  );
    FRTOS_CMD_register( "ina",    cmdIna    );
    FRTOS_CMD_register( "sd",     cmdSd     );
    FRTOS_CMD_register( "vin",    cmdVin    );
    FRTOS_CMD_register( "cnt",    cmdCnt    );
    FRTOS_CMD_register( "ev",     cmdEv     );
    FRTOS_CMD_register( "lte",    cmdLte    );
    FRTOS_CMD_register( "config", cmdConfig );
    FRTOS_CMD_register( "kill",   cmdKill   );
    FRTOS_CMD_register( "poll",   cmdPoll   );
    FRTOS_CMD_register( "frame",  cmdFrame  );
    FRTOS_CMD_register( "cls",    cmdCls    );
    FRTOS_CMD_register( "fs",     cmdFs     );
    FRTOS_CMD_register( "keys",   cmdKeys   );
    FRTOS_CMD_register( "reset",  cmdReset  );

    /* La versión y la fecha de compilación en el banner, no sólo en 'status':
       es lo primero que uno quiere ver al enchufar la terminal, y contesta sin
       tipear nada la pregunta de si quedó flasheado el binario que se creía. */
    xprintf( "\r\n\r\n%s %s - consola TERM\r\n", FW_NOMBRE, FW_VERSION );
    xprintf( "compilado %s\r\n", FW_FECHA );
    prvImprimirCausaReset();

    /*
     * ⚠ Acá NO se mueve la válvula, y es a propósito (decidido por Pablo el
     * 2026-08-18).
     *
     * La política de arranque —asumir ABIERTA y mandar un cierre— es correcta,
     * pero es de la capa de APLICACIÓN: depende de en qué condiciones conviene
     * mover una válvula al energizar el equipo, y eso todavía no está definido.
     * Ponerla acá significaría 5 segundos de motor en cada reset, incluidos los
     * diez seguidos de una sesión de flasheo y los espurios que pueda meter el
     * watchdog cuando exista.
     *
     * Mientras tanto el driver arranca con el estado en "ABIERTA asumida", que
     * `drv_valvula_estado_asumido()` deja ver, y la válvula se mueve sólo cuando
     * alguien lo pide.
     */
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
    { "modbus", "Modbus RTU: poleo de canales y lectura generica",        prvModbusUso  },
    { "cpres",  "control de presion: consignas y valvulas externas",      prvCpresUso   },
    { "ina",    "INA3221: medida de los lazos de 4-20 mA",               prvInaUso     },
    { "sd",     "tarjeta microSD: energia, arranque y sectores",         prvSdUso      },
    { "vin",    "tension de los rieles: 12 V y VDDA (3V3)",              prvVinUso     },
    { "cnt",    "contador de pulsos CNT0 (PA12): cuenta y estado",       prvCntUso     },
    { "ev",     "electrovalvula TOYI: abrir, cerrar y estado",           prvEvUso      },
    { "lte",    "modem LTE, etapa 1: la energia y el PWRKEY",            prvLteUso     },
    { "config", "configuracion del datalogger (EEPROM)",                 prvConfigUso  },
    { "kill",   "mata una tarea para trabajar su periferico a mano",     prvKillUso    },
    { "poll",   "fuerza un poleo de todos los canales y lo imprime",    NULL          },
    { "frame",  "arma el frame de datos y lo muestra (sin modem)",      NULL          },
    { "cls",    "limpia la pantalla de la terminal",                    NULL          },
    { "fs",     "memoria de registros: estado, lectura y formateo",     prvFsUso      },
    { "keys",   "muestra el codigo crudo de cada tecla (diagnostico)",   NULL          },
    { "reset",  "reset por NVIC_SystemReset (pulsa NRST)",               NULL          },
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

        /*
         * ⛔ Acá decía "matchea por prefijo: 'res'/'reb', 'st'/'se'…" y **era
         * mentira desde el 2026-09-08**, cuando el parser pasó a exigir el
         * comando completo. Una ayuda que miente es peor que no tener ayuda: el
         * que la lee prueba 'st', no funciona, y termina dudando de la consola.
         */
        xprintf( "\r\n  el comando va COMPLETO: 'status', no 'st'\r\n" );
        return;
    }

    /* ---- 'help <comando>' ---- */
    size_t xLargo = strlen( argv[ 1 ] );

    for( uint32_t i = 0U; i < AYUDA_COUNT; i++ )
    {
        /* Igualdad, no prefijo: la misma regla que usa el parser de comandos
           desde el 2026-09-08. Con prefijo, 'help c' caería en el primero que
           empiece con c y el que pregunta no sabría por qué. */
        if( ( strlen( xAyuda[ i ].pcNombre ) == xLargo ) &&
            ( strncmp( xAyuda[ i ].pcNombre, argv[ 1 ], xLargo ) == 0 ) )
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
    /* Los tres campos tal cual viajan en el frame: así se verifica de un vistazo
       con qué identidad se va a presentar el equipo ante el servidor. */
    xprintf( "identidad    : HW=%s TYPE=%s VER=%s\r\n", FW_HW, FW_TYPE, FW_VERSION );
    xprintf( "proximo poleo: en %lu s (timerpoll = %u s)\r\n",
             ( unsigned long ) tkSys_segundos_al_proximo(),
             ( unsigned ) xCfgBase.usTimerPoll );
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
    /*
     * ⚠ El *high water mark* es el MÍNIMO que quedó libre desde que arrancó la
     * tarea, no lo que hay libre ahora: es la marca del peor momento, que es lo
     * único que sirve para dimensionar.
     *
     * ⚠ **Estos números son de Debug (`-O0`), que usa más stack que `-Os`.** Son
     * conservadores para Release —el cambio va en la dirección segura— pero no
     * son los que van a valer: antes de campo hay que rehacer la medición sobre
     * un binario Release.
     */
    xprintf( "tareas (stack libre minimo en palabras, y estado):\r\n" );
    xprintf( "  tkCmd  : %4u de %-5u activa (soy yo)\r\n",
             ( unsigned ) uxTaskGetStackHighWaterMark( NULL ), tkCmd_STACK_SIZE );

    prvEstadoTarea( "tkCtl", xHandle_tkCtl, tkCtl_STACK_SIZE, false, NULL );
    prvEstadoTarea( "tkSys", xHandle_tkSys, tkSys_STACK_SIZE, false, NULL );
    prvEstadoTarea( "tkWan", xHandle_tkWan, tkWan_STACK_SIZE, wan_matada(),
                    wan_estado_str() );
    prvEstadoTarea( "tkCPres", xHandle_tkCtlPres, tkCtlPres_STACK_SIZE,
                    tkCtlPres_matada(), NULL );

    /*
     * ---- La memoria de registros ----
     *
     * ⭐ Pedido de Pablo (2026-09-22). No es un adorno: es **cuánto aguanta el
     * equipo sin transmitir**. Con la ventana llena se empiezan a pisar los
     * registros más viejos, y saberlo de un vistazo —sin tener que correr
     * `fs`— es lo que dice si un equipo viene teniendo problemas de enlace.
     */
    fs_datos_stats_t xFs;
    fs_sd_stats_t    xSd;

    fs_datos_stats( &xFs );

    xprintf( "memoria de registros (la ventana, en la EEPROM):\r\n" );
    xprintf( "  ocupados : %u de %u", ( unsigned ) xFs.usCount,
             ( unsigned ) xFs.usLength );

    if( xFs.usLength > 0U )
    {
        xprintf( "  (%u%%)", ( unsigned ) ( ( 100UL * xFs.usCount ) / xFs.usLength ) );
    }

    xprintf( "\r\n  libres   : %u\r\n",
             ( unsigned ) ( xFs.usLength - xFs.usCount ) );

    /*
     * ⚠ Los PISADOS se informan sólo si los hay, pero cuando los hay importan
     * mucho: son registros que se perdieron porque la ventana dio la vuelta sin
     * poder transmitir ni volcar a la microSD. Es información de campo, no un
     * contador de diagnóstico.
     */
    if( xFs.ulPisados > 0UL )
    {
        xprintf( "  [!] PISADOS: %lu registros perdidos por ventana llena\r\n",
                 ( unsigned long ) xFs.ulPisados );
    }

    /* Los lotes de la microSD salen de la FAT en la SRAM del RTC, así que se
       saben sin encender la tarjeta. */
    fs_sd_stats( &xSd );

    if( xSd.usLotes > 0U )
    {
        xprintf( "  lotes en la microSD sin transmitir: %u\r\n",
                 ( unsigned ) xSd.usLotes );
    }
}
//------------------------------------------------------------------------------
/*
 * Una línea por tarea: stack libre y **si está viva o matada**.
 *
 * ⭐ Pedido de Pablo (2026-09-22), y hace falta: después de un `kill` no había
 * forma de confirmar que la tarea se había suspendido de verdad — y el `kill`
 * es cooperativo, así que entre pedirlo y que ocurra pasa hasta una vuelta
 * entera de esa tarea.
 *
 * Por eso se informan los dos estados por separado: `eTaskGetState()` dice si ya
 * se suspendió, y la bandera del módulo dice si está pedido pero todavía no
 * ocurrió. Confundirlos haría creer que se puede tocar el periférico cuando la
 * tarea todavía lo está usando.
 */
static void prvEstadoTarea( const char *pcNombre, TaskHandle_t xHandle,
                            uint16_t usStack, bool bKillPedido,
                            const char *pcExtra )
{
    const char *pcEstado;

    if( xHandle == NULL )
    {
        xprintf( "  %-7s: NO EXISTE\r\n", pcNombre );
        return;
    }

    if( eTaskGetState( xHandle ) == eSuspended )
    {
        pcEstado = "MATADA";
    }
    else if( bKillPedido )
    {
        pcEstado = "kill PEDIDO (todavia corriendo)";
    }
    else
    {
        pcEstado = "activa";
    }

    xprintf( "  %-7s: %4u de %-5u %s", pcNombre,
             ( unsigned ) uxTaskGetStackHighWaterMark( xHandle ),
             ( unsigned ) usStack, pcEstado );

    if( pcExtra != NULL )
    {
        xprintf( " (%s)", pcExtra );
    }

    xprintf( "\r\n" );
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
        /*
         * ⚠ Esta lista es lo que uno mira cuando FALTA un chip, así que tiene
         * que decir la verdad. Estaba desactualizada: hablaba de una M24M02 en
         * 50..53 —el chip es una M24M01 y contesta sólo en 50/51, ver CLAUDE.md—
         * y no mencionaba ni la identification page ni el INA3221. El 2026-09-08
         * el INA no contestó y la lista no ayudaba a notarlo.
         */
        xprintf( "esperados en R001: 50,51  = EEPROM M24M01 128KB (dev A0/A2)\r\n" );
        xprintf( "                   58,59  = M24M01 identification page (solo lectura!)\r\n" );
        xprintf( "                   41     = INA3221, medida de 4-20 mA\r\n" );
        xprintf( "                   6f     = MCP79410 RTCC (dev DE)\r\n" );
        xprintf( "                   57     = MCP79410 EEPROM interna (dev AE)\r\n" );
        xprintf( "son 7 en total; si falta alguno, el chip no esta en el bus.\r\n" );
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
/*------------------------------------------------------------------------------
 * Los comandos de diagnóstico del modem.
 *
 * ⚠ La SESIÓN —`ping`, `conf`, `data`— **no está acá**: vive en `tkWan.c`, y
 * estos comandos la disparan. Se movió al escribir la FSM (paso 5d) para que
 * sean el mismo código y no dos copias, igual que `poll` con `tkSys_poll()`.
 *----------------------------------------------------------------------------*/

/* 256 alcanza para cualquier respuesta AT y para un bloque de datos del puente.
   Es estático: 256 bytes en el stack de tkCmd, que tiene 2 KB, no entran. */
#define LTE_BUF             256U

/* Techo para que el módulo empiece a contestar. Un AT simple contesta en
   milisegundos; los comandos de red tardan mucho más, y para ésos está 'lte rx'
   con el tiempo explícito. */
#define LTE_ESCUCHA_MS      1000U

//------------------------------------------------------------------------------
static void prvEvUso( void )
{
    xprintf( "uso:\r\n" );
    xprintf( "  ev                 estado que cree el driver y nivel de los pines\r\n" );
    xprintf( "  ev abrir           movimiento completo de apertura (bloquea 5 s)\r\n" );
    xprintf( "  ev cerrar          movimiento completo de cierre   (bloquea 5 s)\r\n" );
    xprintf( "  ev pwr on|off      SOLO el load switch EN_EV_TOYI (PA6)\r\n" );
    xprintf( "  ev ctl on|off      SOLO la direccion CTL_EV_TOYI (PA7): on=abrir\r\n" );
    xprintf( "\r\n" );
    xprintf( "  La valvula NO tiene realimentacion: el estado es el ultimo comando\r\n" );
    xprintf( "  ejecutado, no una medicion. Al arrancar se asume ABIERTA y se manda\r\n" );
    xprintf( "  un cierre.\r\n" );
    xprintf( "\r\n" );
    xprintf( "  'pwr' y 'ctl' saltean la secuencia y son SOLO para el banco: dejan\r\n" );
    xprintf( "  el estado que informa el driver diciendo cualquier cosa.\r\n" );
}

static void cmdEv( void )
{
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    if( ( ucArgs >= 1U ) && ( argv[ 1 ] != NULL ) )
    {
        if( ( strcmp( argv[ 1 ], "abrir" ) == 0 ) ||
            ( strcmp( argv[ 1 ], "cerrar" ) == 0 ) )
        {
            bool bAbrir = ( argv[ 1 ][ 0 ] == 'a' );

            xprintf( "%s la valvula, %u s...\r\n",
                     bAbrir ? "abriendo" : "cerrando",
                     ( unsigned ) ( DRV_VALVULA_MS_RECORRIDO / 1000U ) );

            if( bAbrir ? drv_valvula_abrir() : drv_valvula_cerrar() )
            {
                xprintf( "  hecho: valvula %s\r\n", bAbrir ? "ABIERTA" : "CERRADA" );
            }
            else
            {
                xprintf( "  ERROR: hay otro movimiento en curso\r\n" );
            }
            return;
        }

        if( strcmp( argv[ 1 ], "pwr" ) == 0 )
        {
            if( ( ucArgs >= 2U ) && ( argv[ 2 ] != NULL ) )
            {
                bool bOn = ( strcmp( argv[ 2 ], "on" ) == 0 );

                drv_valvula_pin_pwr( bOn );
                xprintf( "EN_EV_TOYI (PA6) = %s\r\n", bOn ? "1 (servo ALIMENTADO)" : "0 (apagado)" );
                return;
            }
        }

        if( strcmp( argv[ 1 ], "ctl" ) == 0 )
        {
            if( ( ucArgs >= 2U ) && ( argv[ 2 ] != NULL ) )
            {
                bool bOn = ( strcmp( argv[ 2 ], "on" ) == 0 );

                drv_valvula_pin_ctl( bOn );
                xprintf( "CTL_EV_TOYI (PA7) = %s\r\n", bOn ? "1 (abrir)" : "0 (cerrar)" );
                return;
            }
        }

        prvEvUso();
        return;
    }

    /* ---- 'ev' pelado ---- */
    xprintf( "  estado     : %s%s\r\n",
             ( drv_valvula_estado() == valvulaABIERTA ) ? "ABIERTA" : "CERRADA",
             drv_valvula_estado_asumido() ? "  (ASUMIDO: todavia no se movio)" : "" );
    xprintf( "  movimientos: %lu desde el arranque\r\n",
             ( unsigned long ) drv_valvula_movimientos() );
    xprintf( "  PA6 pwr    : %s\r\n",
             drv_valvula_pin_pwr_estado() ? "1 - servo ALIMENTADO" : "0 - apagado (reposo)" );
    xprintf( "  PA7 ctl    : %s\r\n",
             drv_valvula_pin_ctl_estado() ? "1 - abrir" : "0 - cerrar (reposo)" );
}


static int16_t prvLteEscuchar( uint32_t ulMs );
static void    prvLteBridge  ( void );

static void prvLteUso( void )
{
    xprintf( "uso:\r\n" );
    xprintf( "  lte                 estado\r\n" );
    xprintf( "  lte on | off        energia del modem (no toca el power switch)\r\n" );
    xprintf( "  lte key on|off      nivel de LTE_PWR (PA5). on = apretado\r\n" );
    xprintf( "  lte key <ms>        pulso de <ms> y lo suelta\r\n" );
    xprintf( "  lte esc             ENTRA AL MODO COMANDO (+++ / a / a / +ok)\r\n" );
    xprintf( "  lte info            como esta configurado el MODULO (lo lee de el)\r\n" );
    xprintf( "  lte clock [set]     la hora del modulo (NTP); 'set' pone en hora el RTC\r\n" );
    xprintf( "  lte exit            SALE del modo comando, vuelve a transparente\r\n" );
    xprintf( "  lte ping            manda un PING al servidor (en modo TRANSPARENTE)\r\n" );
    xprintf( "  lte conf            CONF_ALL + los CONF_* que pida, y los APLICA\r\n" );
    xprintf( "  lte data            vacia la ventana: transmite y borra lo confirmado\r\n" );
    xprintf( "  lte at <cmd>        manda <cmd>+CR y muestra la respuesta\r\n" );
    xprintf( "  lte tx <texto>      manda el texto CRUDO, sin CR, y escucha\r\n" );
    xprintf( "  lte rx <ms>         solo escucha\r\n" );
    xprintf( "  lte bridge          puente terminal <-> modem (Ctrl-D para salir)\r\n" );
    xprintf( "\r\n" );
    xprintf( "  El modem arranca en modo TRANSPARENTE y no entiende AT hasta que se\r\n" );
    xprintf( "  hace 'lte esc'. Mandar '+++' con 'tx' NO alcanza: la secuencia son 3\r\n" );
    xprintf( "  tiempos y el 2do no da tiempo a tipearlo. Para salir: 'lte at AT+ENTM'\r\n" );
    xprintf( "\r\n" );
    xprintf( "  'at' y 'tx' toman UNA palabra: el parser corta en los espacios. Para\r\n" );
    xprintf( "  cualquier comando con espacios, usar 'bridge'.\r\n" );
    xprintf( "\r\n" );
    xprintf( "  La fuente es en CASCADA: EN_LTE_DCIN (PC13) enciende el load switch\r\n" );
    xprintf( "  y de el cuelgan las dos ramas, DCIN (JP1) y el convertidor de 3V8\r\n" );
    xprintf( "  (JP2), que son excluyentes. Por eso hay un solo comando de energia y\r\n" );
    xprintf( "  PA4 quedo sin funcion. Al apagar, el QOD tarda ~0,5 s en descargar\r\n" );
    xprintf( "  C1 (470 uF) con JP1: cortar y reponer enseguida NO resetea nada.\r\n" );
    xprintf( "\r\n" );
    xprintf( "  CONFIGURACION del modulo (la IP y el puerto viven en EL, no aca):\r\n" );
    xprintf( "    lte set server <ip> <puerto>    ej: lte set server 192.168.0.20 5000\r\n" );
    xprintf( "    lte set apn <apn>               ej: lte set apn SPYMOVIL.VPNANTEL\r\n" );
    xprintf( "    lte set url <url>               ej: lte set url /apidlg?\r\n" );
    xprintf( "    lte set httpd                   modo de trabajo HTTPD\r\n" );
    xprintf( "    lte save                        GRABA y reinicia el modulo\r\n" );
    xprintf( "\r\n" );
    xprintf( "  ⚠ 'set', 'info' y 'save' ASUMEN el modo comando: NO entran solos.\r\n" );
    xprintf( "    La secuencia es:  lte esc  ->  configurar  ->  lte save\r\n" );
    xprintf( "  Y NADA queda grabado hasta 'lte save'.\r\n" );
}
//------------------------------------------------------------------------------
/*
 * Manda un comando AT de configuración y dice si el módulo lo aceptó.
 *
 * ⚠ **Existe para que el técnico no tenga que saber la sintaxis AT.** Los
 * comandos crudos siguen disponibles con `lte at`, pero pedirle a alguien que
 * está instalando un equipo en el campo que recuerde `AT+HTPSV=ip,puerto` es
 * trasladarle un problema que el firmware puede resolver. Es el mismo criterio
 * que tiene FWDLGX con `modem set server`.
 *
 * Entra en modo comando solo: obligar a un `lte esc` previo sería otra cosa que
 * recordar, y falla de una forma que no se parece a "el comando no anduvo".
 */
static void prvLteSet( const char *pcCmd )
{
    static char pcRta[ 128 ];

    if( !wan_modem_listo() )
    {
        return;
    }

    int16_t sRet = drv_lte_at( pcCmd, pcRta, sizeof( pcRta ), 2000U );

    if( sRet <= 0 )
    {
        xprintf( "%s -> sin respuesta. El modulo esta en MODO COMANDO? ('lte esc')\r\n", pcCmd );
        return;
    }

    ( void ) frtos_write( fdTERM, pcRta, ( uint16_t ) sRet );

    /*
     * Se busca el OK en vez de dar por bueno cualquier respuesta: el módulo
     * contesta igual ante un parámetro mal formado, y un "listo" sobre una
     * configuración que no entró es peor que un error.
     */
    if( strstr( pcRta, "OK" ) != NULL )
    {
        xprintf( "\r\nok - RECORDAR 'lte save', sin eso se pierde al reiniciar\r\n" );
    }
    else
    {
        xprintf( "\r\n[!] el modulo no contesto OK: revisar el valor\r\n" );
    }
}
//------------------------------------------------------------------------------
/*
 * `AT+S`: graba la configuración en el módulo **y lo reinicia**.
 *
 * El reinicio no es un efecto lateral molesto, es parte del comando —así lo
 * documenta el fabricante— y por eso el módulo queda en modo TRANSPARENTE
 * después. Conviene decirlo, porque si no el `lte info` siguiente parece fallar.
 */
static void prvLteGrabar( void )
{
    static char pcRta[ 128 ];

    if( !wan_modem_listo() )
    {
        return;
    }

    xprintf( "grabando (el modulo se REINICIA)...\r\n" );

    int16_t sRet = drv_lte_at( "AT+S", pcRta, sizeof( pcRta ), 3000U );

    if( sRet > 0 )
    {
        ( void ) frtos_write( fdTERM, pcRta, ( uint16_t ) sRet );
    }

    xprintf( "\r\nel modulo quedo REINICIADO y en modo TRANSPARENTE.\r\n" );
    xprintf( "para seguir configurando: esperar unos segundos y hacer 'lte esc' de nuevo.\r\n" );
}
//------------------------------------------------------------------------------
/*
 * `lte clock`: lee la hora del MÓDULO, que la sincroniza por NTP contra la red.
 *
 * ⭐ **Es una fuente de hora independiente del servidor Y de la pila del
 * MCP79410**, o sea la salida de fondo al porta pila intermitente: el equipo
 * puede ponerse en hora al abrir una sesión, **antes** de transmitir, en vez de
 * esperar el `CLOCK=` que viene en la respuesta a un frame de datos.
 *
 * La diferencia no es cosmética: hoy, con el RTC en arranque frío, los registros
 * se guardan con `DATE=0101xx` y **ya salen mal grabados**. El `CLOCK=` del
 * servidor llega tarde para esos — corrige el reloj, no los datos que ya se
 * tomaron.
 *
 * ⚠ **ASUME modo AT**, igual que `info`/`set`/`save`.
 *
 * ⚠ **`set` NO se hace solo todavía, y es a propósito**: falta decidir si el
 * módulo entrega hora LOCAL o UTC. El `+32` del ejemplo del manual son cuartos
 * de hora de huso (UTC+8), así que el dato viene con huso — pero **quién lo
 * aplica depende de la red**, y si el equipo estampara UTC donde el AVR estampa
 * local, todos los registros quedarían corridos 3 horas contra los de FWDLGX.
 * Eso rompería la comparación en campo justo donde no se vería: los datos serían
 * plausibles y estarían mal.
 */
static void prvLteClock( bool bAplicar )
{
    char    pcRta[ 96 ];
    int16_t sRet;

    if( !wan_modem_listo() )
    {
        return;
    }

    sRet = drv_lte_at( "AT+CCLK?", pcRta, sizeof( pcRta ), 2000U );

    if( sRet <= 0 )
    {
        xprintf( "sin respuesta: el modulo esta en modo AT? ('lte esc')\r\n" );
        return;
    }

    xprintf( "%s\r\n", pcRta );

    RtcTimeType_t xHora;

    if( !wan_cclk_parsear( pcRta, &xHora ) )
    {
        xprintf( "[!] no se pudo interpretar la respuesta (o el modulo no\r\n" );
        xprintf( "    sincronizo todavia con la red)\r\n" );
        return;
    }

    /* El huso se muestra aparte: el parseo no lo necesita —la hora ya viene en
       local— pero verlo es lo que permitió decidir que era local y no UTC. */
    const char *p = strchr( pcRta, '"' );
    p = ( p != NULL ) ? ( p + 1 ) : pcRta;

    xprintf( "hora del modulo : %02u/%02u/%02u %02u:%02u:%02u\r\n",
             ( unsigned ) xHora.day, ( unsigned ) xHora.month, ( unsigned ) xHora.year,
             ( unsigned ) xHora.hour, ( unsigned ) xHora.min, ( unsigned ) xHora.sec );

    /* El huso que informa el módulo, en cuartos de hora. Se MUESTRA para poder
       decidir si la hora que da es local o UTC — ver el comentario de arriba. */
    const char *pcHuso = ( strlen( p ) > 17U ) ? strpbrk( &p[ 17 ], "+-" ) : NULL;

    if( pcHuso != NULL )
    {
        int iCuartos = atoi( pcHuso );

        xprintf( "huso informado  : %+d cuartos de hora = UTC%+d\r\n",
                 iCuartos, iCuartos / 4 );
    }
    else
    {
        xprintf( "huso informado  : (el modulo no lo dice)\r\n" );
    }

    RtcTimeType_t xActual;

    if( drv_rtc_leer( &xActual ) )
    {
        xprintf( "hora del equipo : %02u/%02u/%02u %02u:%02u:%02u  (%s)\r\n",
                 ( unsigned ) xActual.day, ( unsigned ) xActual.month,
                 ( unsigned ) xActual.year, ( unsigned ) xActual.hour,
                 ( unsigned ) xActual.min, ( unsigned ) xActual.sec,
                 ( drv_rtc_validez() == rtcHORA_VALIDA ) ? "confiable"
                                                         : "NO CONFIABLE" );
    }

    if( !bAplicar )
    {
        xprintf( "\r\npara ponerlo en hora: 'lte clock set'\r\n" );
        return;
    }

    /* Un año menor al de compilación es imposible: la hora del módulo tampoco
       sirve. Mismo criterio que `tkSys`. */
    if( xHora.year < TKSYS_ANIO_COMPILACION )
    {
        xprintf( "[!] el modulo informa un anio anterior al de compilacion:\r\n" );
        xprintf( "    no se aplica (todavia no sincronizo con la red?)\r\n" );
        return;
    }

    /*
     * Va por `wan_rtc_sincronizar()` y no por `drv_rtc_escribir()` directo para
     * que **este camino también mida la deriva**: es el mismo punto por el que
     * pasa el `CLOCK=` del servidor. Ver el header de `wan_frame.h`.
     *
     * `bSiempre = true` porque acá lo pidió una persona: el umbral de 90 s
     * existe para que el ajuste automático no reajuste en cada poleo, no para
     * discutirle a un comando explícito.
     */
    if( wan_rtc_sincronizar( &xHora, "el modulo (NTP)", true ) )
    {
        /* `drv_rtc_escribir()` escribe además la firma de la SRAM, así que esto
           saca al MCP79410 de un arranque en frío. */
        xprintf( "la hora ya es CONFIABLE.\r\n" );
    }
    else
    {
        xprintf( "no se escribio el RTC (ya estaba en hora, o fallo el chip)\r\n" );
    }
}
//------------------------------------------------------------------------------
/*
 * Muestra cómo está configurado el MÓDULO, leyéndolo de él.
 *
 * ⚠ **La IP, el puerto y la URL del servidor viven en el módulo, no en la
 * configuración del datalogger** (criterio de Pablo, 2026-09-09). El equipo sólo
 * escribe el payload por la UART; el GET entero lo arma el DTU. Por eso acá no
 * hay una copia local que pueda quedar desactualizada: **se pregunta cada vez**,
 * y lo que se ve es lo que de verdad se va a usar.
 */
static void prvLteInfo( void )
{
    static char pcRta[ 128 ];
    bool bSimOk = false;
    bool bIpOk  = false;

    /* Los que están confirmados en `Datasheets/Componentes/PUSR/`. Agregar uno
       es agregar una línea. */
    static const char * const pcConsultas[] = {
        "AT+IMEI?",     /* la identidad: es el ID del frame       */
        "AT+ICCID?",    /* la SIM                                  */
        "AT+CSQ",       /* señal de radio                          */
        /*
         * ⚠ LA CONSULTA QUE DECIDE SI SE PUEDE TRANSMITIR.
         *
         * `AT+CIP?` devuelve la IP que le dio la red. **Tener señal no es tener
         * conexión de datos**: con `CSQ 31` —excelente— pero sin attach, el
         * módulo se traga el payload y no lo envía. El servidor no ve nada
         * llegar y desde el equipo se ve como "sin respuesta", que manda a
         * buscar al lugar equivocado.
         *
         * Es lo que hace la FSM del AVR: se queda en OFFLINE hasta que
         * `AT+CIP?` devuelve una IP, y recién ahí pasa a LINK y transmite.
         * Encontrado el 2026-09-09 comparando contra un AVR con el MISMO módulo,
         * que sí transmitía.
         */
        "AT+CIP?",      /* la IP local: SIN ESTO NO SE TRANSMITE   */
        "AT+WKMOD?",    /* modo de trabajo: tiene que decir HTTPD  */
        "AT+APN?",
        "AT+HTPSV?",    /* IP y puerto del servidor               */
        "AT+HTPURL?",   /* el prefijo del GET                     */
        "AT+HTPTP?",    /* GET o POST                             */
        /*
         * ⚠ LOS DOS CRITERIOS CON LOS QUE EL MÓDULO CIERRA UNA TRAMA.
         *
         * En modo transparente no hay terminador: el módulo arma el GET con lo
         * que recibió cuando se cumple **lo que pase primero** —
         *
         *   `UARTFT`  el puerto se queda callado tantos ms   (10..500, def 50)
         *   `UARTFL`  se juntaron tantos bytes               (5..4096, def 1024)
         *
         * `UARTFT` es el que fija cuánto hay que esperar entre dos frames
         * seguidos: si se manda más rápido, los dos se le juntan en un solo GET
         * y del otro lado llega un frame corrupto. Por eso existe
         * `LTE_DATA_MS_ENTRE_FRAMES`.
         *
         * ⛔ **El comando NO se llama `AT+FTIME`** — eso devuelve `+CME ERROR:58`
         * (no soportado), y así se descubrió en banco el 2026-09-18.
         *
         * `UARTFL` se consulta aunque hoy no apriete: con frames de ~150 bytes
         * nunca se llega a 1024, pero **si alguien lo bajara por debajo del
         * largo de un frame, el módulo lo partiría en dos GET** — y eso del lado
         * del servidor se vería como frames corruptos sin que el log del equipo
         * muestre nada raro.
         */
        "AT+UARTFT?",   /* el silencio que cierra la trama        */
        "AT+UARTFL?",   /* el largo que tambien la cierra         */
        /*
         * ⭐ El RELOJ DEL MÓDULO, que se sincroniza por NTP contra la red.
         *
         * Formato: `+CCLK: "20/06/19,20:05:19+32"` — el `+32` son cuartos de
         * hora de huso (32/4 = UTC+8).
         *
         * Es una fuente de hora **independiente del servidor y de la pila del
         * MCP79410**, o sea la salida de fondo al porta pila intermitente: el
         * equipo puede ponerse en hora solo al abrir una sesión, sin esperar la
         * respuesta a un frame de datos y sin que nadie vaya al sitio.
         */
        "AT+CCLK?",     /* la hora del modulo, por NTP            */
    };

    if( !wan_modem_listo() )
    {
        return;
    }

    for( uint32_t i = 0U; i < ( sizeof( pcConsultas ) / sizeof( pcConsultas[ 0 ] ) ); i++ )
    {
        int16_t sRet = drv_lte_at( pcConsultas[ i ], pcRta, sizeof( pcRta ), 1000U );

        if( sRet <= 0 )
        {
            xprintf( "  %-12s (sin respuesta - esta en MODO COMANDO? 'lte esc')\r\n",
                     pcConsultas[ i ] );
            continue;
        }

        /* La respuesta trae el eco del comando y los CR/LF del módulo, así que
           sale tal cual en vez de intentar recortarla: cualquier parseo acá
           sería una suposición sobre un formato que todavía no está fijado. */
        ( void ) frtos_write( fdTERM, pcRta, ( uint16_t ) sRet );

        /*
         * El IMEI se aprovecha de paso: es el `ID` con el que el servidor
         * identifica al equipo, y leerlo acá evita un comando aparte. Se guarda
         * para toda la corrida —el módulo puede apagarse, su IMEI no cambia— así
         * que después el frame se arma sin necesitar el modem encendido.
         */
        char *pcVal = strstr( pcRta, "+IMEI:" );

        if( pcVal != NULL )
        {
            wan_imei_set( pcVal + 6 );
        }

        /*
         * Se mira si hay SIM y si hay IP, porque **son las dos condiciones sin
         * las cuales no se transmite nada**, y leerlas entre nueve respuestas es
         * fácil de pasar por alto: el 2026-09-09 el equipo no mandaba un solo
         * frame y el `+ICCID:` vacío estaba a la vista en la pantalla.
         *
         * El criterio es mínimo a propósito —un dígito después del prefijo— para
         * no suponer nada sobre el formato exacto de la respuesta.
         */
        pcVal = strstr( pcRta, "+ICCID:" );

        if( ( pcVal != NULL ) && ( pcVal[ 7 ] >= '0' ) && ( pcVal[ 7 ] <= '9' ) )
        {
            bSimOk = true;
            wan_iccid_set( pcVal + 7 );
        }

        /* El CSQ también se cachea: el frame de configuración lo lleva, y leerlo
           acá evita otra entrada a modo comando justo antes de transmitir. */
        pcVal = strstr( pcRta, "+CSQ:" );

        if( pcVal != NULL )
        {
            wan_csq_set( ( uint8_t ) atoi( pcVal + 5 ) );
        }

        pcVal = strstr( pcRta, "+CIP:" );

        if( ( pcVal != NULL ) && ( pcVal[ 5 ] >= '0' ) && ( pcVal[ 5 ] <= '9' ) )
        {
            bIpOk = true;
        }
    }

    xprintf( "\r\n" );

    /*
     * ⚠ El orden del diagnóstico va de la CAUSA al efecto: sin SIM no hay red, y
     * sin red no hay IP. Avisar de la IP cuando el problema es la SIM mandaría a
     * buscar al lugar equivocado.
     */
    if( !bSimOk )
    {
        xprintf( "[!] SIN SIM: el modulo no lee la tarjeta (+ICCID vacio).\r\n" );
        xprintf( "    Sin SIM no hay red, no hay IP y NO SE TRANSMITE NADA, por mas\r\n" );
        xprintf( "    que el CSQ sea bueno: eso es señal de radio, no conexion.\r\n" );
    }
    else if( !bIpOk )
    {
        xprintf( "[!] SIN IP: hay SIM pero el modulo todavia no esta en la red.\r\n" );
        xprintf( "    Esperar unos segundos y volver a consultar: hasta que +CIP\r\n" );
        xprintf( "    traiga una direccion, un 'lte ping' no va a salir.\r\n" );
    }
    else
    {
        xprintf( "SIM y red OK: se puede transmitir ('lte exit' y despues 'lte ping')\r\n" );
    }

    /*
     * ⚠ El CSQ crudo tiene DOS valores que no son medidas, y uno de ellos costó
     * una tarde el 2026-09-09 porque se lee como "señal excelente":
     *
     *   99   = desconocido / no detectable (3GPP)
     *   >=31 = lo que devuelve el modulo ANTES de campar en la red
     *
     * El AVR ya lo tenía documentado —le pasó en mayo de 2026— junto con el
     * `+CME ERROR:50` de `AT+CIP?` que viene detrás. Decirlo acá cierra el
     * despiste en vez de dejarlo para la próxima.
     */
    if( !wan_csq_valido() )
    {
        xprintf( "[!] el CSQ leido NO es una medida: 99 = desconocido, >=31 = todavia\r\n" );
        xprintf( "    no campo en la red. Un 31 se lee como señal excelente y no lo es.\r\n" );
    }
    else
    {
        xprintf( "señal: %u dBm negativos (CSQ del frame = %u)\r\n",
                 ( unsigned ) wan_csq(), ( unsigned ) wan_csq() );
    }

    xprintf( "(queda en MODO COMANDO: 'lte exit' para volver a transparente)\r\n" );
}

/*
 * Vuelca lo que conteste el modem. A diferencia del RS485 —que transporta tramas
 * binarias y se lee mejor en hexa— acá lo que viene es texto AT, así que sale tal
 * cual: los CR/LF del módulo hacen el trabajo. El hexa queda para los bytes que no
 * se pueden imprimir, que es donde se ve si el baudrate está mal.
 */
static int16_t prvLteEscuchar( uint32_t ulMs )
{
    static char pcDatos[ LTE_BUF ];

    int16_t sRet = drv_lte_read( pcDatos, LTE_BUF, ulMs );

    if( sRet <= 0 )
    {
        xprintf( "silencio (%lu ms, nada recibido)\r\n", ( unsigned long ) ulMs );
        return sRet;
    }

    xprintf( "recibidos %d:\r\n", ( int ) sRet );

    for( int16_t i = 0; i < sRet; i++ )
    {
        char c = pcDatos[ i ];

        if( ( ( c >= 0x20 ) && ( c < 0x7F ) ) || ( c == '\r' ) || ( c == '\n' ) )
        {
            xputChar( c );
        }
        else
        {
            xprintf( "<%02X>", ( unsigned ) ( ( uint8_t ) c ) );
        }
    }
    xprintf( "\r\n" );

    return sRet;
}

/*
 * Puente transparente entre la terminal y el modem, que es la herramienta que
 * hace utilizable el bring-up: deja hablarle al módulo a mano, con comandos de
 * cualquier largo y con espacios, sin pasar por el parser.
 *
 * ⚠ Las dos puntas corren a velocidades distintas —la consola a 9600, el modem
 * mucho más rápido— así que si el módulo larga una ráfaga larga, la terminal no da
 * abasto y se pierde texto. Para leer una respuesta larga sin perder nada,
 * 'lte at' y 'lte rx' la juntan primero en el buffer y recién después la imprimen.
 */
static void prvLteBridge( void )
{
    static char pcBuf[ LTE_BUF ];

    xprintf( "puente abierto. Ctrl-D para salir.\r\n" );

    for( ;; )
    {
        int16_t sN = drv_uart_read( drvUART_TERM, pcBuf, LTE_BUF, pdMS_TO_TICKS( 20U ) );

        for( int16_t i = 0; i < sN; i++ )
        {
            if( pcBuf[ i ] == 0x04 )    /* Ctrl-D */
            {
                /* Lo tecleado antes del Ctrl-D sí se manda: cortarlo sería perder
                   un comando entero por apurarse a salir. */
                if( i > 0 )
                {
                    ( void ) drv_lte_write( pcBuf, ( uint16_t ) i );
                }
                xprintf( "\r\npuente cerrado.\r\n" );
                return;
            }
        }

        if( sN > 0 )
        {
            ( void ) drv_lte_write( pcBuf, ( uint16_t ) sN );
        }

        sN = drv_uart_read( drvUART_LTE, pcBuf, LTE_BUF, pdMS_TO_TICKS( 20U ) );

        if( sN > 0 )
        {
            ( void ) frtos_write( fdTERM, pcBuf, ( uint16_t ) sN );
        }
    }
}

static void cmdLte( void )
{
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    if( ( ucArgs >= 1U ) && ( argv[ 1 ] != NULL ) )
    {
        if( ( strcmp( argv[ 1 ], "on" ) == 0 ) || ( strcmp( argv[ 1 ], "off" ) == 0 ) )
        {
            bool bOn = ( argv[ 1 ][ 1 ] == 'n' );

            drv_lte_power( bOn );
            xprintf( "energia del modem %s (PWRKEY sin tocar)\r\n",
                     bOn ? "ENCENDIDA: EN_LTE_DCIN = 1" : "apagada: EN_LTE_DCIN = 0" );
            return;
        }

        if( strcmp( argv[ 1 ], "bridge" ) == 0 )
        {
            prvLteBridge();
            return;
        }

        if( strcmp( argv[ 1 ], "data" ) == 0 )
        {
            ( void ) wan_sesion_datos();   /* desde la consola el resultado ya se imprimio */
            return;
        }

        if( strcmp( argv[ 1 ], "conf" ) == 0 )
        {
            wan_sesion_config();
            return;
        }

        if( strcmp( argv[ 1 ], "ping" ) == 0 )
        {
            wan_sesion_ping();
            return;
        }

        if( strcmp( argv[ 1 ], "exit" ) == 0 )
        {
            static char pcRta[ 64 ];

            /*
             * `AT+ENTM` devuelve el módulo al modo TRANSPARENTE, que es donde
             * tiene que estar para trabajar: en modo comando no transmite nada.
             *
             * Como todos los de esta familia, **asume** que el módulo está en
             * modo comando: manda el AT y ya. Si no estaba, lo trata como datos
             * y no pasa nada.
             */
            int16_t sRet = drv_lte_at( "AT+ENTM", pcRta, sizeof( pcRta ), 1000U );

            if( ( sRet > 0 ) && ( strstr( pcRta, "OK" ) != NULL ) )
            {
                xprintf( "modo TRANSPARENTE\r\n" );
            }
            else
            {
                xprintf( "sin confirmacion del modulo (puede que ya estuviera en transparente)\r\n" );
            }
            return;
        }

        if( strcmp( argv[ 1 ], "save" ) == 0 )
        {
            prvLteGrabar();
            return;
        }

        if( strcmp( argv[ 1 ], "set" ) == 0 )
        {
            if( ( ucArgs < 2U ) || ( argv[ 2 ] == NULL ) )
            {
                prvLteUso();
                return;
            }

            /* `httpd` no lleva valor; los otros tres sí. */
            if( strcmp( argv[ 2 ], "httpd" ) == 0 )
            {
                prvLteSet( "AT+WKMOD=HTTPD" );
                return;
            }

            if( ( ucArgs < 3U ) || ( argv[ 3 ] == NULL ) )
            {
                prvLteUso();
                return;
            }

            static char pcCmd[ 96 ];

            if( strcmp( argv[ 2 ], "server" ) == 0 )
            {
                if( ( ucArgs < 4U ) || ( argv[ 4 ] == NULL ) )
                {
                    xprintf( "faltan datos: 'lte set server <ip> <puerto>'\r\n" );
                    return;
                }
                snprintf( pcCmd, sizeof( pcCmd ), "AT+HTPSV=%s,%s", argv[ 3 ], argv[ 4 ] );
            }
            else if( strcmp( argv[ 2 ], "apn" ) == 0 )
            {
                /* Los tres campos vacíos y el 0 final son los del AVR
                   (`modem_atcmd_write_apn`): usuario, clave y tipo de
                   autenticación. Se conservan porque es lo que funciona con
                   este operador. */
                snprintf( pcCmd, sizeof( pcCmd ), "AT+APN=%s,,,0", argv[ 3 ] );
            }
            else if( strcmp( argv[ 2 ], "url" ) == 0 )
            {
                snprintf( pcCmd, sizeof( pcCmd ), "AT+HTPURL=%s", argv[ 3 ] );
            }
            else
            {
                prvLteUso();
                return;
            }

            prvLteSet( pcCmd );
            return;
        }

        if( strcmp( argv[ 1 ], "info" ) == 0 )
        {
            prvLteInfo();
            return;
        }

        if( strcmp( argv[ 1 ], "clock" ) == 0 )
        {
            prvLteClock( ( ucArgs >= 2U ) && ( argv[ 2 ] != NULL ) &&
                         ( strcmp( argv[ 2 ], "set" ) == 0 ) );
            return;
        }

        if( strcmp( argv[ 1 ], "esc" ) == 0 )
        {
            xprintf( "secuencia de escape: +++ / a / a / +ok ...\r\n" );

            switch( drv_lte_escape() )
            {
                case lteESC_OK:
                    xprintf( "MODO COMANDO. Ya acepta 'lte at AT+CSQ'\r\n" );
                    break;

                case lteESC_SIN_OK:
                    /* Vale más que un error: el modulo contesto algo que le
                       mandamos, o sea que el TX del micro SI le llega. */
                    xprintf( "contesto la 'a' pero no el '+ok'.\r\n"
                             "  -> el TX FUNCIONA (le llego el '+++'). Falla el 2do tramo:\r\n"
                             "     probar subiendo DRV_LTE_MS_ESPERA_OK, o mirar 'lte' por errores\r\n" );
                    break;

                case lteESC_SIN_A:
                    xprintf( "no contesto la 'a'. Los sospechosos, en orden:\r\n"
                             "  1. el TX del micro no le llega (su UART es de 3,0 V y la nuestra\r\n"
                             "     de 3,3: el manual pide adaptacion de niveles)\r\n"
                             "  2. no esta en modo transparente, o ya esta en modo comando\r\n"
                             "  3. le cambiaron la password con AT+CMDPW (de fabrica es '+++')\r\n" );
                    break;

                default:
                    xprintf( "ERROR: la UART no pudo transmitir\r\n" );
                    break;
            }
            return;
        }

        if( ( ucArgs >= 2U ) && ( argv[ 2 ] != NULL ) )
        {
            if( strcmp( argv[ 1 ], "key" ) == 0 )
            {
                if( ( strcmp( argv[ 2 ], "on" ) == 0 ) || ( strcmp( argv[ 2 ], "off" ) == 0 ) )
                {
                    bool bOn = ( argv[ 2 ][ 1 ] == 'n' );

                    drv_lte_pwrkey( bOn );
                    xprintf( "LTE_PWR (PA5) = %u  ->  power switch %s\r\n", bOn ? 1U : 0U,
                             bOn ? "APRETADO (nivel bajo en el modulo)" : "suelto" );
                }
                else
                {
                    /* Un pulso cronometrado: es la forma de averiguar cuánto
                       necesita ESTE módulo, que es un dato que todavía no está. */
                    uint32_t ulMs = ( uint32_t ) atoi( argv[ 2 ] );

                    if( ulMs == 0U )
                    {
                        prvLteUso();
                        return;
                    }

                    xprintf( "pulso de %lu ms...\r\n", ( unsigned long ) ulMs );
                    drv_lte_pwrkey_pulso( ulMs );
                    xprintf( "listo, LTE_PWR suelto\r\n" );
                }
                return;
            }

            if( strcmp( argv[ 1 ], "at" ) == 0 )
            {
                static char pcRta[ LTE_BUF ];

                int16_t sRet = drv_lte_at( argv[ 2 ], pcRta, LTE_BUF, LTE_ESCUCHA_MS );

                if( sRet < 0 )
                {
                    xprintf( "ERROR: no se pudo transmitir\r\n" );
                }
                else if( sRet == 0 )
                {
                    xprintf( "'%s' -> sin respuesta en %u ms\r\n",
                             argv[ 2 ], ( unsigned ) LTE_ESCUCHA_MS );
                }
                else
                {
                    xprintf( "'%s' -> %d bytes:\r\n%s\r\n", argv[ 2 ], ( int ) sRet, pcRta );
                }
                return;
            }

            if( strcmp( argv[ 1 ], "tx" ) == 0 )
            {
                uint16_t usLargo = ( uint16_t ) strlen( argv[ 2 ] );

                drv_lte_flush();

                if( drv_lte_write( argv[ 2 ], usLargo ) != ( int16_t ) usLargo )
                {
                    xprintf( "ERROR: no se pudo transmitir\r\n" );
                    return;
                }

                xprintf( "transmitidos %u bytes, escuchando...\r\n", ( unsigned ) usLargo );
                ( void ) prvLteEscuchar( LTE_ESCUCHA_MS );
                return;
            }

            if( strcmp( argv[ 1 ], "rx" ) == 0 )
            {
                ( void ) prvLteEscuchar( ( uint32_t ) atoi( argv[ 2 ] ) );
                return;
            }
        }

        prvLteUso();
        return;
    }

    /* ---- 'lte' pelado ---- */
    xprintf( "  EN_LTE_DCIN PC13 : %s\r\n",
             drv_lte_power_estado() ? "1 - modem ALIMENTADO (DCIN y 3V8)"
                                    : "0 - cortado (reposo)" );
    xprintf( "  LTE_PWR     PA5  : %s\r\n",
             drv_lte_pwrkey_estado() ? "1 - APRETADO (el transistor conduce)"
                                     : "0 - suelto (reposo)" );
    xprintf( "  UART4 errores    : %lu  (ISR acumulado 0x%08lX)\r\n",
             ( unsigned long ) drv_uart_errores( drvUART_LTE ),
             ( unsigned long ) drv_uart_ultimo_isr( drvUART_LTE ) );
    xprintf( "  pwr locks        : 0x%08lX %s\r\n",
             ( unsigned long ) pwr_lock_estado(),
             pwr_deep_sleep_permitido() ? "(Stop 2 habilitado)" : "(solo Sleep)" );
}
//------------------------------------------------------------------------------
/*------------------------------------------------------------------------------
 * Configuración del datalogger
 *
 * La sintaxis es la MISMA que la de FWDLGX en el AVR, a propósito: los técnicos
 * que configuran equipos en campo ya la tienen en los dedos, y cambiarla sólo
 * para que quede más linda cuesta errores en la instalación.
 *----------------------------------------------------------------------------*/
static void prvConfigUso( void )
{
    xprintf( "uso:\r\n" );
    xprintf( "  config                          muestra todo, con los hashes\r\n" );
    xprintf( "  config save | load | default\r\n" );
    xprintf( "  config hash                     los STRINGS que se hashean (diagnostico)\r\n" );
    xprintf( "\r\n" );
    xprintf( "  config timerpoll <s>            periodo de muestreo\r\n" );
    xprintf( "  config timerdial <s>            periodo de disque (modo discreto)\r\n" );
    xprintf( "  config pwrmodo <continuo|discreto|mixto|rtu|silent>\r\n" );
    xprintf( "     rtu    = transmite si hay enlace; SIN enlace DESCARTA el dato\r\n" );
    xprintf( "     silent = no enciende el modem nunca; los datos van a la microSD\r\n" );
    xprintf( "  config pwron <hhmm>             solo en mixto\r\n" );
    xprintf( "  config pwroff <hhmm>            solo en mixto\r\n" );
    xprintf( "\r\n" );
    xprintf( "  config ainput <0..2> <enable> <name> <imin> <imax> <mmin> <mmax> <offset>\r\n" );
    xprintf( "  config pst <s>                  settle time de los sensores 4-20\r\n" );
    xprintf( "  config counter <enable> <name> <magpp> <caudal|pulsos> <qmax> <alpha>\r\n" );
    xprintf( "  config consigna <enable> <diurna_hhmm> <nocturna_hhmm>\r\n" );
    xprintf( "\r\n" );
    xprintf( "  config modbus enable <true|false>\r\n" );
    xprintf( "  config modbus localaddr <1..247>\r\n" );
    xprintf( "  config modbus channel <0..4> <enable> <name> <slaaddr> <regaddr>\r\n" );
    xprintf( "                        <nro_regs> <fcode> <tipo> <codec> <div_p10>\r\n" );
    xprintf( "     tipo : U16|I16|U32|I32|FLOAT      codec: C0123|C1032|C3210|C2301\r\n" );
    xprintf( "\r\n" );
    xprintf( "  'default' y 'load' trabajan SOLO en RAM: nada se graba hasta que se\r\n" );
    xprintf( "  haga 'config save'. Asi un 'default' mal tipeado se deshace con 'load'.\r\n" );
}
//------------------------------------------------------------------------------
static void prvKillUso( void )
{
    xprintf( "  kill wan     mata tkWan: deja el modem libre para 'lte ...'\r\n" );
    xprintf( "  kill sys     mata tkSys: deja de polear y de escribir la ventana\r\n" );
    xprintf( "  kill cpres   mata tkCtlPres: deja libre el control de presion\r\n" );
    xprintf( "\r\n" );
    xprintf( "  NO hay forma de revivir una tarea, y es a proposito: despues de\r\n" );
    xprintf( "  trabajar a mano se hace 'reset' y el equipo arranca limpio.\r\n" );
}
//------------------------------------------------------------------------------
/*
 * `kill`: mata una tarea para que un operador pueda trabajar con SU periférico
 * sin pisarse con ella.
 *
 * ⭐ Copiado del AVR —mismo nombre y misma semántica— para no reeducar a los
 * técnicos, igual que con la sintaxis de `config`. Allá es `WD_stop_task()`, y
 * lo que hace bien es **desregistrar del watchdog ANTES de suspender**:
 * suspender una tarea que el watchdog sigue vigilando la daría por colgada y
 * **resetearía el equipo justo mientras el operador trabaja**, que es el síntoma
 * más desconcertante posible. ⏳ Acá el watchdog todavía no existe (paso 8), y
 * el lugar donde va ese desregistro está marcado en `tkWan.c`.
 *
 * ⚠ **No hay "revivir"** (criterio de Pablo, 2026-09-21): la idea es que después
 * de entrar en modo comando para pruebas o diagnóstico, el operador **resetee el
 * datalogger para que entre en modo de funcionamiento limpio**.
 */
static void cmdKill( void )
{
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    if( ( ucArgs == 0U ) || ( argv[ 1 ] == NULL ) )
    {
        prvKillUso();
        return;
    }

    if( strcmp( argv[ 1 ], "wan" ) == 0 )
    {
        if( wan_matada() )
        {
            xprintf( "tkWan ya estaba matada\r\n" );
            return;
        }

        xprintf( "tkWan esta en %s; kill pedido, se mata en su proxima vuelta.\r\n",
                 wan_estado_str() );
        wan_pedir_kill();
        return;
    }

    if( strcmp( argv[ 1 ], "cpres" ) == 0 )
    {
        if( tkCtlPres_matada() )
        {
            xprintf( "tkCtlPres ya estaba matada\r\n" );
            return;
        }

        xprintf( "kill pedido: tkCtlPres se mata en su proxima vuelta (hasta 45 s).\r\n" );
        xprintf( "Si esta en medio de una consigna, termina primero.\r\n" );
        tkCtlPres_pedir_kill();
        return;
    }

    if( strcmp( argv[ 1 ], "sys" ) == 0 )
    {
        if( xHandle_tkSys == NULL )
        {
            xprintf( "tkSys no existe\r\n" );
            return;
        }

        vTaskSuspend( xHandle_tkSys );
        xprintf( "tkSys MATADA: no polea mas ni escribe la ventana.\r\n" );
        xprintf( "Para volver a operacion normal: 'reset'.\r\n" );
        return;
    }

    prvKillUso();
}
//------------------------------------------------------------------------------
static void cmdConfig( void )
{
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    /* 'config' pelado: el estado completo. */
    if( ( ucArgs == 0U ) || ( argv[ 1 ] == NULL ) )
    {
        cfg_nvm_print_all();
        return;
    }

    /* ---- los que no llevan valor ---- */

    if( strcmp( argv[ 1 ], "save" ) == 0 )
    {
        ( void ) cfg_nvm_save_all();
        return;
    }

    /*
     * `config hash`: los STRINGS sobre los que se calcula cada hash.
     *
     * ⭐ Es el diagnóstico del contrato, y no hay otra forma de hacerlo. El
     * servidor compara su hash con el nuestro; si difieren, pide reconfigurar
     * ese bloque **en todas las sesiones, para siempre**, y el valor del hash no
     * dice en qué carácter está la diferencia. El string, sí.
     *
     * Se imprime desde `cfg_hash_string()`, o sea que es **lo que entra al
     * Pearson** y no una reimpresión que podría diferir justo en el decimal que
     * se vino a mirar.
     */
    if( strcmp( argv[ 1 ], "hash" ) == 0 )
    {
        xprintf( "los strings que se hashean, bloque por bloque:\r\n" );

        cfg_hash_verbose( true );

        xprintf( "BASE     " );  uint8_t ucB = cfg_base_hash();
        xprintf( "AINPUTS  " );  uint8_t ucA = cfg_ainputs_hash();
        xprintf( "COUNTER  " );  uint8_t ucC = cfg_counter_hash();
        xprintf( "MODBUS   " );  uint8_t ucM = cfg_modbus_hash();
        xprintf( "CONSIGNA " );  uint8_t ucP = cfg_consigna_hash();

        cfg_hash_verbose( false );

        xprintf( "\r\nBH=0x%02X AH=0x%02X CH=0x%02X MH=0x%02X PH=0x%02X\r\n",
                 ( unsigned ) ucB, ( unsigned ) ucA, ( unsigned ) ucC,
                 ( unsigned ) ucM, ( unsigned ) ucP );
        return;
    }

    if( strcmp( argv[ 1 ], "load" ) == 0 )
    {
        ( void ) cfg_nvm_load_all();
        cfg_nvm_print_all();
        return;
    }

    if( strcmp( argv[ 1 ], "default" ) == 0 )
    {
        cfg_nvm_defaults_all();
        xprintf( "configuracion por defecto cargada EN RAM ('config save' para grabarla)\r\n" );
        return;
    }

    /* ---- de acá en adelante hace falta al menos un valor ---- */

    if( ( ucArgs < 2U ) || ( argv[ 2 ] == NULL ) )
    {
        prvConfigUso();
        return;
    }

    bool bOk = false;

    if     ( strcmp( argv[ 1 ], "timerpoll" ) == 0 ) { bOk = cfg_base_set_timerpoll( argv[ 2 ] ); }
    else if( strcmp( argv[ 1 ], "timerdial" ) == 0 ) { bOk = cfg_base_set_timerdial( argv[ 2 ] ); }
    else if( strcmp( argv[ 1 ], "pwrmodo"   ) == 0 ) { bOk = cfg_base_set_pwrmodo  ( argv[ 2 ] ); }
    else if( strcmp( argv[ 1 ], "pwron"     ) == 0 ) { bOk = cfg_base_set_pwron    ( argv[ 2 ] ); }
    else if( strcmp( argv[ 1 ], "pwroff"    ) == 0 ) { bOk = cfg_base_set_pwroff   ( argv[ 2 ] ); }
    else if( strcmp( argv[ 1 ], "pst"       ) == 0 ) { bOk = cfg_ainputs_set_settle_time( argv[ 2 ] ); }

    else if( strcmp( argv[ 1 ], "ainput" ) == 0 )
    {
        /* config ainput <ch> <enable> <name> <imin> <imax> <mmin> <mmax> <offset> */
        if( ( ucArgs >= 9U ) && ( argv[ 9 ] != NULL ) )
        {
            bOk = cfg_ainputs_set_canal( ( uint8_t ) atoi( argv[ 2 ] ),
                                         argv[ 3 ], argv[ 4 ], argv[ 5 ], argv[ 6 ],
                                         argv[ 7 ], argv[ 8 ], argv[ 9 ] );
        }
        else
        {
            prvConfigUso();
            return;
        }
    }

    else if( strcmp( argv[ 1 ], "counter" ) == 0 )
    {
        /* config counter <enable> <name> <magpp> <modo> <qmax> <alpha>
           Mismo orden que el AVR: el modo va TERCERO. */
        if( ( ucArgs >= 7U ) && ( argv[ 7 ] != NULL ) )
        {
            bOk = cfg_counter_set( argv[ 2 ], argv[ 3 ], argv[ 4 ], argv[ 5 ],
                                   argv[ 6 ], argv[ 7 ] );
        }
        else
        {
            prvConfigUso();
            return;
        }
    }

    else if( strcmp( argv[ 1 ], "consigna" ) == 0 )
    {
        /* config consigna <enable> <diurna> <nocturna> */
        if( ( ucArgs >= 4U ) && ( argv[ 4 ] != NULL ) )
        {
            bOk = cfg_consigna_set( argv[ 2 ], argv[ 3 ], argv[ 4 ] );
        }
        else
        {
            prvConfigUso();
            return;
        }
    }

    else if( strcmp( argv[ 1 ], "modbus" ) == 0 )
    {
        if( strcmp( argv[ 2 ], "enable" ) == 0 )
        {
            bOk = ( ( ucArgs >= 3U ) && ( argv[ 3 ] != NULL ) ) ?
                  cfg_modbus_set_enable( argv[ 3 ] ) : false;
        }
        else if( strcmp( argv[ 2 ], "localaddr" ) == 0 )
        {
            bOk = ( ( ucArgs >= 3U ) && ( argv[ 3 ] != NULL ) ) ?
                  cfg_modbus_set_localaddr( argv[ 3 ] ) : false;
        }
        else if( strcmp( argv[ 2 ], "channel" ) == 0 )
        {
            /* config modbus channel <ch> <enable> <name> <sla> <reg> <nregs> <fcode> <tipo> <codec> <div> */
            if( ( ucArgs >= 12U ) && ( argv[ 12 ] != NULL ) )
            {
                bOk = cfg_modbus_set_canal( ( uint8_t ) atoi( argv[ 3 ] ),
                                            argv[ 4 ], argv[ 5 ], argv[ 6 ], argv[ 7 ],
                                            argv[ 8 ], argv[ 9 ], argv[ 10 ], argv[ 11 ],
                                            argv[ 12 ] );
            }
            else
            {
                prvConfigUso();
                return;
            }
        }
        else
        {
            prvConfigUso();
            return;
        }
    }

    else
    {
        prvConfigUso();
        return;
    }

    if( bOk )
    {
        xprintf( "ok  (recordar 'config save')\r\n" );
    }
    else
    {
        xprintf( "ERROR: valor invalido\r\n" );
    }
}
//------------------------------------------------------------------------------
/*
 * Fuerza un poleo y lo imprime.
 *
 * Llama a `tkSys_poll()`, la MISMA función que usa la tarea en cada vuelta, y no
 * a una copia "de prueba": si fueran dos caminos distintos, lo que se valida a
 * mano dejaría de ser lo que hace el equipo solo, que es la clase de diferencia
 * que aparece recién en campo.
 */
static void cmdPoll( void )
{
    static dataRcd_t xDr;

    xprintf( "poleando...\r\n" );
    ( void ) tkSys_poll( &xDr );
    tkSys_print( &xDr );
}
//------------------------------------------------------------------------------
/*
 * Arma el frame y lo imprime, sin tocar el modem.
 *
 * Es LA herramienta de validación de esta etapa: permite poner la misma
 * configuración en un equipo AVR y en éste y comparar los dos frames carácter
 * por carácter, sin red, sin servidor y sin que un dato de prueba llegue a
 * producción. Si son iguales, el contrato queda cerrado antes de que el modem
 * entre en juego.
 *
 * Imprime también el LARGO, que es la primera diferencia que salta si algo no
 * coincide, y sirve para ver cuánto margen queda contra el buffer.
 */
static void cmdFrame( void )
{
    static char      pcFrame[ WAN_FRAME_BUFFER_SIZE ];
    static dataRcd_t xDr;

    /* Se polea de nuevo en vez de usar el último registro: así el frame refleja
       el estado de AHORA, que es lo que uno quiere al compararlo contra otro
       equipo. */
    ( void ) tkSys_poll( &xDr );

    uint16_t usLargo = wan_frame_data( pcFrame, sizeof( pcFrame ), &xDr, true );

    if( usLargo == 0U )
    {
        return;     /* wan_frame_data() ya explicó por qué */
    }

    /*
     * ⚠ Se escribe DIRECTO al fd, sin pasar por xprintf.
     *
     * `xprintf` formatea en un buffer estático de XPRINTF_BUFFER_SIZE (160
     * bytes) y **el frame es más largo**: 174 en el primer intento de banco, y
     * hasta ~350 con los 9 canales y nombres largos. Pasarlo por ahí lo truncaba
     * en silencio — se veía un frame cortado a mitad de campo mientras el
     * contador de bytes informaba el largo correcto, que es de los síntomas que
     * hacen dudar del dato en vez de de la impresión.
     *
     * El frame ya es una cadena terminada: no necesita formateo, sólo salir.
     */
    ( void ) frtos_write( fdTERM, pcFrame, usLargo );

    xprintf( "\r\n  (%u bytes de %u)\r\n",
             ( unsigned ) usLargo, ( unsigned ) sizeof( pcFrame ) );

    if( xDr.usInvalidos != 0U )
    {
        xprintf( "  [!] hay campos en %d: no se pudieron medir\r\n",
                 ( int ) WAN_CENTINELA_SIN_DATO );
    }
}
//------------------------------------------------------------------------------
/*
 * Limpia la pantalla. Se llamaba igual en FWDLGX, así que el nombre se conserva.
 *
 * Son secuencias ANSI, no un truco: `ESC[2J` borra la pantalla y `ESC[H` manda
 * el cursor al ángulo. Las entiende cualquier terminal seria —minicom, PuTTY,
 * screen—; en una que no las soporte se verían los caracteres crudos y no pasa
 * nada más.
 *
 * Se escriben con xputChar y no con xprintf: el ESC (0x1B) es un carácter de
 * control, y meterlo en una cadena de formato es pedir que algún día alguien lo
 * "arregle".
 */
static void cmdCls( void )
{
    xputChar( 0x1B ); xprintf( "[2J" );     /* borrar toda la pantalla */
    xputChar( 0x1B ); xprintf( "[H"  );     /* cursor a 1,1            */
}
//------------------------------------------------------------------------------
static void prvFsUso( void )
{
    xprintf( "uso:\r\n" );
    xprintf( "  fs                 estado de la memoria de registros\r\n" );
    xprintf( "  fs read <n>        muestra los <n> mas viejos SIN borrarlos\r\n" );
    xprintf( "  fs frame <n>       arma el frame del registro <n> (0 = el mas viejo)\r\n" );
    xprintf( "  fs pop <n>         DESCARTA los <n> mas viejos\r\n" );
    xprintf( "  fs format          vacia la memoria\r\n" );
    xprintf( "\r\n" );
    xprintf( "  fs sd              estado de la microSD y sus lotes\r\n" );
    xprintf( "  fs sd list         lista los archivos de la tarjeta\r\n" );
    xprintf( "  fs sd dump         vuelca la ventana a un lote AHORA\r\n" );
    xprintf( "  fs sd retirar      vuelca el remanente y dice si ya se puede sacar\r\n" );
    xprintf( "  fs sd ver <arch>   muestra las primeras lineas de un lote\r\n" );
    xprintf( "  fs sd format borrar   FORMATEA la tarjeta en FAT (BORRA TODO)\r\n" );
    xprintf( "\r\n" );
    xprintf( "  Las tarjetas nuevas de mas de 32 GB vienen en exFAT, que este\r\n" );
    xprintf( "  firmware no lee: hay que formatearlas con 'fs sd format borrar'.\r\n" );
    xprintf( "  La palabra 'borrar' va a proposito: sin ella no hace nada.\r\n" );
    xprintf( "\r\n" );
    xprintf( "  'read' y 'pop' son operaciones distintas a proposito: un registro\r\n" );
    xprintf( "  se borra recien cuando el servidor confirmo que lo recibio.\r\n" );
    xprintf( "  Al cambiar la configuracion conviene 'fs format': los registros\r\n" );
    xprintf( "  guardados se armarian con los nombres NUEVOS y los datos VIEJOS.\r\n" );
}
//------------------------------------------------------------------------------
static void prvFsEstado( void )
{
    fs_datos_stats_t xSt;

    fs_datos_stats( &xSt );

    xprintf( "  guardados : %u de %u\r\n", ( unsigned ) xSt.usCount, ( unsigned ) xSt.usLength );
    xprintf( "  head/tail : %u / %u\r\n", ( unsigned ) xSt.usHead, ( unsigned ) xSt.usTail );

    /* Con timerpoll conocido, "cuántos registros quedan" se entiende mucho
       mejor como tiempo: es lo que dice cuánto puede estar sin transmitir. */
    if( xCfgBase.usTimerPoll > 0U )
    {
        uint32_t ulLibres = ( uint32_t ) ( xSt.usLength - xSt.usCount );
        uint32_t ulHoras  = ( ulLibres * xCfgBase.usTimerPoll ) / 3600UL;

        xprintf( "  autonomia : %lu registros libres = %lu h con timerpoll de %u s\r\n",
                 ( unsigned long ) ulLibres, ( unsigned long ) ulHoras,
                 ( unsigned ) xCfgBase.usTimerPoll );
    }

    if( xSt.ulPisados > 0U )
    {
        xprintf( "  [!] PERDIDOS: %lu registros pisados por memoria llena\r\n",
                 ( unsigned long ) xSt.ulPisados );
    }
}
//------------------------------------------------------------------------------
static void cmdFs( void )
{
    static dataRcd_t xDr;
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    if( ( ucArgs == 0U ) || ( argv[ 1 ] == NULL ) )
    {
        prvFsEstado();
        return;
    }

    if( strcmp( argv[ 1 ], "format" ) == 0 )
    {
        fs_datos_format();
        return;
    }

    if( strcmp( argv[ 1 ], "sd" ) == 0 )
    {
        /* 'fs sd' pelado: el estado. */
        if( ( ucArgs < 2U ) || ( argv[ 2 ] == NULL ) )
        {
            fs_sd_stats_t xSd;

            fs_sd_stats( &xSd );

            if( !xSd.bPresente )
            {
                xprintf( "  microSD    : NO (sin tarjeta, o no se pudo montar)\r\n" );
                xprintf( "  la ventana en EEPROM sigue funcionando igual\r\n" );
                return;
            }

            xprintf( "  microSD    : montada\r\n" );
            xprintf( "  lotes      : %u sin transmitir\r\n", ( unsigned ) xSd.usLotes );
            xprintf( "  proximo    : %s%04lu%s\r\n", FS_SD_PREFIJO,
                     ( unsigned long ) ( xSd.ulProximoLote % 10000UL ), FS_SD_EXTENSION );
            xprintf( "  libre      : %lu KB\r\n", ( unsigned long ) xSd.ulLibreKB );
            return;
        }

        if( strcmp( argv[ 2 ], "list" ) == 0 )
        {
            fs_sd_listar();
            return;
        }

        /*
         * `fs sd retirar`: el procedimiento para LLEVARSE la tarjeta.
         *
         * Existe por el modo `SILENT` (Pablo, 2026-09-12), donde los datos no se
         * transmiten nunca y la única forma de sacarlos es leyendo la microSD en
         * una PC. Antes de retirarla hay que volcar lo que quedó en la ventana,
         * que si no se pierde.
         *
         * ⚠ **No es un alias de `dump`, y la diferencia es el VEREDICTO.** Un
         * volcado fallido —tarjeta llena, error de escritura— deja la ventana
         * intacta a propósito… pero si el técnico saca la tarjeta igual, se
         * lleva datos incompletos **y no se entera hasta que abre los archivos
         * en la oficina**. Acá se le dice, en una línea, si puede sacarla o no.
         *
         * Que después sea seguro sacarla no es casualidad: `prvDesmontar()`
         * desmonta y **corta la alimentación** de la tarjeta al terminar cada
         * operación, así que al volver el prompt ya está fría.
         */
        if( strcmp( argv[ 2 ], "retirar" ) == 0 )
        {
            fs_datos_stats_t xVent;

            fs_datos_stats( &xVent );

            xprintf( "volcando el remanente de la ventana (%u registros)...\r\n",
                     ( unsigned ) xVent.usCount );

            bool bOk = fs_sd_volcar_ventana();

            fs_datos_stats( &xVent );

            if( bOk && ( xVent.usCount == 0U ) )
            {
                fs_sd_stats_t xSd;

                fs_sd_stats( &xSd );

                xprintf( "\r\nLISTO: la ventana quedo vacia y la tarjeta esta apagada.\r\n" );
                xprintf( "       YA PUEDE RETIRAR LA MICROSD (%u lotes guardados).\r\n",
                         ( unsigned ) xSd.usLotes );
            }
            else
            {
                xprintf( "\r\n[!] NO RETIRE LA TARJETA: quedan %u registros sin volcar.\r\n",
                         ( unsigned ) xVent.usCount );
                xprintf( "    revisar con 'fs sd' si hay tarjeta y si le queda lugar.\r\n" );
            }

            return;
        }

        if( strcmp( argv[ 2 ], "dump" ) == 0 )
        {
            xprintf( "volcando la ventana a la tarjeta...\r\n" );
            ( void ) fs_sd_volcar_ventana();
            prvFsEstado();
            return;
        }

        if( ( strcmp( argv[ 2 ], "ver" ) == 0 ) && ( ucArgs >= 3U ) && ( argv[ 3 ] != NULL ) )
        {
            fs_sd_ver( argv[ 3 ], 20U );
            return;
        }

        if( strcmp( argv[ 2 ], "format" ) == 0 )
        {
            /*
             * ⛔ La palabra de confirmación no es burocracia: esto borra los
             * lotes que todavía no se transmitieron. El parser ya exige el
             * comando completo, pero `fs sd format` es lo bastante parecido a
             * `fs format` —que sólo vacía la ventana— como para que un tipeo
             * apurado se lleve los datos de una instalación.
             */
            if( ( ucArgs < 3U ) || ( argv[ 3 ] == NULL ) ||
                ( strcmp( argv[ 3 ], "borrar" ) != 0 ) )
            {
                xprintf( "esto BORRA TODA la tarjeta, incluidos los lotes sin transmitir.\r\n" );
                xprintf( "si es lo que queres: 'fs sd format borrar'\r\n" );
                return;
            }

            ( void ) fs_sd_format();
            return;
        }

        prvFsUso();
        return;
    }

    if( ( ucArgs < 2U ) || ( argv[ 2 ] == NULL ) )
    {
        prvFsUso();
        return;
    }

    uint16_t usN = ( uint16_t ) atoi( argv[ 2 ] );

    if( strcmp( argv[ 1 ], "read" ) == 0 )
    {
        for( uint16_t i = 0U; i < usN; i++ )
        {
            if( !fs_datos_peek( &xDr, i ) )
            {
                xprintf( "  (no hay mas registros o el %u esta corrupto)\r\n", ( unsigned ) i );
                break;
            }
            xprintf( "[%u] ", ( unsigned ) i );
            tkSys_print( &xDr );
        }
        return;
    }

    if( strcmp( argv[ 1 ], "frame" ) == 0 )
    {
        static char pcFrame[ WAN_FRAME_BUFFER_SIZE ];

        if( !fs_datos_peek( &xDr, usN ) )
        {
            xprintf( "no hay registro %u\r\n", ( unsigned ) usN );
            return;
        }

        uint16_t usLargo = wan_frame_data( pcFrame, sizeof( pcFrame ), &xDr, true );

        if( usLargo > 0U )
        {
            ( void ) frtos_write( fdTERM, pcFrame, usLargo );
            xprintf( "\r\n  (%u bytes)\r\n", ( unsigned ) usLargo );
        }
        return;
    }

    if( strcmp( argv[ 1 ], "pop" ) == 0 )
    {
        xprintf( "descartados %u registros\r\n", ( unsigned ) fs_datos_pop( usN ) );
        prvFsEstado();
        return;
    }

    prvFsUso();
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
 * ⚠ 'reboot' EXISTIÓ Y SE SACÓ el 2026-09-08. No reponerlo sin leer esto.
 *
 * Era un reinicio TIBIO —saltaba al vector de reset sin tocar NRST— y existía
 * como EXPERIMENTO, para separar dos causas que desde afuera se ven iguales:
 *
 *   reboot anda y reset mata la placa -> es la LÍNEA DE NRST: algo colgado de
 *                                        ella apaga la fuente. Es hardware.
 *   los dos matan la placa            -> no es NRST.
 *
 * **El experimento dio su resultado, y fue el contrario del que se suponía.** Al
 * 2026-09-08, sobre la placa nueva: **`reset` anda** (reinicia y el arranque
 * siguiente informa `SOFT PIN`) y **`reboot` cuelga** — hay que cortar y reponer
 * la alimentación. O sea que **el problema nunca fue la línea de NRST**, y con
 * eso se cierra el pendiente que estaba anotado desde el bring-up.
 *
 * Y no anda por una razón de fondo, no por un detalle a corregir: saltar al
 * vector **sin resetear nada** deja el I2C, el SPI, las UARTs, el LPTIM y
 * FreeRTOS corriendo con sus interrupciones pendientes, y después los
 * `MX_*_Init()` los reprograman en caliente. Con el firmware chico del bring-up
 * eso sobrevivía; con el I2C hablándole a la EEPROM en el arranque para cargar
 * la configuración, no. Su propio comentario ya avisaba que no era un mecanismo
 * para producción.
 *
 * Si alguna vez hace falta de nuevo, el camino es `HAL_DeInit()` antes del
 * salto — pero conviene preguntarse primero qué agrega sobre `reset`, que
 * funciona.
 */
//------------------------------------------------------------------------------

#endif  /* TKCMD_MODO_BANCO */
//------------------------------------------------------------------------------
static void prvModbusUso( void )
{
    xprintf( "modbus                              estado del bus y de la configuracion\r\n" );
    xprintf( "modbus on | off                     los dos rieles: el SP3485 y el modulo\r\n" );
    xprintf( "modbus debug on | off               traza hexadecimal de lo que sale y entra\r\n" );
    xprintf( "modbus ch <0..4>                    lee UN canal de los configurados\r\n" );
    xprintf( "modbus poll                         lee TODOS los canales habilitados\r\n" );
    xprintf( "modbus read <sla> <reg> <nregs> <fcode> <tipo> <codec> <p10>\r\n" );
    xprintf( "                                    poleo generico, sin tocar la configuracion\r\n" );
    xprintf( "                                    tipo: U16|I16|U32|I32|FLOAT\r\n" );
    xprintf( "                                    codec: C0123|C1032|C3210|C2301\r\n" );
    xprintf( "modbus write <sla> <reg> <valor>    escribe UN registro (funcion 06)\r\n" );
    xprintf( "\r\n" );
    xprintf( "ej: modbus on ; modbus read 1 0 2 3 FLOAT C3210 0\r\n" );
}
//------------------------------------------------------------------------------
static void prvModbusEstado( void )
{
    uint8_t i;

    xprintf( "bus (SP3485) : %s\r\n",
             drv_rs485_power_estado( rs485RAIL_BUS ) ? "ENCENDIDO" : "apagado" );
    xprintf( "riel qmbus   : %s\r\n",
             drv_rs485_power_estado( rs485RAIL_QMBUS ) ? "ENCENDIDO" : "apagado" );
    xprintf( "debug        : %s\r\n", drv_modbus_debug_estado() ? "on" : "off" );
    xprintf( "intentos     : %u por canal, %u ms entre ellos\r\n",
             ( unsigned ) MODBUS_INTENTOS, ( unsigned ) MODBUS_MS_ENTRE_INTENTOS );
    xprintf( "timeout      : %u ms; silencio de trama: %u ms\r\n",
             ( unsigned ) DRV_MODBUS_MS_TIMEOUT, ( unsigned ) DRV_MODBUS_MS_SILENCIO );

    xprintf( "\r\nconfiguracion: %s, localaddr=%u\r\n",
             xCfgModbus.bEnabled ? "habilitado" : "DESHABILITADO",
             ( unsigned ) xCfgModbus.ucLocalAddr );

    for( i = 0U; i < CFG_MODBUS_NRO_CANALES; i++ )
    {
        const cfg_modbus_canal_t *p = &xCfgModbus.xCanal[ i ];

        xprintf( "  ch%u %s %-12s sla=%u reg=%u n=%u fc=%u %s %s /10^%u\r\n",
                 ( unsigned ) i, p->bEnabled ? "ON " : "off", p->pcName,
                 ( unsigned ) p->ucSlaveAddress, ( unsigned ) p->usRegAddress,
                 ( unsigned ) p->ucNroRegs, ( unsigned ) p->ucFcode,
                 cfg_modbus_tipo_str( p->eTipo ), cfg_modbus_codec_str( p->eCodec ),
                 ( unsigned ) p->ucDivisorP10 );
    }
}
//------------------------------------------------------------------------------
/*
 * Lee un canal y lo informa. Es la MISMA función que va a usar el poleo del
 * paso 6b (`modbus_leer_canal()`), no una copia de prueba: el mismo criterio
 * que `poll` con `tkSys_poll()`.
 */
static void prvModbusLeerCanal( const cfg_modbus_canal_t *pxCanal, uint8_t ucNro )
{
    float       fValor = 0.0f;
    mb_result_t eRes   = mbOK;

    xprintf( "ch%u [%s] sla=%u reg=%u: ", ( unsigned ) ucNro, pxCanal->pcName,
             ( unsigned ) pxCanal->ucSlaveAddress,
             ( unsigned ) pxCanal->usRegAddress );

    /* Con la traza encendida, las tramas se meten en el medio de esta línea y
       el resultado termina a tres renglones del canal al que pertenece. */
    if( drv_modbus_debug_estado() )
    {
        xprintf( "\r\n" );
    }

    if( modbus_leer_canal( pxCanal, &fValor, &eRes ) )
    {
        xprintf( "%.3f\r\n", fValor );
        return;
    }

    /*
     * ⭐ El error dice QUÉ pasó, y en el banco eso vale más que el valor: sin
     * respuesta manda a mirar el cableado y la dirección; un CRC malo, el ruido
     * y la velocidad; y una excepción dice que el enlace está PERFECTO y el
     * problema es el registro que pedimos.
     */
    xprintf( "ERROR: %s", drv_modbus_error_str( eRes ) );

    if( eRes == mbEXCEPCION )
    {
        xprintf( " (codigo %u)", ( unsigned ) drv_modbus_ultima_excepcion() );
    }

    xprintf( "\r\n" );
}
//------------------------------------------------------------------------------
static void cmdModbus( void )
{
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    if( ( ucArgs == 0U ) || ( argv[ 1 ] == NULL ) )
    {
        prvModbusEstado();
        return;
    }

    /* ---- los rieles ---- */
    bool bOn  = ( strcmp( argv[ 1 ], "on"  ) == 0 );
    bool bOff = ( strcmp( argv[ 1 ], "off" ) == 0 );

    if( bOn || bOff )
    {
        /*
         * Atajo de banco: prende el transceiver Y el riel del módulo. No
         * duplica nada —llama a `drv_rs485_power()`, igual que el comando
         * `rs485`— y evita el olvido más común, que es prender uno solo y no
         * entender por qué nadie contesta.
         */
        drv_rs485_power( rs485RAIL_BUS,   bOn );
        drv_rs485_power( rs485RAIL_QMBUS, bOn );

        if( bOn )
        {
            /* El SP3485 está listo en microsegundos; el caudalímetro no. Este
               tiempo es el mismo que espera el AVR antes de polear. */
            xprintf( "esperando %u ms a que arranque el modulo...\r\n",
                     ( unsigned ) MODBUS_MS_ARRANQUE_MODULO );
            vTaskDelay( pdMS_TO_TICKS( MODBUS_MS_ARRANQUE_MODULO ) );
        }

        prvModbusEstado();
        return;
    }

    /* ---- la traza ---- */
    if( strcmp( argv[ 1 ], "debug" ) == 0 )
    {
        if( ( ucArgs < 2U ) || ( argv[ 2 ] == NULL ) )
        {
            prvModbusUso();
            return;
        }

        drv_modbus_debug( strcmp( argv[ 2 ], "on" ) == 0 );
        xprintf( "debug %s\r\n", drv_modbus_debug_estado() ? "on" : "off" );
        return;
    }

    /* ---- un canal configurado ---- */
    if( strcmp( argv[ 1 ], "ch" ) == 0 )
    {
        if( ( ucArgs < 2U ) || ( argv[ 2 ] == NULL ) )
        {
            prvModbusUso();
            return;
        }

        long lCh = atol( argv[ 2 ] );

        if( ( lCh < 0 ) || ( lCh >= ( long ) CFG_MODBUS_NRO_CANALES ) )
        {
            xprintf( "ERROR: el canal va de 0 a %u\r\n",
                     ( unsigned ) ( CFG_MODBUS_NRO_CANALES - 1U ) );
            return;
        }

        prvModbusLeerCanal( &xCfgModbus.xCanal[ lCh ], ( uint8_t ) lCh );
        return;
    }

    /* ---- todos los habilitados ---- */
    if( strcmp( argv[ 1 ], "poll" ) == 0 )
    {
        uint8_t i;
        uint8_t ucLeidos = 0U;

        for( i = 0U; i < CFG_MODBUS_NRO_CANALES; i++ )
        {
            if( xCfgModbus.xCanal[ i ].bEnabled )
            {
                prvModbusLeerCanal( &xCfgModbus.xCanal[ i ], i );
                ucLeidos++;
            }
        }

        if( ucLeidos == 0U )
        {
            xprintf( "no hay ningun canal habilitado\r\n" );
        }
        return;
    }

    /* ---- poleo genérico: sin tocar la configuración ---- */
    if( strcmp( argv[ 1 ], "read" ) == 0 )
    {
        if( ( ucArgs < 8U ) || ( argv[ 8 ] == NULL ) )
        {
            prvModbusUso();
            return;
        }

        /*
         * ⭐ Se arma un canal TEMPORAL y se lee con la misma función que usa el
         * poleo. Así el comando de diagnóstico ejercita el camino de verdad
         * —codecs, tipos, divisor, reintentos— en vez de un atajo que podría
         * comportarse distinto justo en lo que se vino a probar.
         */
        cfg_modbus_canal_t xTmp;

        memset( &xTmp, 0, sizeof( xTmp ) );
        strcpy( xTmp.pcName, "test" );

        if( !cfg_modbus_parse_tipo( argv[ 6 ], &xTmp.eTipo ) )
        {
            xprintf( "ERROR: tipo invalido (U16|I16|U32|I32|FLOAT)\r\n" );
            return;
        }

        if( !cfg_modbus_parse_codec( argv[ 7 ], &xTmp.eCodec ) )
        {
            xprintf( "ERROR: codec invalido (C0123|C1032|C3210|C2301)\r\n" );
            return;
        }

        xTmp.bEnabled       = true;
        xTmp.ucSlaveAddress = ( uint8_t )  atol( argv[ 2 ] );
        xTmp.usRegAddress   = ( uint16_t ) atol( argv[ 3 ] );
        xTmp.ucNroRegs      = ( uint8_t )  atol( argv[ 4 ] );
        xTmp.ucFcode        = ( uint8_t )  atol( argv[ 5 ] );
        xTmp.ucDivisorP10   = ( uint8_t )  atol( argv[ 8 ] );

        prvModbusLeerCanal( &xTmp, 0U );
        return;
    }

    /* ---- escribir un registro ---- */
    if( strcmp( argv[ 1 ], "write" ) == 0 )
    {
        if( ( ucArgs < 4U ) || ( argv[ 4 ] == NULL ) )
        {
            prvModbusUso();
            return;
        }

        uint8_t     ucSla   = ( uint8_t )  atol( argv[ 2 ] );
        uint16_t    usReg   = ( uint16_t ) atol( argv[ 3 ] );
        uint16_t    usValor = ( uint16_t ) atol( argv[ 4 ] );
        uint16_t    usRta   = 0U;
        mb_result_t eRes    = drv_modbus_escribir( ucSla, usReg, usValor, &usRta );

        if( eRes == mbOK )
        {
            /* ⚠ El valor devuelto NO tiene por qué ser el eco: el control de
               presión contesta su status. Por eso se informa aparte. */
            xprintf( "OK: sla=%u reg=%u <- %u ; el esclavo devolvio 0x%04X\r\n",
                     ( unsigned ) ucSla, ( unsigned ) usReg, ( unsigned ) usValor,
                     ( unsigned ) usRta );
        }
        else
        {
            xprintf( "ERROR: %s", drv_modbus_error_str( eRes ) );

            if( eRes == mbEXCEPCION )
            {
                xprintf( " (codigo %u)", ( unsigned ) drv_modbus_ultima_excepcion() );
            }

            xprintf( "\r\n" );
        }
        return;
    }

    prvModbusUso();
}
//------------------------------------------------------------------------------
static void prvCpresUso( void )
{
    xprintf( "cpres                     estado: configuracion y riel\r\n" );
    xprintf( "cpres status              lee el registro del dispositivo\r\n" );
    xprintf( "cpres diurna              aplica la consigna DIURNA\r\n" );
    xprintf( "cpres nocturna            aplica la consigna NOCTURNA\r\n" );
    xprintf( "cpres open  v0 | v1       abre una valvula externa\r\n" );
    xprintf( "cpres close v0 | v1       la cierra\r\n" );
    xprintf( "\r\n" );
    xprintf( "⚠ cada orden tarda 30-45 s: el dispositivo mueve las electrovalvulas\r\n" );
    xprintf( "  de a una por consumo, y hay que esperar a que termine.\r\n" );
    xprintf( "⚠ estos comandos NO pasan por tkCtlPres: si la tarea esta viva puede\r\n" );
    xprintf( "  aplicar una consigna en el medio. Para trabajar tranquilo: kill cpres\r\n" );
}
//------------------------------------------------------------------------------
/*
 * Lee el status con el riel encendido. Es la única forma de ver el registro sin
 * mandar una orden — y sirve para medir cuánto tarda de verdad el dispositivo.
 */
static void prvCpresStatus( void )
{
    uint16_t    usStatus = 0U;
    mb_result_t eRes;

    if( !drv_rs485_tomar_bus( pdMS_TO_TICKS( 60000 ) ) )
    {
        xprintf( "el bus RS485 esta ocupado\r\n" );
        return;
    }

    drv_rs485_power( rs485RAIL_CPRES, true );
    drv_rs485_power( rs485RAIL_BUS,   true );
    vTaskDelay( pdMS_TO_TICKS( DRV_CPRES_MS_ARRANQUE ) );

    eRes = drv_cpres_leer_status( &usStatus );

    if( eRes == mbOK )
    {
        xprintf( "status = 0x%02X: %s\r\n", ( unsigned ) usStatus,
                 drv_cpres_status_idle( usStatus ) ? "IDLE" : "TRABAJANDO" );
        xprintf( "  V0: %s\r\n", drv_cpres_status_v0( usStatus ) );
        xprintf( "  V1: %s\r\n", drv_cpres_status_v1( usStatus ) );
        xprintf( "\r\n" );
        xprintf( "⚠ la posicion es SOLO diagnostico: el dispositivo la olvida al\r\n" );
        xprintf( "  quedarse sin alimentacion, asi que recien encendido dice\r\n" );
        xprintf( "  'desconocida' hasta que reciba una orden.\r\n" );
    }
    else
    {
        xprintf( "ERROR: %s", drv_modbus_error_str( eRes ) );

        if( eRes == mbEXCEPCION )
        {
            xprintf( " (codigo %u)", ( unsigned ) drv_modbus_ultima_excepcion() );
        }

        xprintf( "\r\n" );
    }

    drv_rs485_power( rs485RAIL_BUS,   false );
    drv_rs485_power( rs485RAIL_CPRES, false );
    drv_rs485_soltar_bus();
}
//------------------------------------------------------------------------------
static void prvCpresOrden( cpres_cmd_t eCmd )
{
    if( !drv_rs485_tomar_bus( pdMS_TO_TICKS( 60000 ) ) )
    {
        xprintf( "el bus RS485 esta ocupado\r\n" );
        return;
    }

    ( void ) drv_cpres_comando( eCmd );

    drv_rs485_soltar_bus();
}
//------------------------------------------------------------------------------
static void cmdCpres( void )
{
    uint8_t ucArgs = FRTOS_CMD_makeArgv();

    if( ( ucArgs == 0U ) || ( argv[ 1 ] == NULL ) )
    {
        xprintf( "consignas    : %s\r\n",
                 xCfgConsigna.bEnabled ? "HABILITADAS" : "deshabilitadas" );
        xprintf( "  diurna     : %04d\r\n",   ( int ) xCfgConsigna.usDiurna );
        xprintf( "  nocturna   : %04d\r\n",   ( int ) xCfgConsigna.usNocturna );
        xprintf( "riel cpres   : %s\r\n",
                 drv_rs485_power_estado( rs485RAIL_CPRES ) ? "ENCENDIDO" : "apagado" );
        xprintf( "esclavo      : 0x%02X, registro %u\r\n",
                 ( unsigned ) DRV_CPRES_SLAVE, ( unsigned ) DRV_CPRES_REG );
        xprintf( "tarea        : %s\r\n",
                 tkCtlPres_matada() ? "MATADA" : "viva (chequea cada 45 s)" );
        return;
    }

    if( strcmp( argv[ 1 ], "status" ) == 0 )
    {
        prvCpresStatus();
        return;
    }

    if( strcmp( argv[ 1 ], "diurna" ) == 0 )
    {
        prvCpresOrden( cpresCMD_CONSIGNA_DIURNA );
        return;
    }

    if( strcmp( argv[ 1 ], "nocturna" ) == 0 )
    {
        prvCpresOrden( cpresCMD_CONSIGNA_NOCTURNA );
        return;
    }

    bool bOpen  = ( strcmp( argv[ 1 ], "open"  ) == 0 );
    bool bClose = ( strcmp( argv[ 1 ], "close" ) == 0 );

    if( bOpen || bClose )
    {
        if( ( ucArgs < 2U ) || ( argv[ 2 ] == NULL ) )
        {
            prvCpresUso();
            return;
        }

        if( strcmp( argv[ 2 ], "v0" ) == 0 )
        {
            prvCpresOrden( bOpen ? cpresCMD_ABRIR_V0 : cpresCMD_CERRAR_V0 );
            return;
        }

        if( strcmp( argv[ 2 ], "v1" ) == 0 )
        {
            prvCpresOrden( bOpen ? cpresCMD_ABRIR_V1 : cpresCMD_CERRAR_V1 );
            return;
        }
    }

    prvCpresUso();
}
