/*
 * drv_adc.c  -  ver drv_adc.h
 */

#include "drv_adc.h"
#include "main.h"

/*
 * El User Label del .ioc es parte de la interfaz: si falta, el error tiene que
 * decir QUÉ hacer. ⚠ `EN_SENS3V3` NO va: ese circuito se eliminó de la placa.
 */
#if !defined( EN_SENS12V_Pin )
#error "Falta el User Label en el .ioc: PC4 = EN_SENS12V (y PB0 = ADC1_IN15, sin label)"
#endif

extern ADC_HandleTypeDef hadc1;

#define CONVERSION_TIMEOUT_MS   100U

/* Arranque del regulador interno del ADC: el datasheet pide 20 µs. */
#define ADCVREG_STUP_US         25U

static bool     b12vOn      = false;
/* Diagnóstico: ADC_CR leído en el instante siguiente a la escritura de la
   dormida. Lo imprime el comando `vin`. */
volatile uint32_t ulCrTrasDormir = 0UL;

/*
 * Los bits de ADC_CR con propiedad de hardware "rs" (read-set): se ponen por
 * software y los limpia el hardware. ⛔ NUNCA hay que reescribirlos desde una
 * lectura ni dejarlos en 0 por accidente: hay que FORZARLOS a 0 en la máscara,
 * que es exactamente lo que hace `ADC_CR_BITS_PROPERTY_RS` en la LL de ST.
 */
#define ADC_CR_RS   ( ADC_CR_ADCAL | ADC_CR_JADSTP | ADC_CR_ADSTP \
                    | ADC_CR_JADSTART | ADC_CR_ADSTART | ADC_CR_ADDIS | ADC_CR_ADEN )

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
    ( void ) HAL_ADC_Stop( &hadc1 );   /* deshabilita el ADC: deja ADEN en 0 */

    /*
     * ⛔ NO con SET_BIT / CLEAR_BIT, y costó los 337 µA del 2026-10-02.
     *
     * Esas macros son read-modify-write, y `ADC_CR` tiene SIETE bits con
     * propiedad de hardware "rs" —ADEN, ADDIS, ADSTART, ADSTP, ADCAL, JADSTART,
     * JADSTP—: reescribirlos desde una lectura interfiere con ellos. El
     * comentario de la propia LL de ST lo dice: *"write register with some
     * additional bits forced to state reset instead of modifying only the
     * selected bit, to not interfere with bits with HW property rs"*.
     *
     * ⚠ El síntoma era MUDO: la medida salía perfecta y el ADC quedaba
     * despierto con su regulador encendido. Lo delató el volcado de ADC_CR que
     * imprime el comando `vin` —DEEPPWD=0 ADVREGEN=1 después de medir—, no la
     * lectura, que era correcta.
     *
     * El orden sí importa: el regulador primero, el deep power-down después.
     */
    /*
     * Escritura DIRECTA del registro completo, no read-modify-write.
     * Los siete bits de propiedad "rs" (ADEN, ADDIS, ADSTART, ADSTP, ADCAL,
     * JADSTART, JADSTP) son SET-ONLY: escribirles 0 no tiene efecto, así que
     * poner el registro entero en DEEPPWD es seguro y deja ADVREGEN en 0 por
     * construcción. Es lo mismo que hace la LL, pero sin leer antes.
     */
/*
     * ⛔ ACÁ SÍ VA LA ESCRITURA ENTERA, y NO `MODIFY_REG`. Medido el 2026-10-02:
     * con MODIFY_REG el reposo se fue a **230 µA**; con esto son **3 µA**.
     *
     * El mecanismo, que es sutil: la escritura de `ADVREGEN = 0` **no entra**
     * (el registro leído lo demuestra), así que el `MODIFY_REG` siguiente lee
     * `ADVREGEN` todavía en 1 y escribe **DEEPPWD junto con ADVREGEN**
     * (`0xA0000000`), que es un estado contradictorio. Escribiendo el registro
     * entero va `DEEPPWD` SOLO, y aunque el valor leído después sea el mismo,
     * **el efecto físico sobre el regulador sí es apagarlo**.
     *
     * ⚠ O sea que acá el registro MIENTE sobre el estado real del periférico:
     * lo único que dice la verdad es el amperímetro. Por eso el criterio para
     * tocar estas dos líneas es el consumo medido, nunca lo que se lee.
     */
    hadc1.Instance->CR = ADC_CR_DEEPPWD;
    __DSB();

    /* ⭐ Y se captura EN EL ACTO. Si esto dice DEEPPWD=1 y el registro leído
       después por el comando `vin` dice 0, entonces la escritura entra y algo
       vuelve a despertar el ADC — que es una falla distinta y en otro lugar. */
    ulCrTrasDormir = hadc1.Instance->CR;
}
//------------------------------------------------------------------------------
static void prvAdcDespertar( void )
{
    /*
     * ⛔ ESTO NO ES `CR = 0` SEGUIDO DE `CR = ADVREGEN`, y la diferencia costó
     * una bajada (2026-10-02): escribir el registro entero **APAGA el regulador
     * si ya estaba encendido**, y entonces cada medida lo reenciende y lee antes
     * de que la referencia se asiente. El síntoma era VDDA informando 3,05-3,17 V
     * contra 3,32 del tester, **variable** — un sesgo sería de calibración, la
     * dispersión delata un transitorio.
     *
     * Se replica lo que hace la LL de ST: `MODIFY_REG` tocando UN bit y forzando
     * los "rs" a 0. (No se usa la LL misma porque `stm32l4xx_ll_adc.h` no está
     * en el proyecto: CubeMX sólo copia la HAL.)
     *
     * El orden lo pide el RM0351: primero salir del deep power-down, después el
     * regulador.
     */
    MODIFY_REG( hadc1.Instance->CR, ADC_CR_RS | ADC_CR_DEEPPWD,  0UL );
    MODIFY_REG( hadc1.Instance->CR, ADC_CR_RS | ADC_CR_ADVREGEN, ADC_CR_ADVREGEN );

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
    /* Explícito aunque el TPS22810 traiga pull-down: el firmware no debe depender
       de una resistencia para un estado que le corresponde. */
    drv_adc_pwr_12v( false );

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
bool drv_adc_pwr_12v_estado( void ) { return b12vOn; }
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
uint16_t drv_adc_vrefint_cal( void )
{
    /* El valor de calibración de fábrica de ESTE chip, grabado en la memoria de
       sistema y medido a VDDA = 3,0 V. Es el numerador de la cuenta de VDDA, y
       sin él esa cuenta no se puede verificar a mano. */
    return *( ( uint16_t * ) VREFINT_CAL_ADDR );
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
