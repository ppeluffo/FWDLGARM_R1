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

#define tkCmd_STACK_SIZE    1024    /* palabras. Necesita lugar para vsnprintf */
#define tkCmd_PRIORITY      ( tskIDLE_PRIORITY + 1 )

void tkCmd( void *pvParameters );

/*------------------------------------------------------------------------------
 * La causa del último reset, como código chico para el campo `WDG` del frame de
 * configuración. Se lee de `RCC_CSR` al arrancar, antes de limpiarla.
 *
 * ⚠ **La semántica NO es la del AVR.** Allá `wdg_resetCause` lleva los bits
 * crudos de su propio registro de reset, que son otros bits y otro micro. Acá va
 * un código enumerado; si el servidor interpretara el valor del AVR habría que
 * mapearlo. En el uso conocido es un dato de diagnóstico, no una decisión.
 *
 * En un datalogger a batería esto no es cosmético: saber si el equipo rebotó por
 * watchdog, por BOR (batería floja) o por software es información de campo.
 *----------------------------------------------------------------------------*/
typedef enum {
    wanRESET_NINGUNO = 0,   /* reinicio tibio: ninguna bandera puesta */
    wanRESET_LPWR    = 1,
    wanRESET_WWDG    = 2,
    wanRESET_IWDG    = 3,
    wanRESET_SOFT    = 4,
    wanRESET_BOR     = 5,   /* incluye el power-on normal */
    wanRESET_PIN     = 6
} wan_causa_reset_t;

uint8_t wan_causa_reset( void );

extern StaticTask_t tkCmd_TCB;
extern StackType_t  tkCmd_Stack[ tkCmd_STACK_SIZE ];

#endif /* APPLICATION_TASKS_TKCMD_H_ */
