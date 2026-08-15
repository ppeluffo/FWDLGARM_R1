/*
 * drv_adc.c  -  ver drv_adc.h
 */

#include "drv_adc.h"
#include "main.h"

extern ADC_HandleTypeDef hadc1;

#define CONVERSION_TIMEOUT_MS   100U

/* Arranque del regulador interno del ADC: el datasheet pide 20 µs. */
#define ADCVREG_STUP_US         25U

static bool     b12vOn      = false;
static bool     b3v3On      = false;
static uint32_t ulCalFactor = 0UL;

/*==============================================================================
 * Encendido y apagado del ADC
 *
 * El ADC no queda habilitado entre medidas. Su regulador interno consume del
 * orden de microamperes, que contra los ~5 µA del micro dormido no es
 * despreciable: es exactamente el tipo de fuga que ya costó 89 µA con el pull-up
 * de SD_DET, y la lección de aquello fue mirar el consumo de reposo de cada
 * bloque nuevo en vez de suponerlo.
 *
 * El estado de reposo es **deep power-down**, el más bajo del periférico. De ahí
 * no se sale gratis: al despertar hay que reponer el regulador, esperar su
 * arranque y **restaurar el factor de calibración**, que ese modo no conserva.
 * Sin lo último la medida sigue saliendo, pero con el error que la calibración
 * venía a corregir — un bug callado.
 *============================================================================*/

static void prvEsperarUs( uint32_t ulUs )
{
    /* Calculado sobre el reloj real y no a ojo, para que el retardo no cambie
       entre Debug y Release, donde el mismo lazo dura tres veces menos. */
    uint32_t ulVueltas = ( SystemCoreClock / 1000000UL ) * ulUs / 4UL;

    for( volatile uint32_t i = 0U; i < ulVueltas; i++ )
    {
    }
}
//------------------------------------------------------------------------------
static void prvAdcDormir( void )
{
    ( void ) HAL_ADC_Stop( &hadc1 );

    /* ADEN tiene que estar en 0 antes de tocar el regulador. */
    CLEAR_BIT( hadc1.Instance->CR, ADC_CR_ADVREGEN );
    SET_BIT  ( hadc1.Instance->CR, ADC_CR_DEEPPWD );
}
//------------------------------------------------------------------------------
static void prvAdcDespertar( void )
{
    CLEAR_BIT( hadc1.Instance->CR, ADC_CR_DEEPPWD );
    SET_BIT  ( hadc1.Instance->CR, ADC_CR_ADVREGEN );

    prvEsperarUs( ADCVREG_STUP_US );

    /* El deep power-down se lleva el factor de calibración. Reponerlo es más
       barato que recalibrar, y recalibrar en cada medida sería absurdo. */
    ( void ) HAL_ADCEx_Calibration_SetValue( &hadc1, ADC_SINGLE_ENDED, ulCalFactor );
}
//------------------------------------------------------------------------------
/*
 * Una conversión de un canal, por poleo.
 *
 * Por poleo y no por interrupción a propósito: con 640,5 ciclos a 15 MHz la
 * conversión son ~43 µs, y montar una interrupción y un semáforo para eso cuesta
 * más de lo que ahorra. La comparación válida es contra el I2C, donde una
 * transacción son ~900 µs y ahí sí paga.
 */
static bool prvConvertir( uint32_t ulCanal, uint16_t *pusRaw )
{
    ADC_ChannelConfTypeDef xCanal = { 0 };
    bool                   bOk    = false;

    xCanal.Channel      = ulCanal;
    xCanal.Rank         = ADC_REGULAR_RANK_1;
    xCanal.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
    xCanal.SingleDiff   = ADC_SINGLE_ENDED;
    xCanal.OffsetNumber = ADC_OFFSET_NONE;
    xCanal.Offset       = 0U;

    if( HAL_ADC_ConfigChannel( &hadc1, &xCanal ) != HAL_OK )
    {
        return false;
    }

    if( HAL_ADC_Start( &hadc1 ) != HAL_OK )
    {
        return false;
    }

    if( HAL_ADC_PollForConversion( &hadc1, CONVERSION_TIMEOUT_MS ) == HAL_OK )
    {
        *pusRaw = ( uint16_t ) HAL_ADC_GetValue( &hadc1 );
        bOk     = true;
    }

    ( void ) HAL_ADC_Stop( &hadc1 );

    return bOk;
}
//------------------------------------------------------------------------------
/* Envuelve una conversión con el despertar y el dormir del periférico. */
static bool prvMedirCanal( uint32_t ulCanal, uint16_t *pusRaw )
{
    prvAdcDespertar();

    bool bOk = prvConvertir( ulCanal, pusRaw );

    prvAdcDormir();

    return bOk;
}

/*==============================================================================
 * API pública
 *============================================================================*/

bool drv_adc_init( void )
{
    /* Explícitos aunque los TPS22810 traigan pull-down: el firmware no debe
       depender de una resistencia para un estado que le corresponde. Y el de
       3,3 V queda apagado para siempre, porque su circuito no se usa. */
    drv_adc_pwr_12v( false );
    drv_adc_pwr_3v3( false );

    /* Salir de deep power-down y levantar el regulador antes de calibrar. */
    CLEAR_BIT( hadc1.Instance->CR, ADC_CR_DEEPPWD );
    SET_BIT  ( hadc1.Instance->CR, ADC_CR_ADVREGEN );
    prvEsperarUs( ADCVREG_STUP_US );

    /*
     * La calibración es OBLIGATORIA en el STM32L4: sin ella el ADC arrastra un
     * error de offset de varias cuentas, que en un divisor 56K/10K se
     * multiplican por 6,6 al volver a la tensión del riel.
     */
    if( HAL_ADCEx_Calibration_Start( &hadc1, ADC_SINGLE_ENDED ) != HAL_OK )
    {
        return false;
    }

    ulCalFactor = HAL_ADCEx_Calibration_GetValue( &hadc1, ADC_SINGLE_ENDED );

    prvAdcDormir();

    return true;
}
//------------------------------------------------------------------------------
void drv_adc_pwr_12v( bool bOn )
{
    /* TPS22810: EN activo ALTO. Ojo, al revés que el SI2301 de la microSD. */
    HAL_GPIO_WritePin( EN_SENS12V_GPIO_Port, EN_SENS12V_Pin,
                       bOn ? GPIO_PIN_SET : GPIO_PIN_RESET );
    b12vOn = bOn;
}
//------------------------------------------------------------------------------
void drv_adc_pwr_3v3( bool bOn )
{
    HAL_GPIO_WritePin( EN_SENS3V3_GPIO_Port, EN_SENS3V3_Pin,
                       bOn ? GPIO_PIN_SET : GPIO_PIN_RESET );
    b3v3On = bOn;
}
//------------------------------------------------------------------------------
bool drv_adc_pwr_12v_estado( void ) { return b12vOn; }
bool drv_adc_pwr_3v3_estado( void ) { return b3v3On; }
//------------------------------------------------------------------------------
bool drv_adc_raw_vrefint( uint16_t *pusRaw )
{
    return prvMedirCanal( ADC_CHANNEL_VREFINT, pusRaw );
}
//------------------------------------------------------------------------------
bool drv_adc_raw_12v( uint16_t *pusRaw )
{
    return prvMedirCanal( ADC_CHANNEL_15, pusRaw );
}
//------------------------------------------------------------------------------
bool drv_adc_vdda_mv( uint32_t *pulMiliV )
{
    uint16_t usRaw = 0U;

    if( drv_adc_raw_vrefint( &usRaw ) == false )
    {
        return false;
    }

    /* El macro de la HAL hace 3000 mV x VREFINT_CAL / dato, leyendo el valor de
       calibración de fábrica de la memoria de sistema. */
    *pulMiliV = __HAL_ADC_CALC_VREFANALOG_VOLTAGE( usRaw, ADC_RESOLUTION_12B );

    return true;
}
//------------------------------------------------------------------------------
bool drv_adc_v12_mv( uint32_t *pulMiliV, bool bDejarEncendido )
{
    uint32_t ulVdda = 0UL;
    uint16_t usRaw  = 0U;
    bool     bOk    = false;

    *pulMiliV = 0UL;

    /* Si ya venía encendido no se paga el asentamiento otra vez. */
    if( b12vOn == false )
    {
        drv_adc_pwr_12v( true );
        vTaskDelay( pdMS_TO_TICKS( DRV_ADC_SETTLE_MS ) );
    }

    /*
     * VREFINT PRIMERO, y en la misma medida. No alcanza con haberlo leído alguna
     * vez: VDDA se mueve con la carga y con la batería, y es justamente el
     * denominador de la cuenta que sigue.
     */
    if( drv_adc_vdda_mv( &ulVdda ) && drv_adc_raw_12v( &usRaw ) )
    {
        uint32_t ulPinMv = __HAL_ADC_CALC_DATA_TO_VOLTAGE( ulVdda, usRaw,
                                                           ADC_RESOLUTION_12B );

        /* Deshacer el divisor: V_riel = V_pin x 66 / 10 */
        *pulMiliV = ( ulPinMv * DRV_ADC_DIV12_NUM ) / DRV_ADC_DIV12_DEN;
        bOk       = true;
    }

    if( bDejarEncendido == false )
    {
        drv_adc_pwr_12v( false );
    }

    return bOk;
}
//------------------------------------------------------------------------------
