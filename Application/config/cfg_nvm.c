/*
 * cfg_nvm.c  -  ver cfg_nvm.h
 */

#include <stdio.h>
#include <string.h>

#include "cfg_nvm.h"
#include "cfg_hash.h"
#include "drv_eeprom.h"
#include "frtos-io.h"

/*
 * Chequeos en tiempo de compilación: si un bloque crece más de lo que tiene
 * reservado, el compilador lo dice acá y no se descubre en campo pisando el
 * bloque siguiente. Es barato y evita el peor modo de falla del mapa.
 */
_Static_assert( sizeof( cfg_base_t )     <= ( CFG_NVM_AINPUTS_ADDR  - CFG_NVM_BASE_ADDR     ), "cfg_base_t no entra en su bloque" );
_Static_assert( sizeof( cfg_ainputs_t )  <= ( CFG_NVM_COUNTER_ADDR  - CFG_NVM_AINPUTS_ADDR  ), "cfg_ainputs_t no entra en su bloque" );
_Static_assert( sizeof( cfg_counter_t )  <= ( CFG_NVM_MODBUS_ADDR   - CFG_NVM_COUNTER_ADDR  ), "cfg_counter_t no entra en su bloque" );
_Static_assert( sizeof( cfg_modbus_t )   <= ( CFG_NVM_CONSIGNA_ADDR - CFG_NVM_MODBUS_ADDR   ), "cfg_modbus_t no entra en su bloque" );
_Static_assert( sizeof( cfg_consigna_t ) <= ( CFG_NVM_FS_ADDR       - CFG_NVM_CONSIGNA_ADDR ), "cfg_consigna_t no entra en su bloque" );

/*
 * Un bloque, una fila. Así `load_all` y `save_all` son un lazo y no cinco copias
 * de lo mismo: agregar el bloque de flowcontrol el día que entre es una línea.
 *
 * `pucChecksum` apunta al último byte de la struct, que es donde vive el
 * checksum en los cinco bloques.
 */
typedef struct {
    const char *pcNombre;
    uint32_t    ulAddr;
    void       *pvData;
    uint16_t    usSize;
    void      ( *pfDefaults )( void );
} cfg_bloque_t;

static const cfg_bloque_t xBloques[] = {
    { "base",     CFG_NVM_BASE_ADDR,     &xCfgBase,     sizeof( xCfgBase ),     cfg_base_defaults     },
    { "ainputs",  CFG_NVM_AINPUTS_ADDR,  &xCfgAinputs,  sizeof( xCfgAinputs ),  cfg_ainputs_defaults  },
    { "counter",  CFG_NVM_COUNTER_ADDR,  &xCfgCounter,  sizeof( xCfgCounter ),  cfg_counter_defaults  },
    { "modbus",   CFG_NVM_MODBUS_ADDR,   &xCfgModbus,   sizeof( xCfgModbus ),   cfg_modbus_defaults   },
    { "consigna", CFG_NVM_CONSIGNA_ADDR, &xCfgConsigna, sizeof( xCfgConsigna ), cfg_consigna_defaults },
};

#define CFG_NRO_BLOQUES     ( sizeof( xBloques ) / sizeof( xBloques[ 0 ] ) )

//------------------------------------------------------------------------------
void cfg_nvm_defaults_all( void )
{
    uint32_t i;

    for( i = 0U; i < CFG_NRO_BLOQUES; i++ )
    {
        xBloques[ i ].pfDefaults();
    }
}
//------------------------------------------------------------------------------
bool cfg_nvm_save_all( void )
{
    uint32_t i;
    bool     bOk = true;

    for( i = 0U; i < CFG_NRO_BLOQUES; i++ )
    {
        uint8_t *pucData = ( uint8_t * ) xBloques[ i ].pvData;
        uint16_t usSize  = xBloques[ i ].usSize;

        /* El checksum es el último byte y se calcula sobre todo lo anterior. */
        pucData[ usSize - 1U ] = cfg_checksum( pucData, ( uint16_t ) ( usSize - 1U ) );

        if( drv_eeprom_write( xBloques[ i ].ulAddr, ( const char * ) pucData, usSize )
            != ( int32_t ) usSize )
        {
            xprintf( "CFG:: ERROR: no se pudo grabar el bloque '%s'\r\n", xBloques[ i ].pcNombre );
            bOk = false;
        }
    }

    if( bOk )
    {
        xprintf( "CFG:: configuracion grabada\r\n" );
    }

    /* Se avisa DESPUÉS de grabar, no antes: la configuración es del operador y
       un equipo que se niega a guardar en medio de una instalación es peor que
       uno que advierte. Ver cfg_nvm_chequear_nombres(). */
    ( void ) cfg_nvm_chequear_nombres();

    return bOk;
}
//------------------------------------------------------------------------------
bool cfg_nvm_load_all( void )
{
    uint32_t i;
    bool     bOk = true;

    for( i = 0U; i < CFG_NRO_BLOQUES; i++ )
    {
        uint8_t *pucData = ( uint8_t * ) xBloques[ i ].pvData;
        uint16_t usSize  = xBloques[ i ].usSize;

        memset( pucData, 0, usSize );

        if( drv_eeprom_read( xBloques[ i ].ulAddr, ( char * ) pucData, usSize )
            != ( int32_t ) usSize )
        {
            xprintf( "CFG:: ERROR de lectura en el bloque '%s' -> defaults\r\n",
                     xBloques[ i ].pcNombre );
            xBloques[ i ].pfDefaults();
            bOk = false;
            continue;
        }

        uint8_t ucLeido    = pucData[ usSize - 1U ];
        uint8_t ucCalculado = cfg_checksum( pucData, ( uint16_t ) ( usSize - 1U ) );

        if( ucLeido != ucCalculado )
        {
            /*
             * Bloque corrupto, o una EEPROM virgen (todo 0xFF). Se cae a
             * defaults y se sigue: en un equipo desatendido es mejor medir con
             * la configuración de fábrica que no arrancar.
             */
            xprintf( "CFG:: bloque '%s' con checksum malo (leido 0x%02X, calculado 0x%02X) -> defaults\r\n",
                     xBloques[ i ].pcNombre, ( unsigned ) ucLeido, ( unsigned ) ucCalculado );
            xBloques[ i ].pfDefaults();
            bOk = false;
        }
    }

    return bOk;
}
//------------------------------------------------------------------------------
uint8_t cfg_nvm_chequear_nombres( void )
{
    /* Todos los canales habilitados, con su nombre y de dónde salió. 3 + 1 + 5. */
    const char *pcNombre[ CFG_AINPUTS_NRO_CANALES + 1U + CFG_MODBUS_NRO_CANALES ];
    char        pcOrigen[ CFG_AINPUTS_NRO_CANALES + 1U + CFG_MODBUS_NRO_CANALES ][ 8 ];
    uint8_t     ucN = 0U;
    uint8_t     i, j;
    uint8_t     ucRepetidos = 0U;

    for( i = 0U; i < CFG_AINPUTS_NRO_CANALES; i++ )
    {
        if( xCfgAinputs.xCanal[ i ].bEnabled )
        {
            pcNombre[ ucN ] = xCfgAinputs.xCanal[ i ].pcName;
            snprintf( pcOrigen[ ucN ], sizeof( pcOrigen[ 0 ] ), "a%u", ( unsigned ) i );
            ucN++;
        }
    }

    if( xCfgCounter.bEnabled )
    {
        pcNombre[ ucN ] = xCfgCounter.pcName;
        snprintf( pcOrigen[ ucN ], sizeof( pcOrigen[ 0 ] ), "c0" );
        ucN++;
    }

    if( xCfgModbus.bEnabled )
    {
        for( i = 0U; i < CFG_MODBUS_NRO_CANALES; i++ )
        {
            if( xCfgModbus.xCanal[ i ].bEnabled )
            {
                pcNombre[ ucN ] = xCfgModbus.xCanal[ i ].pcName;
                snprintf( pcOrigen[ ucN ], sizeof( pcOrigen[ 0 ] ), "m%u", ( unsigned ) i );
                ucN++;
            }
        }
    }

    for( i = 0U; i < ucN; i++ )
    {
        for( j = ( uint8_t ) ( i + 1U ); j < ucN; j++ )
        {
            if( strcmp( pcNombre[ i ], pcNombre[ j ] ) == 0 )
            {
                xprintf( "  [!] '%s' esta repetido: %s y %s -> en el frame saldrian DOS campos iguales\r\n",
                         pcNombre[ i ], pcOrigen[ i ], pcOrigen[ j ] );
                ucRepetidos++;
            }
        }
    }

    return ucRepetidos;
}
//------------------------------------------------------------------------------
void cfg_nvm_print_all( void )
{
    xprintf( "Configuracion:\r\n" );

    cfg_base_print();
    cfg_ainputs_print();
    cfg_counter_print();
    cfg_modbus_print();
    cfg_consigna_print();

    /*
     * Los hashes son lo que se compara contra el servidor, así que se imprimen
     * con el mismo formato con el que viajan en el frame CONF_ALL. Es lo que
     * permite validarlos contra un equipo AVR sin necesidad de modem.
     */
    xprintf( "Hashes: BH=0x%02X AH=0x%02X CH=0x%02X MH=0x%02X PH=0x%02X\r\n",
             ( unsigned ) cfg_base_hash(),
             ( unsigned ) cfg_ainputs_hash(),
             ( unsigned ) cfg_counter_hash(),
             ( unsigned ) cfg_modbus_hash(),
             ( unsigned ) cfg_consigna_hash() );

    ( void ) cfg_nvm_chequear_nombres();
}
//------------------------------------------------------------------------------
