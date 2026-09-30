/*
 * tkCtl.c  -  ver tkCtl.h
 */


#include "tkCtl.h"
#include "pwr_lock.h"
#include "frtos-io.h"
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

/*
 * ---------------------------------------------------------------------------
 * LA VENTANA ANTES DE DORMIR  (2026-09-30)
 * ---------------------------------------------------------------------------
 * El equipo arranca con pwrLOCK_TERM tomado —o sea sin dormir— y lo suelta a
 * los TKCTL_SEG_DESPIERTO segundos. A partir de ahí entra en Stop 2 entre
 * despertadas y **la consola deja de recibir**, porque en Stop 2 la USART no
 * puede.
 *
 * ⭐ La ventana existe para poder trabajar: sin ella el banner sale y un
 * segundo después el equipo ya no escucha, así que no hay forma de ver el
 * estado ni de tipear un comando. Con diez segundos alcanza para leer el
 * arranque y correr un `status`.
 *
 * ⚠ Que la consola se muera después NO es una falla: es la prueba de que el
 * tickless entró. Lo que hay que mirar entonces es el amperímetro.
 *
 * ⏳ Cuando entre TERM_SENSE, el candado lo va a tomar y soltar el PIN en vez
 * del reloj — y este mecanismo desaparece. Se usa el mismo candado a propósito,
 * así el camino que se valida ahora es el que va a quedar.
 */
#define TKCTL_SEG_DESPIERTO   10U

TaskHandle_t xHandle_tkCtl;

/* Memoria estática: la tarea no toca el heap. */
StaticTask_t tkCtl_TCB;
StackType_t  tkCtl_Stack[ tkCtl_STACK_SIZE ];

static volatile bool bDurmiendo = false;

//------------------------------------------------------------------------------
bool tkCtl_durmiendo( void )
{
    return bDurmiendo;
}
//------------------------------------------------------------------------------
void tkCtl( void *pvParameters )
{
    ( void ) pvParameters;

    uint32_t ulSegundos = 0U;

    /* Sin dormir hasta que se cumpla la ventana. Se toma acá y no en main()
       porque los candados son de tareas, y acá ya corre el scheduler. */
    pwr_lock_acquire( pwrLOCK_TERM );

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

        if( ( bDurmiendo == false ) && ( ++ulSegundos >= TKCTL_SEG_DESPIERTO ) )
        {
            /*
             * El aviso va ANTES de soltar el candado, y eso importa: xprintf
             * bloquea hasta que el último byte salió, así que el mensaje se
             * transmite entero con el equipo todavía despierto. Al revés, el
             * Stop 2 podría cortar la trama a mitad.
             */
            xprintf( "\r\n[!] %lu s: entrando en TICKLESS. La consola deja de "
                     "recibir; el LED sigue destellando.\r\n",
                     ( unsigned long ) ulSegundos );

            bDurmiendo = true;
            pwr_lock_release( pwrLOCK_TERM );
        }

        vTaskDelay( pdMS_TO_TICKS( TKCTL_PERIOD_MS - TKCTL_LED_ON_MS ) );
    }
}
//------------------------------------------------------------------------------
