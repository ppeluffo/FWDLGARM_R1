/*
 * drv_modbus.h
 *
 * **Modbus RTU sobre el RS485: LA TRANSACCIÓN, y nada más.**
 *
 * Esta capa arma un ADU, lo transmite, espera la respuesta, la valida y devuelve
 * el payload **crudo**. No sabe qué es un canal, ni de codecs, ni de divisores:
 * eso vive arriba, en `Application/tasks/modbus.{h,c}`.
 *
 * ---------------------------------------------------------------------------
 * ⭐ POR QUÉ SE PARTIÓ EN DOS, Y NO ES UNA MANÍA DE ARQUITECTURA
 *
 * En FWDLGX hay **dos caminos** para hablar Modbus:
 *
 *   1. `modbus_io()`, atado a la struct de configuración de un canal.
 *   2. `cpres.c`, que **arma las tramas byte a byte a mano** —incluido su propio
 *      llamado a `modbus_CRC16()`— porque lo único que quiere es leer el
 *      registro 1 del esclavo 0x64, y para eso el camino 1 no le sirve.
 *
 * Lo duplicado es justo lo que arma el CRC y valida la respuesta, así que cada
 * bug de esa parte está en dos lugares y hay que acordarse de arreglar los dos.
 * Con la transacción expuesta acá, el poleo de canales y la consigna del control
 * de presión usan **el mismo motor**, y hay un solo sitio donde se arma un ADU.
 *
 * Es el mismo criterio que `wan_sesion_*`: una sola implementación, no dos
 * caminos que se parecen.
 *
 * ---------------------------------------------------------------------------
 * ⭐ LA RECEPCIÓN BLOQUEA EN EL KERNEL — el AVR poleaba cada 50 ms
 *
 * `modbus_rcvd_ADU()` de FWDLGX mira el contador del buffer **cada 50 ms hasta
 * un segundo**, que es exactamente lo que el checklist de portación de CLAUDE.md
 * prohíbe: despertaría al micro 20 veces por segundo y anularía el tickless.
 *
 * Acá se usa `drv_rs485_read_frame()`, que **bloquea en el kernel** y corta la
 * trama por **silencio en la línea** — que es, literalmente, la delimitación que
 * define Modbus RTU (el t3.5). Sale más simple *y* más correcto.
 *
 * ---------------------------------------------------------------------------
 * ⚠ LO QUE ESTA CAPA VALIDA Y EL AVR NO
 *
 * Aquel daba por buena cualquier respuesta con largo >= 3 y CRC correcto. Eso
 * deja pasar tres cosas, y las tres devuelven un número plausible y falso:
 *
 *   1. **La respuesta de OTRO esclavo.** Con dos dispositivos en el bus, uno que
 *      conteste tarde se toma como respuesta del que estamos poleando. Es el
 *      mismo mecanismo que nos comió una tarde con los acuses rezagados de los
 *      `DATANR` (ver CLAUDE.md, paso 5c).
 *   2. **La respuesta a OTRA función.**
 *   3. ⛔ **Una EXCEPCIÓN Modbus.** El esclavo contesta `fcode | 0x80` más un
 *      código de error; eso tiene **CRC válido y largo 5**, así que el AVR la da
 *      por buena y **decodifica el código de excepción como si fuera el dato**.
 *      Con un registro inexistente devolvería, por ejemplo, un 2 perfectamente
 *      creíble. Del mismo tipo que el signo que perdía el INA3221.
 *
 * ---------------------------------------------------------------------------
 * FUNCIONES IMPLEMENTADAS: 03, 04 y 06. A PROPÓSITO.
 *
 * | 03 | Read Holding Registers | el poleo de canales                      |
 * | 04 | Read Input Registers   | el poleo — muchos caudalímetros usan ésta |
 * | 06 | Write Single Register  | la consigna del control de presión        |
 *
 * El AVR no implementa la **04** aunque su configuración ya la acepta
 * (`cfg_modbus_set()` valida "fcode tiene que ser 3 o 4"): un canal configurado
 * así se guarda bien y falla recién al polear. Acá ese hueco queda cerrado.
 *
 * El resto —01, 02, 05, 15 y el 16— **no se implementa**, y es la misma regla
 * del bring-up: no escribir código que no se puede validar. El día que aparezca
 * un dispositivo que pida otra función, se agrega **con ese dispositivo en el
 * banco**.
 */

#ifndef APPLICATION_DRIVERS_DRV_MODBUS_H_
#define APPLICATION_DRIVERS_DRV_MODBUS_H_

#include <stdbool.h>
#include <stdint.h>

/*------------------------------------------------------------------------------
 * ⚠ EL TECHO DE REGISTROS POR TRANSACCIÓN, y por qué está acá
 *
 * Una respuesta de lectura son `5 + 2*N` bytes (SLA, FC, byte count, payload,
 * CRC), así que este número fija el buffer de recepción. `cfg_modbus.c` lo usa
 * para **rechazar en el setter** una configuración que no entraría: es la
 * diferencia entre fallar al configurar y fallar en campo seis meses después.
 *
 * 16 es holgado: el decodificador de canales interpreta a lo sumo 4 bytes
 * (u32/float), o sea 2 registros.
 *----------------------------------------------------------------------------*/
#define DRV_MODBUS_MAX_REGS         16U
#define DRV_MODBUS_MAX_PAYLOAD      ( 2U * DRV_MODBUS_MAX_REGS )

/*------------------------------------------------------------------------------
 * Los tiempos.
 *
 * `TIMEOUT` es lo que se espera al PRIMER byte de la respuesta; es el mismo
 * segundo que usa el AVR y es lo habitual en Modbus RTU.
 *
 * `SILENCIO` es el t3.5 que cierra la trama: a 9600 8N1 un carácter son 1,04 ms,
 * así que 3,5 son 3,65 ms. Se usan 6 ms porque **el piso de resolución es un
 * tick, 1,95 ms**, y 6 ms son 3 ticks limpios. Ver la advertencia de
 * `drv_uart_read_frame()`: a 19200 esto quedaría al límite y habría que ir al
 * registro `RTOR` del USART.
 *----------------------------------------------------------------------------*/
#define DRV_MODBUS_MS_TIMEOUT     1000U
#define DRV_MODBUS_MS_SILENCIO       6U

/*------------------------------------------------------------------------------
 * ⭐ El resultado dice QUÉ falló, no sólo que falló.
 *
 * En el bring-up esto vale más que el éxito, igual que con la secuencia de
 * escape del modem: `mbSIN_RESPUESTA` manda a mirar el cableado y la dirección
 * del esclavo; `mbCRC` manda a mirar el ruido y la velocidad; y `mbEXCEPCION`
 * dice que el enlace está **perfecto** y el problema es el registro que pedimos.
 * Los tres se ven igual si lo único que se devuelve es `false`.
 *----------------------------------------------------------------------------*/
typedef enum {
    mbOK = 0,
    mbPARAMETRO,        /* argumento fuera de rango: no se transmitió nada    */
    mbBUS_APAGADO,      /* el riel del SP3485 está cortado                    */
    mbSIN_RESPUESTA,    /* timeout: nadie contestó                            */
    mbTRAMA_CORTA,      /* llegó algo, pero no alcanza para ser una respuesta */
    mbCRC,              /* el CRC no cierra: se ignora, como manda el protocolo */
    mbOTRO_ESCLAVO,     /* contestó una dirección que no es la que se pidió   */
    mbOTRO_FCODE,       /* la respuesta es de otra función                    */
    mbEXCEPCION,        /* el esclavo devolvió fcode|0x80 (ver la excepción)  */
    mbLARGO_INESPERADO  /* el byte count no coincide con los registros pedidos */
} mb_result_t;

const char *drv_modbus_error_str( mb_result_t eRes );

/*------------------------------------------------------------------------------
 * Lee registros. `ucFcode` es 3 (holding) o 4 (input).
 *
 * Deja en `pucPayload` los `2 * ucNroRegs` bytes **tal como vinieron del bus**,
 * sin reordenar: interpretar el orden es trabajo del codec, que vive arriba.
 * `pucLargo` recibe cuántos bytes quedaron (puede ser NULL).
 *----------------------------------------------------------------------------*/
mb_result_t drv_modbus_leer( uint8_t ucSla, uint8_t ucFcode, uint16_t usReg,
                             uint8_t ucNroRegs,
                             uint8_t *pucPayload, uint8_t *pucLargo );

/*------------------------------------------------------------------------------
 * Escribe un registro (función 06).
 *
 * La norma dice que la respuesta es **el eco del pedido**, y acá se verifica
 * **la dirección del registro**: un esclavo que conteste sobre otro registro no
 * entendió lo que se le pidió. El AVR no mira nada (*"No se analiza la respuesta
 * ya que es echo"*), así que una escritura rechazada se le ve idéntica a una
 * exitosa.
 *
 * ⛔ **Pero el VALOR devuelto NO se verifica, y no es una omisión.** El control
 * de presión de Spymovil **no devuelve el eco del valor: devuelve su registro de
 * status** — se ve en `modbus_slave_process_frame06()` de su firmware, y en sus
 * propias capturas: a un pedido de `05` contesta `01`, y a uno de `06` contesta
 * `04`. Exigir el eco lo rechazaría **siempre**.
 *
 * ⚠ Y ese valor es información útil, pero **no dice que el trabajo arrancó**.
 * Verificado en banco el 2026-09-22: devolvió `0x0A`, o sea **IDLE**, justo
 * después de aceptar una consigna. Su firmware lo explica —notifica a su propia
 * tarea y contesta enseguida con el status **de antes**—, así que el bit RUN lo
 * pone después.
 *
 * Lo que confirma entonces es que **el dispositivo estaba libre cuando aceptó**.
 * Sale por `pusRespuesta` (puede ser NULL) porque eso igual vale, pero
 * **interpretarlo como "ya empezó" sería un error**.
 *----------------------------------------------------------------------------*/
mb_result_t drv_modbus_escribir( uint8_t ucSla, uint16_t usReg, uint16_t usValor,
                                 uint16_t *pusRespuesta );

/* El código de la última excepción recibida (1 = función ilegal, 2 = dirección
   ilegal, 3 = dato ilegal, 4 = falla del esclavo…). Sólo vale tras `mbEXCEPCION`. */
uint8_t drv_modbus_ultima_excepcion( void );

/* Traza hexadecimal de lo que sale y lo que entra. Es el `f_debug_modbus` del
   AVR, y en un bus ajeno es la única forma de ver qué está pasando. */
void drv_modbus_debug( bool bOn );
bool drv_modbus_debug_estado( void );

/* El CRC16 de Modbus. Público porque es útil para diagnóstico —verificar a mano
   una trama que devolvió `mbCRC`— y porque no depende de nada de acá. */
uint16_t drv_modbus_crc16( const uint8_t *pucMsg, uint8_t ucSize );

#endif /* APPLICATION_DRIVERS_DRV_MODBUS_H_ */
