/*
 * drv_wdt.c  -  ver drv_wdt.h
 *
 * Referencia: RM0351 (STM32L4x5/x6), capítulo "Independent watchdog (IWDG)".
 */

#include "drv_wdt.h"
#include "main.h"

/*
 * Las tres llaves del registro KR. Normalmente vienen de
 * `stm32l4xx_hal_iwdg.h`, que en este proyecto no está compilado (ver el
 * header): se definen acá con los mismos valores del manual.
 */
#define WDT_KEY_ARRANCAR        0x0000CCCCU   /* arranca el IWDG y el LSI      */
#define WDT_KEY_DESTRABAR       0x00005555U   /* habilita escribir PR y RLR    */
#define WDT_KEY_RECARGAR        0x0000AAAAU   /* recarga el contador           */

/*
 * Prescaler 6 = /256, el máximo, o sea la ventana más larga que sabe hacer el
 * chip. Con el LSI nominal de 32 kHz:
 *
 *      32000 / 256 = 125 Hz  ->  (4095 + 1) / 125 = 32,77 s
 *
 * ⚠ Ese 32,77 es nominal. El LSI del L4 está especificado entre 29,5 y 34 kHz,
 * así que la ventana REAL cae entre ~30,8 y ~35,5 s. Por eso no se la usa como
 * plazo de nada: el plazo lo pone la capa de arriba (90 s) y `tkCtl` patea cada
 * segundo, o sea con 30 veces de margen.
 */
#define WDT_PRESCALER_CODIGO           6U
#define WDT_PRESCALER_DIVISOR        256U
#define WDT_RELOAD                  4095U
#define WDT_LSI_HZ                 32000UL

/* El tiempo que tardan PR y RLR en propagarse al dominio del LSI son ~6 ciclos
   (≈190 µs). El lazo se acota para no colgar el arranque si el LSI no anduviera:
   con el perro sin arrancar el equipo funciona igual, sólo que sin vigilancia. */
#define WDT_ESPERA_SR_VUELTAS     100000UL

static bool bCorriendo = false;

//------------------------------------------------------------------------------
void drv_wdt_arrancar( void )
{
    if( bCorriendo )
    {
        return;         /* idempotente: arrancarlo dos veces no significa nada */
    }

    /*
     * ⚠ Que el perro NO cuente cuando el debugger frena el núcleo. Sin esto,
     * cada breakpoint de más de ~32 s resetea la placa, y el síntoma —"se
     * reinicia sola mientras depuro"— manda a buscar cualquier cosa menos esto.
     *
     * El bit vive en el dominio de debug y no lo toca el reset del sistema, así
     * que ponerlo acá alcanza. ⚠ Y a diferencia de la serie F1, en el L4 el
     * DBGMCU **no necesita que se le habilite el reloj**: está siempre
     * alimentado, y `__HAL_RCC_DBGMCU_CLK_ENABLE()` ni siquiera existe.
     */
    __HAL_DBGMCU_FREEZE_IWDG();

    /*
     * Arrancar el IWDG enciende el LSI solo, por hardware: no hay que tocar
     * `RCC_CSR.LSION` ni esperar `LSIRDY`.
     */
    IWDG->KR = WDT_KEY_ARRANCAR;
    IWDG->KR = WDT_KEY_DESTRABAR;

    IWDG->PR  = WDT_PRESCALER_CODIGO;
    IWDG->RLR = WDT_RELOAD;

    /* PR y RLR viven en el dominio del LSI: hay que esperar a que se propaguen
       antes de recargar, o la recarga se pierde. */
    for( uint32_t ulVueltas = WDT_ESPERA_SR_VUELTAS;
         ( IWDG->SR != 0U ) && ( ulVueltas > 0UL );
         ulVueltas-- )
    {
        /* espera activa, y es correcta: son microsegundos y esto corre una sola
           vez, en el arranque. */
    }

    IWDG->KR = WDT_KEY_RECARGAR;

    bCorriendo = true;
}
//------------------------------------------------------------------------------
void drv_wdt_kick( void )
{
    if( bCorriendo )
    {
        IWDG->KR = WDT_KEY_RECARGAR;
    }
}
//------------------------------------------------------------------------------
bool drv_wdt_corriendo( void )
{
    return bCorriendo;
}
//------------------------------------------------------------------------------
uint32_t drv_wdt_ventana_ms( void )
{
    /* Nominal, con el LSI en su valor típico. Ver la advertencia del header. */
    return ( uint32_t ) ( ( ( ( uint64_t ) ( WDT_RELOAD + 1U )
                              * WDT_PRESCALER_DIVISOR * 1000ULL ) ) / WDT_LSI_HZ );
}
//------------------------------------------------------------------------------
