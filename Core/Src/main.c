/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#if ( ETAPA >= 4 )
#include "tkCmd.h"
#include "tkSys.h"
#include "tkWan.h"
#include "tkCtlPres.h"
#include "tkFlow.h"
#endif
#include <string.h>

#include "tkCtl.h"
#include "tkCmd.h"
#include "tkSys.h"
#include "tkWan.h"
#include "tkCtlPres.h"
#include "tkFlow.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* LED_PORT / LED_PIN están en main.h, para que los vean los demás .c. */

/* Patrones de Error_Handler: cantidad de destellos cortos antes de la pausa larga. */
#define ERR_BLINKS_RELOJ      2U   /* un oscilador de baja velocidad no arrancó */
#define ERR_BLINKS_GENERIC    5U   /* cualquier otra falla                      */

#define ERR_BLINK_ON_MS       120U
#define ERR_BLINK_OFF_MS      200U
#define ERR_PAUSE_MS         1200U

/*
 * ---------------------------------------------------------------------------
 * PRUEBA MINIMA DE PLACA  (2026-09-02)
 * ---------------------------------------------------------------------------
 * En 1, main() NO llega a ninguna de las inicializaciones generadas por CubeMX
 * ni al scheduler: se desvía a prvPruebaMinima(), que no retorna.
 *
 * Existe para el bring-up de una placa nueva, donde lo único que se quiere
 * contestar es "¿la puedo programar y el micro corre?". Por eso NO usa:
 *
 *   - el cristal de 32.768 kHz  (el reloj sale entero del MSI interno)
 *   - el RTC ni el LPTIM1       (los dos cuelgan del LSE)
 *   - FreeRTOS                  (el tick del kernel también sale del LPTIM1)
 *   - ningún periférico externo  (I2C, SPI, ADC, RS485)
 *
 * El .ioc queda intacto: el cristal sigue configurado, simplemente no se usa.
 * Volver a la operación normal es poner esto en 0 y recompilar.
 */
#define PRUEBA_MINIMA           0

#define PM_RAPIDO_MS          100U   /* etapa 1: 10 destellos rápidos, 5 Hz */
#define PM_PAUSA_MS          1000U   /* apagón que separa las etapas         */
#define PM_LENTO_MS           400U   /* etapa 4: latido lento, ~1,2 Hz       */

/*
 * ---------------------------------------------------------------------------
 * PATRON DE CONSUMO  (2026-09-29)
 * ---------------------------------------------------------------------------
 * En 1, main() se desvía a prvPatronConsumo() ANTES de que se inicialice nada
 * —ni siquiera la consola— y no vuelve.
 *
 * Contesta UNA sola pregunta: **¿cuánto consume este micro dormido, con todo
 * lo demás apagado?** Todo lo que hace está elegido para que la respuesta no
 * dependa de nada más:
 *
 *   - NO configura el PLL: se queda con el MSI a 4 MHz tal como quedó el micro
 *     tras el reset. Cuanto menos se toque, menos hay que sospechar.
 *   - NO inicializa un solo periférico: ni USART, ni I2C, ni SPI, ni ADC.
 *   - Pone TODOS los pines de todos los puertos en ANALÓGICO, que es el estado
 *     de menor fuga. Es justamente lo que el firmware de R001 no hace: él
 *     configura los pines de la placa entera, y en una placa despoblada las
 *     entradas quedan flotando.
 *   - LIMPIA DBGMCU->CR. Ver el comentario de prvPatronConsumo().
 *
 * Referencia contra la cual comparar: el equipo dormido en Stop 2, con el LSE,
 * el RTC y el LPTIM1 vivos, mide **~5 µA** (medido dos veces, con dos pisos de
 * fuente distintos). Acá no está el LPTIM1, así que si algo debería dar MENOS.
 *
 * ⚠ Se mide con el ST-LINK DESENCHUFADO DEL USB: aporta ~145 µA.
 *
 * ⚠ El micro queda en Stop 2 PARA SIEMPRE, donde el SWD está muerto. Para
 *   volver a programarlo hay que conectarse con Mode = "Under reset"
 *   (mode=UR por CLI). NRST sigue funcionando siempre.
 */
#define PATRON_CONSUMO          0     /* 0 = operación normal */

/*
 * ---------------------------------------------------------------------------
 * ETAPA  (2026-09-29) — el bring-up del CONSUMO, de nuevo y desde abajo
 * ---------------------------------------------------------------------------
 * PATRON_CONSUMO dejó probado que el micro dormido da 3 µA. Lo que sobra hasta
 * los ~390 µA está en lo que el firmware agrega encima, y en vez de quitárselo
 * al firmware completo —que ya costó dos bajadas— se reconstruye desde ese
 * punto conocido-bueno, agregando UNA cosa por etapa.
 *
 * Es el bring-up incremental del proyecto aplicado al consumo: cada etapa se
 * mide en banco antes de pasar a la siguiente, así el sospechoso es siempre lo
 * último que entró.
 *
 *   0 = operación normal (el firmware completo)
 *   1 = FreeRTOS + UNA tarea que destella el LED a 1 Hz. Nada más.  -> 3 µA ✅
 *   2 = la 1 + MX_GPIO_Init(): los pines de R001 completa.        -> 8 µA ✅
 *   3 = la 2 + los periféricos de CubeMX (ver ETAPA3_PERIF).   -> 1,15 mA ⚠
 *   4 = la 3 + las tareas del firmware (ver ETAPA4_TAREAS).
 *
 * La numeración de FW_VERSION arranca de nuevo en 0.0.1 y acompaña a la etapa.
 *
 * ⚠ La etapa 1 NO llama a MX_GPIO_Init(): los pines quedan como los dejó el
 *   reset, o sea en ANALÓGICO, que es el estado de menor fuga y el mismo con el
 *   que se midieron los 3 µA. Configurar los pines de R001 es una etapa propia,
 *   justamente porque es uno de los sospechosos.
 */
#define ETAPA                   4

/*
 * Sólo con ETAPA >= 4: qué tareas del firmware se crean, en bitmask.
 *
 * ⭐ Acá el consumo puede BAJAR, y por eso esta etapa importa más que la 3: la
 * etapa 3 dio 1,15 mA, o sea MÁS que el firmware completo (~390 µA). La
 * diferencia es que CubeMX deja los periféricos recién inicializados y son los
 * DRIVERS los que los ponen en reposo — el ADC en deep power-down, el INA
 * dormido, los rieles cortados.
 *
 * Y a los drivers los inicializa tkCmd. Así que la predicción para 0x01 es que
 * el consumo baje de 1,15 mA a algo cercano a los 390 µA; si baja, el firmware
 * queda reproducido y desde ahí se bisecta sobre lo real.
 *
 *   0x01 tkCmd  (la que inicializa TODOS los drivers)
 *   0x02 tkSys        0x08 tkCtlPres
 *   0x04 tkWan        0x10 tkFlow
 *
 * ⚠ tkCtl NO está: su lugar lo ocupa prvTareaLed, que es el destello de la
 *   etapa 1 y lo único que hay que mantener igual entre todas las etapas para
 *   que los números se puedan comparar.
 */
#define ETAPA4_TK_CMD           0x01U
#define ETAPA4_TK_SYS           0x02U
#define ETAPA4_TK_WAN           0x04U
#define ETAPA4_TK_CTLPRES       0x08U
#define ETAPA4_TK_FLOW          0x10U

#define ETAPA4_TAREAS           0x01U

/*
 * Sólo con ETAPA >= 3: qué periféricos se inicializan, en bitmask. Reusa los
 * PC_PERIF_* del patrón, que están más abajo.
 *
 * Plan: primero 0xFF. Si reproduce los ~390 µA, búsqueda binaria —0x0F, después
 * la mitad que salte— y en tres bajadas queda el periférico.
 *
 *   0x01 RTC        0x10 UART4 (LTE)
 *   0x04 USART1     0x20 I2C2
 *   0x08 USART3     0x40 SPI3
 *                   0x80 ADC1
 *
 * (0x02 es LPTIM1 y ya entró en la etapa 1: el tick del kernel lo necesita.)
 */
#define ETAPA3_PERIF            0xFFU

/*
 * Sólo con ETAPA 2. En 1 deshabilita la EXTI de CNT0 (PA12) después de
 * MX_GPIO_Init(), dejando ese pin en analógico.
 *
 * ⭐ Separa las dos formas en que un pin puede costar consumo, que son
 * distintas y se arreglan distinto:
 *   - FUGA: el buffer de entrada de un pin flotando conduce por sus dos
 *     transistores a la vez. Cuesta corriente aunque nada pase.
 *   - DESPERTADAS: PA12 está en GPIO_MODE_IT_FALLING y en esta placa el
 *     74AUP1G17 que lo maneja no está. Un pin EXTI flotando genera flancos
 *     espurios, y cada uno saca al micro de Stop 2 y le hace rehacer
 *     SystemClock_Config(). Eso no es fuga: es trabajo.
 *
 * Con ETAPA 2 en alto y esto en 1, si baja eran las despertadas.
 */
#define ETAPA2_SIN_EXTI         0



/*
 * En 1 deja el LSE y el RTC corriendo, que es el estado real del equipo: se
 * compara directo contra los ~5 µA de referencia.
 * En 0 no enciende ningún oscilador de baja velocidad y mide el PISO ABSOLUTO
 * del Stop 2, que es más discriminante para decidir si el micro está sano.
 * Los destellos de arranque dicen cuál de las dos se bajó.
 */
#define PC_CON_LSE              1

/*
 * En 1 el patrón limpia DBGMCU_CR antes de dormir; en 0 lo deja como esté.
 *
 * ⭐ Poner esto en 0 es LA PRUEBA que separa las dos causas posibles, porque
 * deja una sola variable: si el consumo vuelve a subir a ~390 µA, el culpable
 * eran los bits de debug; si se queda en ~3 µA, eran los pines flotando.
 */
#define PC_LIMPIAR_DBGMCU       0

/*
 * ⭐ LA BISECCIÓN DEL CONSUMO, en bitmask.
 *
 * El patrón solo da 3 µA. Acá se le agregan periféricos —los mismos
 * MX_*_Init() que corre la operación normal, con los mismos pines— y se vuelve
 * a dormir en Stop 2. Cada bit que se prende acerca el ensayo al firmware
 * real, así que el salto de consumo señala al culpable.
 *
 * Se inicializan DESPUÉS de poner todo en analógico, así que lo que se mide es
 * el aporte del periférico MÁS el de los pines que él configura — que es
 * exactamente el par que interesa.
 *
 * Plan: primero 0xFF para reproducir los ~390 µA en un entorno sin FreeRTOS.
 * Con eso reproducido, búsqueda binaria: 0x0F, después la mitad que salte, y
 * en tres bajadas queda el periférico.
 *
 *   0x01 RTC        0x10 UART4 (LTE)
 *   0x02 LPTIM1     0x20 I2C2
 *   0x04 USART1     0x40 SPI3
 *   0x08 USART3     0x80 ADC1
 */
#define PC_PERIF_RTC        0x01U
#define PC_PERIF_LPTIM1     0x02U
#define PC_PERIF_USART1     0x04U
#define PC_PERIF_USART3     0x08U
#define PC_PERIF_UART4      0x10U
#define PC_PERIF_I2C2       0x20U
#define PC_PERIF_SPI3       0x40U
#define PC_PERIF_ADC1       0x80U

#define PC_PERIFERICOS      0x00U

#define PC_DESTELLOS_CON_LSE   10U   /* "llegué, y con el LSE encendido"  */
#define PC_DESTELLOS_SIN_LSE    5U   /* "llegué, sin osciladores"         */
#define PC_DESTELLOS_SIN_XTAL   2U   /* el LSE se pidió y NO arrancó      */
#define PC_ON_MS              100U
#define PC_OFF_MS             100U
#define PC_DESPERTAR_MS        60U   /* destello de "algo me despertó"    */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c2;

LPTIM_HandleTypeDef hlptim1;

RTC_HandleTypeDef hrtc;

SPI_HandleTypeDef hspi3;

UART_HandleTypeDef huart4;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE BEGIN PV */
/* La memoria estática de cada tarea la define la tarea misma, en su .c de
   Application/tasks/. Acá no va nada. */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_RTC_Init(void);
static void MX_LPTIM1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_I2C2_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_SPI3_Init(void);
static void MX_ADC1_Init(void);
static void MX_UART4_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */
#if ( PRUEBA_MINIMA == 1 )
static void prvPruebaMinima( void );
#endif
#if ( PATRON_CONSUMO == 1 )
static void prvPatronConsumo( void );
#endif
#if ( ETAPA > 0 )
static void prvEtapa( void );
static void prvTareaLed( void *pvParameters );
#endif
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*
 * Configura el pin del LED como salida.
 *
 * Error_Handler() puede dispararse desde SystemClock_Config(), es decir ANTES de
 * MX_GPIO_Init(): si no configuramos el pin acá, el patrón no se ve. Es idempotente,
 * no molesta volver a llamarlo.
 */
static void led_config( void )
{
    GPIO_InitTypeDef gpio = { 0 };

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin   = LED_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init( LED_PORT, &gpio );
}

/*
 * Emisión CRUDA por la TERM: HAL por poleo, sin FreeRTOS y sin FRTOS-IO.
 *
 * Es el canal de diagnóstico de último recurso, para el código que corre antes
 * del scheduler y para Error_Handler(). No usarlo en la aplicación: ahí va
 * xprintf(), que transmite por interrupción y toma el candado de energía.
 *
 * Que exista salió de un día entero perdido (2026-08-11) contando destellos de
 * un LED que además mentía. Un renglón de texto vale por diez patrones de
 * parpadeo, y desde que la USART se levanta antes de SystemClock_Config() —ver
 * USER CODE BEGIN Init— este canal anda incluso cuando el que falla es el reloj.
 */
static void error_print( const char *pcTexto )
{
    ( void ) HAL_UART_Transmit( &huart1, ( uint8_t * ) pcTexto,
                                ( uint16_t ) strlen( pcTexto ), 500U );
}

/*
 * Demora por sondeo, para usar con las interrupciones cortadas.
 *
 * Acá NO sirve HAL_Delay(): desde que el timebase de la HAL está en TIM6, uwTick lo
 * incrementa la ISR de TIM6, y con __disable_irq() esa ISR no corre. HAL_Delay() se
 * colgaría para siempre.
 *
 * Se usa el COUNTFLAG del SysTick, que sigue contando aunque su IRQ esté enmascarada.
 * Ojo con el matiz introducido por FreeRTOS: el SysTick ya no lo configura la HAL sino
 * el kernel, al arrancar el scheduler, a configTICK_RATE_HZ (1 kHz) -> cada COUNTFLAG
 * es 1 ms. Si Error_Handler() se dispara ANTES de vTaskStartScheduler(), el SysTick
 * está apagado y se cae al lazo tosco: impreciso, pero nunca se cuelga.
 */
static void error_delay_ms( uint32_t ms )
{
    if( ( SysTick->CTRL & SysTick_CTRL_ENABLE_Msk ) != 0U )
    {
        while( ms-- )
        {
            while( ( SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk ) == 0U )
            {
            }
        }
    }
    else
    {
        volatile uint32_t vueltas = ms * ( SystemCoreClock / 8000U );
        while( vueltas-- )
        {
        }
    }
}

#if ( PRUEBA_MINIMA == 1 )

/*
 * El mismo árbol de clocks que SystemClock_Config() pero SIN el LSE:
 * MSI (range 6 = 4 MHz) -> PLL (M=1, N=30, /2) -> 60 MHz.
 *
 * Es una copia y no una llamada a la original a propósito: la original vive
 * fuera de los bloques USER CODE y la regenera CubeMX, así que tocarla sería
 * perder el cambio en la próxima regeneración. Acá la copia es de usar y tirar.
 *
 * Devuelve 0 si algo falló. NO llama a Error_Handler(): el LED tiene que
 * destellar igual, porque lo que esta prueba contesta es "el micro corre", y
 * eso es cierto aunque el PLL no arranque. Si falla, se sigue con el MSI a
 * 4 MHz y se dice por la consola.
 */
static int prvClockSinCristal( void )
{
    RCC_OscInitTypeDef osc = { 0 };
    RCC_ClkInitTypeDef clk = { 0 };

    if ( HAL_PWREx_ControlVoltageScaling( PWR_REGULATOR_VOLTAGE_SCALE1 ) != HAL_OK )
    {
        return 0;
    }

    /* Sin RCC_OSCILLATORTYPE_LSE, sin HAL_PWR_EnableBkUpAccess() y sin
       __HAL_RCC_LSEDRIVE_CONFIG(): el cristal no se toca ni para encenderlo. */
    osc.OscillatorType      = RCC_OSCILLATORTYPE_MSI;
    osc.MSIState            = RCC_MSI_ON;
    osc.MSICalibrationValue = 0;
    osc.MSIClockRange       = RCC_MSIRANGE_6;
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_MSI;
    osc.PLL.PLLM            = 1;
    osc.PLL.PLLN            = 30;
    osc.PLL.PLLP            = RCC_PLLP_DIV2;
    osc.PLL.PLLQ            = RCC_PLLQ_DIV2;
    osc.PLL.PLLR            = RCC_PLLR_DIV2;

    if ( HAL_RCC_OscConfig( &osc ) != HAL_OK )
    {
        return 0;
    }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    if ( HAL_RCC_ClockConfig( &clk, FLASH_LATENCY_3 ) != HAL_OK )
    {
        return 0;
    }

    return 1;
}

/*
 * La prueba mínima. NO retorna.
 *
 * Se entra desde USER CODE BEGIN Init, o sea ANTES de SystemClock_Config() y
 * de todos los MX_*_Init(): ninguna línea generada por CubeMX llega a correr.
 *
 * La demora es HAL_Delay(), que anda porque el timebase de la HAL está en TIM6
 * —no en el SysTick— y HAL_RCC_ClockConfig() rellama a HAL_InitTick() con el
 * reloj nuevo. Si el PLL no arrancó, TIM6 se queda con el divisor de 4 MHz, que
 * también es correcto: en los dos casos el destello sale a 2 Hz.
 */
static void prvPruebaMinima( void )
{
    uint32_t i;
    int      iPll;

    /* -----------------------------------------------------------------------
     * ETAPA 1 - prueba de vida cruda, ANTES de tocar nada.
     *
     * Corre con el MSI a 4 MHz, tal cual quedó el micro después del reset, y la
     * demora es el lazo por sondeo de error_delay_ms(): no usa TIM6, ni uwTick,
     * ni HAL_Delay(), ni una sola interrupción. Es el mínimo absoluto que puede
     * hacer un STM32 vivo.
     *
     * 10 destellos rápidos. Si NO se ven, el firmware no llegó o no corre, y no
     * tiene sentido mirar nada de lo que sigue.
     * --------------------------------------------------------------------- */
    led_config();

    for ( i = 0U; i < 20U; i++ )
    {
        HAL_GPIO_TogglePin( LED_PORT, LED_PIN );
        error_delay_ms( PM_RAPIDO_MS );
    }

    /* Apagado largo, para que se note la frontera entre las etapas. */
    HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_RESET );
    error_delay_ms( PM_PAUSA_MS );

    /* -----------------------------------------------------------------------
     * ETAPA 2 - el reloj, sin el cristal. Si se colgara acá, se habrían visto
     * los 10 destellos y después el LED queda quieto: eso ya localiza la falla.
     * --------------------------------------------------------------------- */
    iPll = prvClockSinCristal();

    /* -----------------------------------------------------------------------
     * ETAPA 3 - la consola. El divisor se recalcula contra el reloj que haya
     * quedado; sin esto, si el PLL arrancó, la USART sigue con el divisor de
     * 4 MHz y sale ilegible.
     * --------------------------------------------------------------------- */
    MX_USART1_UART_Init();

    error_print( "\r\n\r\n=== PRUEBA MINIMA DE PLACA ===\r\n" );
    error_print( iPll ? "reloj: MSI -> PLL, sin LSE (el cristal NO se usa)\r\n"
                      : "reloj: [!] el PLL no arranco, sigo con MSI a 4 MHz\r\n" );
    error_print( "LED  : PB9, latido lento de aca en mas.\r\n"
                 "sin RTC, sin LPTIM1, sin FreeRTOS y sin perifericos externos.\r\n" );

    /* -----------------------------------------------------------------------
     * ETAPA 4 - latido permanente, con el mismo lazo por sondeo de la etapa 1.
     * --------------------------------------------------------------------- */
    for ( ;; )
    {
        HAL_GPIO_TogglePin( LED_PORT, LED_PIN );
        error_delay_ms( PM_LENTO_MS );
    }
}

#endif /* PRUEBA_MINIMA */

#if ( PATRON_CONSUMO == 1 )
/*
 * Deja el LED destellando N veces y después duerme para siempre en Stop 2.
 * No retorna. Ver el comentario del #define, más arriba.
 */
static void prvPatronConsumo( void )
{
    GPIO_InitTypeDef gpio      = { 0 };
    uint32_t         destellos = 0U;
    uint32_t         i;

    /* error_delay_ms() calibra su lazo con SystemCoreClock, y acá el SysTick no
       corre. Tras el reset el MSI está en 4 MHz; sin esta línea los destellos
       saldrían quince veces más rápidos, que es el mismo pozo que costó medio
       día el 2026-08-11 con Error_Handler(). */
    SystemCoreClockUpdate();

    /*
     * ⛔ EL SOSPECHOSO NÚMERO UNO, y por eso es lo primero que se hace.
     *
     * Al conectarse, CubeProgrammer y el GDB server del IDE informan
     * "Debug in Low Power mode enabled" y ponen DBG_SLEEP / DBG_STOP /
     * DBG_STANDBY en DBGMCU_CR. Con DBG_STOP puesto **el micro NO apaga los
     * relojes al entrar en Stop 2**: los mantiene para que el debugger no
     * pierda el enganche. Un micro que cree estar dormido y tiene el reloj
     * corriendo consume cientos de µA en vez de unidades.
     *
     * Y lo que lo vuelve traicionero: DBGMCU_CR **NO se resetea con el reset
     * del sistema**, sólo con el power-on reset. O sea que los bits sobreviven
     * a NRST, a un reset por software y a cuantos firmwares se bajen encima —
     * pero desaparecen si se corta la alimentación. Eso explica que una misma
     * placa mida distinto según si hubo o no un ciclo de energía antes.
     *
     * Nadie del firmware los pone; los pone la herramienta al conectarse.
     */
#if ( PC_LIMPIAR_DBGMCU == 1 )
    DBGMCU->CR = 0U;
#endif

    /* ---- los destellos dicen QUÉ binario se bajó ---------------------- */
#if ( PC_CON_LSE == 1 )
    destellos = PC_DESTELLOS_CON_LSE;
#else
    destellos = PC_DESTELLOS_SIN_LSE;
#endif

    led_config();

    for( i = 0U; i < destellos; i++ )
    {
        HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_SET );
        error_delay_ms( PC_ON_MS );
        HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_RESET );
        error_delay_ms( PC_OFF_MS );
    }

#if ( PC_CON_LSE == 1 )
    /*
     * El LSE va ANTES de poner los pines en analógico, por dos razones: para
     * poder avisar por el LED si el cristal no arranca, y porque una vez
     * encendido el oscilador toma control de PC14/PC15 y el GPIO deja de
     * mandar sobre ellos.
     */
    {
        RCC_OscInitTypeDef       osc    = { 0 };
        RCC_PeriphCLKInitTypeDef periph = { 0 };

        HAL_PWR_EnableBkUpAccess();

        osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
        osc.LSEState       = RCC_LSE_ON;
        osc.PLL.PLLState   = RCC_PLL_NONE;      /* que no toque el PLL */

        if( HAL_RCC_OscConfig( &osc ) == HAL_OK )
        {
            __HAL_RCC_LSEDRIVE_CONFIG( RCC_LSEDRIVE_LOW );

            /* El RTC es el consumidor que hace que el LSE quede realmente en
               uso; sin un consumidor el oscilador no aporta nada al ensayo. */
            periph.PeriphClockSelection = RCC_PERIPHCLK_RTC;
            periph.RTCClockSelection    = RCC_RTCCLKSOURCE_LSE;
            ( void ) HAL_RCCEx_PeriphCLKConfig( &periph );
            __HAL_RCC_RTC_ENABLE();
        }
        else
        {
            /* No se llama a Error_Handler(): el ensayo sigue valiendo sin el
               cristal, sólo hay que saber que se está midiendo sin él. */
            error_delay_ms( 800U );

            for( i = 0U; i < PC_DESTELLOS_SIN_XTAL; i++ )
            {
                HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_SET );
                error_delay_ms( 400U );
                HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_RESET );
                error_delay_ms( 400U );
            }
        }
    }
#endif /* PC_CON_LSE */

    /*
     * ---- TODOS los pines en analógico -------------------------------------
     *
     * Analógico + sin pull es el estado de menor fuga que tiene el silicio:
     * desconecta el buffer de entrada digital, que es lo que consume cuando un
     * pin queda flotando cerca del umbral y hace conducir a la vez los dos
     * transistores de su etapa.
     *
     * Es también el estado en que quedan los pines tras el reset, así que esto
     * es explícito a propósito: el ensayo no debe depender de suposiciones
     * sobre lo que quedó de antes.
     *
     * Dos exclusiones, las dos deliberadas:
     *   - PA13/PA14 (SWD): sus pulls internos no conducen con el dongle
     *     desconectado, así que dejarlos vivos es gratis y conserva la
     *     posibilidad de engancharse.
     *   - PC14/PC15 (OSC32): con el LSE encendido los maneja el oscilador.
     *
     * Sólo se barren los puertos que tienen pines en el LQFP64. Un pin de un
     * puerto que no sale al encapsulado está atado internamente y no flota.
     */
    gpio.Mode  = GPIO_MODE_ANALOG;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_All & ~( GPIO_PIN_13 | GPIO_PIN_14 );
    HAL_GPIO_Init( GPIOA, &gpio );

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_All;                    /* incluye el LED: se apaga */
    HAL_GPIO_Init( GPIOB, &gpio );

    __HAL_RCC_GPIOC_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_All & ~( GPIO_PIN_14 | GPIO_PIN_15 );
    HAL_GPIO_Init( GPIOC, &gpio );

    __HAL_RCC_GPIOD_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_All;
    HAL_GPIO_Init( GPIOD, &gpio );

    __HAL_RCC_GPIOH_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_All;
    HAL_GPIO_Init( GPIOH, &gpio );

    /*
     * ---- los periféricos que se quieran sumar al ensayo -------------------
     *
     * Van DESPUÉS del aislado a propósito: cada MX_*_Init() reconfigura los
     * pines que usa, así que lo que queda medido es el periférico con sus
     * pines tal como los deja la operación normal.
     */
#if ( ( PC_PERIFERICOS & PC_PERIF_RTC ) != 0U )
    MX_RTC_Init();
#endif
#if ( ( PC_PERIFERICOS & PC_PERIF_LPTIM1 ) != 0U )
    MX_LPTIM1_Init();
#endif
#if ( ( PC_PERIFERICOS & PC_PERIF_USART1 ) != 0U )
    MX_USART1_UART_Init();
#endif
#if ( ( PC_PERIFERICOS & PC_PERIF_USART3 ) != 0U )
    MX_USART3_UART_Init();
#endif
#if ( ( PC_PERIFERICOS & PC_PERIF_UART4 ) != 0U )
    MX_UART4_Init();
#endif
#if ( ( PC_PERIFERICOS & PC_PERIF_I2C2 ) != 0U )
    MX_I2C2_Init();
#endif
#if ( ( PC_PERIFERICOS & PC_PERIF_SPI3 ) != 0U )
    MX_SPI3_Init();
#endif
#if ( ( PC_PERIFERICOS & PC_PERIF_ADC1 ) != 0U )
    MX_ADC1_Init();
#endif

    /*
     * ---- a dormir, y no volver --------------------------------------------
     *
     * El SysTick se suspende: con él corriendo habría una interrupción
     * pendiente cada milisegundo, que es justo lo que no deja entrar al WFI.
     *
     * El lazo está porque despertar NO debería pasar: con todos los pines en
     * analógico no hay EXTI posible y no hay periférico configurado. Si el LED
     * destella tres veces, ALGO está despertando al micro y la medición no vale
     * — y eso es un diagnóstico, no un adorno: distingue "consume dormido" de
     * "no se queda dormido", que desde el amperímetro se ven igual.
     */
    HAL_SuspendTick();

    for( ;; )
    {
        HAL_PWREx_EnterSTOP2Mode( PWR_STOPENTRY_WFI );

        /* Al salir de Stop el reloj vuelve al MSI: recalibrar el lazo. */
        SystemCoreClockUpdate();

        led_config();

        for( i = 0U; i < 3U; i++ )
        {
            HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_SET );
            error_delay_ms( PC_DESPERTAR_MS );
            HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_RESET );
            error_delay_ms( PC_DESPERTAR_MS );
        }

        gpio.Mode = GPIO_MODE_ANALOG;
        gpio.Pull = GPIO_NOPULL;
        gpio.Pin  = LED_PIN;
        HAL_GPIO_Init( LED_PORT, &gpio );
    }
}
#endif /* PATRON_CONSUMO */

#if ( ETAPA > 0 )

/* Memoria estática de la tarea: no toca el heap, igual que las del firmware. */
#define ETAPA_LED_STACK      256U
#define ETAPA_LED_ON_MS       50U
#define ETAPA_LED_PERIODO_MS 1000U

static StaticTask_t xEtapaTCB;
static StackType_t  xEtapaStack[ ETAPA_LED_STACK ];

/* El destello: 50 ms encendido cada segundo, o sea 5 % de duty. */
static void prvTareaLed( void *pvParameters )
{
    ( void ) pvParameters;

    for( ;; )
    {
        HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_SET );
        vTaskDelay( pdMS_TO_TICKS( ETAPA_LED_ON_MS ) );
        HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_RESET );
        vTaskDelay( pdMS_TO_TICKS( ETAPA_LED_PERIODO_MS - ETAPA_LED_ON_MS ) );
    }
}

/*
 * Arranque mínimo. No retorna: entrega el control al scheduler.
 * Ver el comentario del #define ETAPA, más arriba.
 */
static void prvEtapa( void )
{
    uint32_t i;

    /* El reloj se configura acá porque esta función se desvía ANTES de que
       main() llegue a SystemClock_Config(). Es la misma llamada: enciende el
       LSE, que es de donde sale el tick del kernel por LPTIM1. */
    SystemClock_Config();

    /* El tick del kernel usa el handle que inicializa CubeMX (ver
       port_lptim_tick.c), así que este init no es opcional. No toca pines. */
    MX_LPTIM1_Init();

#if ( ETAPA >= 2 )
    /*
     * Los pines de R001 COMPLETA, tal como los configura la operación normal.
     * En una placa despoblada eso deja siete entradas sin quién las fije:
     * TERM_RX, RS485_RX, LTE_RXD, las dos del I2C2, SD_MISO y CNT0.
     */
    MX_GPIO_Init();

#if ( ETAPA2_SIN_EXTI == 1 )
    /* DeInit limpia la configuración EXTI del pin y lo deja en analógico, que
       es lo que hace falta acá: no alcanza con reconfigurar el modo. */
    HAL_GPIO_DeInit( CNT0_GPIO_Port, CNT0_Pin );
#endif
#endif

#if ( ETAPA >= 3 )
    /* Los periféricos de CubeMX, con los pines que cada MspInit configura. */
#if ( ( ETAPA3_PERIF & PC_PERIF_RTC ) != 0U )
    MX_RTC_Init();
#endif
#if ( ( ETAPA3_PERIF & PC_PERIF_USART1 ) != 0U )
    MX_USART1_UART_Init();
#endif
#if ( ( ETAPA3_PERIF & PC_PERIF_USART3 ) != 0U )
    MX_USART3_UART_Init();
#endif
#if ( ( ETAPA3_PERIF & PC_PERIF_UART4 ) != 0U )
    MX_UART4_Init();
#endif
#if ( ( ETAPA3_PERIF & PC_PERIF_I2C2 ) != 0U )
    MX_I2C2_Init();
#endif
#if ( ( ETAPA3_PERIF & PC_PERIF_SPI3 ) != 0U )
    MX_SPI3_Init();
#endif
#if ( ( ETAPA3_PERIF & PC_PERIF_ADC1 ) != 0U )
    MX_ADC1_Init();
#endif
#endif

    /* El único pin que se configura en toda la etapa. */
    led_config();

    /*
     * Tres destellos antes de arrancar el scheduler. Es la marca que separa
     * "no arrancó el reloj" de "no arrancó el scheduler": sin ella las dos se
     * ven igual —LED apagado— y en el amperímetro se leen como consumo alto.
     */
    for( i = 0U; i < 3U; i++ )
    {
        HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_SET );
        error_delay_ms( 80U );
        HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_RESET );
        error_delay_ms( 80U );
    }

    if( xTaskCreateStatic( prvTareaLed,
                           "LED",
                           ETAPA_LED_STACK,
                           NULL,
                           tskIDLE_PRIORITY + 1,
                           xEtapaStack,
                           &xEtapaTCB ) == NULL )
    {
        Error_Handler();
    }

#if ( ETAPA >= 4 )
    /* Las tareas del firmware, con los mismos parámetros que en main(). */
#if ( ( ETAPA4_TAREAS & ETAPA4_TK_CMD ) != 0U )
    if( xTaskCreateStatic( tkCmd, "CMD", tkCmd_STACK_SIZE, NULL,
                           tkCmd_PRIORITY, tkCmd_Stack, &tkCmd_TCB ) == NULL )
    {
        Error_Handler();
    }
#endif
#if ( ( ETAPA4_TAREAS & ETAPA4_TK_SYS ) != 0U )
    xHandle_tkSys = xTaskCreateStatic( tkSys, "SYS", tkSys_STACK_SIZE, NULL,
                                       tkSys_PRIORITY, tkSys_Stack, &tkSys_TCB );
#endif
#if ( ( ETAPA4_TAREAS & ETAPA4_TK_WAN ) != 0U )
    xHandle_tkWan = xTaskCreateStatic( tkWan, "WAN", tkWan_STACK_SIZE, NULL,
                                       tkWan_PRIORITY, tkWan_Stack, &tkWan_TCB );
#endif
#if ( ( ETAPA4_TAREAS & ETAPA4_TK_CTLPRES ) != 0U )
    xHandle_tkCtlPres = xTaskCreateStatic( tkCtlPres, "CPRES", tkCtlPres_STACK_SIZE, NULL,
                                           tkCtlPres_PRIORITY, tkCtlPres_Stack, &tkCtlPres_TCB );
#endif
#if ( ( ETAPA4_TAREAS & ETAPA4_TK_FLOW ) != 0U )
    xHandle_tkFlow = xTaskCreateStatic( tkFlow, "FLOW", tkFlow_STACK_SIZE, NULL,
                                        tkFlow_PRIORITY, tkFlow_Stack, &tkFlow_TCB );
#endif
#endif /* ETAPA >= 4 */

    vTaskStartScheduler();

    /* Sólo se llega acá si el scheduler no pudo arrancar. */
    Error_Handler();
}
#endif /* ETAPA */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
#if ( ETAPA > 0 )
  /* Se va acá y no vuelve. Ver el comentario del #define ETAPA. */
  prvEtapa();
#endif

#if ( PATRON_CONSUMO == 1 )
  /*
   * Se va acá y no vuelve, y va ANTES que la USART a propósito: levantarla
   * dejaría PB6/PB7 en modo alterno y el periférico encendido, que es
   * exactamente lo que este ensayo no quiere tener prendido.
   */
  prvPatronConsumo();
#endif

  /*
   * La USART se levanta ANTES de SystemClock_Config(), o sea corriendo con el MSI
   * a 4 MHz y todavía sin PLL. Es a propósito: si el que falla es el RELOJ, esta
   * es la única forma de que Error_Handler() pueda decirlo por texto en vez de
   * dejarnos contando destellos.
   *
   * A 4 MHz el divisor de 9600 baudios da 417, con 0,08 % de error. Se vuelve a
   * inicializar apenas cambia el reloj (USER CODE BEGIN SysInit) para recalcular
   * el divisor contra los 60 MHz.
   */
  MX_USART1_UART_Init();

#if ( PRUEBA_MINIMA == 1 )
  /* Se va acá y no vuelve. Ver el comentario del #define, arriba. */
  prvPruebaMinima();
#endif
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* Recalcula el divisor contra el reloj nuevo (60 MHz). Sin esto la USART queda
     con el divisor del MSI y todo lo que emitan los MX_*_Init() —Error_Handler()
     incluido— sale ilegible. La reinicialización de más abajo es redundante y no
     molesta: es la que genera CubeMX y no se puede sacar. */
  MX_USART1_UART_Init();
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_RTC_Init();
  MX_LPTIM1_Init();
  MX_USART1_UART_Init();
  MX_I2C2_Init();
  MX_USART3_UART_Init();
  MX_SPI3_Init();
  MX_ADC1_Init();
  MX_UART4_Init();
  MX_FATFS_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* Tareas propias, con la API nativa de FreeRTOS (no el wrapper CMSIS). */
  xHandle_tkCtl = xTaskCreateStatic( tkCtl,                /* función de la tarea      */
                                     "CTL",                /* nombre para depurar      */
                                     tkCtl_STACK_SIZE,     /* stack en PALABRAS, no bytes */
                                     NULL,                 /* pvParameters             */
                                     tkCtl_PRIORITY,
                                     tkCtl_Stack,          /* stack provisto por la app */
                                     &tkCtl_TCB );         /* TCB provisto por la app   */

  if ( xHandle_tkCtl == NULL )
  {
    Error_Handler();
  }

  /* Consola TERM. Abre los drivers de FRTOS-IO desde adentro de la tarea, porque
     crear semáforos y stream buffers necesita el scheduler ya corriendo. */
  if ( xTaskCreateStatic( tkCmd,
                          "CMD",
                          tkCmd_STACK_SIZE,
                          NULL,
                          tkCmd_PRIORITY,
                          tkCmd_Stack,
                          &tkCmd_TCB ) == NULL )
  {
    Error_Handler();
  }

  /* Medida. Arranca después de tkCmd a propósito: tkCmd es quien inicializa los
     drivers y carga la configuración desde la EEPROM, y tkSys los necesita. La
     espera de arranque de tkSys (10 s) es lo que le da margen. */
  xHandle_tkSys = xTaskCreateStatic( tkSys,
                                     "SYS",
                                     tkSys_STACK_SIZE,
                                     NULL,
                                     tkSys_PRIORITY,
                                     tkSys_Stack,
                                     &tkSys_TCB );
  if ( xHandle_tkSys == NULL )
  {
    Error_Handler();
  }

  /* La WAN: la maquina de estados que abre la sesion con el servidor. Arranca
     sola y disca enseguida, para que un equipo recien energizado se configure y
     vacie la memoria en vez de esperar al primer timerdial. */
  xHandle_tkWan = xTaskCreateStatic( tkWan,
                                     "WAN",
                                     tkWan_STACK_SIZE,
                                     NULL,
                                     tkWan_PRIORITY,
                                     tkWan_Stack,
                                     &tkWan_TCB );
  if ( xHandle_tkWan == NULL )
  {
    Error_Handler();
  }

  /* La doble consigna del control de presion. Arranca aunque este deshabilitada:
     asi se la puede habilitar desde la consola sin reiniciar, y el comando
     'cpres' funciona igual para probar el dispositivo a mano. */
  xHandle_tkCtlPres = xTaskCreateStatic( tkCtlPres,
                                         "CPRES",
                                         tkCtlPres_STACK_SIZE,
                                         NULL,
                                         tkCtlPres_PRIORITY,
                                         tkCtlPres_Stack,
                                         &tkCtlPres_TCB );
  if ( xHandle_tkCtlPres == NULL )
  {
    Error_Handler();
  }

  /* La valvula TOYI interna. Hoy solo atiende las ordenes del servidor; la tabla
     de horarios de flowcontrol se configura pero todavia no se ejecuta. */
  xHandle_tkFlow = xTaskCreateStatic( tkFlow,
                                      "FLOW",
                                      tkFlow_STACK_SIZE,
                                      NULL,
                                      tkFlow_PRIORITY,
                                      tkFlow_Stack,
                                      &tkFlow_TCB );
  if ( xHandle_tkFlow == NULL )
  {
    Error_Handler();
  }

  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 30;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_15;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = 0x10A077A8;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */

}

/**
  * @brief LPTIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_LPTIM1_Init(void)
{

  /* USER CODE BEGIN LPTIM1_Init 0 */

  /* USER CODE END LPTIM1_Init 0 */

  /* USER CODE BEGIN LPTIM1_Init 1 */

  /* USER CODE END LPTIM1_Init 1 */
  hlptim1.Instance = LPTIM1;
  hlptim1.Init.Clock.Source = LPTIM_CLOCKSOURCE_APBCLOCK_LPOSC;
  hlptim1.Init.Clock.Prescaler = LPTIM_PRESCALER_DIV32;
  hlptim1.Init.Trigger.Source = LPTIM_TRIGSOURCE_SOFTWARE;
  hlptim1.Init.OutputPolarity = LPTIM_OUTPUTPOLARITY_HIGH;
  hlptim1.Init.UpdateMode = LPTIM_UPDATE_IMMEDIATE;
  hlptim1.Init.CounterSource = LPTIM_COUNTERSOURCE_INTERNAL;
  hlptim1.Init.Input1Source = LPTIM_INPUT1SOURCE_GPIO;
  hlptim1.Init.Input2Source = LPTIM_INPUT2SOURCE_GPIO;
  if (HAL_LPTIM_Init(&hlptim1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LPTIM1_Init 2 */

  /* USER CODE END LPTIM1_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */

  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x1;
  sDate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief SPI3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI3_Init(void)
{

  /* USER CODE BEGIN SPI3_Init 0 */

  /* USER CODE END SPI3_Init 0 */

  /* USER CODE BEGIN SPI3_Init 1 */

  /* USER CODE END SPI3_Init 1 */
  /* SPI3 parameter configuration*/
  hspi3.Instance = SPI3;
  hspi3.Init.Mode = SPI_MODE_MASTER;
  hspi3.Init.Direction = SPI_DIRECTION_2LINES;
  hspi3.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi3.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi3.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi3.Init.NSS = SPI_NSS_SOFT;
  hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
  hspi3.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi3.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi3.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi3.Init.CRCPolynomial = 7;
  hspi3.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi3.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI3_Init 2 */

  /* USER CODE END SPI3_Init 2 */

}

/**
  * @brief UART4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART4_Init(void)
{

  /* USER CODE BEGIN UART4_Init 0 */

  /* USER CODE END UART4_Init 0 */

  /* USER CODE BEGIN UART4_Init 1 */

  /* USER CODE END UART4_Init 1 */
  huart4.Instance = UART4;
  huart4.Init.BaudRate = 115200;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN UART4_Init 2 */

  /* USER CODE END UART4_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 9600;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_RS485Ex_Init(&huart3, UART_DE_POLARITY_HIGH, 0, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, EN_LTE_DCIN_Pin|EN_SENS12V_Pin|EN_PWR_RS485_Pin|EN_PWR_QMBUS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LTE_PWR_Pin|EN_EV_TOYI_Pin|CTL_EV_TOYI_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, EN_SENS3V3_Pin|EN_PWR_SENS420_Pin|EN_PWR_CPRES_Pin|LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SD_SS_GPIO_Port, SD_SS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(EN_PWR_SD_GPIO_Port, EN_PWR_SD_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : EN_LTE_DCIN_Pin EN_SENS12V_Pin EN_PWR_RS485_Pin EN_PWR_QMBUS_Pin */
  GPIO_InitStruct.Pin = EN_LTE_DCIN_Pin|EN_SENS12V_Pin|EN_PWR_RS485_Pin|EN_PWR_QMBUS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : LTE_PWR_Pin EN_EV_TOYI_Pin CTL_EV_TOYI_Pin SD_SS_Pin */
  GPIO_InitStruct.Pin = LTE_PWR_Pin|EN_EV_TOYI_Pin|CTL_EV_TOYI_Pin|SD_SS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : EN_SENS3V3_Pin EN_PWR_SENS420_Pin EN_PWR_CPRES_Pin EN_PWR_SD_Pin
                           LED_Pin */
  GPIO_InitStruct.Pin = EN_SENS3V3_Pin|EN_PWR_SENS420_Pin|EN_PWR_CPRES_Pin|EN_PWR_SD_Pin
                          |LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : CNT0_Pin */
  GPIO_InitStruct.Pin = CNT0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(CNT0_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SD_DET_Pin */
  GPIO_InitStruct.Pin = SD_DET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(SD_DET_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : TERM_SENSE_Pin */
  GPIO_InitStruct.Pin = TERM_SENSE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(TERM_SENSE_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  ( void ) argument;

  /*
   * CubeMX no permite dejar la lista de tareas vacía, así que defaultTask existe
   * por obligación de la herramienta, no porque haga falta. En vez de dejarla
   * girando —se despertaba cada 1 ms en prioridad 24, muy por encima de tkCtl—
   * se elimina a sí misma apenas arranca el scheduler.
   *
   * Se creó con asignación dinámica, así que la idle task devuelve su stack y su
   * TCB al heap. vTaskDelete(NULL) no retorna.
   */
  vTaskDelete( NULL );

  for( ;; )
  {
  }
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /*
   * Con la placa pelada y sin consola, el LED es el único canal de diagnóstico: en vez
   * de colgarse mudo, parpadea un patrón que dice QUÉ falló.
   *
   *   2 destellos + pausa -> un oscilador de baja velocidad no arrancó. Con el cristal
   *                          de 32.768 kHz: sospechar del cristal, de los condensadores
   *                          de carga o de un LSE Drive Capability demasiado bajo.
   *   5 destellos + pausa -> cualquier otra falla (regulador, PLL, init de un periférico).
   *
   * Se detecta el caso "pedido pero no arrancó": LSEON en 1 con LSERDY en 0. Esto es más
   * confiable que mirar qué fuente tiene seleccionada el RTC, porque cuando el LSE no
   * arranca, HAL_RCC_OscConfig() cae acá por timeout ANTES de que nadie haya seleccionado
   * el mux del RTC — mirando RTCSEL, una falla de cristal se reportaría como genérica.
   *
   * Los flags se leen ANTES de cortar las interrupciones, y el pin se reconfigura acá
   * porque Error_Handler() puede dispararse desde SystemClock_Config(), antes de
   * MX_GPIO_Init().
   */
  uint32_t reloj_caido = 0U;

  if( ( ( RCC->BDCR & RCC_BDCR_LSEON ) != 0U ) &&
      ( ( RCC->BDCR & RCC_BDCR_LSERDY ) == 0U ) )
  {
    reloj_caido = 1U;                                  /* se pidió el LSE y no arrancó */
  }
  else if( ( ( RCC->CSR & RCC_CSR_LSION ) != 0U ) &&
           ( ( RCC->CSR & RCC_CSR_LSIRDY ) == 0U ) )
  {
    reloj_caido = 1U;                                  /* idem con el LSI */
  }

  uint32_t destellos = reloj_caido ? ERR_BLINKS_RELOJ : ERR_BLINKS_GENERIC;

  /*
   * Decirlo por la consola, no sólo por el LED. Desde que la USART se levanta
   * antes de SystemClock_Config(), esto funciona incluso si el que falla es el
   * reloj. Va ANTES de cortar las interrupciones, porque HAL_UART_Transmit()
   * usa HAL_GetTick() para su timeout.
   */
  error_print( reloj_caido
      ? "\r\n[!] Error_Handler: NO ARRANCO UN OSCILADOR DE BAJA VELOCIDAD (LSE/LSI)\r\n"
      : "\r\n[!] Error_Handler: falla generica\r\n" );

  __disable_irq();

  /*
   * Sin esto los destellos salen MAL. Si Error_Handler() se dispara desde adentro
   * de SystemClock_Config(), SystemCoreClock todavía vale 4 MHz en vez de 60, y
   * error_delay_ms() —que con el tick por LPTIM1 usa SIEMPRE el lazo tosco,
   * porque el SysTick nunca se arranca— sale quince veces más rápido: el patrón
   * de 2 destellos se convierte en un parpadeo tan veloz que a ojo parece un LED
   * PRENDIDO FIJO. Costó confundir eso con un cuelgue mudo. SystemCoreClockUpdate()
   * recalcula el valor real leyendo los registros del RCC.
   */
  SystemCoreClockUpdate();

  led_config();

  while (1)
  {
    for( uint32_t i = 0U; i < destellos; i++ )
    {
      HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_SET );
      error_delay_ms( ERR_BLINK_ON_MS );
      HAL_GPIO_WritePin( LED_PORT, LED_PIN, GPIO_PIN_RESET );
      error_delay_ms( ERR_BLINK_OFF_MS );
    }
    error_delay_ms( ERR_PAUSE_MS );
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
