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

/* Estado del decodificador VT100: 0 normal, 1 recibí ESC, 2 recibí ESC [ */
static uint8_t VT100State;

static void pv_CMD_print_prompt( void );
static void pv_CMD_print_error( void );
static void pv_CMD_execute( void );
static void pv_CMD_init( void );

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
        /* Llegó ESC [ , así que este carácter es un código de control. */
        switch( cRxedChar )
        {
            case VT100_ARROWUP:   FRTOS_CMD_History( CMDLINE_HISTORY_PREV ); break;
            case VT100_ARROWDOWN: FRTOS_CMD_History( CMDLINE_HISTORY_NEXT ); break;
            default: break;
        }
        VT100State = 0;
        return ret;
    }

    if( VT100State == 1 )
    {
        VT100State = ( cRxedChar == '[' ) ? 2 : 0;
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
            if( strlen( cmdLine_buffer ) )
            {
                memset( cmdLine_History_buffer, '\0', MAX_INPUT_LENGTH );
                pv_strlcpy( cmdLine_History_buffer, cmdLine_buffer, MAX_INPUT_LENGTH );
            }
            break;

        case CMDLINE_HISTORY_PREV:
            memset( cmdLine_buffer, '\0', MAX_INPUT_LENGTH );
            pv_strlcpy( cmdLine_buffer, cmdLine_History_buffer, MAX_INPUT_LENGTH );
            cmdLine_ptr = strlen( cmdLine_buffer );
            xprintf( "%s", cmdLine_buffer );
            break;

        case CMDLINE_HISTORY_NEXT:
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
