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
#include <stdint.h>

/* Lee el estado inicial del pin y sincroniza el candado de energía. La ISR ya
   puede estar corriendo antes de esto; por eso la lectura es del pin, no un
   valor asumido. */
void drv_term_sense_init( void );

/* true = hay una terminal conectada. */
bool drv_term_sense_presente( void );

/*------------------------------------------------------------------------------
 * Diagnóstico
 *
 * Existe porque el pin lee "ausente" con la terminal enchufada y hay que separar
 * tres causas que se parecen entre sí: que el nivel eléctrico no baje (hardware),
 * que baje pero la EXTI no dispare (configuración), o que dispare pero el estado
 * quede mal (firmware).
 *
 * Va acá y no en la tarea a propósito: leer el pin y los registros de la EXTI es
 * tocar hardware, y en este proyecto eso vive en la capa de drivers. La tarea
 * sólo formatea lo que estas funciones le devuelven.
 *----------------------------------------------------------------------------*/

/* Nivel ELÉCTRICO del pin, sin interpretar. true = alto. Con el pull-up interno
   y nada conectado da true, que es justamente el síntoma a descartar. */
bool drv_term_sense_nivel_pin( void );

/* Cuántas veces disparó la EXTI desde el arranque. Si el nivel cambia y esto no
   se mueve, el problema es la configuración de la interrupción, no el cable. */
uint32_t drv_term_sense_eventos( void );

typedef struct {
    uint32_t ulModer;      /* 0 = entrada, 1 = salida, 2 = alternada, 3 = analógica */
    uint32_t ulPupdr;      /* 0 = flotante, 1 = pull-up, 2 = pull-down             */
    bool     bImr;         /* máscara de la línea EXTI                             */
    bool     bRtsr;        /* flanco de subida habilitado                          */
    bool     bFtsr;        /* flanco de bajada habilitado                          */
    bool     bNvic;        /* la IRQ del grupo, habilitada en el NVIC              */
} drv_term_sense_cfg_t;

void drv_term_sense_config( drv_term_sense_cfg_t *pxCfg );

#endif /* APPLICATION_DRIVERS_DRV_TERM_SENSE_H_ */
