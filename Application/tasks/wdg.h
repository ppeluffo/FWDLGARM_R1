/*
 * wdg.h
 *
 * **El watchdog cooperativo.** Portado de `SRC/XLIBS/watchdog.{h,c}` de FWDLGX.
 *
 * El perro de hardware (`drv_wdt`) sólo sabe resetear el micro cuando nadie lo
 * patea. Lo que decide si hay que patearlo está acá: una tabla con una entrada
 * por tarea vigilada, cada tarea dice "sigo viva" al pasar por su lazo, y
 * **`tkCtl` es el juez**: si alguna se pasó de plazo, deja de patear y el micro
 * se resetea solo en ~32 s.
 *
 * ---------------------------------------------------------------------------
 * ⭐ POR QUÉ UN PLAZO ÚNICO Y CORTO, Y NO UNO POR TAREA
 *
 * Fue la decisión de diseño del paso, y la corrigió Pablo (2026-09-22): *"La
 * ventaja del fraccionamiento de esperas cada 120 segundos en los AVR es que nos
 * asegurábamos que nunca pasaba más de este tiempo para enterarnos que algo
 * andaba mal."*
 *
 * La alternativa natural —que cada tarea declare su propio plazo, largo si
 * duerme mucho— parece más prolija y **es peor**: `tkWan` en modo `DISCRETO`
 * duerme hasta seis horas, así que su plazo tendría que ser de seis horas, y un
 * cuelgue suyo tardaría eso en detectarse. En un equipo que tiene que correr
 * 7x24 eso no sirve.
 *
 * El mecanismo correcto es al revés: **plazo único y corto para todos, y las
 * tareas que esperan mucho TROCEAN la espera y reportan en cada trozo**. Dormir
 * seis horas en trozos de 60 s cuesta 360 despertadas — al lado de las 21.600
 * que ya hace `tkCtl` en ese rato, es ruido.
 *
 * ---------------------------------------------------------------------------
 * ⚠ REPORTAR NO ES LO MISMO QUE CEDER LA CPU
 *
 * Una tarea puede estar perfectamente viva —cediendo el procesador en cada
 * `vTaskDelay()`— y aun así estar colgada: girando en un lazo que no avanza,
 * esperando una respuesta que no va a llegar. **El watchdog vigila el progreso,
 * no la ejecución.**
 *
 * Por eso el `wdg_report()` no va en cualquier `vTaskDelay()`, sino en los
 * puntos donde pasar de nuevo significa que la tarea avanzó: una vuelta del
 * lazo, un frame transmitido, un trozo de espera cumplido.
 *
 * ---------------------------------------------------------------------------
 * ⭐ `tkCtl` NO ESTÁ EN LA TABLA, Y SE VIGILA SOLA
 *
 * Es el juez y es el único que patea. Si `tkCtl` se cuelga, nadie patea y el
 * IWDG resetea el equipo a los ~32 s. No necesita una entrada: **vigilarla
 * sería vigilar al que vigila**, y el hardware ya lo hace mejor.
 */

#ifndef APPLICATION_TASKS_WDG_H_
#define APPLICATION_TASKS_WDG_H_

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

/*------------------------------------------------------------------------------
 * EL PLAZO. Único para todas, y el número del que depende todo lo demás.
 *
 * 90 s = el trozo de espera más largo que usa una tarea (60 s, en `tkWan`) más
 * 30 s de holgura. La holgura no es decorativa: cubre que un trozo de 60 s se
 * estire porque la tarea estaba haciendo algo cuando le tocaba —una sesión con
 * el modem, un volcado a la microSD— sin que eso se confunda con un cuelgue.
 *
 * ⚠ El AVR usa 120 s. Acá se bajó a 90 porque el trozo es de 60 y no de 120:
 * el criterio es "un trozo y medio", no un número redondo. Subirlo es
 * conservador y no rompe nada; bajarlo de 60 sí, porque un trozo entero de
 * espera legítima empezaría a dar falsos positivos.
 *----------------------------------------------------------------------------*/
#define WDG_PLAZO_MS            90000UL

/* El trozo en que las tareas parten sus esperas largas. Vive acá para que se
   lea junto al plazo: son el mismo criterio visto de los dos lados. */
#define WDG_TROZO_ESPERA_S         60UL

/*------------------------------------------------------------------------------
 * Las tareas vigiladas. `tkCtl` no está: ver el header.
 *
 * ⚠ El orden no es contrato con nadie —no viaja en ningún frame ni se guarda en
 * la EEPROM—, así que se puede reordenar sin consecuencias. No es el caso del
 * `pwrmodo`, que sí viaja en el hash.
 *----------------------------------------------------------------------------*/
typedef enum {
    wdgTK_CMD = 0,
    wdgTK_SYS,
    wdgTK_WAN,
    wdgTK_CTLPRES,
    wdgTK_FLOW,
    wdgTK_CANTIDAD
} wdg_tarea_t;

/*------------------------------------------------------------------------------
 * La tarea entra a la vigilancia. Se llama **al principio de la función de la
 * tarea**, antes de cualquier espera de arranque: así el plazo empieza a correr
 * desde que la tarea existe de verdad.
 *
 * De paso anota su `TaskHandle_t`, que es lo que le permite a `wdg_kick()`
 * saber sola quién está reportando.
 *
 * Una tarea que nunca se registra **no se vigila** y el comando `wdg` lo
 * muestra. Es el comportamiento correcto: al arrancar, hasta que una tarea no
 * dio su primera señal de vida no hay nada que juzgar.
 *----------------------------------------------------------------------------*/
void wdg_registrar( wdg_tarea_t eTarea );

/*------------------------------------------------------------------------------
 * "Sigo viva y avancé." Renueva el plazo de **la tarea que la llama**.
 *
 * ⭐ NO RECIBE UN IDENTIFICADOR, Y ESA ES LA PARTE IMPORTANTE.
 *
 * La primera versión tomaba un `wdg_tarea_t`, y eso tenía un agujero que no se
 * ve hasta que uno mira quién ejecuta cada función: **`wan_sesion_ping()`,
 * `wan_sesion_config()` y `wan_sesion_datos()` corren en `tkWan` cuando las
 * llama la FSM, pero en `tkCmd` cuando las llama `lte ping` / `lte conf` /
 * `lte data`**. Un `wdg_report( wdgTK_WAN )` adentro de esas funciones le
 * renovaría el plazo a una tarea que no es la que está trabajando, y `tkCmd`
 * —que puede pasarse varios minutos ahí adentro— quedaría sin reportar. El
 * equipo se resetearía **a mitad de un comando tipeado por un técnico**, que es
 * el peor momento posible.
 *
 * Resolviendo por `xTaskGetCurrentTaskHandle()` eso no puede pasar: el reporte
 * siempre va a quien está corriendo. Es el mismo criterio que hizo separar
 * `peek()` de `pop()` o que puso `drv_lte_pwrkey( bApretado )` en vez de "poner
 * PA5 en alto": que la forma de usarlo mal no exista.
 *
 * Llamarla desde una tarea sin registrar —o desde una ISR— no hace nada.
 *----------------------------------------------------------------------------*/
void wdg_kick( void );

/*------------------------------------------------------------------------------
 * ⚠ LA PRÓRROGA, Y ES EL ÚLTIMO RECURSO.
 *
 * Como `wdg_kick()`, pero renovando por `ulMs` en vez de por los 90 s de
 * siempre. Sirve para lo que **no se puede trocear**: una llamada que no vuelve
 * hasta terminar y que por dentro no tiene dónde reportar.
 *
 * Hoy hay exactamente dos casos, los dos de la microSD:
 *
 *  - **`f_mkfs()`** (`fs sd format borrar`), que escribe las dos copias de la
 *    FAT sector por sector y en una tarjeta grande son varios segundos.
 *  - **`fs_sd_volcar_ventana()`**, hasta 1984 líneas en una sola llamada.
 *
 * ⛔ **Donde se pueda reportar, se reporta** — es estrictamente mejor, porque el
 * watchdog sigue vigilando mientras tanto. Una prórroga es una ventana de
 * ceguera consentida, y por eso se declara con un número: quien la pide dice
 * cuánto va a tardar y se hace responsable de ese número.
 *
 * ⭐ Y es POR TAREA, no global. El AVR tiene un `sys_reset_in_progress` que
 * suspende el watchdog entero mientras dura un formateo, así que durante esos
 * segundos **ninguna** tarea queda vigilada. Acá una prórroga de `tkCmd` no le
 * quita la vigilancia a `tkWan`.
 *----------------------------------------------------------------------------*/
void wdg_kick_largo( uint32_t ulMs );

/*------------------------------------------------------------------------------
 * La tarea que llama sale de la vigilancia. Es lo que hace `kill`, y **es
 * obligatorio**: una tarea suspendida deja de reportar, así que sin
 * desregistrarla el watchdog la daría por colgada y **resetearía el equipo justo
 * mientras el operador está trabajando** — el síntoma más desconcertante
 * posible.
 *
 * Es el `WD_stop_task()` del AVR, y ese detalle es lo que hace correcto su
 * `kill`.
 *----------------------------------------------------------------------------*/
void wdg_stop_task( void );

/*------------------------------------------------------------------------------
 * El veredicto, para `tkCtl`. Devuelve false apenas una tarea registrada se
 * pasó del plazo, y deja en `*ppcCulpable` su nombre (o NULL si no se quiere).
 *----------------------------------------------------------------------------*/
bool wdg_todas_sanas( const char **ppcCulpable );

/* El comando `wdg`: la tabla entera con lo que le queda a cada una. */
void wdg_print( void );

/* Nombre para los mensajes. */
const char *wdg_tarea_str( wdg_tarea_t eTarea );

#endif /* APPLICATION_TASKS_WDG_H_ */
