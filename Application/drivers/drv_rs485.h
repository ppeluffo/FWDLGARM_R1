/*
 * drv_rs485.h
 *
 * RS485 sobre **USART3**: PB10 TX, PB11 RX, **PB1 DE**, transceiver **SP3485**.
 * 9600 8N1.
 *
 * Este driver NO mueve bytes: eso lo hace `drv_uart` con la instancia
 * `drvUART_RS485`, que es una fila más de su tabla. Acá vive lo que el driver de
 * UART no sabe: **la energía**.
 *
 * ---------------------------------------------------------------------------
 * EL DE LO MANEJA EL HARDWARE (PB1 = USART3_RTS_DE)
 *
 * PB1 no es un GPIO cualquiera: es el pin de *Driver Enable* del USART3. El
 * periférico levanta el DE antes del primer bit y lo baja después del último bit
 * de stop, con tiempos programables, **sin que el firmware intervenga**.
 *
 * Eso evita el bug más clásico del RS485: soltar el DE cuando el registro de
 * transmisión se vació (`TXE`) en vez de cuando la trama terminó de salir (`TC`).
 * Con `TXE` se corta el último byte, y el síntoma es desesperante — funciona con
 * tramas cortas, falla con largas, y depende de la carga del bus.
 *
 * ---------------------------------------------------------------------------
 * TRES RIELES DE ALIMENTACIÓN, CON TPS22819 (EN=1 prende)
 *
 *   rs485RAIL_BUS    PC6  - el SP3485. Sin esto no hay bus.
 *   rs485RAIL_QMBUS  PC7  - alimenta al caudalímetro
 *   rs485RAIL_CPRES  PB15 - alimenta al control de presión
 *
 * Los tres tienen pull-down de 100 K en la placa, así que arrancan APAGADOS y se
 * mantienen apagados entre el reset y `MX_GPIO_Init()`. Eso está bien y no hay
 * que compensarlo por firmware.
 *
 * ⚠ **APAGAR EL RIEL NO ALCANZA PARA APAGAR EL TRANSCEIVER.**
 *
 * Si el micro sigue manejando TX y DE en alto contra un SP3485 sin alimentación,
 * la corriente entra por los diodos de protección de esas patas y **lo alimenta
 * por las entradas**. El consumo no baja, el chip queda a medio encender, y con
 * el tester en el riel no se ve nada raro. Por eso al apagar, los tres pines del
 * bus pasan a **modo analógico** —la entrada de menor fuga del STM32, sin Schmitt
 * trigger ni pull— y vuelven a función alternada al prender.
 *
 * Es la misma trampa anotada para la microSD en CLAUDE.md.
 */

#ifndef APPLICATION_DRIVERS_DRV_RS485_H_
#define APPLICATION_DRIVERS_DRV_RS485_H_

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

typedef enum {
    rs485RAIL_BUS = 0,      /* SP3485 (PC6)                      */
    rs485RAIL_QMBUS,        /* caudalímetro (PC7)                */
    rs485RAIL_CPRES,        /* control de presión (PB15)         */
    rs485RAIL_COUNT
} rs485_rail_t;

/* Deja los tres rieles apagados y los pines del bus en alta impedancia. Va
   después de drv_uart_init(), que es quien crea los semáforos. */
bool drv_rs485_init( void );

/*
 * Prende o apaga un riel.
 *
 * Para `rs485RAIL_BUS` hace además el trabajo fino: pone los pines en función
 * alternada o en analógico, y habilita o deshabilita la recepción. Prender el
 * bus toma `pwrLOCK_RS485`; apagarlo lo suelta.
 *
 * Los rieles de los módulos externos **no** toman el candado: alimentarlos y
 * esperar a que arranquen es justo un rato en el que conviene dormir en Stop 2.
 */
void drv_rs485_power( rs485_rail_t eRail, bool bOn );

bool drv_rs485_power_estado( rs485_rail_t eRail );

/*
 * Los módulos externos tardan en arrancar. El SP3485 está listo en microsegundos,
 * pero un caudalímetro puede necesitar cientos de milisegundos antes de contestar.
 * Va como parámetro del que llama y no como número mágico acá adentro, porque
 * depende del módulo.
 */
#define DRV_RS485_SETTLE_BUS_MS     5U      /* el transceiver, con margen */

/*
 * Transmisión y recepción. Son envoltorios finos sobre `drv_uart`: existen para
 * que la capa de arriba no tenga que saber que el RS485 es la instancia N de una
 * tabla, y para poder rechazar la operación con el bus apagado en vez de dejar
 * que falle de formas raras.
 */
int16_t drv_rs485_write( const char *pvBuffer, uint16_t xBytes );
int16_t drv_rs485_read ( char *pvBuffer, uint16_t xBytes, TickType_t xTicksToWait );
void    drv_rs485_rx_flush( void );

#endif /* APPLICATION_DRIVERS_DRV_RS485_H_ */
