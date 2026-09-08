/*
 * tkSys.h
 *
 * La tarea de MEDIDA: despierta cada `timerpoll` segundos, polea todos los
 * canales configurados y deja el resultado en un `dataRcd_t`. Portada de FWDLGX
 * 2.0.12 (`SRC/TASKS/FWDLGX_tkSys.c` y `u_poll_data()` de `FWDLGX_utils.c`).
 *
 * Es el corazón del datalogger: todo lo demás —el frame, el almacenamiento, la
 * sesión con el servidor— trabaja sobre lo que esta tarea produce.
 *
 * ---------------------------------------------------------------------------
 * ⚠ ESTADO: EL REGISTRO ESTÁ INCOMPLETO A PROPÓSITO (2026-09-08)
 *
 * Dos de los campos todavía no se pueden medir, y **el registro lo dice** en vez
 * de rellenar con ceros:
 *
 *   - **Las 3 analógicas**: el INA3221 no contesta en el I2C de la placa nueva
 *     (`i2c scan` no muestra el `41`). Es hardware, no firmware.
 *   - **El contador**: el dato que viaja al servidor es el CAUDAL, no los
 *     pulsos —el `modo_medida` sólo cambia cómo se imprime en consola—, y ese
 *     caudal sale de un EMA calculado por pulso en la ISR, con decay por
 *     silencio, clamp de slew-rate y arranque con alpha variable. Portarlo es
 *     un paso propio (2b) porque toca la ISR de `drv_pulsos`, que hoy sólo
 *     cuenta y no lleva timestamps.
 *
 * **Por qué un bitmask de inválidos y no un cero**: un canal que no se pudo
 * medir y otro que midió cero se ven idénticos en un float. En un datalogger eso
 * es lo peor que puede pasar — un dato plausible y falso se mezcla con los
 * buenos y después no hay forma de separarlos. Es el mismo criterio que la firma
 * en la SRAM del MCP79410 para la hora, o el `estado_asumido` de la válvula.
 *
 * ⏳ **Qué hace el frame con un campo inválido es decisión del paso 3**, no de
 * acá: lo natural es no emitirlo, pero eso lo tiene que aceptar el servidor.
 */

#ifndef APPLICATION_TASKS_TKSYS_H_
#define APPLICATION_TASKS_TKSYS_H_

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "cfg_ainputs.h"
#include "cfg_modbus.h"
#include "drv_rtc79410.h"

#define tkSys_STACK_SIZE    512                       /* palabras, no bytes */
#define tkSys_PRIORITY      ( tskIDLE_PRIORITY + 1 )

/*------------------------------------------------------------------------------
 * Qué campos NO se pudieron medir. Un bit por campo; 0 = el registro es entero.
 *----------------------------------------------------------------------------*/
#define dataINVALIDO_AIN0       ( 1U << 0 )
#define dataINVALIDO_AIN1       ( 1U << 1 )
#define dataINVALIDO_AIN2       ( 1U << 2 )
#define dataINVALIDO_CONTADOR   ( 1U << 3 )
#define dataINVALIDO_BT12V      ( 1U << 4 )
#define dataINVALIDO_BT3V3      ( 1U << 5 )
#define dataINVALIDO_RTC        ( 1U << 6 )

/*------------------------------------------------------------------------------
 * El registro de medida. **Mismos campos y mismo orden que el `dataRcd_s` del
 * AVR**, porque de acá sale el frame y el frame es el contrato.
 *
 * `ucValvula` es 0 = abierta, 1 = cerrada — así viaja en `&V0=%d`, y es al revés
 * de lo que uno esperaría. Se conserva tal cual.
 *----------------------------------------------------------------------------*/
typedef struct {
    float         fAinputs[ CFG_AINPUTS_NRO_CANALES ];
    float         fContador;
    float         fModbus[ CFG_MODBUS_NRO_CANALES ];
    uint8_t       ucValvula;
    float         fBt3v3;
    float         fBt12v;
    RtcTimeType_t xRtc;

    /* No viaja en el frame: es del equipo, para saber en qué creerle. */
    uint16_t      usInvalidos;
} dataRcd_t;

void tkSys( void *pvParameters );

extern TaskHandle_t xHandle_tkSys;
extern StaticTask_t tkSys_TCB;
extern StackType_t  tkSys_Stack[ tkSys_STACK_SIZE ];

/*------------------------------------------------------------------------------
 * Un poleo completo, a pedido. Lo usa la tarea en cada vuelta y también el
 * comando `poll` de la consola: que sea la MISMA función es lo que garantiza que
 * lo que se prueba a mano sea exactamente lo que hace el equipo solo.
 *
 * Devuelve false si algún campo quedó inválido (el detalle, en `usInvalidos`).
 *----------------------------------------------------------------------------*/
bool tkSys_poll( dataRcd_t *pxDr );

/* Imprime un registro en el formato de la consola del AVR: `nombre=valor;` */
void tkSys_print( const dataRcd_t *pxDr );

/* El último registro poleado. Todavía no hay quien lo consuma —eso llega con el
   frame y el almacenamiento— pero es lo que van a mirar los pasos 3 y 4. */
const dataRcd_t *tkSys_ultimo( void );

/* Segundos que faltan para el próximo poleo. Para `status`. */
uint32_t tkSys_segundos_al_proximo( void );

#endif /* APPLICATION_TASKS_TKSYS_H_ */
