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

/*------------------------------------------------------------------------------
 * Fija el IMEI leído del módulo. Lo llama quien haya hecho el `AT+IMEI?`.
 *
 * Se guarda **una vez por corrida**: el módulo puede apagarse entre sesiones,
 * pero su IMEI no cambia. Así el frame se arma sin necesitar el modem encendido.
 *----------------------------------------------------------------------------*/
void wan_imei_set( const char *pcImei );

/*------------------------------------------------------------------------------
 * Los otros dos datos que el módulo sabe y el frame de configuración necesita.
 *
 * `ICCID` identifica a la SIM y `CSQ` es la calidad de señal — los dos se leen
 * con AT y se cachean, igual que el IMEI.
 *
 * ⚠ **El `CSQ` que viaja en el frame NO es el `rssi` crudo del módulo**: es el
 * valor absoluto de los dBm, `|dBm| = 113 - 2·rssi`, que es lo que manda el AVR
 * (`+CSQ: 20,99` → `CSQ=73`). Convertirlo acá y no en el frame deja el dato
 * listo en la unidad del contrato.
 *
 * ⚠ Y dos valores de `rssi` NO son medidas, son centinelas —lo descubrió el AVR
 * en mayo de 2026 y a nosotros nos costó una tarde el 2026-09-09—:
 *
 *   - **99** = "desconocido / no detectable" (3GPP).
 *   - **>= 31** = lo que devuelve el módulo **ANTES de campar en la red**. Se ve
 *     como señal excelente y no lo es: le sigue un `+CME ERROR:50` en `AT+CIP?`
 *     y el equipo no transmite nada.
 *----------------------------------------------------------------------------*/
void wan_iccid_set( const char *pcIccid );
void wan_csq_set  ( uint8_t ucRssi );

const char *wan_iccid( void );
uint8_t     wan_csq  ( void );      /* ya convertido a |dBm| */

/* true si el `rssi` crudo era una medida y no un centinela. */
bool wan_csq_valido( void );

/*------------------------------------------------------------------------------
 * El frame de CONFIGURACIÓN: manda un hash por bloque y el servidor contesta
 * cuáles quiere reconfigurar.
 *
 * ⚠ **Van CINCO hashes, no seis.** El AVR manda además `FH`, el de
 * *flowcontrol*, que en este equipo no existe. Pablo lo autorizó explícitamente
 * (2026-09-11): *"puede no mandar el FH, pero el servidor tomará uno por defecto
 * y mandará en la respuesta que debe pedir reconfigurar el flowcontrol. Luego si
 * el datalogger no lo hace, no pasa nada"*.
 *
 * O sea que el servidor va a pedir `FLOWC` en **todas** las sesiones. Es
 * inofensivo, pero obliga a una cosa: **la máquina de estados tiene que pasar a
 * transmitir datos aunque queden bloques pedidos sin configurar.** Si esperara
 * un `CONFIG=OK` que nunca va a llegar, el equipo no mandaría una sola muestra.
 *----------------------------------------------------------------------------*/
uint16_t wan_frame_conf_all( char *pcBuf, uint16_t usSize );

/*------------------------------------------------------------------------------
 * Qué pidió reconfigurar el servidor. `bFlowcontrol` se parsea igual **aunque no
 * se implemente**: verlo en la consola explica por qué la configuración nunca
 * cierra, y sin eso parecería un error.
 *----------------------------------------------------------------------------*/
typedef struct {
    bool bBase;
    bool bAinputs;
    bool bCounter;
    bool bModbus;
    bool bConsigna;
    bool bFlowcontrol;
} wan_conf_flags_t;

typedef enum {
    wanCONF_OK = 0,         /* CONFIG=OK: no hay nada que reconfigurar        */
    wanCONF_RECONFIGURAR,   /* el servidor listó bloques -> ver las flags     */
    wanCONF_DESCONOCIDO,    /* CONFIG=ERROR / FAIL: no nos reconoce           */
    wanCONF_SIN_RESPUESTA
} wan_conf_rta_t;

wan_conf_rta_t wan_frame_conf_all_rta( const char *pcRta, wan_conf_flags_t *pxFlags );

/*------------------------------------------------------------------------------
 * Los bloques de configuración, en el orden en que se los pide.
 *----------------------------------------------------------------------------*/
typedef enum {
    wanBLOQUE_BASE = 0,
    wanBLOQUE_AINPUTS,
    wanBLOQUE_COUNTER,
    wanBLOQUE_MODBUS,
    wanBLOQUE_CONSIGNA,
    wanBLOQUE_NRO          /* centinela: cuántos son */
} wan_bloque_t;

/* El nombre de la clase tal como viaja: "CONF_BASE", "CONF_AINPUTS", … */
const char *wan_conf_clase( wan_bloque_t eBloque );

/* true si el servidor pidió reconfigurar ESE bloque. Evita que cada llamador
   tenga que escribir su propio switch sobre los campos de la struct. */
bool wan_conf_pedido( const wan_conf_flags_t *pxFlags, wan_bloque_t eBloque );

/*------------------------------------------------------------------------------
 * El frame de un bloque: manda su hash y el servidor contesta con la
 * configuración que quiere que tenga el equipo.
 *
 * ⚠ **`CONF_BASE` lleva UID, ICCID, CSQ y WDG; los otros cuatro NO.** Es una
 * asimetría del AVR —ver `wan_process_frame_configBase()` contra los otros
 * cuatro— y se reproduce tal cual: es el contrato, no una decisión.
 *----------------------------------------------------------------------------*/
uint16_t wan_frame_conf_bloque( char *pcBuf, uint16_t usSize, wan_bloque_t eBloque );

/*------------------------------------------------------------------------------
 * Parsea la respuesta de un bloque y **aplica la configuración en RAM**.
 *
 * Formatos (los del AVR, `wan_process_rsp_config*`):
 *
 *   CLASS=CONF_BASE&TPOLL=30&TDIAL=900&PWRMODO=CONTINUO&PWRON=1800&PWROFF=1440
 *   CLASS=CONF_AINPUTS&PST=12&A0=true,pA,4,20,0.0,10.0,0.0&A1=…&A2=…
 *   CLASS=CONF_COUNTERS&C0=TRUE,CAU0,5.3,CAUDAL,200.0,0.25
 *   CLASS=CONF_MODBUS&ENABLE=TRUE&LOCALADDR=2&M0=TRUE,CAU0,2,2069,2,3,FLOAT,C1032,0&…
 *   CLASS=CONF_CONSIGNA&ENABLE=TRUE&DIURNA=700&NOCTURNA=2300
 *
 * ⚠ El comentario del AVR sobre `CONF_COUNTERS` muestra **cuatro** campos y está
 * desactualizado: su código parsea **seis** (agrega `qmax` y `alpha`). Vale el
 * código, no el comentario.
 *
 * ⚠ **NO graba en la EEPROM**: eso lo decide el llamador, con un solo
 * `cfg_nvm_save_all()` al final y sólo si algún bloque devolvió
 * `wanCONF_RECONFIGURAR`. Grabar bloque por bloque serían cinco escrituras de
 * las que cuatro podrían quedar a medias si la sesión se corta.
 *
 * ⚠ **`wanCONF_DESCONOCIDO` NO se actúa acá.** El AVR, ante un `CONFIG=ERROR`,
 * reconfigura el equipo a `PWR_DISCRETO` con los timers en 3600 para reintentar
 * en una hora. Eso es **política de la máquina de estados** y entra con ella en
 * el paso 5c: acá se informa y nada más. Si se hiciera desde un comando de
 * banco, probar la configuración cambiaría el modo de operación del equipo sin
 * que nadie lo pidiera.
 *----------------------------------------------------------------------------*/
wan_conf_rta_t wan_conf_aplicar( wan_bloque_t eBloque, const char *pcRta );

/*------------------------------------------------------------------------------
 * LA RESPUESTA A UN FRAME DE DATOS
 *
 * El servidor contesta `CLASS=DATA` y **puede agregar órdenes**. De las que
 * conoce el AVR, acá se implementan dos (acordado con Pablo, 2026-09-11):
 *
 *   CLOCK=YYMMDDhhmm   pone en hora el equipo
 *   RESET              reiniciar
 *
 * ⏳ `VOPEN`/`VCLOSE` y `EXT_V0/V1_*` mueven válvulas y **van con el paso 7**,
 * que es donde vive esa política. Acá se ignoran.
 *----------------------------------------------------------------------------*/
typedef enum {
    wanDATA_ACEPTADO = 0,   /* el servidor contestó `CLASS=DATA`              */
    wanDATA_OTRA_CLASE,     /* contestó algo, pero no es de este frame        */
    wanDATA_SIN_RESPUESTA
} wan_data_rta_t;

/*
 * `bReset` NO se ejecuta acá: se informa y lo hace el llamador. Reiniciar en
 * medio del procesamiento de una respuesta dejaría a medias todo lo que venga
 * después —incluido el `pop()` de los registros ya confirmados—, y el equipo
 * volvería a transmitir lo mismo después del reset.
 */
typedef struct {
    bool bClock;    /* vino un `CLOCK=` válido y se aplicó */
    bool bReset;    /* el servidor pide reiniciar          */
} wan_data_ordenes_t;

wan_data_rta_t wan_frame_data_rta( const char *pcRta, wan_data_ordenes_t *pxOrdenes );

/*------------------------------------------------------------------------------
 * El frame de PING, que es el primero de toda sesión: pregunta si el servidor
 * está del otro lado. La respuesta esperada es `CLASS=PONG`.
 *
 * No lleva datos ni fecha — sólo la identidad del equipo.
 *----------------------------------------------------------------------------*/
uint16_t wan_frame_ping( char *pcBuf, uint16_t usSize );

#endif /* APPLICATION_TASKS_WAN_FRAME_H_ */
