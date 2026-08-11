/*
 * drv_uart.h
 *
 * Driver de UART sobre la HAL del STM32. Es la ÚNICA capa que toca el hardware
 * serie: FRTOS-IO y la aplicación no ven ni un registro ni un UART_HandleTypeDef.
 *
 * DIFERENCIAS CONTRA EL drv_uart.c DE FWDLGZ_V1 (AVR/SAM4L), y por qué:
 *
 *  - Una sola implementación con TABLA en vez de drv_uart0..4_init/write/read.
 *    En el AVR los registros de cada USART eran constantes de compilación y no
 *    había handles, así que las cinco copias eran inevitables. Acá cada UART ya
 *    es un UART_HandleTypeDef, con lo cual alcanza una fila por instancia.
 *
 *  - RX en un STREAM BUFFER de FreeRTOS, no en un ringBuffer poleado. El lector
 *    se bloquea en el kernel y lo despierta la ISR. El driver viejo poleaba con
 *    vTaskDelay(1), lo que acá despertaría al micro 512 veces por segundo y
 *    anularía el tickless.
 *
 *  - TX por interrupción con semáforo, no por poleo. Transmitir una línea a
 *    115200 son ~5,5 ms: pollearlos deja la CPU girando en vano.
 *
 *  - TX toma un candado de energía (ver pwr_lock.h) mientras dura, para que el
 *    tickless no entre en Stop 2 y corte la transmisión a la mitad.
 *
 * Todo el storage es ESTÁTICO, como el resto del proyecto: no se toca el heap,
 * que además está en 3000 bytes.
 */

#ifndef APPLICATION_DRIVERS_DRV_UART_H_
#define APPLICATION_DRIVERS_DRV_UART_H_

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

/* Instancias físicas. Hoy sólo está poblada la consola; las otras entran cuando
   se pueble el hardware, agregando una fila a la tabla de drv_uart.c. */
typedef enum {
    drvUART_TERM = 0,
    drvUART_COUNT
} drv_uart_id_t;

#define DRV_UART_TERM_RXSIZE    128U    /* buffer de RX de la consola */
#define DRV_UART_TX_TIMEOUT_MS  1000U   /* techo para que termine una transmisión */

/* Arranca todas las instancias de la tabla. Se llama una vez, antes del scheduler
   NO: necesita el kernel corriendo (crea semáforos y stream buffers), así que va
   desde la primera tarea. Devuelve false si algo no se pudo crear. */
bool drv_uart_init( void );

/* Bloqueante hasta que sale el último byte o vence DRV_UART_TX_TIMEOUT_MS.
   Devuelve los bytes escritos, o -1 si el fd no existe / hubo error. */
int16_t drv_uart_write( drv_uart_id_t id, const char *pvBuffer, uint16_t xBytes );

/* Bloqueante hasta juntar xBytes o vencer xTicksToWait. Devuelve cuántos leyó
   (puede ser menos que los pedidos si venció el timeout). */
int16_t drv_uart_read( drv_uart_id_t id, char *pvBuffer, uint16_t xBytes, TickType_t xTicksToWait );

void drv_uart_rx_flush( drv_uart_id_t id );
bool drv_uart_rx_enable( drv_uart_id_t id );
void drv_uart_rx_disable( drv_uart_id_t id );

#endif /* APPLICATION_DRIVERS_DRV_UART_H_ */
