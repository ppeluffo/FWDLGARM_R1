/*
 * tkCtl.c  -  ver tkCtl.h
 */

#include "tkCtl.h"
#include "drv_term_sense.h"
#include "main.h"

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

        vTaskDelay( pdMS_TO_TICKS( TKCTL_PERIOD_MS - TKCTL_LED_ON_MS ) );
    }
}
//------------------------------------------------------------------------------
