/*
 * fs_datos.h
 *
 * El almacén de registros de medida: un **buffer circular sobre la EEPROM
 * externa M24M01**. Portado de FWDLGX 2.0.12 (`SRC/ULIBS/fileSystem.c`).
 *
 * Es lo que le da autonomía al equipo: en modo DISCRETO las muestras se acumulan
 * acá y se transmiten todas juntas cuando el modem se enciende, y si el servidor
 * no contesta se siguen acumulando en vez de perderse.
 *
 * ---------------------------------------------------------------------------
 * EL MAPA
 *
 *     0x00000 - 0x00FFF     4 KB   configuración (cfg_nvm.h)
 *     0x01000 - 0x1FFFF   124 KB   ESTO -> 1984 registros de 64 bytes
 *
 * Son casi el doble de los 1024 del AVR, que usaba la EEPROM entera para datos
 * porque su configuración vivía en la NVM interna del micro — el STM32 no la
 * tiene y las dos cosas comparten el chip.
 *
 * Con un registro cada 5 minutos, 1984 posiciones son **casi 7 días** de
 * autonomía sin transmitir.
 *
 * ---------------------------------------------------------------------------
 * ⚠ LA FAT VA EN LA SRAM DEL MCP79410, NO EN LA EEPROM
 *
 * Y no es una comodidad: es **vida útil**. Los datos se reparten sobre 1984
 * posiciones, así que cada celda se reescribe una vez por semana —unos 500
 * ciclos en 10 años sobre un chip que aguanta millones—. Pero la FAT se
 * actualiza **en cada registro**: en la EEPROM serían más de un millón de
 * escrituras sobre las mismas celdas. La SRAM del RTC no tiene límite de
 * ciclos, y ya está respaldada por la pila.
 *
 * Es lo que hace el AVR (`RTC_write( FAT_ADDRESS, … )`), y acá encaja con el
 * área de usuario que `drv_rtc79410` ya reserva después de la firma.
 *
 * ⚠ **El riesgo que eso trae, y que en ESTE equipo no es teórico**: si se pierde
 * la pila se pierde la FAT, y con ella la referencia a todos los datos
 * guardados. El porta pila del MCP79410 **falla de forma intermitente** —de
 * cuatro cortes de alimentación aguantó tres, medido el 2026-08-12—. Pablo
 * decidió mantener el diseño del AVR (2026-09-08); la defensa que sí se puso es
 * barata: **la FAT se valida al leerla** y si no es coherente se formatea
 * avisando, en vez de operar con punteros basura y pisar datos al azar.
 *
 * Los registros llevan un tag, así que el día que haga falta se puede
 * reconstruir la FAT escaneando la EEPROM.
 *
 * ---------------------------------------------------------------------------
 * CIRCULAR DE VERDAD: EL NUEVO PISA AL MÁS VIEJO (decisión de Pablo, 2026-09-08)
 *
 * ⚠ **Acá se cambió a propósito el comportamiento del AVR.** Aquel dice
 * "ringbuffer" en el comentario pero **no lo es**: cuando se llena rechaza el
 * registro nuevo (`ERROR: FS full`), o sea que conserva lo viejo y **pierde lo
 * que está pasando**. Con el modem sin señal una semana, el equipo deja de
 * registrar justo cuando más importa.
 *
 * Acá el registro nuevo **pisa al más viejo**: en un datalogger el dato reciente
 * vale más que el de hace días. Cuando eso ocurre se avisa, porque significa que
 * hubo pérdida de datos y es información de campo.
 *
 * ---------------------------------------------------------------------------
 * LEER Y BORRAR SON DOS OPERACIONES SEPARADAS
 *
 * `fs_datos_peek()` mira sin consumir y `fs_datos_pop()` descarta. Es a
 * propósito: un registro **se borra recién cuando el servidor confirmó que lo
 * recibió**. Si fueran una sola operación, cada transmisión fallida —una sesión
 * cortada, un servidor caído— se llevaría los datos puestos.
 */

#ifndef APPLICATION_TASKS_FS_DATOS_H_
#define APPLICATION_TASKS_FS_DATOS_H_

#include <stdbool.h>
#include <stdint.h>

#include "tkSys.h"

/* Dónde empieza y cuánto mide. El inicio lo fija cfg_nvm.h (CFG_NVM_FS_ADDR). */
#define FS_DATOS_RCD_SIZE       64U
#define FS_DATOS_MAX_RCDS       1984U   /* (0x20000 - 0x1000) / 64 */

/* Estado del almacén, para la consola y para la política de transmisión. */
typedef struct {
    uint16_t usHead;        /* dónde se escribe el próximo   */
    uint16_t usTail;        /* el más viejo sin transmitir   */
    uint16_t usCount;       /* cuántos hay guardados         */
    uint16_t usLength;      /* capacidad total               */
    uint32_t ulPisados;     /* cuántos se perdieron por lleno, desde el arranque */
} fs_datos_stats_t;

/*------------------------------------------------------------------------------
 * Arranque: lee la FAT de la SRAM del RTC y la valida. Si no es coherente
 * —o si el respaldo se perdió— formatea y lo avisa.
 *
 * Devuelve false si hubo que formatear, o sea si se perdieron los datos que
 * hubiera. **No es un error fatal**: el equipo sigue registrando.
 *----------------------------------------------------------------------------*/
bool fs_datos_init( void );

/* Guarda un registro. Si el almacén está lleno, descarta el más viejo. */
bool fs_datos_write( const dataRcd_t *pxDr );

/*------------------------------------------------------------------------------
 * Lee SIN consumir. `usOffset` es 0 para el más viejo, 1 para el siguiente, etc.
 * Devuelve false si no hay tantos registros o si el que hay está corrupto.
 *----------------------------------------------------------------------------*/
bool fs_datos_peek( dataRcd_t *pxDr, uint16_t usOffset );

/* Descarta `usCuantos` registros desde el más viejo. Se llama DESPUÉS de que el
   servidor confirmó. Devuelve cuántos descartó de verdad. */
uint16_t fs_datos_pop( uint16_t usCuantos );

/* Vacía el almacén. Es lo que hay que hacer al cambiar la configuración: los
   registros guardados se armarían con los nombres nuevos y los datos viejos. */
void fs_datos_format( void );

void fs_datos_stats( fs_datos_stats_t *pxStats );

#endif /* APPLICATION_TASKS_FS_DATOS_H_ */
