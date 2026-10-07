/*
 * caudal.h
 *
 * **El caudal a partir del tiempo entre pulsos.** Paso 2b.
 *
 * Un caudalímetro de contacto seco da un pulso cada `magpp` metros cúbicos. El
 * caudal no se cuenta: **se deduce del tiempo entre dos pulsos**.
 *
 *     Q [m³/h] = magpp · 3.600.000 / dT_ms
 *
 * ---------------------------------------------------------------------------
 * ⭐ ES UNA VERSIÓN SIMPLIFICADA DEL ALGORITMO DEL AVR, Y A PROPÓSITO
 *
 * `XLIBS/contadores.c` de FWDLGX trae ~200 líneas: EMA con **cuatro alphas por
 * tramo de caudal**, fronteras precalculadas, **clamp de slew-rate al ±10 % por
 * pulso**, y una ventana de arranque de 5 pulsos. Todo eso existe por una razón
 * concreta: **había pulsos de ruido que se colaban y se medían como caudal**.
 *
 * Acá se sacó, con el criterio de Pablo (2026-09-23) y tres datos suyos:
 *
 *   1. *"El criterio simple es mejor que complejo, siempre."*
 *   2. **En régimen no hay variaciones bruscas**: es agua potable en redes.
 *   3. **Los escalones —arranque, roturas— importan pero no son frecuentes.**
 *
 * ⛔ Y el punto de fondo: **un filtro que aplasta el ruido aplasta igual los
 * escalones reales**. Con el ruido ya resuelto por el hardware —el circuito de
 * entrada tiene pasa-bajos RC y un Schmitt— ese aparato pagaba todo el costo y
 * ya no cobraba el beneficio: tardaba ~24 pulsos en seguir una rotura, que es
 * justo el evento que hay que detectar.
 *
 * ⚠ **Si en campo reaparece el ruido, volver es un `git revert`**: la versión
 * completa del AVR está en `contadores.c` y este cambio va en un commit propio.
 *
 * ---------------------------------------------------------------------------
 * LO QUE SÍ SE CONSERVA, y qué resuelve cada cosa
 *
 *   - **`alpha = 1` en el primer intervalo.** Arrancar el EMA desde cero
 *     subreporta durante varios pulsos; con caudal bajo eso son minutos.
 *   - **Anti-rebote de 500 ms** (`CAUDAL_MS_ANTIRREBOTE`). El hardware ya filtra
 *     5-12 ms; esto cubre lo que pase de ahí.
 *   - ⭐ **Descarte de lo físicamente imposible** (`Q > CAUDAL_MAX_M3H`). Es la
 *     única red que queda en el primer intervalo, donde `alpha = 1` toma el
 *     valor directo y no hay EMA previo que amortigüe.
 *   - **Decay a cero por silencio.** Sin esto el caudal queda congelado para
 *     siempre cuando el flujo para.
 *   - **Promedio de la ventana de poleo.** ⭐ Es un segundo nivel de suavizado
 *     que ya existía y que nadie contaba: con `timerpoll` de 60 s y
 *     `magpp = 1` promedia ~3 muestras, y hace buena parte del trabajo que se
 *     le pedía al EMA.
 *
 * ---------------------------------------------------------------------------
 * ⚠ EL TIEMPO SE MIDE EN TICKS, NO EN MILISEGUNDOS
 *
 * La tentación es `ulTicks * 1000 / configTICK_RATE_HZ` como hace el AVR. Acá
 * eso **desborda un `uint32_t` a las 2,3 horas** (el tick va a 512 Hz, así que
 * multiplicar por 1000 pasa los 4.294 millones enseguida), y este equipo corre
 * 7×24.
 *
 * Por eso la resta va **en ticks** —que tolera el rollover del contador por
 * aritmética modular— y la conversión a ms se hace recién sobre el `dT`, que
 * siempre es chico.
 *
 * ⚠ La resolución del tick son **1,95 ms**, contra 1 ms del AVR. No importa: el
 * `dT` más corto que puede existir es **1,8 s** (`magpp` 0,1 al caudal máximo de
 * 200 m³/h), así que el error es del 0,1 %.
 */

#ifndef APPLICATION_TASKS_CAUDAL_H_
#define APPLICATION_TASKS_CAUDAL_H_

#include <stdbool.h>
#include <stdint.h>

/*------------------------------------------------------------------------------
 * Los parámetros del filtro. Están acá y no repartidos porque se van a ajustar
 * contra un caudalímetro real: tenerlos juntos es parte del trabajo.
 *----------------------------------------------------------------------------*/

/* Techo físico del caudalímetro (dato de Pablo, 2026-09-23). Un pulso que
   implique más que esto no puede ser real: se descarta. */
#define CAUDAL_MAX_M3H            200.0f

/* Piso de reporte: por debajo de esto se informa 0. El totalizador de pulsos
   NO se toca — sólo afecta al valor instantáneo. */
#define CAUDAL_MIN_M3H              3.0f

/* Anti-rebote por software, sobre el de 5-12 ms que ya hace el hardware.
   ⚠ Con `magpp` = 1 el AVR descartaba todo lo que llegara antes de **18 s**
   (el dT del caudal máximo); 500 ms es mucho más permisivo, y por eso hace
   falta el descarte por `CAUDAL_MAX_M3H`. */
#define CAUDAL_MS_ANTIRREBOTE     500U

/* ⭐ EL parámetro a ajustar. Un EMA sigue el 95 % de un escalón en ~3/α pulsos:
 *
 *      α = 0,1  ->  30 pulsos   (lo que daba el AVR en caudal bajo)
 *      α = 0,5  ->   6 pulsos   <- punto de partida
 *      α = 0,7  ->   4 pulsos
 *
 * Con `magpp` = 1 a 200 m³/h (dT = 18 s), 6 pulsos son ~2 minutos para seguir
 * una rotura. */
#define CAUDAL_ALPHA               0.5f

typedef struct {
    float    fCaudal;        /* m³/h, ya promediado y con el piso aplicado   */
    uint32_t ulPulsos;       /* totalizador, intacto                         */
    float    fEmaVivo;       /* el EMA sin promediar, para diagnóstico       */
    uint16_t usMuestras;     /* cuántos pulsos entraron en esta ventana      */
    uint32_t ulCortos;       /* descartados por el anti-rebote de 500 ms     */
    uint32_t ulImposibles;   /* descartados por implicar Q > CAUDAL_MAX_M3H  */
} caudal_t;

/*
 * ⚠ LOS DOS CONTADORES DE DESCARTE VAN SEPARADOS, y no es un detalle: son
 * diagnósticos OPUESTOS.
 *
 *   - `ulCortos` acusa al **circuito de entrada**: rebotes que el pasa-bajos del
 *     hardware no alcanzó a filtrar.
 *   - `ulImposibles` acusa a **la cadencia**: pulsos más rápidos que el caudal
 *     máximo físico. En banco suele ser alguien puenteando el borne demasiado
 *     rápido; en campo sería un `magpp` mal configurado.
 *
 * Con un contador único los dos se ven iguales, y mandan a mirar lugares
 * distintos.
 */

/*------------------------------------------------------------------------------
 * `magpp` viene de la configuración y puede cambiar en caliente, así que se
 * fija acá en vez de leerla desde la ISR.
 *----------------------------------------------------------------------------*/
void caudal_config( float fMagpp );

/*------------------------------------------------------------------------------
 * Un pulso. **Se llama DESDE LA ISR** — ver `drv_pulsos_pulso_cb()`.
 *----------------------------------------------------------------------------*/
void caudal_pulso_desde_isr( uint32_t ulTicks );

/*------------------------------------------------------------------------------
 * La lectura periódica, para el registro. Promedia la ventana, aplica el decay
 * por silencio y deja los acumuladores en cero para el ciclo siguiente.
 *----------------------------------------------------------------------------*/
void caudal_leer( caudal_t *pxOut );

/*------------------------------------------------------------------------------
 * ⭐ Como `caudal_leer()` pero SIN CONSUMIR: no promedia la ventana ni pone los
 * acumuladores en cero.
 *
 * Existe por lo mismo que `fs_datos_peek()` está separada de `pop()`: mirar y
 * consumir son cosas distintas. El comando `cnt` tiene que poder mostrar el
 * estado **ahora** —mientras alguien puentea el borne a mano— sin robarle al
 * poleo las muestras que todavía no reportó.
 *
 * `fCaudal` sale como el promedio de lo acumulado HASTA ACÁ, que es lo que
 * reportaría el poleo si ocurriera en este instante.
 *----------------------------------------------------------------------------*/
void caudal_peek( caudal_t *pxOut );

/* Estado a cero. Se llama al arrancar y al cambiar `magpp`. */
void caudal_reset( void );

#endif /* APPLICATION_TASKS_CAUDAL_H_ */
