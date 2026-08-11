/*
 * tkCmd.h
 *
 * Tarea de la consola: lee de fdTERM y alimenta el ciclo de comandos de
 * frtos_cmd. Es la primera pieza de diagnóstico interactivo del proyecto — hasta
 * ahora el único canal eran los destellos del LED.
 */

#ifndef APPLICATION_TASKS_TKCMD_H_
#define APPLICATION_TASKS_TKCMD_H_

#include "FreeRTOS.h"
#include "task.h"

#define tkCmd_STACK_SIZE    512     /* palabras. Necesita lugar para vsnprintf */
#define tkCmd_PRIORITY      ( tskIDLE_PRIORITY + 1 )

void tkCmd( void *pvParameters );

extern StaticTask_t tkCmd_TCB;
extern StackType_t  tkCmd_Stack[ tkCmd_STACK_SIZE ];

#endif /* APPLICATION_TASKS_TKCMD_H_ */
