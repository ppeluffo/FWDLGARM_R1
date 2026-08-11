/*
 * frtos-io.h
 *
 * Capa de acceso genérico a periféricos, estilo POSIX: cada periférico se asocia
 * a un file descriptor y se usa siempre con frtos_open/read/write/ioctl. La
 * aplicación no sabe si del otro lado hay una UART, un bus I2C o una NVM.
 *
 * Es la misma API de FWDLGX/FWDLGZ_V1 —mismos nombres, mismos códigos de ioctl,
 * mismo int16_t con -1 en error— reimplementada sobre la HAL del STM32. El código
 * de aplicación heredado compila sin cambios; lo que cambió es lo que hay debajo
 * de la capa de drivers, que es justamente lo que tiene que cambiar al cambiar
 * de micro.
 *
 * DIFERENCIA DE IMPLEMENTACIÓN: el despacho es por TABLA, no por switch con una
 * función por instancia (frtos_write_uart0..4). Cada fd apunta a un juego de
 * operaciones y a un número de instancia, así que frtos_write() es un chequeo de
 * rango y una llamada indirecta, sin importar cuántos periféricos haya. Agregar
 * el modem o el RS485 es agregar una fila.
 */

#ifndef APPLICATION_FRTOS_IO_FRTOS_IO_H_
#define APPLICATION_FRTOS_IO_FRTOS_IO_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include "FreeRTOS.h"
#include "task.h"

/*------------------------------------------------------------------------------
 * File descriptors. Se conserva el orden de FWDLGX.
 *----------------------------------------------------------------------------*/
typedef enum {
    fdTERM = 0,
    fdWAN,          /* modem LTE   - sin implementar (hardware no poblado) */
    fdRS485A,       /* Modbus      - sin implementar                       */
    fdI2C,          /* bus I2C     - sin implementar                       */
    fdNVM,          /* config      - sin implementar                       */
    fdCOUNT
} file_descriptor_t;

/*------------------------------------------------------------------------------
 * Códigos de ioctl. Mismos números que FWDLGX para no romper código heredado.
 *----------------------------------------------------------------------------*/
#define ioctl_OBTAIN_BUS_SEMPH          1
#define ioctl_RELEASE_BUS_SEMPH         2
#define ioctl_SET_TIMEOUT               3

#define ioctl_UART_CLEAR_RX_BUFFER      4
#define ioctl_UART_CLEAR_TX_BUFFER      5
#define ioctl_UART_ENABLE_TX_INT        6
#define ioctl_UART_DISABLE_TX_INT       7
#define ioctl_UART_ENABLE_RX_INT        8
#define ioctl_UART_DISABLE_RX_INT       9
#define ioctl_UART_ENABLE_TX            10
#define ioctl_UART_DISABLE_TX           11
#define ioctl_UART_ENABLE_RX            12
#define ioctl_UART_DISABLE_RX           13
#define ioctl_UART_SET_RTS              14
#define ioctl_UART_CLEAR_RTS            15

#define ioctl_I2C_SET_DEVADDRESS        20
#define ioctl_I2C_SET_DATAADDRESS       22
#define ioctl_I2C_SET_DATAADDRESSLENGTH 23
#define ioctl_I2C_GET_LAST_ERROR        24
#define ioctl_I2C_SET_DEBUG             26
#define ioctl_I2C_CLEAR_DEBUG           27
#define ioctl_I2C_RESET                 28

#define ioctl_NVM_SET_EEADDRESS         30
#define ioctl_NVM_SET_OPERATION         31

/*------------------------------------------------------------------------------
 * API genérica
 *----------------------------------------------------------------------------*/

/* Arranca la capa y los drivers que hay debajo. Necesita el scheduler corriendo
   (crea semáforos y stream buffers), así que va desde la primera tarea. */
bool    frtos_open_all( void );

int16_t frtos_write( file_descriptor_t fd, const char *pvBuffer, uint16_t xBytes );
int16_t frtos_read ( file_descriptor_t fd, char *pvBuffer, uint16_t xBytes );
int16_t frtos_ioctl( file_descriptor_t fd, uint32_t ulRequest, void *pvValue );

/*------------------------------------------------------------------------------
 * printf de la casa
 *
 * NO se usa el printf de newlib: con --specs=nano.specs sigue reservando cientos
 * de bytes por tarea y el heap del proyecto está en 3000. Acá se formatea en un
 * buffer estático propio, protegido por mutex, y se manda por frtos_write().
 *
 * NO EXISTEN xprintf_P() ni PSTR(), y es a propósito. En el AVR hacían falta
 * porque es Harvard: un literal iba a RAM salvo que PSTR() lo dejara en flash, y
 * entonces había que leerlo con pgm_read_byte(), de ahí la variante _P del printf.
 * El Cortex-M4 tiene espacio de direcciones unificado: los literales quedan en
 * .rodata, en flash, y se leen con un ldr común. No hay nada que marcar ni dos
 * variantes posibles.
 *
 * Al portar código de FWDLGX la conversión es mecánica:
 *     sed -i 's/xprintf_P/xprintf/g; s/PSTR(\("[^"]*"\))/\1/g' archivo.c
 *----------------------------------------------------------------------------*/
#define XPRINTF_BUFFER_SIZE     160

int  xprintf ( const char *fmt, ... );                          /* -> fdTERM */
int  xfprintf( file_descriptor_t fd, const char *fmt, ... );
void xputChar( char c );                                        /* -> fdTERM */

#endif /* APPLICATION_FRTOS_IO_FRTOS_IO_H_ */
