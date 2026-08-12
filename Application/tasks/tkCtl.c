/*
 * tkCtl.c  -  ver tkCtl.h
 *
 * Se programa con la API NATIVA de FreeRTOS (vTaskDelay, xTaskCreateStatic...),
 * no con el wrapper CMSIS-RTOS.
 */

#include "tkCtl.h"
#include "drv_term_sense.h"
#include "main.h"

/* Memoria estática de la tarea: definida acá una sola vez. */
TaskHandle_t xHandle_tkCtl;
StaticTask_t tkCtl_TCB;
StackType_t  tkCtl_Stack[ tkCtl_STACK_SIZE ];

#define TKCTL_PERIOD_MS     1000U   /* período del lazo                */
#define TKCTL_ARRANQUE_MS     500U   /* espera inicial antes del lazo   */
#define TKCTL_LED_ON_MS       100U   /* duración del destello           */

static void led_flash(void);

//------------------------------------------------------------------------------
void tkCtl(void *pvParameters)
{
    ( void ) pvParameters;

    vTaskDelay( pdMS_TO_TICKS( TKCTL_ARRANQUE_MS ) );

    for( ;; )
    {
        /*
         * Poleo de TERM_SENSE. Sale gratis: esta tarea ya despierta acá para el
         * destello, así que leer un pin no agrega una sola despertada. Por eso
         * el pin NO está en EXTI — ver el porqué largo en drv_term_sense.h.
         *
         * La latencia de detección es una vuelta del lazo, y con la terminal
         * siendo un caso excepcional de monitoreo, eso no le molesta a nadie.
         */
        drv_term_sense_poll();

        led_flash();
        vTaskDelay( pdMS_TO_TICKS( TKCTL_PERIOD_MS ) );
    }
}
//------------------------------------------------------------------------------
static void led_flash(void)
{
    /*
     * OJO: acá NO va HAL_Delay(). Es una espera activa que se queda con la CPU
     * durante todo el retardo y no cede el procesador, así que ninguna tarea de
     * prioridad igual o menor puede correr mientras tanto. Dentro de una tarea
     * la espera se hace siempre con vTaskDelay(), que bloquea y libera la CPU.
     */
    HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_SET );
    vTaskDelay( pdMS_TO_TICKS( TKCTL_LED_ON_MS ) );
    HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_RESET );
}
//------------------------------------------------------------------------------
