/*
 * wdg.c  -  ver wdg.h
 */

#include "wdg.h"
#include "drv_wdt.h"
#include "frtos-io.h"

typedef enum {
    wdgSIN_REGISTRAR = 0,
    wdgVIGILADA,
    wdgMATADA
} wdg_estado_t;

typedef struct {
    volatile wdg_estado_t eEstado;
    volatile TickType_t   xVence;    /* el instante en que se la da por colgada */
    TaskHandle_t          xHandle;   /* quién es, para resolver `wdg_kick()`    */
} wdg_entrada_t;

static wdg_entrada_t xTabla[ wdgTK_CANTIDAD ];

static const char * const pcNombres[ wdgTK_CANTIDAD ] = {
    "tkCmd", "tkSys", "tkWan", "tkCtlPres", "tkFlow"
};

/*
 * ⚠ SIN SECCIÓN CRÍTICA, Y ES CORRECTO.
 *
 * Cinco tareas escriben su propia fila y `tkCtl` las lee todas. No hay sección
 * crítica porque no hace falta: en un Cortex-M4 una escritura de 32 bits
 * alineada es **atómica por construcción**, así que el lector nunca ve un valor
 * a medio escribir. Y cada tarea es la única que escribe su fila.
 *
 * Lo peor que puede pasar es que `tkCtl` lea una fila un instante antes de que
 * se renueve y la dé por vencida. Eso no ocurre: el plazo es de 90 s y el
 * reporte llega cada 60 como mucho, así que entre el vencimiento y el reporte
 * siempre hay 30 s de distancia.
 */

//------------------------------------------------------------------------------
static bool prvVencida( TickType_t xVence )
{
    /*
     * ⚠ La comparación va por DIFERENCIA y no con `>=`, por el rollover del
     * contador de ticks. Con el tick a 512 Hz, un `uint32_t` da la vuelta cada
     * **97 días** — y este equipo corre 7x24, así que no es un caso teórico:
     * ocurre unas cuatro veces por año.
     *
     * Con `xTaskGetTickCount() >= xVence` un plazo fijado justo antes de la
     * vuelta quedaría con `xVence` enorme y `ahora` chico: la tarea se vería
     * sana durante 97 días. Con la resta, el resultado da chico y positivo sólo
     * cuando el plazo pasó de verdad.
     */
    return ( ( TickType_t ) ( xTaskGetTickCount() - xVence ) )
           < ( TickType_t ) 0x80000000U;
}
//------------------------------------------------------------------------------
void wdg_registrar( wdg_tarea_t eTarea )
{
    if( eTarea < wdgTK_CANTIDAD )
    {
        xTabla[ eTarea ].xHandle = xTaskGetCurrentTaskHandle();
        xTabla[ eTarea ].xVence  = xTaskGetTickCount()
                                   + pdMS_TO_TICKS( WDG_PLAZO_MS );
        xTabla[ eTarea ].eEstado = wdgVIGILADA;
    }
}
//------------------------------------------------------------------------------
/*
 * Quién está corriendo. Devuelve NULL si esta tarea no está en la tabla, que es
 * lo que pasa con `tkCtl` —el juez, que no se vigila a sí misma— y con el idle.
 * En ese caso los kicks no hacen nada, que es lo correcto.
 */
static wdg_entrada_t *prvActual( void )
{
    TaskHandle_t xYo = xTaskGetCurrentTaskHandle();

    for( uint8_t i = 0U; i < ( uint8_t ) wdgTK_CANTIDAD; i++ )
    {
        if( ( xTabla[ i ].eEstado == wdgVIGILADA ) && ( xTabla[ i ].xHandle == xYo ) )
        {
            return &xTabla[ i ];
        }
    }

    return NULL;
}
//------------------------------------------------------------------------------
void wdg_kick( void )
{
    wdg_entrada_t *px = prvActual();

    if( px != NULL )
    {
        px->xVence = xTaskGetTickCount() + pdMS_TO_TICKS( WDG_PLAZO_MS );
    }
}
//------------------------------------------------------------------------------
void wdg_kick_largo( uint32_t ulMs )
{
    wdg_entrada_t *px = prvActual();

    if( px == NULL )
    {
        return;
    }

    /*
     * Nunca menos que el plazo normal: una prórroga corta sería un report con
     * pasos de más, y pedir 5 s donde el plazo son 90 significaría acortarle la
     * vida a la tarea sin quererlo.
     */
    if( ulMs < WDG_PLAZO_MS )
    {
        ulMs = WDG_PLAZO_MS;
    }

    /*
     * ⚠ Tope de una hora. Con el tick a 512 Hz, `pdMS_TO_TICKS()` desborda un
     * `uint32_t` a partir de ~2,3 h (multiplica por 512 antes de dividir), así
     * que un número grande no daría un plazo largo sino uno cualquiera. Y una
     * prórroga de más de una hora ya no es una prórroga: es apagar el watchdog.
     */
    if( ulMs > ( 60UL * 60UL * 1000UL ) )
    {
        ulMs = 60UL * 60UL * 1000UL;
    }

    px->xVence = xTaskGetTickCount() + pdMS_TO_TICKS( ulMs );
}
//------------------------------------------------------------------------------
void wdg_stop_task( void )
{
    wdg_entrada_t *px = prvActual();

    if( px != NULL )
    {
        px->eEstado = wdgMATADA;
    }
}
//------------------------------------------------------------------------------
bool wdg_todas_sanas( const char **ppcCulpable )
{
    for( uint8_t i = 0U; i < ( uint8_t ) wdgTK_CANTIDAD; i++ )
    {
        if( ( xTabla[ i ].eEstado == wdgVIGILADA ) && prvVencida( xTabla[ i ].xVence ) )
        {
            if( ppcCulpable != NULL )
            {
                *ppcCulpable = pcNombres[ i ];
            }

            return false;
        }
    }

    return true;
}
//------------------------------------------------------------------------------
const char *wdg_tarea_str( wdg_tarea_t eTarea )
{
    return ( eTarea < wdgTK_CANTIDAD ) ? pcNombres[ eTarea ] : "?";
}
//------------------------------------------------------------------------------
void wdg_print( void )
{
    xprintf( "WDG:: perro de hardware (IWDG): %s",
             drv_wdt_corriendo() ? "CORRIENDO" : "detenido" );

    if( drv_wdt_corriendo() )
    {
        xprintf( ", ventana ~%lu ms", ( unsigned long ) drv_wdt_ventana_ms() );
    }

    xprintf( "\r\n      plazo por tarea: %lu ms   (trozo de espera: %lu s)\r\n",
             ( unsigned long ) WDG_PLAZO_MS,
             ( unsigned long ) WDG_TROZO_ESPERA_S );

    for( uint8_t i = 0U; i < ( uint8_t ) wdgTK_CANTIDAD; i++ )
    {
        xprintf( "      %-10s ", pcNombres[ i ] );

        switch( xTabla[ i ].eEstado )
        {
            case wdgVIGILADA:
            {
                /*
                 * Lo que le queda, en ms. Va con resta y cast a `int32_t` por el
                 * mismo motivo que `prvVencida()`: si ya venció, el número tiene
                 * que salir negativo y no enorme.
                 */
                int32_t lRestan = ( int32_t ) ( xTabla[ i ].xVence - xTaskGetTickCount() );

                xprintf( "vigilada, le quedan %ld ms%s\r\n",
                         ( long ) ( lRestan * 1000L / ( long ) configTICK_RATE_HZ ),
                         ( lRestan <= 0 ) ? "   <-- ⛔ VENCIDA" : "" );
                break;
            }

            case wdgMATADA:
                xprintf( "MATADA (fuera de la vigilancia, por 'kill')\r\n" );
                break;

            default:
                /*
                 * ⚠ Al arrancar es normal: las tareas se registran recién cuando
                 * entran a su función. Si queda así con el equipo andando, esa
                 * tarea **no se está vigilando** y hay que averiguar por qué.
                 */
                xprintf( "sin registrar (no arranco todavia, o no se vigila)\r\n" );
                break;
        }
    }
}
//------------------------------------------------------------------------------
