/*
 * tkCtl.h  -  el destello del LED y el poleo de TERM_SENSE, en la misma vuelta.
 *
 * ⏳ Falta la tercera cosa que hacía en el firmware de referencia: el kick del
 * watchdog. Cuando entre hay que decidir el período de la vuelta para las tres
 * juntas — hoy son 5 s, y ese número es también el plazo con el que se va a
 * comparar la ventana del IWDG.
 */
#ifndef TKCTL_H
#define TKCTL_H

#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"

#define tkCtl_STACK_SIZE    384                       /* palabras, no bytes */
#define tkCtl_PRIORITY      ( tskIDLE_PRIORITY + 1 )

extern TaskHandle_t xHandle_tkCtl;   /* lo usa 'status' para el high water mark */

extern StaticTask_t tkCtl_TCB;
extern StackType_t  tkCtl_Stack[ tkCtl_STACK_SIZE ];

void tkCtl( void *pvParameters );

#endif /* TKCTL_H */
