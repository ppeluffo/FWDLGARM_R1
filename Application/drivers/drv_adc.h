/*
 * drv_adc.h
 *
 * Medida de los rieles de alimentación con el ADC del micro.
 *
 * Es la primera etapa que usa el ADC1: hasta ahora las únicas medidas analógicas
 * eran las de 4-20 mA, y esas las hace el INA3221 por I2C.
 *
 * ---------------------------------------------------------------------------
 * DOS CANALES, Y UNO NO TIENE HARDWARE PROPIO
 *
 *   | Riel  | Cómo se mide                                              |
 *   |-------|-----------------------------------------------------------|
 *   | 12 V  | divisor 56K/10K -> seguidor TLV8802 -> PB0 (ADC1_IN15)     |
 *   | 3,3 V | **VREFINT**, sin un solo componente externo                |
 *
 * ⚠ **El riel de 3,3 V NO se puede medir con un ADC referenciado a él mismo.**
 * R001 trae el circuito —divisor 56K/56K, load switch y seguidor— pero la cuenta
 * se cancela sola:
 *
 *     ADC = (V3V3 / 2) / VREF+ x 4095 = (V3V3 / 2) / V3V3 x 4095 = 2047, SIEMPRE
 *
 * Da 2047 con el riel en 3,3 V, en 3,0 o en 3,6. No es un problema de precisión:
 * la medida no contiene información sobre lo que se quiere medir.
 *
 * La solución es `VREFINT`, la referencia interna de ~1,212 V, que es un valor
 * ABSOLUTO y viene **calibrada de fábrica por chip** (`VREFINT_CAL`, medida a
 * VDDA = 3,0 V):
 *
 *     VDDA_real = 3000 mV x VREFINT_CAL / VREFINT_leído
 *
 * Y encima sale mejor que el circuito: contra el divisor, que acumula la
 * tolerancia de dos resistencias más el offset del operacional, `VREFINT` sólo
 * tiene deriva térmica. Por eso **el circuito de 3,3 V queda sin usar**, con su
 * load switch apagado a propósito (`drv_adc_pwr_3v3()` existe sólo para poder
 * ejercitarlo desde la consola si algún día hiciera falta).
 *
 * Esto vale porque en R001 **VDDA del micro ES el riel de 3,3 V** (confirmado por
 * Pablo el 2026-08-14). Si alguna vez fueran nodos distintos, `VREFINT` mediría
 * el del micro y esta decisión habría que rehacerla.
 *
 * ---------------------------------------------------------------------------
 * SIN VREFINT, LA MEDIDA DE 12 V TAMBIÉN ESTÁ MAL
 *
 * Y de forma menos evidente, porque igual devuelve un número plausible. El ADC
 * mide contra VREF+, así que convertir con un 3,3 V nominal supuesto traslada
 * **directo y proporcional** cualquier desvío del riel a la medida de los 12 V.
 * Por eso `drv_adc_v12_mv()` lee siempre los dos canales: primero VREFINT para
 * saber contra qué está midiendo, después el divisor.
 *
 * ---------------------------------------------------------------------------
 * EL MUESTREO ES LARGO A PROPÓSITO
 *
 * El TLV8802 es un operacional *nanopower*: ~320 nA de consumo y apenas ~6 kHz de
 * ancho de banda. Eso le deja una impedancia de salida alta a la frecuencia con
 * la que el ADC carga su capacitor de muestreo, y con un tiempo corto ese golpe
 * de carga produce un error que el opamp no alcanza a corregir. De ahí los
 * **640,5 ciclos** del `.ioc`: a 15 MHz son ~43 µs, de sobra.
 *
 * Por lo mismo el riel necesita `DRV_ADC_SETTLE_MS` antes de creerle a una
 * medida: el soft-start del TPS22810 más lo que tarda un opamp de 6 kHz en
 * llegar a 1,8 V.
 */

#ifndef APPLICATION_DRIVERS_DRV_ADC_H_
#define APPLICATION_DRIVERS_DRV_ADC_H_

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

/*------------------------------------------------------------------------------
 * Divisor del canal de 12 V: 56K arriba, 10K a GND.
 *
 *      V_pin = V_riel x 10 / 66   ->   12 V dan 1,818 V, cómodo en el rango
 *
 * La cuenta inversa se hace con enteros —multiplicar por 66 y dividir por 10—
 * para no arrastrar float a un camino que no lo necesita.
 *----------------------------------------------------------------------------*/
#define DRV_ADC_DIV12_NUM       66U
#define DRV_ADC_DIV12_DEN       10U

/* Asentamiento del riel antes de convertir: soft-start del load switch más el
   tiempo del operacional nanopower. Generoso: la medida no es frecuente. */
#define DRV_ADC_SETTLE_MS       10U

/*------------------------------------------------------------------------------
 * Arranque. Calibra el ADC —obligatorio en el STM32L4 para que la medida valga—,
 * guarda el factor de calibración y deja los dos load switches apagados y el ADC
 * en su estado de menor consumo.
 *----------------------------------------------------------------------------*/
bool drv_adc_init( void );

/*------------------------------------------------------------------------------
 * VDDA real, en milivolts, medido con VREFINT. Es también la tensión del riel de
 * 3,3 V, porque en R001 son el mismo nodo.
 *
 * No necesita prender nada ni esperar: VREFINT es interno.
 *----------------------------------------------------------------------------*/
bool drv_adc_vdda_mv( uint32_t *pulMiliV );

/*------------------------------------------------------------------------------
 * El riel de 12 V, en milivolts. Ciclo completo: prende el load switch, espera el
 * asentamiento, lee VREFINT y el canal, y apaga.
 *
 * Si `bDejarEncendido` es true no apaga al terminar — para medir varias veces
 * seguidas en banco sin pagar el asentamiento cada vez.
 *----------------------------------------------------------------------------*/
bool drv_adc_v12_mv( uint32_t *pulMiliV, bool bDejarEncendido );

/*------------------------------------------------------------------------------
 * Los load switches, sueltos. `drv_adc_v12_mv()` ya los maneja; estas quedan
 * expuestas para el banco.
 *
 * El de 3,3 V **está pensado para quedar siempre apagado**: su circuito no se
 * usa, por lo explicado arriba. Existe para poder prenderlo y ver que el load
 * switch responde, no para medir.
 *----------------------------------------------------------------------------*/
void drv_adc_pwr_12v ( bool bOn );
void drv_adc_pwr_3v3 ( bool bOn );
bool drv_adc_pwr_12v_estado( void );
bool drv_adc_pwr_3v3_estado( void );

/* Cuentas crudas de un canal, sin convertir. Para diagnóstico: es lo que permite
   distinguir "el ADC no lee" de "la cuenta de conversión está mal". */
bool drv_adc_raw_12v    ( uint16_t *pusRaw );
bool drv_adc_raw_vrefint( uint16_t *pusRaw );

#endif /* APPLICATION_DRIVERS_DRV_ADC_H_ */
