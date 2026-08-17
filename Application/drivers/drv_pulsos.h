/*
 * drv_pulsos.h
 *
 * Contador de pulsos CNT0 (PA12), el que cuenta los del caudalímetro.
 *
 * ---------------------------------------------------------------------------
 * EL CIRCUITO, Y POR QUÉ ESTE DRIVER ES TAN CORTO
 *
 *   contacto seco --> opto (cátodo a GND al cerrar)
 *                       |
 *              colector open-collector, pull-up de 10K a 3V3
 *                       |
 *                 4K7 --+-- 1 µF a GND          <- pasabajos
 *                       |
 *                  74AUP1G17 (Schmitt, NO inversor) --> PA12
 *
 * **Toda la parte difícil de contar pulsos está resuelta en el hardware.** Al
 * firmware no le queda más que sumar flancos, y por eso acá no hay antirrebote,
 * ni máquina de estados, ni filtros: agregarlos sólo daría otra forma de perder
 * pulsos.
 *
 * **Polaridad.** Contacto cerrado -> conduce el LED del opto -> el fototransistor
 * satura -> el nodo cae -> el 1G17 **no es inversor**, así que **PA12 queda en
 * BAJO mientras el contacto está cerrado**. El pulso se cuenta en el **flanco de
 * bajada**, que es el instante del cierre.
 *
 * **El filtro no es simétrico**, y eso fija el techo de frecuencia. Descarga por
 * 4K7 y carga por 10K + 4K7:
 *
 *     tau_bajada = 4,7K x 1 µF  =  4,7 ms
 *     tau_subida = 14,7K x 1 µF = 14,7 ms
 *
 * Con los umbrales del 74AUP1G17 a 3,3 V (VT+ ~1,8 V, VT- ~1,2 V):
 *
 *     cierre más corto que se detecta   ~ 4,8 ms
 *     apertura más corta que se detecta ~ 11,6 ms
 *     período mínimo                    ~ 16 ms  ->  techo absoluto ~60 Hz
 *
 * O sea que el hardware trae **un antirrebote de 5 a 12 ms** —de sobra para un
 * reed, que rebota menos de 2 ms— y acepta **hasta unos 30 Hz con margen**, que
 * es lo que da un caudalímetro. Por encima de eso el filtro se come pulsos y no
 * hay firmware que lo arregle.
 *
 * Y el Schmitt no es un detalle: con flancos de 12 ms una compuerta común se
 * quedaría en la zona lineal el tiempo suficiente para oscilar y para conducir
 * corriente de cortocircuito.
 *
 * ---------------------------------------------------------------------------
 * ⚠ EL PIN VA SIN PULL INTERNO, Y ACÁ IMPORTA MÁS QUE DE COSTUMBRE
 *
 * La salida del 1G17 es CMOS push-pull: maneja el pin en los dos sentidos y no
 * necesita ayuda. Un pull interno del STM32 (30-50 kΩ) sólo pelearía contra él,
 * y peor todavía: son 82 µA cada vez que el driver empuja al lado contrario.
 *
 *   - un pull-UP cuesta 82 µA mientras el contacto está cerrado (nivel bajo);
 *   - un pull-DOWN cuesta 82 µA **en reposo**, o sea las 24 horas.
 *
 * Es la misma trampa que costó 89 µA con SD_DET, y por eso `drv_pulsos_config()`
 * expone MODER/PUPDR: para poder verificar desde la consola que una regeneración
 * de CubeMX no metió un pull de vuelta. Lo que tiene que decir es
 * `GPIO_NOPULL`.
 *
 * **Lo que sí consume es de hardware y no se puede apagar desde acá:** mientras
 * el contacto está cerrado, el pull-up de 10K del opto drena 3,3 V / 10 kΩ =
 * **330 µA**. Como el contacto **queda abierto en reposo** (confirmado por Pablo
 * el 2026-08-17), eso se paga sólo durante el pulso y no en el reposo, que es lo
 * único que hubiera importado.
 *
 * ---------------------------------------------------------------------------
 * POR QUÉ EXTI Y NO UN TIMER CONTANDO SOLO
 *
 * Un contador por hardware sería mejor: contaría sin despertar al micro. No se
 * puede, y conviene dejar escrito por qué para no volver a averiguarlo:
 *
 *   - Los LPTIM son los únicos timers que **siguen contando en Stop 2**, pero
 *     **PA12 no es entrada de ningún LPTIM** en este chip.
 *   - PA12 sí es `TIM1_ETR`, pero TIM1 cuelga de APB2, que en Stop 2 no tiene
 *     reloj: contaría sólo con el micro despierto, que es justamente lo que no
 *     sirve.
 *
 * Así que cada pulso despierta al micro de Stop 2 y le hace rehacer
 * `SystemClock_Config()`. A la frecuencia de un caudalímetro es despreciable.
 *
 * **Ningún pulso se pierde durante el `__disable_irq()` del tickless**, que es lo
 * que sí le pasaba a los bytes del USART: la EXTI **latchea** el flanco en su
 * registro de pendientes, así que la interrupción queda demorada, no perdida, y
 * corre apenas el port rehabilita. Se perdería sólo si llegaran DOS pulsos
 * dentro de esa ventana de ~100 µs; a 30 Hz están a 33 ms uno de otro.
 *
 * ---------------------------------------------------------------------------
 * POR QUÉ NO HAY MEDIDA DE PERÍODO NI DE FRECUENCIA
 *
 * Sería tentador estampar `xTaskGetTickCountFromISR()` en cada pulso y sacar el
 * caudal instantáneo de la diferencia. **Daría mal, y de forma difícil de ver.**
 *
 * La ISR corre **antes** de que el port del tickless corrija el tick: cuando un
 * pulso despierta al micro de Stop 2, el `vTaskStepTick()` que le informa al
 * kernel los ticks dormidos todavía no se ejecutó. O sea que adentro de la ISR
 * el tick vale lo que valía **antes de dormir**, y la diferencia entre dos
 * pulsos saldría cualquier cosa.
 *
 * El caudal es de la capa de aplicación, que lo va a calcular como pulsos por
 * intervalo de muestreo contra el RTC, que sí es hora de verdad.
 */

#ifndef APPLICATION_DRIVERS_DRV_PULSOS_H_
#define APPLICATION_DRIVERS_DRV_PULSOS_H_

#include <stdbool.h>
#include <stdint.h>

/*------------------------------------------------------------------------------
 * Arranque. Pone los dos contadores en cero y descarta cualquier flanco que
 * hubiera quedado latcheado antes de arrancar, para no contar un pulso fantasma
 * de la propia inicialización.
 *
 * El pin y la EXTI los configura CubeMX (`MX_GPIO_Init()`), como todo el resto.
 *----------------------------------------------------------------------------*/
void drv_pulsos_init( void );

/*------------------------------------------------------------------------------
 * Los DOS contadores, y son independientes a propósito.
 *
 * `total`   es libre: no lo baja nadie más que `drv_pulsos_reset()`. Sirve para
 *           el banco y para el diagnóstico — se mira dos veces y se resta.
 *
 * `tomar`   devuelve los pulsos acumulados desde la vez anterior y los descuenta
 *           **en la misma operación atómica**. Es lo que va a usar el registro de
 *           muestras: leer y poner en cero por separado pierde los pulsos que
 *           caigan en el medio.
 *
 * ⚠ La atomicidad de `tomar` descansa en que la EXTI corra con prioridad
 * numérica >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5), que es la que le
 * puso CubeMX: por debajo de eso `taskENTER_CRITICAL()` **deja de enmascararla**
 * y se puede perder un pulso. Es una dependencia silenciosa entre el `.ioc` y
 * este archivo.
 *----------------------------------------------------------------------------*/
uint32_t drv_pulsos_total( void );
uint32_t drv_pulsos_tomar( void );
void     drv_pulsos_reset( void );

/* Mira los pendientes SIN llevárselos. Es para la consola: sirve para ver que la
   cuenta avanza sin alterar lo que va a leer el registro de muestras. */
uint32_t drv_pulsos_pendientes( void );

/*------------------------------------------------------------------------------
 * Diagnóstico: lo usa el comando 'cnt' de la consola. Va acá y no en la tarea
 * porque leer el pin y los registros del GPIO es tocar hardware.
 *----------------------------------------------------------------------------*/

/* Nivel ELÉCTRICO del pin ahora. true = alto = contacto ABIERTO (el reposo). */
bool drv_pulsos_nivel_pin( void );

typedef struct {
    uint32_t ulModer;      /* 0 = entrada, 1 = salida, 2 = alternada, 3 = analógica */
    uint32_t ulPupdr;      /* 0 = flotante, 1 = pull-up, 2 = pull-down             */
} drv_pulsos_cfg_t;

void drv_pulsos_config( drv_pulsos_cfg_t *pxCfg );

#endif /* APPLICATION_DRIVERS_DRV_PULSOS_H_ */
