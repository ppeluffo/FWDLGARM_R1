/*
 * tkCtl.c  -  ver tkCtl.h
 */

#include "tkCtl.h"
#include "drv_term_sense.h"
#include "main.h"
#include "pwr_lock.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;

/*
 * ---------------------------------------------------------------------------
 * LATIDO POR LA CONSOLA  (2026-09-30)
 * ---------------------------------------------------------------------------
 * En 1, tkCtl emite cada TKCTL_LATIDO_S segundos una línea con el estado, POR
 * POLEO —HAL_UART_Transmit() directo, sin el driver, sin ISR, sin semáforo y
 * sin candado de energía—.
 *
 * ⭐ Existe porque la consola tenía un punto ciego: tkCmd emite el banner UNA
 * SOLA VEZ al arrancar y después se bloquea esperando un carácter, así que si
 * el minicom se abre un segundo tarde **no se ve nada nunca más** — y sin RX no
 * se puede tipear para provocar salida. Desde afuera eso se lee idéntico a un
 * TX roto.
 *
 * Lo que informa es justo lo que decide el diagnóstico del RX:
 *
 *   PB5=1  candados=0x00  ->  el equipo duerme en Stop 2 y la USART NO RECIBE.
 *                             O no hay terminal, o TERM_SENSE no está cableado.
 *   PB5=0  candados=0x01  ->  el candado está tomado, el equipo corre en Sleep
 *                             y el RX tiene que funcionar.
 *
 * ⚠ Es instrumento de banco: cuesta ~50 ms de CPU por latido a 9600 baudios y
 * deja de tener sentido cuando la consola ande. Se apaga poniendo esto en 0.
 */
#define TKCTL_LATIDO      1
#define TKCTL_LATIDO_S    5U

/*
 * ---------------------------------------------------------------------------
 * FORZAR EL CANDADO DE LA TERMINAL  (2026-09-30)
 * ---------------------------------------------------------------------------
 * En 1, pwrLOCK_TERM se toma SIEMPRE, sin mirar TERM_SENSE.
 *
 * ⭐ Existe porque en esta placa PB5 **sube solo** a los pocos segundos de
 * arrancar —medido con el latido: `[5] PB5=0` y `[10] PB5=1` con la terminal
 * enchufada todo el tiempo—. Al soltarse el candado el equipo baja a Stop 2,
 * donde la USART NO RECIBE, y la consola queda muda: el TX sigue saliendo, así
 * que desde afuera se lee como un RX roto.
 *
 * ⛔ Y esto NO arregla nada: es un puente para poder seguir trabajando. Que
 * PB5 suba solo es de cableado o de hardware y se resuelve aparte — con el
 * tester en el pin, que es la medición que dice si el nivel es real.
 *
 * ⚠ El costo es que el equipo NUNCA duerme: ~3,5 mA en vez de µA. Para el banco
 * es exactamente lo que se quiere; para medir consumo hay que ponerlo en 0.
 *
 * El latido sigue informando el nivel CRUDO de PB5 igual, así que cuando el pin
 * se comporte bien se va a ver, y ahí esto se apaga.
 */
#define TKCTL_FORZAR_TERM 1

/*
 * Destello del LED y poleo de TERM_SENSE, en la misma vuelta.
 *
 * 50 ms encendido cada segundo, o sea 5 % de duty.
 *
 * ⚠ Las esperas van SIEMPRE con pdMS_TO_TICKS(): con el tick a 512 Hz,
 * portTICK_PERIOD_MS está envenenado a propósito en main.h y cualquier uso
 * falla en compilación. 1000 y 50 son múltiplos de 125, así que los dos dan
 * un número exacto de ticks.
 */
#define TKCTL_PERIOD_MS     1000U
#define TKCTL_LED_ON_MS       50U

/* Memoria estática: la tarea no toca el heap. */
TaskHandle_t xHandle_tkCtl;

StaticTask_t tkCtl_TCB;
StackType_t  tkCtl_Stack[ tkCtl_STACK_SIZE ];

//------------------------------------------------------------------------------
void tkCtl( void *pvParameters )
{
    ( void ) pvParameters;

#if ( TKCTL_LATIDO == 1 )
    uint32_t ulVueltas = 0U;
    char     cLinea[ 80 ];
#endif

    for( ;; )
    {
        /*
         * La espera se hace con vTaskDelay(), que BLOQUEA y libera la CPU. Un
         * lazo de poleo acá despertaría al micro 512 veces por segundo y
         * anularía el tickless, que es de donde salen los µA de reposo.
         */
        HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_SET );
        vTaskDelay( pdMS_TO_TICKS( TKCTL_LED_ON_MS ) );

        HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_RESET );

        /*
         * ⛔ El poleo de TERM_SENSE va acá, y NO es cosmético: sin él nunca se
         * toma pwrLOCK_TERM, el equipo se queda en Stop 2 y la USART NO PUEDE
         * RECIBIR. La consola queda muda y el síntoma es desconcertante, porque
         * el TX funciona igual: se ve el banner y no se acepta nada.
         *
         * Se polea y no va por EXTI a propósito: esta tarea ya despierta cada
         * segundo para el destello, así que leer un pin acá no agrega ni una
         * despertada. Por EXTI costaría caro — el conector rebota, y se midieron
         * hasta 82 interrupciones por un solo enchufe, cada una sacando al micro
         * de Stop 2. Muestrear un nivel una vez por segundo es antirrebote
         * perfecto y gratis.
         */
        drv_term_sense_poll();

#if ( TKCTL_FORZAR_TERM == 1 )
        /* Va DESPUÉS del poleo, que es quien suelta el candado cuando lee PB5
           en alto: si fuera antes, el poleo lo pisaría en la misma vuelta. */
        pwr_lock_acquire( pwrLOCK_TERM );
#endif

#if ( TKCTL_LATIDO == 1 )
        /* Va DESPUÉS del poleo, así el nivel y el candado que informa son los
           de esta misma vuelta y no los de la anterior. */
        ulVueltas++;

        if( ( ulVueltas % TKCTL_LATIDO_S ) == 0U )
        {
            int n = snprintf( cLinea, sizeof( cLinea ),
                              "[%lu] PB5=%d terminal=%s candados=0x%02lX%s\r\n",
                              ( unsigned long ) ulVueltas,
                              drv_term_sense_nivel_pin() ? 1 : 0,
                              drv_term_sense_presente() ? "SI" : "no",
                              ( unsigned long ) pwr_lock_estado(),
                              ( TKCTL_FORZAR_TERM == 1 ) ? "  (TERM forzado)" : "" );

            if( n > 0 )
            {
                ( void ) HAL_UART_Transmit( &huart1, ( const uint8_t * ) cLinea,
                                            ( uint16_t ) n, HAL_MAX_DELAY );
            }
        }
#endif

        vTaskDelay( pdMS_TO_TICKS( TKCTL_PERIOD_MS - TKCTL_LED_ON_MS ) );
    }
}
//------------------------------------------------------------------------------
