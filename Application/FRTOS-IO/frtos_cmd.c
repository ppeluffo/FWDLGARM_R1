/*
 * frtos_cmd.c  -  ver frtos_cmd.h
 */

#include <string.h>

#include "frtos_cmd.h"
#include "frtos-io.h"

/* Storage: definido acá una sola vez (el header sólo lo declara extern). */
char *argv[ CMDLINE_MAX_ARGS ];
char  cmdLine_buffer[ MAX_INPUT_LENGTH ];

static char cmdLine_History_buffer[ MAX_INPUT_LENGTH ];

/* Base de datos de comandos: dos listas paralelas, nombre y función. */
static char               BDCMD_commandList[ CMDLINE_MAX_COMMANDS ][ CMDLINE_MAX_CMD_LENGTH ];
static CmdlineFuncPtrType BDCMD_commandFuntions[ CMDLINE_MAX_COMMANDS ];
static uint8_t            BDCMD_numCommands;

static uint8_t cmdLine_ptr;

/*
 * Estado del decodificador de secuencias de escape:
 *   0 = normal
 *   1 = llegó ESC, espero el introductor
 *   2 = llegó el introductor, espero parámetros y el byte final
 */
static uint8_t VT100State;

static void pv_CMD_print_prompt( void );
static void pv_CMD_print_error( void );
static void pv_CMD_execute( void );
static void pv_CMD_init( void );
static void pv_CMD_borrar_linea( void );

/*
 * strlcpy es de BSD y newlib no la trae, así que va una versión local. Copia
 * truncando siempre con NUL y devuelve el largo que habría necesitado, que es
 * como se detecta el truncamiento.
 */
static size_t pv_strlcpy( char *pcDst, const char *pcSrc, size_t xSize )
{
    size_t xLen = strlen( pcSrc );

    if( xSize != 0U )
    {
        size_t xCopy = ( xLen >= xSize ) ? ( xSize - 1U ) : xLen;
        memcpy( pcDst, pcSrc, xCopy );
        pcDst[ xCopy ] = '\0';
    }

    return xLen;
}

//------------------------------------------------------------------------------
void FRTOS_CMD_init( void )
{
    pv_CMD_init();

    VT100State        = 0;
    BDCMD_numCommands = 0;

    memset( cmdLine_History_buffer, '\0', MAX_INPUT_LENGTH );
}
//------------------------------------------------------------------------------
void FRTOS_CMD_register( const char *newCmdString, void ( *fnptr )( void ) )
{
    if( BDCMD_numCommands >= CMDLINE_MAX_COMMANDS )
    {
        xprintf( "CMD: ERROR: tabla de comandos llena (%d), '%s' ignorado !!\r\n",
                 CMDLINE_MAX_COMMANDS, newCmdString );
        return;
    }

    memset( BDCMD_commandList[ BDCMD_numCommands ], '\0', CMDLINE_MAX_CMD_LENGTH );

    if( pv_strlcpy( BDCMD_commandList[ BDCMD_numCommands ], newCmdString, CMDLINE_MAX_CMD_LENGTH ) >= CMDLINE_MAX_CMD_LENGTH )
    {
        xprintf( "CMD: WARN: comando '%s' truncado a %d chars\r\n",
                 newCmdString, CMDLINE_MAX_CMD_LENGTH - 1 );
    }

    BDCMD_commandFuntions[ BDCMD_numCommands ] = fnptr;
    BDCMD_numCommands++;
}
//------------------------------------------------------------------------------
bool FRTOS_CMD_process( char cRxedChar )
{
    /* Procesa un carácter. Devuelve true sólo cuando ejecutó un comando. */

    bool ret = false;

    if( VT100State == 2 )
    {
        /*
         * Los parámetros numéricos y los ';' son intermedios: la secuencia recién
         * termina en un byte del rango 0x40..0x7E. Sin consumirlos, teclas como
         * Supr (ESC [ 3 ~) dejarían el '~' suelto y la consola lo metería en el
         * comando como si el usuario lo hubiera tipeado.
         */
        if( ( ( cRxedChar >= '0' ) && ( cRxedChar <= '9' ) ) || ( cRxedChar == ';' ) )
        {
            return ret;
        }

        switch( cRxedChar )
        {
            case VT100_ARROWUP:   FRTOS_CMD_History( CMDLINE_HISTORY_PREV ); break;
            case VT100_ARROWDOWN: FRTOS_CMD_History( CMDLINE_HISTORY_NEXT ); break;
            default: break;   /* izquierda, derecha, Inicio, Fin...: se ignoran */
        }
        VT100State = 0;
        return ret;
    }

    if( VT100State == 1 )
    {
        /*
         * Hay DOS introductores válidos y esto costaba que el historial no
         * anduviera: '[' es el modo normal (CSI) y 'O' es el modo "application
         * cursor keys" (SS3), que usan minicom, screen y PuTTY según cómo estén
         * configurados. Con sólo '[' reconocido, en esas terminales la flecha no
         * hacía nada y encima la 'A' final se colaba en el renglón como texto.
         */
        VT100State = ( ( cRxedChar == '[' ) || ( cRxedChar == 'O' ) ) ? 2 : 0;
        return ret;
    }

    switch( cRxedChar )
    {
        case ASCII_ESC:
            VT100State = 1;
            break;

        case ASCII_CR:
            FRTOS_CMD_History( CMDLINE_HISTORY_SAVE );
            xputChar( '\r' );
            xputChar( '\n' );
            pv_CMD_execute();
            ret = true;
            pv_CMD_init();
            pv_CMD_print_prompt();
            break;

        case ASCII_BS:
        case ASCII_DEL:
            if( cmdLine_ptr > 0 )
            {
                cmdLine_ptr--;
                cmdLine_buffer[ cmdLine_ptr ] = '\0';
                xputChar( ASCII_BS );
                xputChar( ' ' );
                xputChar( ASCII_BS );
            }
            break;

        default:
            if( ( cRxedChar >= 0x20 ) && ( cRxedChar < 0x7F ) )
            {
                /* Echo local */
                xputChar( cRxedChar );

                /* < y no <=: hay que dejar lugar para el NUL terminador. */
                if( cmdLine_ptr < ( MAX_INPUT_LENGTH - 1 ) )
                {
                    cmdLine_buffer[ cmdLine_ptr ] = cRxedChar;
                    cmdLine_ptr++;
                }
            }
            break;
    }

    return ret;
}
//------------------------------------------------------------------------------
void FRTOS_CMD_History( uint8_t action )
{
    switch( action )
    {
        case CMDLINE_HISTORY_SAVE:
            /* Sólo si hay algo: así un Enter en vacío no borra el historial, que
               es justo lo que uno hace para ver el prompt antes de repetir. */
            if( strlen( cmdLine_buffer ) )
            {
                memset( cmdLine_History_buffer, '\0', MAX_INPUT_LENGTH );
                pv_strlcpy( cmdLine_History_buffer, cmdLine_buffer, MAX_INPUT_LENGTH );
            }
            break;

        case CMDLINE_HISTORY_PREV:
            /*
             * Borrar lo tipeado ANTES de escribir lo recuperado. Sin esto, con
             * algo a medio escribir la pantalla mostraba lo viejo pegado a lo
             * nuevo mientras el buffer tenía sólo lo nuevo: a partir de ahí, cada
             * backspace borraba en la pantalla un carácter distinto del que
             * borraba en el buffer, y el comando que se ejecutaba no era el que
             * se leía. Confuso de la peor manera.
             */
            pv_CMD_borrar_linea();

            memset( cmdLine_buffer, '\0', MAX_INPUT_LENGTH );
            pv_strlcpy( cmdLine_buffer, cmdLine_History_buffer, MAX_INPUT_LENGTH );
            cmdLine_ptr = strlen( cmdLine_buffer );
            xprintf( "%s", cmdLine_buffer );
            break;

        case CMDLINE_HISTORY_NEXT:
            /* Con un solo nivel de historial, "el siguiente" es el renglón en
               blanco. Da una forma de descartar lo recuperado sin borrarlo a
               backspazos. */
            pv_CMD_borrar_linea();
            pv_CMD_init();
            break;

        default:
            break;
    }
}
//------------------------------------------------------------------------------
uint8_t FRTOS_CMD_makeArgv( void )
{
    /* Parte la línea en tokens separados por espacios. argv[0] es el comando,
       así que devuelve la cantidad de ARGUMENTOS, uno menos que los tokens. */

    char *token = NULL;
    char  parseDelimiters[] = " ";
    int   i = 0;

    memset( argv, '\0', sizeof( argv ) );

    token    = strtok( cmdLine_buffer, parseDelimiters );
    argv[ i++ ] = token;

    while( ( token = strtok( NULL, parseDelimiters ) ) != NULL )
    {
        argv[ i ] = token;
        i++;
        if( i == CMDLINE_MAX_ARGS )
        {
            break;
        }
    }

    return ( uint8_t ) ( i - 1 );
}
/*------------------------------------------------------------------------------
 * Privadas
 *----------------------------------------------------------------------------*/
static void pv_CMD_print_prompt( void )
{
    xprintf( "cmd>" );
}
//------------------------------------------------------------------------------
static void pv_CMD_print_error( void )
{
    xprintf( "command not found\r\n" );
}
//------------------------------------------------------------------------------
static void pv_CMD_init( void )
{
    cmdLine_ptr = 0;
    memset( cmdLine_buffer, 0x00, MAX_INPUT_LENGTH );
}
//------------------------------------------------------------------------------
/*
 * Deja el renglón visualmente vacío, sin tocar el buffer.
 *
 * Se hace con backspace-espacio-backspace y no con una secuencia de escape tipo
 * ESC[2K: así funciona en cualquier terminal, incluso una tonta, que es
 * exactamente el tipo de terminal con el que uno termina en el campo.
 */
static void pv_CMD_borrar_linea( void )
{
    while( cmdLine_ptr > 0 )
    {
        xputChar( ASCII_BS );
        xputChar( ' ' );
        xputChar( ASCII_BS );
        cmdLine_ptr--;
    }
}
//------------------------------------------------------------------------------
static void pv_CMD_execute( void )
{
    uint8_t i = 0;

    /* Quedarse sólo con el nombre del comando: hasta el primer blanco. */
    while( !( ( cmdLine_buffer[ i ] == ' ' ) || ( cmdLine_buffer[ i ] == 0 ) ) )
    {
        i++;
    }

    if( !i )
    {
        return;   /* línea vacía */
    }

    for( uint8_t cmdIndex = 0; cmdIndex < BDCMD_numCommands; cmdIndex++ )
    {
        if( !strncmp( BDCMD_commandList[ cmdIndex ], cmdLine_buffer, i ) )
        {
            BDCMD_commandFuntions[ cmdIndex ]();
            return;
        }
    }

    pv_CMD_print_error();
}
//------------------------------------------------------------------------------
