/*
 * drv_wdt.h
 *
 * **El perro guardián de hardware: el IWDG del STM32L4.**
 *
 * Es la mitad de abajo del watchdog. La de arriba —quién está sano y quién no—
 * vive en `Application/tasks/wdg.{h,c}`; acá sólo está el mecanismo que
 * realmente resetea el micro.
 *
 * El reparto es el del AVR: la lógica cooperativa decide, y si algo no cierra
 * **deja de patear**. Nadie llama a un "resetear ahora": el reset lo produce el
 * silencio.
 *
 * ---------------------------------------------------------------------------
 * ⭐ POR QUÉ EL IWDG Y NO UN `NVIC_SystemReset()` POR SOFTWARE
 *
 * Porque **la causa del reset queda grabada en el hardware**: el bit
 * `RCC_CSR.IWDGRSTF`, que `tkCmd` ya lee al arrancar y que **viaja al servidor
 * en el campo `WDG` del frame `CONF_BASE`** (`wanRESET_IWDG`). Con un reset por
 * software la causa sale como `SOFT PIN`, indistinguible de un `reset` tipeado
 * en la consola — o sea que en campo no habría forma de saber si el equipo se
 * reinició solo o lo reinició un técnico.
 *
 * Y hay una razón más fuerte: **el IWDG es independiente del núcleo**. Corre de
 * su propio LSI y no lo puede desarmar un firmware colgado, que es justamente la
 * situación en la que se lo necesita. Un reset por software depende de que
 * alguna tarea siga corriendo lo suficiente como para ejecutarlo.
 *
 * ---------------------------------------------------------------------------
 * ⭐ SIGUE CONTANDO EN STOP 2, Y POR ESO ESTO FUNCIONA
 *
 * El IWDG se alimenta del **LSI**, un oscilador RC que no se apaga en los modos
 * de bajo consumo. El micro pasa más del 98 % del tiempo en Stop 2 y el perro
 * cuenta igual — que es exactamente lo que hace falta: si el equipo se cuelga
 * dormido, se resetea solo.
 *
 * El LSI cuesta del orden de **200 nA**, contra los ~5 µA del micro dormido:
 * ~4 % del reposo. Es el precio del watchdog y está pago.
 *
 * ⚠ El LSI **no es un cristal**: su tolerancia es de ±5 % típica y hasta ±47 %
 * sobre todo el rango de temperatura. Por eso la ventana de abajo se declara
 * como un rango y no como un número, y por eso el plazo de la capa de arriba
 * (90 s) es varias veces mayor que el período con que `tkCtl` patea (1 s).
 *
 * ---------------------------------------------------------------------------
 * ⛔ UNA VEZ ARRANCADO NO SE PUEDE PARAR
 *
 * Es del silicio: el IWDG sólo se detiene con un reset. **No hay
 * `drv_wdt_parar()` y no se puede escribir.** Consecuencias que hay que tener
 * presentes:
 *
 *  - Si `Error_Handler()` se dispara DESPUÉS de arrancado, el equipo entra en un
 *    ciclo de reset: destella el patrón, se resetea a los ~32 s, vuelve a
 *    fallar. **Es visible y es mejor que un cuelgue mudo**, pero hay que
 *    reconocerlo para no confundirlo con "la placa no arranca". Antes del
 *    scheduler el perro todavía no existe, así que los destellos de diagnóstico
 *    del bring-up siguen andando igual que siempre.
 *  - ⚠ **Depurando con breakpoints el equipo se resetea solo.** Se evita con
 *    `DBGMCU->APB1FZR1 |= DBG_IWDG_STOP`, que ya hace `drv_wdt_arrancar()`.
 *
 * ---------------------------------------------------------------------------
 * ⚠ SE CONFIGURA POR REGISTROS Y NO DESDE CubeMX — a propósito
 *
 * Va contra la regla de oro del proyecto, así que la razón tiene que valer, y
 * son dos:
 *
 *  1. **CubeMX pondría `MX_IWDG_Init()` en `main()`, antes del scheduler**, o
 *     sea que el perro arrancaría cuando todavía no existe ninguna tarea que
 *     pueda patearlo. Habría que sacarlo de ahí igual, y entonces el `.ioc` diría
 *     una cosa y el firmware haría otra — que es el tipo de desincronización que
 *     en este proyecto ya costó un día con el LSE.
 *  2. El IWDG **no tiene nada que CubeMX aporte**: ni pines, ni NVIC, ni mux de
 *     reloj, ni `MspInit`. Lo único que generaría son tres constantes.
 *
 * Además hoy su módulo de la HAL ni siquiera está compilado
 * (`HAL_IWDG_MODULE_ENABLED` está comentado en `stm32l4xx_hal_conf.h` y el
 * `stm32l4xx_hal_iwdg.c` no está copiado al árbol), así que usarlo obligaría a
 * regenerar desde CubeMX para ganar cero.
 *
 * Son doce líneas de registros, documentadas contra el RM0351.
 */

#ifndef APPLICATION_DRIVERS_DRV_WDT_H_
#define APPLICATION_DRIVERS_DRV_WDT_H_

#include <stdbool.h>
#include <stdint.h>

/*------------------------------------------------------------------------------
 * Arranca el perro. **Una sola vez, y no se puede deshacer.**
 *
 * Lo llama `tkCtl` en su primera vuelta, no `main()`: así el perro empieza a
 * contar recién cuando ya hay un scheduler corriendo y una tarea capaz de
 * patearlo.
 *----------------------------------------------------------------------------*/
void drv_wdt_arrancar( void );

/*------------------------------------------------------------------------------
 * Recarga la cuenta. Sólo lo llama `tkCtl`, y **sólo si todas las tareas
 * vigiladas están sanas** — ver `wdg.h`.
 *
 * Es inofensivo llamarlo con el perro sin arrancar: no hace nada.
 *----------------------------------------------------------------------------*/
void drv_wdt_kick( void );

/* Para el comando `wdg` y para el banner: si está andando y con qué ventana. */
bool     drv_wdt_corriendo  ( void );
uint32_t drv_wdt_ventana_ms ( void );

#endif /* APPLICATION_DRIVERS_DRV_WDT_H_ */
