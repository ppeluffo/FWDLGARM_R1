/*
 * drv_term_sense.h
 *
 * TERM_SENSE (PB5): entrada digital que en 0 indica que hay una terminal conectada.
 *
 * DIFERENCIA CONTRA FWDLGX, y es la que importa: allá tkCtl POLEABA el pin en
 * cada vuelta. Acá el micro duerme el 98 % del tiempo, así que polear significa
 * despertarlo sólo para mirar un pin que casi siempre está igual — para detectar
 * la terminal en menos de 250 ms habría que despertar 4 veces por segundo, justo
 * lo que el tickless vino a evitar.
 *
 * El pin está en EXTI por ambos flancos: el flanco despierta al micro solo (las
 * líneas EXTI son el mecanismo de wakeup del Stop 2) y la ISR actualiza el estado.
 * Costo cero mientras no pasa nada, detección inmediata cuando pasa.
 *
 * Al detectar terminal se toma el candado pwrLOCK_TERM, con lo cual el port baja
 * de Stop 2 a Sleep y la USART conserva su reloj para poder recibir.
 */

#ifndef APPLICATION_DRIVERS_DRV_TERM_SENSE_H_
#define APPLICATION_DRIVERS_DRV_TERM_SENSE_H_

#include <stdbool.h>

/* Lee el estado inicial del pin y sincroniza el candado de energía. La ISR ya
   puede estar corriendo antes de esto; por eso la lectura es del pin, no un
   valor asumido. */
void drv_term_sense_init( void );

/* true = hay una terminal conectada. */
bool drv_term_sense_presente( void );

#endif /* APPLICATION_DRIVERS_DRV_TERM_SENSE_H_ */
