/*
 * tkCtl.c  -  ver tkCtl.h
 *
 * Se programa con la API NATIVA de FreeRTOS (vTaskDelay, xTaskCreateStatic...),
 * no con el wrapper CMSIS-RTOS.
 */

#include "tkCtl.h"
#include "drv_term_sense.h"
#include "drv_wdt.h"
#include "wdg.h"
#include "frtos-io.h"
#include "main.h"

/* Memoria estática de la tarea: definida acá una sola vez. */
TaskHandle_t xHandle_tkCtl;
StaticTask_t tkCtl_TCB;
StackType_t  tkCtl_Stack[ tkCtl_STACK_SIZE ];

#define TKCTL_PERIOD_MS     1000U   /* período del lazo                */
#define TKCTL_ARRANQUE_MS     500U   /* espera inicial antes del lazo   */
#define TKCTL_LED_ON_MS        50U   /* duración del destello           */

/* Vueltas seguidas con alguna tarea vencida. Sirve para avisar una sola vez. */
static uint32_t ulVueltasEnFalta = 0UL;

static void led_flash(void);
static void prvJuzgar(void);

//------------------------------------------------------------------------------
void tkCtl(void *pvParameters)
{
    ( void ) pvParameters;

    vTaskDelay( pdMS_TO_TICKS( TKCTL_ARRANQUE_MS ) );

    /*
     * ⭐ EL PERRO ARRANCA ACÁ Y NO EN `main()`.
     *
     * Para cuando esta tarea llega a su primera vuelta, el scheduler lleva medio
     * segundo corriendo y las otras cinco tareas ya pasaron por su
     * `wdg_registrar()` —todas son de prioridad `IDLE+1` o más y se registran en
     * su primera línea, antes de cualquier espera de arranque—.
     *
     * Si arrancara en `main()` el perro contaría desde antes de que existiera
     * ninguna tarea capaz de patearlo, que es justamente lo que obligaría a
     * sacarlo de ahí si lo generara CubeMX. Ver `drv_wdt.h`.
     */
    drv_wdt_arrancar();

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

        /*
         * ⭐ El juicio va DESPUÉS del destello y del poleo, no antes: así el LED
         * sigue latiendo mientras el equipo se está por resetear, y desde afuera
         * se ve un reinicio limpio en vez de un LED que se queda quieto y
         * después arranca. Es información para el que está mirando la placa.
         */
        prvJuzgar();

        vTaskDelay( pdMS_TO_TICKS( TKCTL_PERIOD_MS ) );
    }
}
//------------------------------------------------------------------------------
/*
 * El veredicto del watchdog, una vez por vuelta.
 *
 * ⛔ **No hay ningún "resetear ahora" acá, y es a propósito**: cuando algo no
 * cierra, esta función simplemente **deja de patear** y el IWDG resetea el micro
 * en ~32 s por su cuenta. Un `NVIC_SystemReset()` explícito dependería de que
 * esta misma tarea siga corriendo lo suficiente como para ejecutarlo — o sea,
 * de exactamente lo que el watchdog no puede dar por supuesto.
 *
 * El otro motivo es de diagnóstico: el reset del IWDG deja su marca en
 * `RCC_CSR.IWDGRSTF`, que **viaja al servidor en el campo `WDG`** del
 * `CONF_BASE`. Un reset por software se informaría como `SOFT`, indistinguible
 * de un `reset` tipeado por un técnico en la consola.
 */
static void prvJuzgar(void)
{
    const char *pcCulpable = NULL;

    if( wdg_todas_sanas( &pcCulpable ) )
    {
        drv_wdt_kick();
        ulVueltasEnFalta = 0UL;
        return;
    }

    /*
     * ⚠ El aviso sale UNA VEZ y después se cuenta, no se repite cada vuelta: son
     * ~32 segundos de agonía y a un segundo por línea serían 32 mensajes que
     * empujan fuera de la pantalla justamente al primero, que es el que dice
     * quién fue.
     */
    if( ulVueltasEnFalta == 0UL )
    {
        xprintf( "\r\n⛔ WATCHDOG: '%s' se paso del plazo (%lu ms).\r\n",
                 ( pcCulpable != NULL ) ? pcCulpable : "?",
                 ( unsigned long ) WDG_PLAZO_MS );
        xprintf( "   Dejo de patear el IWDG: el equipo se resetea en ~%lu s.\r\n",
                 ( unsigned long ) ( drv_wdt_ventana_ms() / 1000UL ) );
    }

    ulVueltasEnFalta++;
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
