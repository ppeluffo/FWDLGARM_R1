/*
 * tkWan.h
 *
 * La tarea de la WAN: **la máquina de estados que abre una sesión con el
 * servidor, se configura, transmite y se apaga**. Portada de
 * `FWDLGX_tkWAN.c` (paso 5d).
 *
 * ---------------------------------------------------------------------------
 * ⭐ LA SESIÓN ES LA MISMA QUE HACEN LOS COMANDOS, NO UNA COPIA
 *
 * Todo lo que la FSM necesita —`PING`, `CONF_ALL` + los `CONF_*`, el vaciado de
 * la ventana y los lotes— se validó primero como comandos de consola, uno por
 * uno contra el servidor real. Al escribir la FSM ese código **se movió acá**
 * en vez de duplicarse, y los comandos `lte ping` / `lte conf` / `lte data`
 * pasaron a llamar a estas mismas funciones.
 *
 * Es el mismo criterio que `poll` con `tkSys_poll()`: si fueran dos caminos, lo
 * que se valida a mano dejaría de ser lo que hace el equipo solo.
 * ---------------------------------------------------------------------------
 */

#ifndef APPLICATION_TASKS_TKWAN_H_
#define APPLICATION_TASKS_TKWAN_H_

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

/*------------------------------------------------------------------------------
 * ⚠ LOS STACKS SE AGRANDARON A PROPÓSITO (Pablo, 2026-09-21)
 *
 * El equipo usa **35 KB de los 256 KB** de RAM, así que apretar los stacks no
 * compra nada y cuesta caro: un desborde de stack en FreeRTOS **no da un error,
 * corrompe la memoria de al lado** y el síntoma aparece en cualquier otro
 * lugar, horas después.
 *
 * ⚠ Y hay una razón concreta para no ajustarlos por los *high water mark* que
 * se miden hoy: **todo el bring-up corre en Debug con `-O0`, que usa bastante
 * MÁS stack que `-Os`**. Los números de Debug son conservadores para Release
 * —el cambio va en la dirección segura— pero no son los que van a valer, así
 * que ajustar al límite con ellos es ajustar con los datos equivocados.
 *
 * `status` imprime el mínimo libre de las cuatro tareas: ésos son los números
 * con los que se decide, y hay que volver a mirarlos sobre un binario Release
 * antes de campo.
 *----------------------------------------------------------------------------*/
#define tkWan_STACK_SIZE    2048    /* palabras. Arma frames y habla con FatFs */
#define tkWan_PRIORITY      ( tskIDLE_PRIORITY + 1 )

void tkWan( void *pvParameters );

extern TaskHandle_t xHandle_tkWan;
extern StaticTask_t tkWan_TCB;
extern StackType_t  tkWan_Stack[ tkWan_STACK_SIZE ];

/*------------------------------------------------------------------------------
 * Los cuatro estados, los mismos del AVR.
 *
 * ⚠ **Cualquier fallo vuelve a `APAGADO`**, y no es pereza: un fallo a mitad de
 * sesión deja el módulo en un estado que desde afuera no se puede saber —puede
 * haber quedado en modo AT, con una respuesta a medio llegar—. Apagarlo y
 * empezar de nuevo es lo único que garantiza un punto de partida conocido.
 *----------------------------------------------------------------------------*/
typedef enum {
    wanAPAGADO = 0,     /* el modem apagado; acá se decide cuánto esperar     */
    wanOFFLINE,         /* encendido: modo AT, identidad, red, hora y PING    */
    wanONLINE_CONFIG,   /* CONF_ALL y los CONF_* que el servidor pida         */
    wanONLINE_DATA      /* la ventana, después los lotes de la microSD        */
} wan_estado_t;

const char *wan_estado_str( void );

/*------------------------------------------------------------------------------
 * true si hay enlace con el servidor **ahora mismo**.
 *
 * ⭐ Lo consulta `tkSys` para decidir qué hacer con una muestra en modo `RTU`:
 * con enlace la transmite, sin enlace la **descarta**. Hasta que existió esta
 * tarea, `RTU` descartaba SIEMPRE porque no había a quién preguntarle.
 *----------------------------------------------------------------------------*/
bool wan_hay_enlace( void );

/*------------------------------------------------------------------------------
 * Cuántos segundos va a quedarse apagado el modem tras esta sesión. **0 = no se
 * apaga** (continuo y RTU). Es `u_get_sleep_time()` del AVR.
 *
 * ⭐ Lo consulta `tkSys` para decidir si apaga el riel del módulo Modbus: si el
 * equipo va a dormir hasta el próximo ciclo, apagarlo ahorra; si no va a dormir,
 * apagarlo sólo obliga a pagar otra vez los 5 s de arranque en la vuelta
 * siguiente. Es exactamente lo que hace el AVR en `u_poll_data()`.
 *
 * ⚠ En `MIXTO` la respuesta depende de la HORA, así que no se puede deducir del
 * `pwrmodo` solo — por eso vive acá, donde ya está resuelto el cruce de
 * medianoche, y no se reimplementa en `tkSys`.
 *----------------------------------------------------------------------------*/
uint32_t wan_segundos_apagado( void );

/*------------------------------------------------------------------------------
 * LAS PIEZAS DE LA SESIÓN, que usan la FSM y los comandos de consola.
 *
 * ⚠ Las tres **asumen modo TRANSPARENTE** y que el modem está encendido, igual
 * que cuando eran comandos: el estado del módulo es explícito en este firmware
 * (criterio de Pablo, 2026-09-09).
 *----------------------------------------------------------------------------*/
bool wan_modem_listo ( void );      /* ¿está alimentado? */
void wan_sesion_ping ( void );
void wan_sesion_config( void );
/* Devuelve false si el vaciado no pudo completarse, o sea **si el enlace se
   cayó**: es lo que le dice a la FSM que tiene que volver a `APAGADO` a
   reestablecerlo en vez de seguir reintentando contra un servidor mudo. */
bool wan_sesion_datos( void );

/*------------------------------------------------------------------------------
 * `kill wan`: suspende la tarea para que un operador pueda trabajar con el
 * módulo a mano sin pisarse con la FSM.
 *
 * ⭐ **Y la saca del watchdog en la misma operación** — cuando el watchdog
 * exista (paso 8). Es el detalle que hace correcto al `kill` del AVR
 * (`WD_stop_task()`): suspender una tarea sin desregistrarla haría que el
 * watchdog la diera por colgada y **reseteara el equipo justo mientras el
 * operador está trabajando**, que es el síntoma más desconcertante posible.
 *
 * ⚠ **No hay "revivir", y es a propósito** (criterio de Pablo, 2026-09-21): la
 * idea es que después de entrar en modo comando para pruebas o diagnóstico, el
 * operador **resetee el datalogger para que entre en modo de funcionamiento
 * limpio**. Reanudar una tarea que quedó a mitad de una sesión la dejaría
 * creyendo cosas que ya no son ciertas.
 *----------------------------------------------------------------------------*/
void wan_pedir_kill( void );
bool wan_matada( void );

#endif /* APPLICATION_TASKS_TKWAN_H_ */
