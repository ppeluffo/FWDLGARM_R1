/*
 * FWDLGARM_R1_tkCtl.c
 *
 * Tarea de control. Es la primera que arranca y, por ahora, lo único que hace es
 * dar señales de vida por el LED.
 *
 * Se programa con la API NATIVA de FreeRTOS (vTaskDelay, xTaskCreateStatic...),
 * no con el wrapper CMSIS-RTOS. main.h ya trae FreeRTOS.h y task.h.
 */

#include "main.h"

#define TKCTL_PERIOD_MS      1000U   /* período del lazo                */
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
