/*
 * tkCtl.h  -  la tarea de control: el destello del LED, y por ahora también
 * quién decide cuándo el equipo empieza a dormir.
 *
 * ⏳ En el firmware de referencia hacía TRES cosas en la misma vuelta —el
 * destello, el poleo de TERM_SENSE y el kick del watchdog—. Las otras dos
 * vuelven con su etapa, y cuando lo hagan hay que decidir el período de la
 * vuelta para las tres juntas, no una por una.
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

/* true una vez que el equipo empezó a dormir. Lo informa 'status'. */
bool tkCtl_durmiendo( void );

#endif /* TKCTL_H */
