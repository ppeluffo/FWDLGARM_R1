/*
 * frtos_cmd.h
 *
 * Ciclo de análisis de comandos de la terminal. Portado del frtos_cmd.c de
 * FWDLGX/FWDLGZ_V1: misma API, misma edición de línea (backspace, historial con
 * flechas VT100), mismo registro de comandos por (nombre, puntero a función).
 *
 * DOS CAMBIOS OBLIGADOS, ninguno de diseño:
 *
 *  - Los buffers (argv, cmdLine_buffer, cmdLine_History_buffer) estaban DEFINIDOS
 *    en el header. Eso funcionaba en el AVR con una sola unidad de compilación,
 *    pero GCC 14 usa -fno-common por defecto y da "multiple definition" apenas el
 *    header se incluye en dos .c. Acá se DECLARAN extern y se definen una vez en
 *    el .c, igual que ya se había hecho con los ring buffers de drv_uart.
 *
 *  - Los xprintf_P(PSTR("…")) pasaron a xprintf("…") a secas. En ARM no hay
 *    progmem: el espacio de direcciones es unificado y los literales ya viven en
 *    flash sin que haya que marcarlos. Ver el checklist de portación en CLAUDE.md.
 */

#ifndef APPLICATION_FRTOS_IO_FRTOS_CMD_H_
#define APPLICATION_FRTOS_IO_FRTOS_CMD_H_

#include <stdbool.h>
#include <stdint.h>

#define CMDLINE_MAX_CMD_LENGTH  10
#define CMDLINE_MAX_COMMANDS    16
#define MAX_INPUT_LENGTH        64
#define CMDLINE_MAX_ARGS        16

#define ASCII_BEL               0x07
#define ASCII_BS                0x08
#define ASCII_CR                0x0D
#define ASCII_LF                0x0A
#define ASCII_ESC               0x1B
#define ASCII_DEL               0x7F
#define VT100_ARROWUP           'A'
#define VT100_ARROWDOWN         'B'

#define CMDLINE_HISTORY_SAVE    0
#define CMDLINE_HISTORY_PREV    1
#define CMDLINE_HISTORY_NEXT    2

typedef void ( *CmdlineFuncPtrType )( void );

/* Definidos en frtos_cmd.c. Los usan las funciones de comando para leer sus
   argumentos, por eso son visibles. */
extern char *argv[ CMDLINE_MAX_ARGS ];
extern char  cmdLine_buffer[ MAX_INPUT_LENGTH ];

void    FRTOS_CMD_init( void );
void    FRTOS_CMD_register( const char *newCmdString, void ( *fnptr )( void ) );
bool    FRTOS_CMD_process( char cRxedChar );
void    FRTOS_CMD_History( uint8_t action );
uint8_t FRTOS_CMD_makeArgv( void );

#endif /* APPLICATION_FRTOS_IO_FRTOS_CMD_H_ */
