/*
 * tkFlow.h
 *
 * **La electroválvula TOYI interna: quién la mueve.** Portada de
 * `FWDLGX_tkFlow.c`.
 *
 * ---------------------------------------------------------------------------
 * ⛔ FLOWCONTROL SE ELIMINÓ ENTERO (2026-09-22)
 *
 * Esta tarea llegó a tener un bloque de configuración con 14 slots horarios
 * —`cfg_flowcontrol`—, su hash `FH` y su `CONF_FLOWC`. **Se borró todo** por
 * decisión de Pablo: *"Esta es una funcionalidad que aún no la estamos usando,
 * así que no vamos a ensuciar el firmware con features que no se usan."* El
 * servidor se ajustó para no pedirlo.
 *
 * Lo que queda es **sólo esto**: atender las órdenes `VOPEN` / `VCLOSE` que el
 * servidor manda en la respuesta a un frame de datos.
 *
 * ---------------------------------------------------------------------------
 * ⭐ POR QUÉ UNA TAREA Y NO UNA LLAMADA DIRECTA DESDE `tkWan`
 *
 * Porque **mover la válvula bloquea 5 segundos** (el servo TOYI no tiene
 * realimentación: el driver le da tiempo y le corta la energía). Hacerlo dentro
 * de `tkWan` dejaría la sesión con el servidor congelada ese rato, en medio de
 * un vaciado de la ventana.
 *
 * Es el mismo criterio que `tkCtlPres`, y el mismo que usa el AVR: la orden
 * viaja por notificación y el que la ejecuta es otro.
 *
 * ---------------------------------------------------------------------------
 * ⚠ QUÉ ORDEN VA A QUIÉN — se confunden fácil
 *
 * | `VOPEN` / `VCLOSE`          | **acá**        | la válvula TOYI **interna** (un GPIO) |
 * | `EXT_V0/V1_OPEN/CLOSE`      | `tkCtlPres`    | las del **control de presión** (Modbus) |
 *
 * Son dos dispositivos distintos y dos tareas distintas.
 */

#ifndef APPLICATION_TASKS_TKFLOW_H_
#define APPLICATION_TASKS_TKFLOW_H_

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

/* Sólo espera notificaciones y mueve un GPIO: no arma frames ni toca FatFs. */
#define tkFlow_STACK_SIZE       384
#define tkFlow_PRIORITY         ( tskIDLE_PRIORITY + 1 )

/* Sólo es el timeout de la espera de órdenes: sin tabla de horarios no hay nada
   periódico que hacer, y existe nada más que para poder atender un `kill`. */
#define TKFLOW_MS_PERIODO         45000U

void tkFlow( void *pvParameters );

extern TaskHandle_t xHandle_tkFlow;
extern StaticTask_t tkFlow_TCB;
extern StackType_t  tkFlow_Stack[ tkFlow_STACK_SIZE ];

typedef enum {
    flowORDEN_NINGUNA = 0,
    flowORDEN_ABRIR,
    flowORDEN_CERRAR
} flow_orden_t;

/*------------------------------------------------------------------------------
 * La orden del servidor. ⚠ Con dos muy seguidas la segunda **pisa** a la
 * primera: encolar movimientos de una válvula no significa nada, porque el
 * destino es el último que se pidió.
 *----------------------------------------------------------------------------*/
void tkFlow_orden( flow_orden_t eOrden );

/* `kill flow`, con el mismo criterio que los otros: no hay cómo revivirla. */
void tkFlow_pedir_kill( void );
bool tkFlow_matada( void );

#endif /* APPLICATION_TASKS_TKFLOW_H_ */
