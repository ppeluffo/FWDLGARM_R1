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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>

#include "tkCtl.h"
#include "tkCmd.h"
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

/*
 * ---------------------------------------------------------------------------
 * PRUEBA_UART  (2026-09-30) — el test más puro posible del serial
 * ---------------------------------------------------------------------------
 * En 1, main() se desvía acá antes de inicializar nada y NO vuelve. Contesta
 * dos preguntas y nada más:
 *
 *   ¿sale el TX?  -> emite una línea con contador una vez por segundo
 *   ¿entra el RX?  -> hace ECO de cada byte que llega
 *
 * Deliberadamente NO usa: FreeRTOS, el tickless, TERM_SENSE, los candados de
 * energía, drv_uart, FRTOS-IO, ni una sola interrupción. Poleo directo sobre
 * los registros del USART. Si acá el serial no anda, no hay firmware que
 * culpar: es el pin, el conector, el cable o el adaptador.
 *
 * ⭐ El contador sirve para ver si se PIERDEN líneas, y el alfabeto y los
 * dígitos para ver si los bytes se corrompen. Un baudrate mal calculado se
 * manifiesta como basura, no como silencio — son dos síntomas distintos.
 *
 * ⛔ El clear de los flags de error no es decorativo: con un ORE pegado el
 * RXNE deja de levantarse y **el eco muere en silencio**. Sin limpiarlos, un
 * solo overrun al principio parecería un RX roto para siempre.
 */
#define PRUEBA_UART             0     /* 0 = operación normal */

#define PU_PATRON_MS         1000U

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
    /* período de la línea de prueba */

#define ERR_BLINK_ON_MS       120U
#define ERR_BLINK_OFF_MS      200U
#define ERR_PAUSE_MS         1200U
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
static void MX_ADC1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_I2C2_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_SPI3_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */
static void led_config( void );
static void error_delay_ms( uint32_t ms );
#if ( PRUEBA_UART == 1 )
static void prvPruebaUart( void );
#endif
#if ( PATRON_CONSUMO == 1 )
static void prvPatronConsumo( void );
#endif
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
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

#if ( PRUEBA_UART == 1 )
/* Ver el comentario del #define PRUEBA_UART, más arriba. No retorna. */
static void prvPruebaUart( void )
{
    char           cLinea[ 80 ];
    uint32_t       ulLinea   = 0U;
    uint32_t       ulProxima = 0U;
    USART_TypeDef *pxUsart;

    /* El reloj va acá porque esta función se desvía ANTES de que main() llegue
       a SystemClock_Config(). Es la misma llamada: 60 MHz, y de ahí sale el
       divisor de 9600 del USART. */
    SystemClock_Config();

    /* La USART y sus pines. El MspInit configura PB6/PB7 en AF7 y habilita el
       reloj del periférico; no hace falta MX_GPIO_Init() para esto. */
    MX_USART1_UART_Init();

    led_config();

    pxUsart = huart1.Instance;

    /* ⚠ El largo va con strlen() y no con un número: el primer intento puso 46
       donde eran 49 y el banner habría salido cortado. Un largo escrito a mano
       es un bug esperando a que alguien cambie el texto. */
    static const char cBanner[] = "\r\n\r\n== PRUEBA_UART: TX cada 1 s + ECO del RX ==\r\n";

    ( void ) HAL_UART_Transmit( &huart1, ( const uint8_t * ) cBanner,
                                ( uint16_t ) strlen( cBanner ), HAL_MAX_DELAY );

    for( ;; )
    {
        /*
         * ---- 1. EL ECO, lo primero y en cada vuelta ----------------------
         *
         * Va antes que el patrón y sin ninguna espera en el medio: así el byte
         * se devuelve en cuanto llega y no hay ventana para perderlo.
         */
        if( ( pxUsart->ISR & USART_ISR_RXNE ) != 0U )
        {
            uint32_t ulRx = pxUsart->RDR;          /* leer RDR limpia RXNE */

            while( ( pxUsart->ISR & USART_ISR_TXE ) == 0U )
            {
            }
            pxUsart->TDR = ulRx;
        }

        /*
         * ---- 2. Limpiar los errores --------------------------------------
         *
         * ⛔ Con un ORE pegado el RXNE no vuelve a levantarse y el eco muere
         * para siempre. Un solo overrun —dos bytes seguidos mientras el TX del
         * patrón estaba ocupado— parecería un RX roto.
         */
        if( ( pxUsart->ISR & ( USART_ISR_ORE | USART_ISR_FE |
                               USART_ISR_NE  | USART_ISR_PE ) ) != 0U )
        {
            pxUsart->ICR = USART_ICR_ORECF | USART_ICR_FECF |
                           USART_ICR_NECF  | USART_ICR_PECF;
        }

        /*
         * ---- 3. El patrón, una vez por segundo ---------------------------
         *
         * HAL_GetTick() sirve acá porque sin FreeRTOS el timebase de la HAL
         * (TIM6) corre normalmente: no hay tickless que lo suspenda.
         */
        if( ( int32_t ) ( HAL_GetTick() - ulProxima ) >= 0 )
        {
            ulProxima = HAL_GetTick() + PU_PATRON_MS;

            int n = snprintf( cLinea, sizeof( cLinea ),
                              "TX %06lu ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789\r\n",
                              ( unsigned long ) ulLinea++ );

            if( n > 0 )
            {
                ( void ) HAL_UART_Transmit( &huart1, ( const uint8_t * ) cLinea,
                                            ( uint16_t ) n, HAL_MAX_DELAY );
            }

            /* Cambia en cada línea: el LED late a 0,5 Hz mientras esto corre. */
            HAL_GPIO_TogglePin( LED_PORT, LED_PIN );
        }
    }
}
#endif /* PRUEBA_UART */

/*
 * Configura el pin del LED. Se llama desde Error_Handler() además del arranque,
 * porque una falla puede dispararse ANTES de MX_GPIO_Init().
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
 * Espera por sondeo, para los destellos de diagnóstico.
 *
 * ⚠ Con el tick del kernel en el LPTIM1 el SysTick NUNCA se arranca, así que en
 * la práctica se usa siempre la rama del lazo tosco. El contador es volatile
 * para que el compilador no lo elimine con -Os.
 *
 * ⚠ El lazo se calibra con SystemCoreClock: quien lo llame después de un cambio
 * de reloj tiene que haber corrido SystemCoreClockUpdate() antes, o los tiempos
 * salen hasta quince veces más rápidos. Ver Error_Handler().
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
#if ( PATRON_CONSUMO == 1 )
  /* Se va acá y NO VUELVE, antes de que se inicialice nada: levantar la USART
     dejaría PB6/PB7 en alterno y el periférico encendido, que es exactamente lo
     que este ensayo no quiere tener prendido. */
  prvPatronConsumo();
#endif

#if ( PRUEBA_UART == 1 )
  /* Se va acá y no vuelve. Ver el comentario del #define PRUEBA_UART. */
  prvPruebaUart();
#endif
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_RTC_Init();
  MX_LPTIM1_Init();
  MX_ADC1_Init();
  MX_USART1_UART_Init();
  MX_I2C2_Init();
  MX_USART3_UART_Init();
  MX_SPI3_Init();
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
  /* La única tarea del firmware, con la API nativa de FreeRTOS (no el wrapper
     CMSIS) y memoria estática: no toca el heap. */
  xHandle_tkCtl = xTaskCreateStatic( tkCtl,
                                     "CTL",
                                     tkCtl_STACK_SIZE,  /* en PALABRAS, no bytes */
                                     NULL,
                                     tkCtl_PRIORITY,
                                     tkCtl_Stack,
                                     &tkCtl_TCB );
  if ( xHandle_tkCtl == NULL )
  {
    Error_Handler();
  }

  /* La consola. Abre los drivers de FRTOS-IO desde adentro de la tarea, porque
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
  sConfig.Channel = ADC_CHANNEL_VREFINT;
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
  hspi3.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI3_Init 2 */

  /* USER CODE END SPI3_Init 2 */

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
  HAL_GPIO_WritePin(GPIOA, EN_EV_TOYI_Pin|CTL_EV_TOYI_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, EN_SENS12V_Pin|EN_PWR_RS485_Pin|EN_PWR_QMBUS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, EN_PWR_SENS420_Pin|EN_PWR_CPRES_Pin|LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SD_SS_GPIO_Port, SD_SS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(EN_PWR_SD_GPIO_Port, EN_PWR_SD_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : EN_EV_TOYI_Pin CTL_EV_TOYI_Pin SD_SS_Pin */
  GPIO_InitStruct.Pin = EN_EV_TOYI_Pin|CTL_EV_TOYI_Pin|SD_SS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : EN_SENS12V_Pin EN_PWR_RS485_Pin EN_PWR_QMBUS_Pin */
  GPIO_InitStruct.Pin = EN_SENS12V_Pin|EN_PWR_RS485_Pin|EN_PWR_QMBUS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : EN_PWR_SENS420_Pin EN_PWR_CPRES_Pin EN_PWR_SD_Pin LED_Pin */
  GPIO_InitStruct.Pin = EN_PWR_SENS420_Pin|EN_PWR_CPRES_Pin|EN_PWR_SD_Pin|LED_Pin;
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

  /* ⏳ Este firmware todavía no tiene consola, así que el LED es el ÚNICO canal
     de diagnóstico. Cuando entre la USART hay que reponer acá el aviso por
     texto: es lo que convirtió "un cuelgue mudo" en un mensaje legible. */

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
