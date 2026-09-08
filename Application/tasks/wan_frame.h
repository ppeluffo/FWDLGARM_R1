/*
 * wan_frame.h
 *
 * El FRAME: la serialización de un `dataRcd_t` en el formato que entiende el
 * servidor. Portado de `wan_load_dr_in_txbuffer()` de FWDLGX 2.0.12
 * (`SRC/TASKS/FWDLGX_tkWAN.c`).
 *
 * ---------------------------------------------------------------------------
 * ⭐ ESTE ARCHIVO ES EL CONTRATO. NO ES UNA DECISIÓN DE DISEÑO.
 *
 * El servidor está en producción y no se toca. Pablo lo puso como la única
 * condición dura de la migración: *"Es importante mantener el formato de los
 * frames de datos ya que el servidor debe poder entendernos"*.
 *
 * O sea que acá **no se mejora nada**: ni el orden de los campos, ni la
 * cantidad de decimales, ni los nombres. Si algo parece raro —y hay cosas que lo
 * parecen— es porque así lo espera el otro lado.
 *
 * El formato:
 *
 *     ID=<imei>&HW=<hw>&TYPE=<type>&VER=<ver>&CLASS=DATA
 *     &DATE=YYMMDD&TIME=HHMMSS
 *     &<nombre>=%0.2f     por cada analógica HABILITADA
 *     &<nombre>=%0.3f     el contador, si está habilitado
 *     &<nombre>=%0.3f     por cada canal modbus habilitado
 *     &V0=<0|1>           la válvula: 0 = abierta, 1 = cerrada
 *     &bt3v3=%0.3f
 *     &bt12v=%0.3f
 *
 * Tres cosas que hay que tener presentes:
 *
 *  - **Los nombres de los campos salen de la configuración**, no son fijos: el
 *    servidor los aprende del frame de configuración. Por eso dos canales con el
 *    mismo nombre rompen el frame — ver `cfg_nvm_chequear_nombres()`.
 *  - **Sólo se emiten los canales habilitados**, así que el conjunto de campos
 *    varía de equipo a equipo.
 *  - **Las precisiones son distintas a propósito**: 2 decimales las analógicas,
 *    3 el contador y los modbus. No unificarlas.
 *
 * `CLASS=DATA` espera respuesta del servidor; `CLASS=DATANR` no ("no response"),
 * y es lo que se usa al vaciar la memoria de golpe.
 *
 * ---------------------------------------------------------------------------
 * EL CENTINELA -9999 (decisión de Pablo, 2026-09-08)
 *
 * Un campo que no se pudo medir viaja como **-9999**, no se omite.
 *
 * El razonamiento es de Pablo y es el correcto: *"si no lo transmitimos el
 * servidor no lo detecta"*. Un campo **ausente** se confunde con un canal
 * deshabilitado y pasa desapercibido; un **-9999** salta a la vista, y como las
 * magnitudes que mide este equipo son positivas, no puede confundirse con una
 * medida real.
 *
 * Es el mismo criterio que el `SIN_DATO` de la consola, la firma del MCP79410
 * para la hora y el `estado_asumido` de la válvula: **hacer visible lo que no se
 * sabe**, en vez de dejar que se mezcle con lo que sí.
 *
 * ⚠ La HORA es la excepción, y no por descuido: no es un campo numérico, así que
 * el centinela no aplica. Si el reloj arrancó frío viaja su fecha tal cual
 * —`010101`— que del lado del servidor es tan detectable como un -9999.
 */

#ifndef APPLICATION_TASKS_WAN_FRAME_H_
#define APPLICATION_TASKS_WAN_FRAME_H_

#include <stdbool.h>
#include <stdint.h>

#include "tkSys.h"

/* Lo que va en un campo que no se pudo medir. Ver arriba. */
#define WAN_CENTINELA_SIN_DATO      ( -9999.0f )

/*
 * 512 bytes. En el AVR eran 255 y **se desbordaban**: con los 9 canales
 * habilitados y nombres normales el frame pasa de 255 y allá corrompía variables
 * globales — está documentado en el propio `wan_load_dr_in_txbuffer()`. Acá la
 * RAM sobra, así que el buffer se dimensionó para el peor caso con holgura.
 */
#define WAN_FRAME_BUFFER_SIZE       512U

/*------------------------------------------------------------------------------
 * Arma el frame de datos en `pcBuf`. Devuelve los bytes escritos (sin el NUL), o
 * 0 si no entró — y en ese caso lo avisa por consola en vez de mandar un frame
 * truncado, que del otro lado sería un registro con campos perdidos.
 *
 * `bConRespuesta` elige entre `CLASS=DATA` y `CLASS=DATANR`.
 *----------------------------------------------------------------------------*/
uint16_t wan_frame_data( char *pcBuf, uint16_t usSize, const dataRcd_t *pxDr,
                         bool bConRespuesta );

/*------------------------------------------------------------------------------
 * El IMEI, que es el `ID` con el que el servidor identifica al equipo.
 *
 * ⏳ **HOY DEVUELVE UN IMEI FALSO, Y ES PROVISORIO DE ESTA ETAPA** (acordado con
 * Pablo, 2026-09-08): esta etapa no incluye el modem, así que no hay de dónde
 * sacar el real.
 *
 * **Lo definitivo, para el paso 5**: tkWAN prende el modem al arrancar, le
 * pregunta el IMEI con `AT+IMEI?` y lo cachea; **queda fijado aunque después el
 * modem se apague**, así que el frame nunca necesita el modem encendido para
 * armarse. Sólo hay que reemplazar lo que devuelve esta función.
 *
 * ⚠ **El valor falso son 15 ceros a propósito.** Es sintácticamente un IMEI, así
 * que no rompe el parseo del servidor, pero **ningún equipo real lo tiene**: si
 * un frame de prueba llegara por error al servidor de producción, sería
 * rechazado como equipo desconocido en vez de mezclarse con los datos de un
 * datalogger que existe. Por eso no se usó el IMEI de un módulo real de los que
 * andan dando vueltas en las notas.
 *----------------------------------------------------------------------------*/
const char *wan_imei( void );

#endif /* APPLICATION_TASKS_WAN_FRAME_H_ */
