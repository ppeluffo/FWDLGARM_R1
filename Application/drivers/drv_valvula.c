/*
 * drv_valvula.c  -  ver drv_valvula.h
 */

#include "drv_valvula.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/*
 * El estado que cree el driver. `volatile` no hace falta —no lo toca ninguna
 * ISR— pero sí protegerlo del solapamiento, y de eso se ocupa el mutex: sólo se
 * escribe adentro del movimiento.
 */
static drv_valvula_estado_t xEstado    = valvulaABIERTA;
static bool                 bAsumido   = true;
static uint32_t             ulMovimientos = 0UL;

/* Mutex estático: como todo lo demás en este firmware, no toca el heap. */
static StaticSemaphore_t xMutexBuffer;
static SemaphoreHandle_t xMutex = NULL;

/*==============================================================================
 * Interno
 *============================================================================*/

static void prvPwr( bool bOn )
{
    /* TPS22810: EN = 1 prende. Es el mismo criterio que los rieles del RS485 y
       el del divisor de 12 V, y el OPUESTO al SI2301 de la microSD. */
    HAL_GPIO_WritePin( EN_EV_TOYI_GPIO_Port, EN_EV_TOYI_Pin,
                       bOn ? GPIO_PIN_SET : GPIO_PIN_RESET );
}
//------------------------------------------------------------------------------
static void prvCtl( bool bAbrir )
{
    HAL_GPIO_WritePin( CTL_EV_TOYI_GPIO_Port, CTL_EV_TOYI_Pin,
                       bAbrir ? GPIO_PIN_SET : GPIO_PIN_RESET );
}
//------------------------------------------------------------------------------
/*
 * El movimiento, que es todo el driver. La secuencia está explicada en el header;
 * acá está sólo el porqué de cada espera.
 */
static bool prvMover( bool bAbrir )
{
    if( xMutex == NULL )
    {
        return false;                   /* nadie llamó a drv_valvula_init() */
    }

    /* Sin espera: si hay un movimiento en curso, el que llega se va con false.
       Encolar movimientos de una válvula no significa nada útil. */
    if( xSemaphoreTake( xMutex, 0 ) != pdTRUE )
    {
        return false;
    }

    /* 1. La dirección PRIMERO, y que se asiente antes de que llegue la energía:
          si no, el servo arranca hacia el lado anterior y después corrige. */
    prvCtl( bAbrir );
    vTaskDelay( pdMS_TO_TICKS( DRV_VALVULA_MS_DIRECCION ) );

    /* 2. Recién ahora se energiza. */
    prvPwr( true );

    /* 3. El recorrido. La tarea queda bloqueada, no girando, y el micro se
          duerme en Stop 2 mientras tanto: los GPIO conservan su estado. */
    vTaskDelay( pdMS_TO_TICKS( DRV_VALVULA_MS_RECORRIDO ) );

    /* 4. Cortar la energía. Desde acá la válvula se queda donde está. */
    prvPwr( false );

    /* 5. Y sólo después de que el riel del servo bajó, `CTL` vuelve a su reposo.
          Bajarlo antes sería darle una orden de cierre a un servo que todavía
          está alimentado; dejarlo alto sería back-powering. */
    vTaskDelay( pdMS_TO_TICKS( DRV_VALVULA_MS_DESCARGA ) );
    prvCtl( false );

    xEstado  = bAbrir ? valvulaABIERTA : valvulaCERRADA;
    bAsumido = false;                   /* ya no es una suposición: se movió */
    ulMovimientos++;

    xSemaphoreGive( xMutex );

    return true;
}

/*==============================================================================
 * API pública
 *============================================================================*/

void drv_valvula_init( void )
{
    /* Estado seguro antes que nada: sin alimentación y con la dirección en
       "cerrar". Los pines ya salen así de MX_GPIO_Init(), que los inicializa en
       0; esto lo deja dicho igual, porque depender de un default del `.ioc` para
       algo que mueve un actuador no es aceptable. */
    prvPwr( false );
    prvCtl( false );

    /* La incógnita del arranque: se asume el estado peligroso —abierta— para que
       el cierre de arranque tenga sentido. Ver el header. */
    xEstado       = valvulaABIERTA;
    bAsumido      = true;
    ulMovimientos = 0UL;

    if( xMutex == NULL )
    {
        xMutex = xSemaphoreCreateMutexStatic( &xMutexBuffer );
    }
}
//------------------------------------------------------------------------------
bool drv_valvula_abrir( void )
{
    return prvMover( true );
}
//------------------------------------------------------------------------------
bool drv_valvula_cerrar( void )
{
    return prvMover( false );
}
//------------------------------------------------------------------------------
drv_valvula_estado_t drv_valvula_estado( void )
{
    return xEstado;
}
//------------------------------------------------------------------------------
bool drv_valvula_estado_asumido( void )
{
    return bAsumido;
}
//------------------------------------------------------------------------------
uint32_t drv_valvula_movimientos( void )
{
    return ulMovimientos;
}
//------------------------------------------------------------------------------
void drv_valvula_pin_pwr( bool bOn )
{
    prvPwr( bOn );
}
//------------------------------------------------------------------------------
void drv_valvula_pin_ctl( bool bOn )
{
    prvCtl( bOn );
}
//------------------------------------------------------------------------------
bool drv_valvula_pin_pwr_estado( void )
{
    return ( HAL_GPIO_ReadPin( EN_EV_TOYI_GPIO_Port, EN_EV_TOYI_Pin ) == GPIO_PIN_SET );
}
//------------------------------------------------------------------------------
bool drv_valvula_pin_ctl_estado( void )
{
    return ( HAL_GPIO_ReadPin( CTL_EV_TOYI_GPIO_Port, CTL_EV_TOYI_Pin ) == GPIO_PIN_SET );
}
//------------------------------------------------------------------------------
