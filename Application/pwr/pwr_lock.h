/*
 * pwr_lock.h
 *
 * Candados de energía: permiten que un driver le diga al port "mientras yo esté
 * trabajando, NO entres en Stop".
 *
 * Por qué hace falta. El tickless duerme en Stop 2, que apaga el PLL y deja sin
 * reloj a casi todos los periféricos. Si el micro entra en Stop mientras la USART
 * está transmitiendo, la transmisión se corta a la mitad. Lo mismo va a pasar con
 * el modem, la microSD y el ADC.
 *
 * Cada consumidor toma su candado antes de necesitar los clocks y lo suelta al
 * terminar. Mientras haya AL MENOS UNO tomado, vPortSuppressTicksAndSleep() baja
 * de Stop 2 a Sleep: el micro se sigue durmiendo entre interrupciones —así que no
 * se pierde todo el ahorro— pero los relojes quedan vivos.
 *
 * Es un bitmask, no un contador: cada dueño tiene su bit, así que tomar dos veces
 * el mismo candado es idempotente y no hay forma de desbalancear los pares
 * acquire/release.
 */

#ifndef APPLICATION_PWR_PWR_LOCK_H_
#define APPLICATION_PWR_PWR_LOCK_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    pwrLOCK_TERM   = 0,   /* hay una terminal conectada (TERM_SENSE en 0) */
    pwrLOCK_TERM_TX,      /* la USART de la consola está transmitiendo    */
    pwrLOCK_WAN,          /* reservado: modem LTE                        */
    pwrLOCK_RS485,        /* reservado: Modbus                           */
    pwrLOCK_SD,           /* reservado: microSD                          */
    pwrLOCK_COUNT
} pwr_lock_id_t;

/* Seguras desde tarea y desde ISR. */
void pwr_lock_acquire( pwr_lock_id_t id );
void pwr_lock_release( pwr_lock_id_t id );

/* La consulta el port. true = no hay nadie que necesite los clocks -> Stop 2. */
bool pwr_deep_sleep_permitido( void );

/* Para diagnóstico por consola: qué candados están tomados. */
uint32_t pwr_lock_estado( void );

#endif /* APPLICATION_PWR_PWR_LOCK_H_ */
