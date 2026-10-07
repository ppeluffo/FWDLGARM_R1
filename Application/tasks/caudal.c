/*
 * caudal.c  -  ver caudal.h
 */

#include <stddef.h>

#include "caudal.h"
#include "drv_pulsos.h"
#include "caudal_log.h"

#include "FreeRTOS.h"
#include "task.h"

/* Precalculados al configurar, para que la ISR no divida.
 *   fK        = magpp · 3.600.000   ->  Q = fK / dT_ms
 *   fMsZero   = el silencio que dispara el decay a cero
 *   fMsPorTick= 1000 / configTICK_RATE_HZ                                   */
static float fK         = 0.0f;
static float fMsZero    = 0.0f;
static const float fMsPorTick = 1000.0f / ( float ) configTICK_RATE_HZ;

/* Umbral del anti-rebote, en TICKS: comparar en ticks evita convertir en cada
   pulso, y es la comparación más frecuente de todas. */
static uint32_t ulTicksAntirrebote = 0;

/*
 * ⚠ TODO ESTO LO ESCRIBE LA ISR Y LO LEE LA TAREA.
 *
 * `volatile` es necesario pero NO suficiente: `fSuma` y `usN` tienen que leerse
 * y ponerse en cero **juntos**, o se pierden pulsos que entren en el medio. Eso
 * va en `taskENTER_CRITICAL()`, y funciona porque la EXTI del contador está en
 * prioridad 5, igual que `configMAX_SYSCALL_INTERRUPT_PRIORITY`: `BASEPRI` la
 * enmascara. Si algún día esa prioridad bajara de 5 —o sea, se volviera más
 * urgente— la sección crítica dejaría de protegerla **en silencio**.
 */
static volatile uint32_t ulUltimoTick   = 0U;
static volatile bool     bHayReferencia = false;
static volatile bool     bEmaArrancado  = false;
static volatile float    fEma           = 0.0f;
static volatile float    fSuma          = 0.0f;
static volatile uint16_t usMuestras     = 0U;
static volatile uint32_t ulValidos      = 0U;
static volatile uint32_t ulCortos      = 0U;
static volatile uint32_t ulImposibles  = 0U;

//------------------------------------------------------------------------------
void caudal_reset( void )
{
    taskENTER_CRITICAL();

    ulUltimoTick   = 0U;
    bHayReferencia = false;
    bEmaArrancado  = false;
    fEma           = 0.0f;
    fSuma          = 0.0f;
    usMuestras     = 0U;
    ulValidos      = 0U;
    ulCortos       = 0U;
    ulImposibles   = 0U;

    taskEXIT_CRITICAL();
}
//------------------------------------------------------------------------------
void caudal_config( float fMagpp )
{
    /* Un magpp de 0 haría Q = 0 siempre y una división por cero en el decay:
       se ignora y el módulo queda inerte, que es mejor que calcular basura. */
    if( fMagpp <= 0.0f )
    {
        fK      = 0.0f;
        fMsZero = 0.0f;
        return;
    }

    fK      = fMagpp * 3600000.0f;
    fMsZero = fK / CAUDAL_MIN_M3H;      /* dT del caudal mínimo reportable */

    ulTicksAntirrebote = pdMS_TO_TICKS( CAUDAL_MS_ANTIRREBOTE );

    caudal_reset();
}
//------------------------------------------------------------------------------
void caudal_pulso_desde_isr( uint32_t ulTicks )
{
    if( fK <= 0.0f )
    {
        return;                          /* sin magpp no hay nada que calcular */
    }

    /*
     * Primer pulso: sólo fija la referencia. **No hay dT todavía**, así que no
     * hay caudal que calcular — pero el pulso es físico y se cuenta.
     */
    if( !bHayReferencia )
    {
        ulUltimoTick   = ulTicks;
        bHayReferencia = true;
        ulValidos++;
        return;
    }

    /* ⚠ La resta va en TICKS y tolera el rollover del contador por aritmética
       modular. Convertir el tick absoluto a ms desbordaría un uint32_t a las
       2,3 h — ver el header. */
    uint32_t ulDt = ulTicks - ulUltimoTick;

    /* Anti-rebote: sobre los 5-12 ms que ya filtra el hardware. */
    if( ulDt < ulTicksAntirrebote )
    {
        ulCortos++;
        caudal_log_agregar_desde_isr( ulTicks,
                                      ( uint32_t ) ( ( float ) ulDt * fMsPorTick ),
                                      0.0f, fEma, cauEV_CORTO );
        return;
    }

    float fDtMs = ( float ) ulDt * fMsPorTick;
    float fQ    = fK / fDtMs;

    /*
     * ⭐ Descarte de lo físicamente imposible.
     *
     * Es la ÚNICA red que queda en el primer intervalo: ahí el EMA arranca con
     * alpha = 1 y toma el valor directo, sin nada previo que lo amortigüe. Sin
     * esto, un pulso espurio que caiga justo al arrancar fija el caudal solo.
     */
    if( fQ > CAUDAL_MAX_M3H )
    {
        ulImposibles++;
        caudal_log_agregar_desde_isr( ulTicks, ( uint32_t ) fDtMs,
                                      fQ, fEma, cauEV_QMAX );
        return;
    }

    ulUltimoTick = ulTicks;
    ulValidos++;

    if( !bEmaArrancado )
    {
        /* alpha = 1 en el primer intervalo: arrancar desde cero subreporta
           durante varios pulsos, y con caudal bajo eso son minutos. */
        fEma          = fQ;
        bEmaArrancado = true;
    }
    else
    {
        fEma = ( CAUDAL_ALPHA * fQ ) + ( ( 1.0f - CAUDAL_ALPHA ) * fEma );
    }

    /* Para el promedio de la ventana de poleo, que es el segundo nivel de
       suavizado — y con timerpoll de 5 min, el principal. */
    fSuma += fEma;
    usMuestras++;

    caudal_log_agregar_desde_isr( ulTicks, ( uint32_t ) fDtMs, fQ, fEma, cauEV_OK );
}
//------------------------------------------------------------------------------
void caudal_leer( caudal_t *pxOut )
{
    float    fSumaLocal;
    uint16_t usNLocal;
    uint32_t ulDtSilencio;
    bool     bVivo;

    if( pxOut == NULL )
    {
        return;
    }

    uint32_t ulAhora = xTaskGetTickCount();

    taskENTER_CRITICAL();

    /*
     * Decay a cero por silencio. Sin esto el caudal queda **congelado para
     * siempre** cuando el flujo para: el último EMA se seguiría reportando en
     * cada ventana, indistinguible de un caudal real y constante.
     */
    bVivo = bHayReferencia && ( fEma > 0.0f );

    if( bVivo )
    {
        ulDtSilencio = ulAhora - ulUltimoTick;

        if( ( ( float ) ulDtSilencio * fMsPorTick ) > fMsZero )
        {
            fEma           = 0.0f;
            fSuma          = 0.0f;
            usMuestras     = 0U;
            bEmaArrancado  = false;
            bHayReferencia = false;     /* el próximo pulso vuelve a arrancar */
        }
    }

    fSumaLocal = fSuma;
    usNLocal   = usMuestras;
    fSuma      = 0.0f;
    usMuestras = 0U;

    pxOut->fEmaVivo      = fEma;
    pxOut->ulPulsos      = ulValidos;
    pxOut->ulCortos      = ulCortos;
    pxOut->ulImposibles  = ulImposibles;

    taskEXIT_CRITICAL();

    /*
     * El valor que se reporta:
     *
     *  - con muestras: el **promedio de la ventana**. Con timerpoll de 5 min
     *    son decenas o centenas de pulsos a caudal medio o alto, así que este
     *    promedio es el filtro principal — más que el propio EMA.
     *
     *  - ⚠ sin muestras: el **último EMA vivo**. Y NO es un caso raro: con
     *    magpp = 1 y caudal bajo el dT entre pulsos llega a 20 minutos, o sea
     *    que 4 de cada 5 ventanas no tienen ni un pulso. Se va a ver el mismo
     *    valor repetido en varias muestras seguidas, y es correcto.
     */
    pxOut->usMuestras = usNLocal;
    pxOut->fCaudal    = ( usNLocal > 0U )
                        ? ( fSumaLocal / ( float ) usNLocal )
                        : pxOut->fEmaVivo;

    /* Piso de reporte. ⚠ Sólo afecta al valor instantáneo: el totalizador de
       pulsos queda intacto. */
    if( pxOut->fCaudal < CAUDAL_MIN_M3H )
    {
        pxOut->fCaudal = 0.0f;
    }
}
//------------------------------------------------------------------------------
void caudal_peek( caudal_t *pxOut )
{
    float    fSumaLocal;
    uint16_t usNLocal;

    if( pxOut == NULL )
    {
        return;
    }

    taskENTER_CRITICAL();

    /* ⚠ Acá NO se aplica el decay ni se ponen los acumuladores en cero: esta
       función mira, no consume. El decay lo hace `caudal_leer()`, que es la que
       corre en el poleo y la única que tiene derecho a cambiar el estado. */
    fSumaLocal           = fSuma;
    usNLocal             = usMuestras;
    pxOut->fEmaVivo      = fEma;
    pxOut->ulPulsos      = ulValidos;
    pxOut->ulCortos      = ulCortos;
    pxOut->ulImposibles  = ulImposibles;

    taskEXIT_CRITICAL();

    pxOut->usMuestras = usNLocal;
    pxOut->fCaudal    = ( usNLocal > 0U )
                        ? ( fSumaLocal / ( float ) usNLocal )
                        : pxOut->fEmaVivo;

    if( pxOut->fCaudal < CAUDAL_MIN_M3H )
    {
        pxOut->fCaudal = 0.0f;
    }
}
//------------------------------------------------------------------------------

/*==============================================================================
 * El enganche con el driver
 *
 * Implementa el callback débil de `drv_pulsos`. Es la única línea que une las
 * dos capas, y va en este sentido a propósito: el driver no sabe qué es un
 * caudal, y este módulo no toca un registro.
 *============================================================================*/
void drv_pulsos_pulso_cb( uint32_t ulTicks )
{
    caudal_pulso_desde_isr( ulTicks );
}
//------------------------------------------------------------------------------
