/*
 * tkCtlPres.h
 *
 * **La DOBLE CONSIGNA: cuándo hay que mandarla.** El cómo está en
 * `drv_cpres.{h,c}`. Portada de `FWDLGX_tkCtlPres.c` + `XLIBS/consignas.c`.
 *
 * A la hora `diurna` se le manda al control de presión la consigna de día, y a
 * la `nocturna` la de noche. Nada más: dos movimientos por día.
 *
 * ---------------------------------------------------------------------------
 * ⭐ POR QUÉ 45 SEGUNDOS: ES MUESTREO AL DOBLE DE LA RESOLUCIÓN
 *
 * La explicación es de Pablo (2026-09-22) y el número no es arbitrario. La
 * configuración tiene resolución de **un minuto**, así que muestreando cada 45 s
 * **siempre caen una o dos muestras dentro de la ventana**: la consigna no se
 * puede perder. *"La prioridad es que se ejecute la consigna si está
 * configurada; que no se pierda."*
 *
 * Por eso la comparación es de **igualdad exacta** de `hhmm`, y no hace falta
 * ninguna ventana, ni recordar "ya se aplicó hoy", ni ningún estado.
 *
 * ⚠ **Y por eso hay que esperar al cambio de minuto después de ejecutar.** Si
 * caen dos muestras en el mismo minuto, la segunda volvería a mandar la orden.
 * El AVR se salva **por accidente**: la consigna tarda 30-45 s, así que el
 * chequeo siguiente cae inevitablemente en otro minuto. Acá es explícito
 * (`prvEsperarCambioDeMinuto()`), porque depender de cuánto tarde el dispositivo
 * es depender de un número que no controlamos y que otro equipo podría bajar.
 *
 * ⭐ El resultado es que el período se **autoajusta**: cuando no hay nada que
 * hacer muestrea rápido y no pierde; cuando ejecuta, se sale del minuto y no
 * repite.
 *
 * ---------------------------------------------------------------------------
 * ⭐ AL ARRANCAR SE APLICA LA QUE CORRESPONDA, Y ESO ES AUTOCORRECTIVO
 *
 * `prvConsignaQueCorresponde()` no pregunta *"¿es la hora?"* sino *"¿qué
 * consigna va AHORA?"*, mirando en qué tramo del día estamos. Es el
 * `pv_consigna_initService()` del AVR.
 *
 * ⛔ **Se evaluó guardar la última consigna aplicada en la SRAM del MCP79410
 * —que sobrevive al reset— y se DESCARTÓ**, aunque ahorraría un movimiento:
 *
 *   - El dispositivo **no recuerda** dónde quedaron las válvulas (ver
 *     `drv_cpres.h`), así que el datalogger es el único que podría saberlo.
 *   - Pero si lo recordara **mal** —porque alguien las movió a mano, o porque la
 *     pila falló y la SRAM quedó con basura— el equipo **no lo corregiría
 *     nunca**: se quedaría con la consigna equivocada indefinidamente.
 *   - Aplicar siempre al arrancar es **autocorrectivo**: si estaba bien el
 *     movimiento es redundante, y si estaba mal lo arregla.
 *
 * Con un dispositivo que no puede informar su estado, esa propiedad vale más que
 * el movimiento que se ahorra. El AVR tiene razón y se copia tal cual.
 *
 * ⚠ **Lo que ningún diseño puede resolver acá**: si alguien mueve las válvulas a
 * mano, el datalogger no se entera hasta la próxima consigna.
 */

#ifndef APPLICATION_TASKS_TKCTLPRES_H_
#define APPLICATION_TASKS_TKCTLPRES_H_

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "drv_cpres.h"

/* No arma frames ni toca FatFs: lo que hace es esperar y hablar por el bus. */
#define tkCtlPres_STACK_SIZE    512
#define tkCtlPres_PRIORITY      ( tskIDLE_PRIORITY + 1 )

/*------------------------------------------------------------------------------
 * ⭐ El período, y el porqué está arriba. **No subirlo por encima de 60 s**: si
 * el muestreo fuera más lento que la ventana, la consigna se perdería — que es
 * exactamente lo que este número evita.
 *----------------------------------------------------------------------------*/
#define TKCTLPRES_MS_PERIODO      45000U

/* Cada cuánto se mira el reloj mientras se espera el cambio de minuto. */
#define TKCTLPRES_MS_CHEQUEO_MIN   5000U

void tkCtlPres( void *pvParameters );

extern TaskHandle_t xHandle_tkCtlPres;
extern StaticTask_t tkCtlPres_TCB;
extern StackType_t  tkCtlPres_Stack[ tkCtlPres_STACK_SIZE ];

/*------------------------------------------------------------------------------
 * Una orden puntual para las válvulas externas, que es lo que manda el servidor
 * en la respuesta a un frame de datos (`EXT_V0_OPEN`, `VCLOSE`, …).
 *
 * ⭐ **Va por notificación y no por llamada directa**, igual que el AVR: una
 * orden tarda 30-45 s, y `tkWan` no puede quedarse bloqueada eso en medio de una
 * sesión con el servidor.
 *
 * ⚠ Con dos órdenes muy seguidas, la segunda **pisa** a la primera
 * (`eSetValueWithOverwrite`). Es lo que corresponde: encolar movimientos de una
 * válvula no significa nada —el destino es el último que se pidió— y es el mismo
 * criterio que el mutex de la válvula TOYI.
 *----------------------------------------------------------------------------*/
void tkCtlPres_orden( cpres_cmd_t eCmd );

/* `kill cpres`, con el mismo criterio que `kill wan`: se suspende para que un
   operador pueda trabajar el dispositivo a mano, y no hay cómo revivirla. */
void tkCtlPres_pedir_kill( void );
bool tkCtlPres_matada( void );

#endif /* APPLICATION_TASKS_TKCTLPRES_H_ */
