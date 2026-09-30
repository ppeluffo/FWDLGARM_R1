/*
 * tkCtl.c  -  ver tkCtl.h
 */

#include "tkCtl.h"
#include "main.h"

/*
 * 50 ms encendido cada segundo, o sea 5 % de duty.
 *
 * ⚠ Las esperas van SIEMPRE con pdMS_TO_TICKS(): con el tick a 512 Hz,
 * portTICK_PERIOD_MS está envenenado a propósito en main.h y cualquier uso
 * falla en compilación. 1000 y 50 son múltiplos de 125, así que los dos dan un
 * número exacto de ticks.
 */
#define TKCTL_PERIOD_MS     1000U
#define TKCTL_LED_ON_MS       50U

TaskHandle_t xHandle_tkCtl;

/* Memoria estática: la tarea no toca el heap. */
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
        vTaskDelay( pdMS_TO_TICKS( TKCTL_PERIOD_MS - TKCTL_LED_ON_MS ) );
    }
}
//------------------------------------------------------------------------------
