/*
 * tkCtl.h
 *
 * Tarea de control. Es la primera que arranca y, por ahora, lo único que hace es
 * dar señales de vida por el LED.
 *
 * La memoria es ESTÁTICA: el stack y el TCB los provee esta tarea, no el heap.
 * Se declaran extern acá y se definen una sola vez en tkCtl.c — definirlos en el
 * header haría que cada unidad de compilación cree los suyos y el linker corte
 * por "multiple definition" (GCC >= 10 usa -fno-common).
 */

#ifndef APPLICATION_TASKS_TKCTL_H_
#define APPLICATION_TASKS_TKCTL_H_

#include "FreeRTOS.h"
#include "task.h"

#define tkCtl_STACK_SIZE    384                       /* palabras, no bytes */
#define tkCtl_PRIORITY      ( tskIDLE_PRIORITY + 1 )

void tkCtl( void *pvParameters );

extern TaskHandle_t xHandle_tkCtl;
extern StaticTask_t tkCtl_TCB;
extern StackType_t  tkCtl_Stack[ tkCtl_STACK_SIZE ];

#endif /* APPLICATION_TASKS_TKCTL_H_ */
