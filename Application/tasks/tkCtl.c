/*
 * tkCtl.c  -  ver tkCtl.h
 */

#include <stdio.h>
#include <string.h>

/*
 * ⛔ `main.h` VA PRIMERO, y no es cosmético: de ahí sale `ARRANCA_WDG`, y los
 * `#if` de este archivo lo consultan. Un `#if` sobre un `#define` que todavía no
 * se vio se lee como **0 y compila el bloque equivocado sin decir nada** — ya
 * pasó dos veces en este proyecto, una de ellas con un `#include`.
 */
#include "main.h"
#include "eventos.h"

#include "tkCtl.h"
#include "drv_term_sense.h"
#include "drv_uart.h"
#include "pwr_lock.h"

#if ( ARRANCA_WDG == 1 )
#include "drv_wdt.h"
#include "wdg.h"
#include "frtos-io.h"
#endif

extern UART_HandleTypeDef huart1;

/*
 * ---------------------------------------------------------------------------
 * EL LATIDO  (2026-09-30)
 * ---------------------------------------------------------------------------
 * En 1, cada vuelta emite una línea de estado POR POLEO —HAL_UART_Transmit()
 * directo, sin drv_uart, sin ISR, sin semáforo y sin candado—.
 *
 * ⭐ Existe porque la consola tiene un punto ciego que ya nos costó dos días:
 * cuando el RX deja de funcionar **no hay forma de preguntarle nada al equipo**,
 * y el banner sale una sola vez al arrancar. Por poleo el TX habla igual, aunque
 * el driver esté trabado o el equipo esté girando sin dormir.
 *
 * Informa las tres cosas que decidieron cada diagnóstico de esta etapa:
 *
 *   PB5       el nivel CRUDO del pin, no la creencia del driver
 *   candados  con alguno tomado el idle GIRA: ~9,5 mA, no 3 µA
 *   errUART   un ORE pegado trabaría el RX; si está en 0, el RX no es eso
 *
 * ⚠ Es instrumento de banco y ensucia la consola, así que queda en 0. **No
 * borrarlo**: se sacó una vez cuando la consola empezó a andar y hubo que
 * reponerlo a las pocas horas, porque el punto ciego reaparece cada vez que el
 * RX falla. Prenderlo es la primera cosa a hacer si la consola vuelve a quedar
 * muda.
 */
#define TKCTL_LATIDO      0

/*
 * ---------------------------------------------------------------------------
 * EL PERÍODO DE LA VUELTA: 5 s  (decidido por Pablo, 2026-09-30)
 * ---------------------------------------------------------------------------
 * Con 1 s, el destello generaba ~7200 despertadas por hora —dos por vuelta, una
 * para encender y otra para apagar— y el grueso del costo de cada una no es el
 * trabajo que hace: es salir de Stop 2, rehacer SystemClock_Config() para
 * arrancar el PLL, y volver a dormir. A 5 s bajan a ~1440.
 *
 * ⚠ El precio es que **la terminal se detecta hasta 5 s tarde**. Al enchufarla,
 * el equipo sigue durmiendo hasta la próxima vuelta, así que los primeros
 * caracteres que se tipeen se pierden. No es una falla: es este número.
 *
 * ⏳ Y cuando entre el watchdog hay que repasar los tres juntos —destello, poleo
 * y kick comparten esta vuelta—, no subir sólo uno.
 */
#define TKCTL_PERIOD_MS     5000U
#define TKCTL_LED_ON_MS       50U   /* 1 % de duty */

#if ( ARRANCA_WDG == 1 )
/*
 * ⛔⛔ ESTE PERÍODO ES EL QUE PATEA EL IWDG, y su ventana son ~32,8 s.
 *
 * Verificado en esta placa con `-ob displ` (2026-10-09): **`IWDG_STOP = 1`**, o
 * sea que el perro **sigue contando durante el Stop 2** — donde este firmware
 * vive el 98 % del tiempo—. Así que no alcanza con que el equipo despierte: hay
 * que **patear** dentro de la ventana, y el único que patea es esta vuelta.
 *
 * Con 5 s el margen es de **6,5×**. ⚠ Pero el pendiente del paso 9 propone subir
 * este número justamente para ahorrar despertadas, y ahí deja de ser una
 * preferencia: **el techo pasa a ser duro**. Por eso el assert — para que el día
 * que alguien lo suba, lo diga el compilador y no el equipo reseteándose en
 * campo.
 */
_Static_assert( TKCTL_PERIOD_MS <= 15000U,
                "tkCtl patea el IWDG, cuya ventana son ~32,8 s: con un periodo "
                "mayor el equipo se resetea solo. Ver drv_wdt.h antes de subirlo." );
#endif

#if ( ARRANCA_WDG == 1 )
static void prvJuzgar( void );
#endif

TaskHandle_t xHandle_tkCtl;

/* Memoria estática: la tarea no toca el heap. */
StaticTask_t tkCtl_TCB;
StackType_t  tkCtl_Stack[ tkCtl_STACK_SIZE ];

//------------------------------------------------------------------------------
void tkCtl( void *pvParameters )
{
    ( void ) pvParameters;

    /* Deja el candado sincronizado con el pin antes de la primera vuelta: si
       hay terminal enchufada al arrancar, no se duerme ni una vez. */
    drv_term_sense_init();

#if ( ARRANCA_WDG == 1 )
    /*
     * ⭐ EL PERRO ARRANCA ACÁ Y NO EN `main()`.
     *
     * En `main()` contaría desde antes de que existiera ninguna tarea capaz de
     * patearlo —que es justamente lo que obligaría a sacarlo de ahí si lo
     * generara CubeMX, ver `drv_wdt.h`—. Acá el scheduler ya corre y la primera
     * vuelta de este lazo patea enseguida.
     *
     * ⚠ No hace falta esperar a que las demás tareas se registren: una tarea sin
     * registrar queda en `wdgSIN_REGISTRAR` y `wdg_todas_sanas()` **la ignora**.
     * Eso es lo que hace que `tkFlow` —que hoy no arranca— no cuente como
     * colgada, y lo mismo vale para el instante del arranque.
     */
    drv_wdt_arrancar();
#endif

    for( ;; )
    {
        /*
         * La espera se hace con vTaskDelay(), que BLOQUEA y libera la CPU. Un
         * lazo de poleo acá despertaría al micro 512 veces por segundo y
         * anularía el tickless, que es de donde salen los 3 µA de reposo.
         */
        HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_SET );
        vTaskDelay( pdMS_TO_TICKS( TKCTL_LED_ON_MS ) );

        HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_RESET );

        /*
         * ⭐ Acá está TODA la política de energía del equipo, y son dos líneas
         * porque el driver ya la tiene adentro: drv_term_sense_poll() lee el
         * NIVEL de PB5 y toma o suelta pwrLOCK_TERM según lo que vea.
         *
         *   sin terminal -> suelta el candado -> el equipo entra en Stop 2
         *   con terminal -> toma el candado   -> el equipo NO duerme y la
         *                                        USART puede recibir
         *
         * Se lee el nivel y no un flanco: un esquema de flancos puede quedar
         * invertido de forma permanente si se pierde uno, mientras que un nivel
         * muestreado converge solo, siempre.
         *
         * Y se POLEA en vez de ir por EXTI porque esta tarea ya despierta igual
         * para el destello: leer un pin acá no agrega ni una despertada. Por
         * EXTI costaría caro — el conector rebota, y se midieron hasta 82
         * interrupciones por un solo enchufe, cada una sacando al micro de
         * Stop 2.
         */
        drv_term_sense_poll();

#if ( TKCTL_LATIDO == 1 )
        {
            /* Va DESPUÉS del poleo: así el nivel y el candado que informa son
               los de esta misma vuelta y no los de la anterior. */
            static uint32_t ulVuelta = 0U;
            char            cLinea[ 80 ];

            int n = snprintf( cLinea, sizeof( cLinea ),
                              "[%lu] PB5=%d term=%s cand=0x%02lX errUART=0x%08lX\r\n",
                              ( unsigned long ) ++ulVuelta,
                              drv_term_sense_nivel_pin() ? 1 : 0,
                              drv_term_sense_presente() ? "SI" : "no",
                              ( unsigned long ) pwr_lock_estado(),
                              ( unsigned long ) drv_uart_errores( drvUART_TERM ) );

            if( n > 0 )
            {
                ( void ) HAL_UART_Transmit( &huart1, ( const uint8_t * ) cLinea,
                                            ( uint16_t ) n, HAL_MAX_DELAY );
            }
        }
#endif

#if ( ARRANCA_WDG == 1 )
        /*
         * ⭐ El juicio va DESPUÉS del destello y del poleo, no antes: así el LED
         * sigue latiendo mientras el equipo se está por resetear, y desde afuera
         * se ve un reinicio limpio en vez de un LED que se queda quieto y
         * después arranca. Es información para el que está mirando la placa.
         */
        prvJuzgar();
#endif

        vTaskDelay( pdMS_TO_TICKS( TKCTL_PERIOD_MS - TKCTL_LED_ON_MS ) );
    }
}
//------------------------------------------------------------------------------
#if ( ARRANCA_WDG == 1 )
/*
 * El veredicto del watchdog, una vez por vuelta.
 *
 * ⛔ **No hay ningún "resetear ahora" acá, y es a propósito**: cuando algo no
 * cierra, esta función simplemente **deja de patear** y el IWDG resetea el micro
 * por su cuenta. Un `NVIC_SystemReset()` explícito dependería de que esta misma
 * tarea siga corriendo lo suficiente como para ejecutarlo — o sea, de
 * exactamente lo que el watchdog no puede dar por supuesto.
 *
 * El otro motivo es de diagnóstico: el reset del IWDG deja su marca en
 * `RCC_CSR.IWDGRSTF`, que **viaja al servidor en el campo `WDG`** del
 * `CONF_BASE` (`wanRESET_IWDG`). Un reset por software se informaría como
 * `SOFT`, indistinguible de un `reset` tipeado por un técnico en la consola — y
 * en campo esa es toda la diferencia entre "se reinició solo" y "alguien lo
 * reinició".
 */
static void prvJuzgar( void )
{
    /* Vueltas seguidas con alguna tarea vencida: para avisar UNA sola vez. */
    static uint32_t ulVueltasEnFalta = 0UL;

    const char *pcCulpable = NULL;

    if( wdg_todas_sanas( &pcCulpable ) )
    {
        drv_wdt_kick();
        ulVueltasEnFalta = 0UL;
        return;
    }

    /*
     * ⚠ El aviso sale UNA VEZ y después se cuenta, no se repite cada vuelta: son
     * ~32 segundos de agonía, y a una línea por vuelta el primer mensaje —el
     * único que dice QUIÉN fue— se iría de la pantalla empujado por los demás.
     */
    if( ulVueltasEnFalta == 0UL )
    {
        xprintf( "\r\n[X] WATCHDOG: '%s' se paso del plazo (%lu ms).\r\n",
                 ( pcCulpable != NULL ) ? pcCulpable : "?",
                 ( unsigned long ) WDG_PLAZO_MS );
        xprintf( "    Dejo de patear el IWDG: el equipo se resetea en ~%lu s.\r\n",
                 ( unsigned long ) ( drv_wdt_ventana_ms() / 1000UL ) );

        /* ⭐ Sin hora a propósito: la tarea colgada puede tener el bus I2C. El
           evento queda en la SRAM2 y llega a la microSD en el arranque que
           sigue — que es lo que hace que el log diga QUIÉN fue. */
        eventos_log_sin_hora( "[X] WATCHDOG: '%s' se paso del plazo: reset en ~%lu s",
                              ( pcCulpable != NULL ) ? pcCulpable : "?",
                              ( unsigned long ) ( drv_wdt_ventana_ms() / 1000UL ) );
    }

    ulVueltasEnFalta++;
}
#endif
//------------------------------------------------------------------------------
