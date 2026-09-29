/*
 * tkCtl.c  -  ver tkCtl.h
 */

#include "tkCtl.h"
#include "drv_term_sense.h"
#include "main.h"

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

        vTaskDelay( pdMS_TO_TICKS( TKCTL_PERIOD_MS - TKCTL_LED_ON_MS ) );
    }
}
//------------------------------------------------------------------------------
