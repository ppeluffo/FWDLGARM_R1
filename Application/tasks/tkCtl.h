/*
 * tkCtl.h  -  la única tarea del firmware: el destello del LED.
 *
 * ⭐ Este firmware arranca de cero a propósito (2026-09-29). Es FreeRTOS con el
 * tick por LPTIM1, el tickless sobre Stop 2 y nada más: la referencia de
 * consumo del equipo dormido. Se va a ir poblando a medida que se puebla la
 * placa, un periférico por vez, midiendo el consumo en cada paso.
 *
 * El firmware completo anterior queda como referencia en el tag
 * v0.0.78-referencia y en Firmware/FWDLGARM_R1_REF_0.0.78/.
 *
 * ⚠ En la versión anterior tkCtl hacía TRES cosas en la misma vuelta —el
 * destello, el poleo de TERM_SENSE y el kick del watchdog—. Acá sólo destella.
 * Cuando vuelvan las otras dos hay que decidir el período de la vuelta para las
 * tres juntas, no una por una.
 */
#ifndef TKCTL_H
#define TKCTL_H

#include "FreeRTOS.h"
#include "task.h"

#define tkCtl_STACK_SIZE    256                       /* palabras, no bytes */
#define tkCtl_PRIORITY      ( tskIDLE_PRIORITY + 1 )

extern StaticTask_t tkCtl_TCB;
extern StackType_t  tkCtl_Stack[ tkCtl_STACK_SIZE ];

void tkCtl( void *pvParameters );

#endif /* TKCTL_H */
