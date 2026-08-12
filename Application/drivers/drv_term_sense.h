/*
 * drv_term_sense.h
 *
 * TERM_SENSE (PB5): entrada digital que en 0 indica que hay una terminal conectada.
 * La activa el propio conector de la terminal, así que el datalogger "sabe" cuándo
 * lo están mirando.
 *
 * Al detectar terminal se toma el candado pwrLOCK_TERM, con lo cual el port baja
 * de Stop 2 a Sleep y la USART conserva su reloj para poder recibir.
 *
 * ---------------------------------------------------------------------------
 * POR QUÉ SE LEE POR POLEO Y NO POR EXTI (decidido el 2026-08-12)
 *
 * La primera versión ponía el pin en EXTI por ambos flancos, con el argumento de
 * que polear despertaría al micro. El argumento era falso, y vale entender por
 * qué antes de que a alguien se le ocurra "mejorarlo" de vuelta:
 *
 * 1. tkCtl YA despierta cada segundo para destellar el LED. Leer un pin en esa
 *    vuelta no agrega ni una despertada: es literalmente gratis. El poleo sale
 *    caro cuando obliga a despertar; acá no obliga a nada.
 *
 * 2. El datalogger trabaja desatendido. La terminal es la excepción, para
 *    monitoreo, y que se entere un segundo más tarde no le importa a nadie.
 *    Detectar en milisegundos era resolver un problema que no existe.
 *
 * 3. La EXTI COSTABA. El conector rebota: se midieron hasta 82 interrupciones
 *    por un solo enchufe. Cada una despierta al micro de Stop 2 y le hace
 *    rehacer SystemClock_Config(). Poco en absoluto, pero es consumo a cambio
 *    de nada. Y peor que el costo era el riesgo: una línea que chicharree sola
 *    mantendría al micro despierto sin que nada parezca roto.
 *
 * 4. Muestrear un nivel una vez por segundo es antirrebote perfecto y gratis.
 *    Con EXTI habría que agregar un filtro; acá no hay nada que filtrar.
 *
 * O sea que el poleo es más simple, más barato Y más robusto. Es el mismo
 * esquema que usaba FWDLGX en el AVR.
 */

#ifndef APPLICATION_DRIVERS_DRV_TERM_SENSE_H_
#define APPLICATION_DRIVERS_DRV_TERM_SENSE_H_

#include <stdbool.h>
#include <stdint.h>

/* Primera lectura del pin, para arrancar con el candado sincronizado y no tener
   que esperar una vuelta de tkCtl. */
void drv_term_sense_init( void );

/* Relee el pin y ajusta el candado. La llama tkCtl en cada vuelta del lazo. */
void drv_term_sense_poll( void );

/* true = hay una terminal conectada. Es el último valor poleado, así que puede
   estar hasta una vuelta de tkCtl atrasado respecto del pin. */
bool drv_term_sense_presente( void );

/*------------------------------------------------------------------------------
 * Diagnóstico: lo usa el comando 'sense' de la consola.
 *
 * Va acá y no en la tarea a propósito: leer el pin y los registros del GPIO es
 * tocar hardware, y en este proyecto eso vive en la capa de drivers. La tarea
 * sólo formatea lo que estas funciones le devuelven.
 *----------------------------------------------------------------------------*/

/* Nivel ELÉCTRICO del pin AHORA, sin pasar por el poleo. true = alto = ausente.
   Comparado contra drv_term_sense_presente() muestra el atraso del muestreo. */
bool drv_term_sense_nivel_pin( void );

/* Cuántas veces cambió el estado desde el arranque. */
uint32_t drv_term_sense_cambios( void );

typedef struct {
    uint32_t ulModer;      /* 0 = entrada, 1 = salida, 2 = alternada, 3 = analógica */
    uint32_t ulPupdr;      /* 0 = flotante, 1 = pull-up, 2 = pull-down             */
} drv_term_sense_cfg_t;

void drv_term_sense_config( drv_term_sense_cfg_t *pxCfg );

#endif /* APPLICATION_DRIVERS_DRV_TERM_SENSE_H_ */
