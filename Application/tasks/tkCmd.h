/*
 * tkCmd.h  -  la consola TERM.
 *
 * ⭐ Versión MÍNIMA (2026-09-29). El firmware se está repoblando de a un
 * periférico por vez, midiendo el consumo en cada paso, así que acá sólo están
 * los comandos que se pueden ejecutar con lo que hay montado: help, status,
 * sense y reset.
 *
 * Los demás —ee, rtc, ina, sd, vin, cnt, ev, lte, modbus, cpres, config, fs,
 * poll, frame, wdg, kill— están escritos y validados en el tag
 * v0.0.78-referencia; vuelven con su driver, no antes.
 *
 * ⚠ Y hay que acordarse de las DOS cosas que cuesta olvidar al agregar uno:
 * registrarlo con FRTOS_CMD_register() **y** ponerlo en el texto del `help`.
 * Son pasos independientes y el comando funciona igual sin el segundo, así que
 * el olvido no se nota hasta que alguien busca el comando y no lo encuentra.
 */
#ifndef TKCMD_H
#define TKCMD_H

#include "FreeRTOS.h"
#include "task.h"

#define tkCmd_STACK_SIZE    512                       /* palabras, no bytes */
#define tkCmd_PRIORITY      ( tskIDLE_PRIORITY + 1 )

extern StaticTask_t tkCmd_TCB;
extern StackType_t  tkCmd_Stack[ tkCmd_STACK_SIZE ];

void tkCmd( void *pvParameters );

#endif /* TKCMD_H */
