/*
 * drv_i2c.h
 *
 * Bus I2C2 (PB13 SCL / PB14 SDA) a 100 kHz, con pull-up externos de 10 kΩ.
 *
 * Es el driver del BUS, no de los chips: mueve bytes entre una dirección de
 * dispositivo y una dirección interna de ese dispositivo. Quién interpreta esos
 * bytes —EEPROM, RTC, monitor de corriente— va una capa más arriba.
 *
 * ---------------------------------------------------------------------------
 * LAS DIRECCIONES VAN EN FORMATO DE 8 BITS (la de 7 bits corrida a la izquierda)
 *
 * O sea `0xA0` para la EEPROM y `0xDE` para el RTC, no `0x50` y `0x6F`. No es un
 * capricho: es la convención de `HAL_I2C_*` **y** la que usaba FWDLGX en el AVR
 * (`DEVADDRESS_EEPROM_M2402 = 0xA0`, `DEVADDRESS_RTC_M79410 = 0xDE`), así que las
 * constantes se portan tal cual. El bit 0 lo pone la HAL según sea lectura o
 * escritura; nosotros lo dejamos en 0.
 *
 * ---------------------------------------------------------------------------
 * POR INTERRUPCIÓN, NO POR POLEO
 *
 * `HAL_I2C_Mem_Read_IT()` / `_Write_IT()` arrancan la transacción y vuelven; la
 * tarea se bloquea en un semáforo que da el callback. A 100 kHz, leer los 7 bytes
 * de fecha y hora del RTC son ~900 µs: pollear eso sería tener la CPU girando casi
 * un milisegundo por lectura, y en un equipo a batería eso no se regala.
 *
 * Mientras dura la transacción se toma `pwrLOCK_I2C`, porque el I2C2 se alimenta
 * de PCLK1 y en Stop 2 se queda sin reloj: entrar a dormir a mitad de una
 * transferencia la cortaría con el bus tomado.
 *
 * ---------------------------------------------------------------------------
 * LO QUE ESTE DRIVER **NO** HACE, y le toca a la capa de arriba
 *
 * - **Paginado de escritura.** La M24M02 tiene páginas de 256 bytes y una
 *   escritura que cruce el borde de página **da la vuelta y pisa el principio de
 *   la misma página** en vez de seguir en la siguiente. Partir en páginas es del
 *   driver de EEPROM.
 * - **Acknowledge polling.** Después de una escritura la EEPROM queda ~5 ms en su
 *   ciclo interno y **NACKea todo** durante ese tiempo. Hay que reintentar hasta
 *   que conteste (`drv_i2c_probe()` sirve para eso). No es un error del bus.
 * - Interpretar BCD, registros de control, etc.
 */

#ifndef APPLICATION_DRIVERS_DRV_I2C_H_
#define APPLICATION_DRIVERS_DRV_I2C_H_

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

/* Direcciones de los chips de R001, en formato de 8 bits. Mismos valores que
   FWDLGX: ver el comentario de arriba. */
#define DRV_I2C_ADDR_EEPROM     0xA0U   /* M24M02 - A16/A17 van en el propio byte */
#define DRV_I2C_ADDR_RTC        0xDEU   /* MCP79410 - RTCC + SRAM                 */
#define DRV_I2C_ADDR_RTC_EE     0xAEU   /* MCP79410 - su EEPROM interna y el UID  */

/* Crea el mutex y el semáforo. Necesita el scheduler corriendo. */
bool drv_i2c_init( void );

/*
 * Leen y escriben `xBytes` a partir de la dirección interna `usMemAddr` del
 * dispositivo `ucDevAddr`.
 *
 * `ucMemAddrLen` es 1 o 2 según cuántos bytes de dirección quiera el chip: 1 para
 * el MCP79410, 2 para la M24M02.
 *
 * Devuelven los bytes movidos, o -1. En caso de -1, `drv_i2c_last_error()` dice
 * qué pasó.
 */
int16_t drv_i2c_read ( uint8_t ucDevAddr, uint16_t usMemAddr, uint8_t ucMemAddrLen,
                       char *pvBuffer, uint16_t xBytes, TickType_t xTicksToWait );

int16_t drv_i2c_write( uint8_t ucDevAddr, uint16_t usMemAddr, uint8_t ucMemAddrLen,
                       const char *pvBuffer, uint16_t xBytes, TickType_t xTicksToWait );

/*
 * ¿Contesta alguien en esa dirección? Manda sólo la dirección y mira si hay ACK.
 *
 * OJO: es la única función del driver que trabaja POR POLEO —`HAL_I2C_IsDeviceReady()`
 * no tiene variante por interrupción—, con un timeout de pocos ms. Está pensada
 * para el escaneo de banco y para el acknowledge polling de la EEPROM, no para
 * usarse seguido.
 */
bool drv_i2c_probe( uint8_t ucDevAddr );

/*
 * Toma el bus para VARIAS transacciones seguidas, y lo suelta.
 *
 * Hace falta cuando una operación lógica no se puede partir: la escritura
 * multipágina de la EEPROM es escribir-esperar-escribir-esperar, y si otra tarea
 * se mete en el medio va a recibir un NACK de la memoria ocupada y lo va a leer
 * como un error suyo.
 *
 * El mutex es recursivo, así que las llamadas a read/write/probe de adentro
 * vuelven a tomarlo sin trabarse. **Cada take necesita su give**: lo más simple
 * es que sólo los drivers de chip los usen, en la misma función.
 */
bool drv_i2c_bus_take( TickType_t xTicksToWait );
void drv_i2c_bus_give( void );

/* Código de error de la HAL de la última operación fallida (HAL_I2C_ERROR_*).
   `HAL_I2C_ERROR_AF` es un NACK: nadie contestó, o la EEPROM está ocupada. */
uint32_t drv_i2c_last_error( void );

/*
 * Reinicializa el periférico. La HAL puede quedar en BUSY si una transacción se
 * corta por la mitad, y de ahí no sale sola. Se llama solo cuando una operación
 * vence por timeout; queda expuesta para poder forzarla desde la consola.
 */
bool drv_i2c_reset( void );

#endif /* APPLICATION_DRIVERS_DRV_I2C_H_ */
