/*
 * port_lptim_tick.h
 *
 * Lo que el port del tickless expone hacia afuera: los contadores que dicen
 * cuánto está perdiendo el mecanismo de sueño.
 *
 * ⛔⛔ POR QUÉ EXISTE ESTE HEADER (2026-10-30)
 *
 * La instalación de campo del 2026-10-20 midió que **el tick atrasa 2,31 %**
 * mientras el equipo duerme, y eso mete un **sesgo de +2,3 % en el caudal** que
 * se reporta al servidor —el `dT` del caudal se mide en ticks—.
 *
 * ⚠ Y la aritmética del arreglo obvio NO cierra: arrastrar el resto de la
 * división explica ~650 ppm de los 23.504 medidos, el 3 %. Falta encontrar el
 * 97 % restante, y para eso está esto: **primero el instrumento, después el
 * arreglo**. Es la regla que el proyecto aprendió a los golpes con el ADC, donde
 * cuatro "arreglos" guiados por un volcado que mentía rompieron código sano.
 *
 * El detalle completo —la medición, lo que explica y lo que no— está en el
 * comentario grande de `port_lptim_tick.c`.
 */

#ifndef PORT_LPTIM_TICK_H
#define PORT_LPTIM_TICK_H

#include <stdbool.h>
#include <stdint.h>

/*------------------------------------------------------------------------------
 * EL MARTILLO PARA DESCARTAR AL PORT DE UN DIAGNÓSTICO.
 *
 * En 1, `vPortSuppressTicksAndSleep()` vuelve sin dormir: el equipo queda sin
 * tickless y **consume ~3,5 mA en vez de µA**, pero el port deja de ser
 * sospechoso de cualquier cosa sin tocar una línea más.
 *
 * ⚠ Vive acá, y no en el `.c`, porque lo necesitan DOS unidades de compilación:
 * el port y `tkSys.c`, que sólo instrumenta el tick si el tickless está activo.
 * Misma razón que `ARRANCA_WDG` en `main.h`.
 *
 * ⭐ Y se apaga acá y no en el `.ioc` a propósito: `configUSE_TICKLESS_IDLE`
 * vive en `FreeRTOSConfig.h`, que lo genera CubeMX, y cambiarlo a mano dejaría
 * el `.ioc` diciendo una cosa y el binario haciendo otra.
 */
#define PORT_SIN_TICKLESS   0

#if ( PORT_SIN_TICKLESS == 0 )

/*------------------------------------------------------------------------------
 * Los contadores del tickless. Devuelve el resto de cuentas pendiente de
 * arrastrar; los demás salen por puntero y admiten NULL:
 *
 *   pulCiclos     cuántas veces se durmió de verdad (en Stop 2)
 *   pulCuentas    cuentas del LPTIM que se le contabilizaron al kernel
 *   pulRescatado  cuentas que ANTES se tiraban por el resto de la división.
 *                 ⭐ Es la prueba de que el arrastre sirve: dividido por 1024
 *                 son los segundos que el tick habría perdido sin él.
 */
uint32_t port_tickless_stats( uint32_t *pulCiclos, uint32_t *pulCuentas,
                              uint32_t *pulRescatado );

/*------------------------------------------------------------------------------
 * ⭐⭐ El tick contra el RTC INTERNO, que corre del MISMO LSE y no se para nunca.
 *
 * Acumula segundos de los dos relojes y los devuelve, de modo que
 * `(rtc - tick) / rtc` son los ppm que pierde el mecanismo de sueño —sin el
 * cristal del MCP79410 de por medio y sin necesitar el servidor—.
 *
 * Hay que llamarla periódicamente (lo hace el poleo). La primera llamada sólo
 * toma la referencia y devuelve false.
 */
bool port_tick_vs_rtc( uint32_t *pulSegRtc, uint32_t *pulSegTick );

#endif /* PORT_SIN_TICKLESS */

#endif /* PORT_LPTIM_TICK_H */
