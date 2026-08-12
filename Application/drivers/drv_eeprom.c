/*
 * drv_eeprom.c  -  ver drv_eeprom.h
 */

#include "drv_eeprom.h"
#include "drv_i2c.h"

/*
 * Espera del ciclo interno de escritura. El tW del datasheet es 5 ms máximo; se
 * sondea cada ~2 ms y se dan 10 intentos (~20 ms) antes de darlo por muerto, que
 * es holgura de sobra sin colgar la tarea para siempre si el chip no vuelve.
 *
 * Se espera BLOQUEANDO en el kernel (vTaskDelay), no girando: son 5 ms por
 * página, y a 60 MHz eso es un cuarto de millón de instrucciones tiradas.
 */
#define ACK_POLL_INTENTOS       10U
#define ACK_POLL_MS             2U

/* Plazo de cada transacción del bus. Una página de 256 bytes a 100 kHz son ~23 ms;
   250 ms es margen sobrado y a la vez un tope que no cuelga la tarea. */
#define TRANSACCION_TIMEOUT_MS  250U

/*
 * Parte la dirección plana en (byte de dispositivo, dirección dentro del bloque).
 *
 * A16 va DENTRO del byte de dispositivo: 0xA0 para los primeros 64 KB, 0xA2 para
 * los segundos. Es lo que hace que la M24M01 se vea como dos chips en el escaneo.
 */
static inline uint8_t prvDev( uint32_t ulAddr )
{
    return ( uint8_t ) ( DRV_I2C_ADDR_EEPROM | ( ( ulAddr >> 15 ) & 0x02U ) );
}

static inline uint16_t prvOffset( uint32_t ulAddr )
{
    return ( uint16_t ) ( ulAddr & 0xFFFFU );
}

/* Cuántos bytes quedan hasta el próximo borde, sin pasarse de lo pedido. */
static uint32_t prvHastaElBorde( uint32_t ulAddr, uint32_t ulPendientes, uint32_t ulTamBloque )
{
    uint32_t ulResto = ulTamBloque - ( ulAddr % ulTamBloque );

    return ( ulPendientes < ulResto ) ? ulPendientes : ulResto;
}

/* Acknowledge polling: reintenta hasta que el chip vuelva a contestar. */
static bool prvEsperarFinDeEscritura( uint8_t ucDev )
{
    for( uint32_t i = 0U; i < ACK_POLL_INTENTOS; i++ )
    {
        if( drv_i2c_probe( ucDev ) )
        {
            return true;
        }

        vTaskDelay( pdMS_TO_TICKS( ACK_POLL_MS ) );
    }

    return false;
}

/* Rango pedido contra el tamaño real. Se valida en vez de dejar que dé la vuelta:
   un desborde silencioso en un datalogger es pisar datos ya guardados. */
static bool prvRangoValido( uint32_t ulAddr, uint32_t ulBytes, const void *pvBuffer )
{
    if( ( pvBuffer == NULL ) || ( ulBytes == 0UL ) )
    {
        return false;
    }

    return ( ( ulAddr < DRV_EEPROM_SIZE ) && ( ( ulAddr + ulBytes ) <= DRV_EEPROM_SIZE ) );
}

/*------------------------------------------------------------------------------
 * Los lazos, ya con el bus tomado por el que llama.
 *----------------------------------------------------------------------------*/

static int32_t prvLeer( uint32_t ulAddr, char *pvBuffer, uint32_t ulBytes )
{
    uint32_t ulLeidos = 0UL;

    while( ulLeidos < ulBytes )
    {
        /* La lectura secuencial no cruza el borde de los 64 KB: el contador
           interno da la vuelta al principio del bloque. Hay que partir ahí. */
        uint32_t ulTramo = prvHastaElBorde( ulAddr + ulLeidos,
                                            ulBytes - ulLeidos,
                                            DRV_EEPROM_BLOCK_SIZE );

        if( drv_i2c_read( prvDev( ulAddr + ulLeidos ),
                          prvOffset( ulAddr + ulLeidos ),
                          2U,
                          &pvBuffer[ ulLeidos ],
                          ( uint16_t ) ulTramo,
                          pdMS_TO_TICKS( TRANSACCION_TIMEOUT_MS ) ) < 0 )
        {
            return -1;
        }

        ulLeidos += ulTramo;
    }

    return ( int32_t ) ulLeidos;
}

static int32_t prvEscribir( uint32_t ulAddr, const char *pvBuffer, uint32_t ulBytes )
{
    uint32_t ulEscritos = 0UL;

    while( ulEscritos < ulBytes )
    {
        uint32_t ulActual = ulAddr + ulEscritos;

        /*
         * Dos bordes que respetar a la vez, y se toma el más cercano:
         *
         *  - PÁGINA (256 B): pasarse no sigue en la página siguiente, DA LA VUELTA
         *    y pisa el principio de la misma. Es la trampa clásica, y es muda.
         *  - BLOQUE (64 KB): del otro lado hay otra dirección de dispositivo, así
         *    que ni siquiera es la misma transacción.
         */
        uint32_t ulTramo  = prvHastaElBorde( ulActual, ulBytes - ulEscritos, DRV_EEPROM_PAGE_SIZE );
        uint32_t ulBloque = prvHastaElBorde( ulActual, ulBytes - ulEscritos, DRV_EEPROM_BLOCK_SIZE );

        if( ulBloque < ulTramo )
        {
            ulTramo = ulBloque;
        }

        uint8_t ucDev = prvDev( ulActual );

        if( drv_i2c_write( ucDev,
                           prvOffset( ulActual ),
                           2U,
                           &pvBuffer[ ulEscritos ],
                           ( uint16_t ) ulTramo,
                           pdMS_TO_TICKS( TRANSACCION_TIMEOUT_MS ) ) < 0 )
        {
            return -1;
        }

        /* Sin esto, la página siguiente falla siempre: el chip todavía está en su
           ciclo interno y NACKea todo, incluso su propia dirección. */
        if( prvEsperarFinDeEscritura( ucDev ) == false )
        {
            return -1;
        }

        ulEscritos += ulTramo;
    }

    return ( int32_t ) ulEscritos;
}

/*------------------------------------------------------------------------------
 * API
 *----------------------------------------------------------------------------*/

int32_t drv_eeprom_read( uint32_t ulAddr, char *pvBuffer, uint32_t ulBytes )
{
    if( prvRangoValido( ulAddr, ulBytes, pvBuffer ) == false )
    {
        return -1;
    }

    /* El bus se toma para TODA la operación: si otra tarea se metiera entre dos
       tramos, la segunda mitad podría leerse después de una escritura ajena. */
    if( drv_i2c_bus_take( pdMS_TO_TICKS( TRANSACCION_TIMEOUT_MS ) ) == false )
    {
        return -1;
    }

    int32_t lRet = prvLeer( ulAddr, pvBuffer, ulBytes );

    drv_i2c_bus_give();

    return lRet;
}
//------------------------------------------------------------------------------
int32_t drv_eeprom_write( uint32_t ulAddr, const char *pvBuffer, uint32_t ulBytes )
{
    if( prvRangoValido( ulAddr, ulBytes, pvBuffer ) == false )
    {
        return -1;
    }

    /*
     * Acá el candado del bus NO es opcional. Una escritura multipágina es
     * escribir-esperar-escribir-esperar, y el "esperar" es preguntarle al chip si
     * ya contesta. Si otra tarea usara el bus en el medio, recibiría el NACK de
     * la memoria ocupada y lo leería como una falla propia.
     */
    if( drv_i2c_bus_take( pdMS_TO_TICKS( TRANSACCION_TIMEOUT_MS ) ) == false )
    {
        return -1;
    }

    int32_t lRet = prvEscribir( ulAddr, pvBuffer, ulBytes );

    drv_i2c_bus_give();

    return lRet;
}
//------------------------------------------------------------------------------
bool drv_eeprom_lista( void )
{
    /*
     * Alcanza con preguntarle al bloque bajo: el ciclo de escritura es de la
     * pastilla entera, no de un bloque, así que mientras dure NACKea sus dos
     * direcciones por igual.
     */
    return drv_i2c_probe( DRV_I2C_ADDR_EEPROM );
}
//------------------------------------------------------------------------------
