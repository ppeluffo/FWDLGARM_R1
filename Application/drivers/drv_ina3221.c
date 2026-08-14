/*
 * drv_ina3221.c  -  ver drv_ina3221.h
 */

#include "drv_ina3221.h"
#include "drv_i2c.h"
#include "main.h"

#define TRANSACCION_TIMEOUT_MS  250U

/* Los registros de shunt y de bus del canal N son consecutivos y arrancan en 0x01,
   dos por canal: CH1 en 0x01/0x02, CH2 en 0x03/0x04, CH3 en 0x05/0x06. */
#define REG_SHV( ch )   ( ( uint8_t ) ( DRV_INA_REG_CH1_SHV  + ( 2U * ( ch ) ) ) )
#define REG_BUSV( ch )  ( ( uint8_t ) ( DRV_INA_REG_CH1_BUSV + ( 2U * ( ch ) ) ) )

static bool bPresente = false;
static bool bRielOn   = false;

/*------------------------------------------------------------------------------
 * Las 13 cuentas con signo que hay en `[15:3]`.
 *
 * El corrimiento se hace sobre un int16_t para que el compilador propague el bit
 * de signo: `>>` sobre un tipo con signo es aritmético en GCC. Un valor negativo
 * significa que la corriente circula al revés de lo previsto —lazo abierto, shunt
 * mal cableado— y hay que poder verlo como negativo, no como 8000 y pico.
 *----------------------------------------------------------------------------*/
static int16_t prvCuentas13( uint16_t usReg )
{
    return ( int16_t ) ( ( ( int16_t ) usReg ) >> 3 );
}
//------------------------------------------------------------------------------
bool drv_ina_reg_leer( uint8_t ucReg, uint16_t *pusVal )
{
    char pcDatos[ 2 ] = { 0 };

    if( drv_i2c_read( DRV_I2C_ADDR_INA, ( uint16_t ) ucReg, 1U,
                      pcDatos, 2U,
                      pdMS_TO_TICKS( TRANSACCION_TIMEOUT_MS ) ) != 2 )
    {
        return false;
    }

    /* Big endian: el MSB primero. */
    *pusVal = ( uint16_t ) ( ( ( uint16_t ) ( uint8_t ) pcDatos[ 0 ] ) << 8 ) |
              ( uint16_t ) ( ( uint8_t ) pcDatos[ 1 ] );

    return true;
}
//------------------------------------------------------------------------------
bool drv_ina_reg_escribir( uint8_t ucReg, uint16_t usVal )
{
    char pcDatos[ 2 ];

    pcDatos[ 0 ] = ( char ) ( ( usVal >> 8 ) & 0xFFU );
    pcDatos[ 1 ] = ( char ) ( usVal & 0xFFU );

    return ( drv_i2c_write( DRV_I2C_ADDR_INA, ( uint16_t ) ucReg, 1U,
                            pcDatos, 2U,
                            pdMS_TO_TICKS( TRANSACCION_TIMEOUT_MS ) ) == 2 );
}
//------------------------------------------------------------------------------
bool drv_ina_init( void )
{
    uint16_t usMfid  = 0U;
    uint16_t usDieid = 0U;

    bPresente = false;

    /* El riel primero y explícito: si el equipo rebotó con la fuente encendida,
       este es el momento de apagarla. */
    drv_ina_pwr_sensores( false );

    /*
     * Identificar el chip, no sólo comprobar que alguien contesta.
     *
     * En este bus hay siete direcciones ocupadas y ya hubo una sorpresa con la
     * EEPROM, que resultó ser una M24M01 y no la M24M02 que decía el código
     * heredado. Un ACK sólo prueba que hay algo; MFID y DIEID prueban qué.
     */
    if( ( drv_ina_reg_leer( DRV_INA_REG_MFID,  &usMfid  ) == false ) ||
        ( drv_ina_reg_leer( DRV_INA_REG_DIEID, &usDieid ) == false ) )
    {
        return false;
    }

    if( ( usMfid != DRV_INA_MFID_ESPERADO ) || ( usDieid != DRV_INA_DIEID_ESPERADO ) )
    {
        return false;
    }

    /* Estado de reposo: configurado pero en power-down. */
    if( drv_ina_sleep() == false )
    {
        return false;
    }

    bPresente = true;
    return true;
}
//------------------------------------------------------------------------------
bool drv_ina_presente( void )
{
    return bPresente;
}
//------------------------------------------------------------------------------
bool drv_ina_awake( void )
{
    return drv_ina_reg_escribir( DRV_INA_REG_CONF, DRV_INA_CONF_MEDIR );
}
//------------------------------------------------------------------------------
bool drv_ina_sleep( void )
{
    return drv_ina_reg_escribir( DRV_INA_REG_CONF, DRV_INA_CONF_SLEEP );
}
//------------------------------------------------------------------------------
bool drv_ina_esperar_conversion( void )
{
    uint16_t usMask = 0U;

    /*
     * Se espera el grueso del barrido de una sola vez y recién después se
     * pregunta. Preguntar antes no adelantaría nada —el chip no termina más
     * rápido porque lo miren— y cada consulta son ~500 µs de I2C y una despertada
     * del micro.
     */
    vTaskDelay( pdMS_TO_TICKS( DRV_INA_BARRIDO_MS ) );

    /*
     * Y después se confirma con el bit CVRF en vez de confiar en la cuenta.
     * El oscilador interno del INA tiene su tolerancia, así que el tiempo teórico
     * es una estimación, no una garantía.
     *
     * ⚠ Leer Mask/Enable LIMPIA CVRF, así que el bit se consume acá. Nadie más
     * debe mirarlo esperando encontrarlo puesto.
     */
    /* La cuenta va por RESTA y no comparando contra un instante futuro: la resta
       sin signo sigue dando el intervalo correcto cuando el contador de ticks da
       la vuelta, y comparar `<` contra una suma no. */
    TickType_t xInicio = xTaskGetTickCount();

    while( ( xTaskGetTickCount() - xInicio ) < pdMS_TO_TICKS( DRV_INA_BARRIDO_TOUT_MS ) )
    {
        if( drv_ina_reg_leer( DRV_INA_REG_MASK_ENABLE, &usMask ) == false )
        {
            return false;
        }

        if( ( usMask & DRV_INA_CVRF ) != 0U )
        {
            return true;
        }

        vTaskDelay( pdMS_TO_TICKS( 50 ) );
    }

    return false;
}
//------------------------------------------------------------------------------
bool drv_ina_shunt_raw( ina_canal_t eCanal, int16_t *psRaw )
{
    uint16_t usReg = 0U;

    if( eCanal >= inaCH_COUNT )
    {
        return false;
    }

    if( drv_ina_reg_leer( REG_SHV( eCanal ), &usReg ) == false )
    {
        return false;
    }

    *psRaw = prvCuentas13( usReg );
    return true;
}
//------------------------------------------------------------------------------
bool drv_ina_shunt_uv( ina_canal_t eCanal, int32_t *plMicroV )
{
    int16_t sRaw = 0;

    if( drv_ina_shunt_raw( eCanal, &sRaw ) == false )
    {
        return false;
    }

    *plMicroV = ( int32_t ) sRaw * ( int32_t ) DRV_INA_SHUNT_LSB_UV;
    return true;
}
//------------------------------------------------------------------------------
bool drv_ina_leer_ma( ina_canal_t eCanal, float *pfMa )
{
    int32_t lMicroV = 0;

    if( drv_ina_shunt_uv( eCanal, &lMicroV ) == false )
    {
        return false;
    }

    /* I[mA] = V[µV] / 1000 / R[Ω]. Con el shunt heredado de 7,32 Ω el cociente
       queda en raw/183, que es el INA_FACTOR de FWDLGX. */
    *pfMa = ( ( float ) lMicroV / 1000.0f ) / DRV_INA_RSHUNT_OHM;
    return true;
}
//------------------------------------------------------------------------------
bool drv_ina_bus_mv( ina_canal_t eCanal, int32_t *plMiliV )
{
    uint16_t usReg = 0U;

    if( eCanal >= inaCH_COUNT )
    {
        return false;
    }

    if( drv_ina_reg_leer( REG_BUSV( eCanal ), &usReg ) == false )
    {
        return false;
    }

    *plMiliV = ( int32_t ) prvCuentas13( usReg ) * ( int32_t ) DRV_INA_BUS_LSB_MV;
    return true;
}
//------------------------------------------------------------------------------
void drv_ina_pwr_sensores( bool bOn )
{
    HAL_GPIO_WritePin( EN_PWR_SENS420_GPIO_Port, EN_PWR_SENS420_Pin,
                       bOn ? GPIO_PIN_SET : GPIO_PIN_RESET );

    bRielOn = bOn;
}
//------------------------------------------------------------------------------
bool drv_ina_pwr_sensores_estado( void )
{
    return bRielOn;
}
//------------------------------------------------------------------------------
bool drv_ina_medir( float *pfMa, bool bDejarEncendido )
{
    bool bOk = true;

    /* Se limpia acá y no en el llamador: por los dos `goto salir` de abajo hay
       caminos que vuelven sin haber leído un solo canal, y devolver el arreglo
       como quedó en el stack sería entregar basura con cara de medida. */
    for( uint32_t i = 0U; i < ( uint32_t ) inaCH_COUNT; i++ )
    {
        pfMa[ i ] = 0.0f;
    }

    /* Si el riel ya venía encendido no se paga el asentamiento de nuevo: es lo
       que hace útil el `bDejarEncendido` de la llamada anterior. */
    if( bRielOn == false )
    {
        drv_ina_pwr_sensores( true );
        vTaskDelay( pdMS_TO_TICKS( DRV_INA_SETTLE_MS ) );
    }

    if( drv_ina_awake() == false )
    {
        bOk = false;
        goto salir;
    }

    if( drv_ina_esperar_conversion() == false )
    {
        bOk = false;
        goto salir;
    }

    for( uint32_t i = 0U; i < ( uint32_t ) inaCH_COUNT; i++ )
    {
        if( drv_ina_leer_ma( ( ina_canal_t ) i, &pfMa[ i ] ) == false )
        {
            pfMa[ i ] = 0.0f;
            bOk = false;
        }
    }

salir:

    /*
     * El INA se duerme SIEMPRE, ande o no ande la medida. Si se saltara este paso
     * en el camino de error, un fallo aislado del I2C dejaría al chip consumiendo
     * 350 µA para siempre, sin que nada lo delate salvo la autonomía.
     */
    ( void ) drv_ina_sleep();

    if( bDejarEncendido == false )
    {
        drv_ina_pwr_sensores( false );
    }

    return bOk;
}
//------------------------------------------------------------------------------
