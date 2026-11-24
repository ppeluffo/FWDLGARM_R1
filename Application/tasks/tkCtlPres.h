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
 * ⭐⭐ RECONCILIACIÓN: NO SE PREGUNTA "¿ES LA HORA?" SINO "¿QUÉ CONSIGNA DEBE
 *     ESTAR APLICADA?"
 *
 * Es el cambio de FWDLGX **2.0.14** (2026-11-24), y acá entró junto con él.
 * Antes —en el AVR y en este firmware— se aplicaba la consigna sólo si la hora
 * leída era **exactamente** `usDiurna` o `usNocturna`. Eso tiene dos modos de
 * falla, y los dos se vieron:
 *
 *   ⛔ **Saltear el minuto**: si la vuelta del lazo se estira, el `hhmm` exacto
 *      no se muestrea nunca y **la consigna se pierde hasta el día siguiente**.
 *      Pablo lo vio en equipos AVR en campo.
 *   ⛔ **Caer dos veces en el mismo minuto**: doble envío Modbus, con el
 *      dispositivo moviendo las válvulas otra vez sin necesidad.
 *
 * ⚠ **Y en este firmware el primero NO estaba descartado, sólo era más raro.**
 * Se muestrea cada 45 s —dos veces por minuto— así que la ventana no se puede
 * perder *si la vuelta dura 45 s*. Pero **atender una orden puntual de válvula
 * tarda ~14 s**, y ahí la vuelta se va a ~59 s: el minuto se puede saltear. Un
 * bug que se vuelve improbable en vez de imposible es peor que uno visible,
 * porque pasa el banco y aparece en campo.
 *
 * ⭐ **La reconciliación lo elimina de raíz**: en cada vuelta se calcula qué
 * consigna *corresponde a la hora actual* y, si difiere de la que está
 * aplicada, se aplica. El día queda partido en dos franjas **semiabiertas**:
 *
 *     diurna   : [ usDiurna , usNocturna )
 *     nocturna : [ usNocturna , usDiurna )
 *
 * En el minuto exacto de un horario rige la consigna que **empieza**. Da igual
 * cuándo se muestree: si la hora está en la franja diurna, la consigna diurna
 * es la que debe estar, a las 06:30 y a las 14:12.
 *
 * ⭐ Y de paso **el doble envío también desaparece**, sin mecanismo alguno: una
 * vez aplicada, `deseada == aplicada` y no hay nada que hacer. Por eso se
 * eliminó `prvEsperarCambioDeMinuto()`, que existía sólo para eso.
 *
 * ---------------------------------------------------------------------------
 * ⭐ EL ESTADO AMBIGUO, y por qué hace falta nombrarlo
 *
 * La reconciliación compara contra "la consigna aplicada", así que ese dato
 * tiene que admitir un tercer valor: **no se sabe**. Es
 * `cpresCMD_NINGUNO` —vale 0, así que es el estado de arranque sin
 * inicializar nada— y ocurre en dos momentos:
 *
 *   - **al arrancar**: nadie sabe qué consigna tiene el dispositivo, que
 *     además **no recuerda la posición de sus válvulas** (ver `drv_cpres.h`);
 *   - **tras un envío fallido**: si el Modbus falló a mitad del diálogo, el
 *     dispositivo puede haber aplicado la orden o no.
 *
 * ⭐ **Ambiguo no es un estado neutro: es el que fuerza la acción.** Como nunca
 * coincide con la deseada, la próxima vuelta reconcilia. Es el mismo criterio
 * que el `estado_asumido` de la válvula TOYI y que la firma del RTC: **hacer
 * visible lo que no se sabe**, en vez de rellenarlo con un valor plausible.
 *
 * ⚠ Y el arranque sale gratis: como la aplicada arranca en AMBIGUA, la primera
 * reconciliación hace lo que antes hacía `prvConsignaDeArranque()`. Esa función
 * ya no existe — y con ella se fue su bug de borde, que con `now <= diurna`
 * aplicaba la **NOCTURNA** si el equipo arrancaba justo en el minuto de la
 * diurna.
 *
 * ---------------------------------------------------------------------------
 * LOS REINTENTOS: 3 intentos separados 5 minutos
 *
 * Si el envío falla, la aplicada queda AMBIGUA y la vuelta siguiente lo
 * intentaría otra vez — cada 45 s, encendiendo el riel del control de presión y
 * esperando ~14 s, para siempre, si el dispositivo no contesta.
 *
 * Por eso hay una serie acotada: `TKCTLPRES_CONSIGNA_MAX_INTENTOS` separados
 * `TKCTLPRES_MS_REINTENTO`. Agotada, **no se insiste hasta el próximo cambio de
 * franja** (o un reinicio). Un cambio de franja reinicia el contador, porque es
 * una orden distinta.
 *
 * ⭐ La asimetría que fija los números: un reintento de más cuesta ~14 s de riel
 * encendido; un reintento de menos deja **el control de presión operando con la
 * consigna equivocada medio día**. Por eso tres y no uno.
 *
 * ---------------------------------------------------------------------------
 * ⚠ EL PERÍODO YA NO ES UN REQUISITO DE CORRECCIÓN  (corrige lo que decía acá)
 *
 * Este header afirmaba que los 45 s son "muestreo al doble de la resolución" y
 * que **no se podía subir de 60 s** porque la consigna se perdería. Con la
 * igualdad exacta eso era cierto. **Con la reconciliación ya no**: el período
 * pasó a ser una elección de *latencia*, no de corrección.
 *
 * Se deja en 45 s igual, por dos razones que no son la original: es la espera
 * con la que también se atienden las **órdenes puntuales del servidor**, y es
 * un número validado en banco. Lo que ahora acota por arriba es el **watchdog**
 * —el plazo son 90 s y la vuelta puede llevar dos ejecuciones de ~14 s—, no la
 * consigna.
 *
 * ---------------------------------------------------------------------------
 * ⛔ POR QUÉ LA CONSIGNA APLICADA NO SE PERSISTE
 *
 * Se evaluó guardarla en la SRAM del MCP79410 —que sobrevive al reset— y se
 * **descartó**, aunque ahorraría el movimiento del arranque:
 *
 *   - El dispositivo **no recuerda** dónde quedaron las válvulas, así que el
 *     datalogger es el único que podría saberlo.
 *   - Pero si lo recordara **mal** —porque alguien las movió a mano, o porque
 *     la pila falló y la SRAM quedó con basura— el equipo **no lo corregiría
 *     nunca**: se quedaría con la consigna equivocada indefinidamente.
 *   - Arrancar en AMBIGUA y reconciliar es **autocorrectivo**: si estaba bien,
 *     el movimiento es redundante; si estaba mal, lo arregla.
 *
 * Con un dispositivo que no puede informar su estado, esa propiedad vale más
 * que el movimiento que se ahorra. El AVR tiene razón y se copia tal cual.
 *
 * ⚠ **Lo que ningún diseño resuelve acá**: si alguien mueve las válvulas a
 * mano, el datalogger no se entera hasta la próxima consigna. Y al revés —⭐ lo
 * que la reconciliación SÍ cambia—: un `cpres diurna` tipeado a mano que no
 * corresponda a la franja **se revierte en la próxima vuelta**, en menos de un
 * minuto. Para trabajar el dispositivo a mano: `kill cpres`.
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
 * El período. Ver arriba: con la reconciliación **ya no es un requisito de
 * corrección**, es latencia. Lo que acota por arriba es el watchdog.
 *----------------------------------------------------------------------------*/
#define TKCTLPRES_MS_PERIODO      45000U

/* La serie de reintentos cuando el envío falla. Ver arriba. */
#define TKCTLPRES_CONSIGNA_MAX_INTENTOS   3U
#define TKCTLPRES_MS_REINTENTO            300000U   /* 5 min */

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
 *
 * ⭐ **Una orden puntual NO toca la consigna aplicada**, y por eso la
 * reconciliación no la pisa: mover una válvula a mano no cambia en qué consigna
 * está el equipo.
 *----------------------------------------------------------------------------*/
void tkCtlPres_orden( cpres_cmd_t eCmd );

/*------------------------------------------------------------------------------
 * El estado de la consigna, para `status` y para el comando `cpres`.
 *
 * ⚠ **Es una creencia en RAM y se pierde con el reset, a propósito** — ver
 * arriba. Dice *qué cree el equipo*, no en qué estado está el dispositivo, que
 * de hecho nadie puede saber.
 *----------------------------------------------------------------------------*/
typedef struct {
    cpres_cmd_t eAplicada;    /* `cpresCMD_NINGUNO` == AMBIGUA: no se sabe */
    cpres_cmd_t eDeseada;     /* la que corresponde a la hora de AHORA.
                                 `cpresCMD_NINGUNO` == no se pudo decidir:
                                 configuración inválida u hora no confiable */
    uint16_t    usHhmm;       /* hora del último cambio aplicado con éxito */
    uint8_t     ucIntentos;   /* intentos hechos en la serie en curso */
} tkctlpres_consigna_t;

void tkCtlPres_consigna_estado( tkctlpres_consigna_t *pxEstado );

/* "DIURNA", "NOCTURNA" o "AMBIGUA". ⚠ No se usa `drv_cpres_cmd_str()` porque
   para `cpresCMD_NINGUNO` devuelve `"?"`, y en el rol de *consigna aplicada*
   ese valor tiene un significado concreto que conviene decir con su nombre. */
const char *tkCtlPres_consigna_str( cpres_cmd_t eCmd );

/* `kill cpres`, con el mismo criterio que `kill wan`: se suspende para que un
   operador pueda trabajar el dispositivo a mano, y no hay cómo revivirla. */
void tkCtlPres_pedir_kill( void );
bool tkCtlPres_matada( void );

#endif /* APPLICATION_TASKS_TKCTLPRES_H_ */
