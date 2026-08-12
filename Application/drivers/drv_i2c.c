/*
 * drv_i2c.c  -  ver drv_i2c.h
 */

#include "drv_i2c.h"
#include "pwr_lock.h"
#include "main.h"
#include "semphr.h"

/* El handle lo crea CubeMX en main.c. Es el único punto donde este driver se ata
   a lo generado; de acá para arriba nadie lo ve. */
extern I2C_HandleTypeDef hi2c2;

/* Serializa a los usuarios del bus: el RTC y la EEPROM se leen desde tareas
   distintas y una transacción no se puede entrelazar con otra. */
static SemaphoreHandle_t xMutex   = NULL;
static StaticSemaphore_t xMutexCtrl;

/* Lo da el callback de fin de transacción, o el de error. */
static SemaphoreHandle_t xDone    = NULL;
static StaticSemaphore_t xDoneCtrl;

static volatile uint32_t ulUltimoError = HAL_I2C_ERROR_NONE;

/* Timeout del poleo de drv_i2c_probe(). A 100 kHz una dirección son ~90 us, así
   que 2 ms es holgado hasta para un chip que estire el clock — y multiplicado por
   las 112 direcciones de un escaneo, mantiene el barrido en un cuarto de segundo. */
#define PROBE_TIMEOUT_MS        2U
#define PROBE_INTENTOS          1U

/*------------------------------------------------------------------------------
 * Tronco común de lectura y escritura.
 *
 * Las dos difieren en una sola llamada a la HAL, y todo lo demás —mutex, candado
 * de energía, espera, manejo del timeout— es idéntico. Se comparte para que no
 * haya forma de arreglar un camino y olvidarse del otro.
 *----------------------------------------------------------------------------*/
static int16_t prvTransferir( bool bEsLectura,
                              uint8_t ucDevAddr, uint16_t usMemAddr, uint8_t ucMemAddrLen,
                              char *pvBuffer, uint16_t xBytes, TickType_t xTicksToWait )
{
    if( ( pvBuffer == NULL ) || ( xBytes == 0U ) || ( xMutex == NULL ) )
    {
        return -1;
    }

    if( ( ucMemAddrLen != 1U ) && ( ucMemAddrLen != 2U ) )
    {
        return -1;
    }

    if( xSemaphoreTake( xMutex, xTicksToWait ) != pdTRUE )
    {
        return -1;
    }

    /* Sin esto el tickless puede entrar en Stop 2 apenas la tarea se bloquee en
       xDone, y el I2C2 —que se alimenta de PCLK1— se queda sin reloj a mitad de
       la transacción, con el bus tomado. */
    pwr_lock_acquire( pwrLOCK_I2C );

    /* Puede haber quedado el semáforo de una transacción anterior que venció por
       timeout y completó después. Si no se limpia, ésta terminaría "al instante"
       con datos que todavía no llegaron. */
    ( void ) xSemaphoreTake( xDone, 0U );

    ulUltimoError = HAL_I2C_ERROR_NONE;

    uint16_t usMemAddSize = ( ucMemAddrLen == 1U ) ? I2C_MEMADD_SIZE_8BIT : I2C_MEMADD_SIZE_16BIT;
    int16_t  sRet;

    HAL_StatusTypeDef xEstado = bEsLectura
        ? HAL_I2C_Mem_Read_IT ( &hi2c2, ucDevAddr, usMemAddr, usMemAddSize,
                                ( uint8_t * ) pvBuffer, xBytes )
        : HAL_I2C_Mem_Write_IT( &hi2c2, ucDevAddr, usMemAddr, usMemAddSize,
                                ( uint8_t * ) pvBuffer, xBytes );

    if( xEstado != HAL_OK )
    {
        ulUltimoError = HAL_I2C_GetError( &hi2c2 );
        sRet = -1;
    }
    else if( xSemaphoreTake( xDone, xTicksToWait ) != pdTRUE )
    {
        /*
         * Vencido el plazo sin que ningún callback diera el semáforo. Es el caso
         * feo: la HAL queda en BUSY y de ahí no sale sola, así que todas las
         * transacciones siguientes fallarían. Se reinicializa el periférico.
         *
         * En la práctica esto es una línea trabada en bajo —un esclavo colgado,
         * o SDA a masa—, no un esclavo lento: un NACK no vence el plazo, lo
         * reporta el callback de error enseguida.
         */
        ulUltimoError = HAL_I2C_ERROR_TIMEOUT;
        ( void ) drv_i2c_reset();
        sRet = -1;
    }
    else
    {
        sRet = ( ulUltimoError == HAL_I2C_ERROR_NONE ) ? ( int16_t ) xBytes : -1;
    }

    pwr_lock_release( pwrLOCK_I2C );
    ( void ) xSemaphoreGive( xMutex );

    return sRet;
}

/*------------------------------------------------------------------------------
 * API
 *----------------------------------------------------------------------------*/

bool drv_i2c_init( void )
{
    xMutex = xSemaphoreCreateMutexStatic( &xMutexCtrl );
    xDone  = xSemaphoreCreateBinaryStatic( &xDoneCtrl );

    return ( ( xMutex != NULL ) && ( xDone != NULL ) );
}
//------------------------------------------------------------------------------
int16_t drv_i2c_read( uint8_t ucDevAddr, uint16_t usMemAddr, uint8_t ucMemAddrLen,
                      char *pvBuffer, uint16_t xBytes, TickType_t xTicksToWait )
{
    return prvTransferir( true, ucDevAddr, usMemAddr, ucMemAddrLen,
                          pvBuffer, xBytes, xTicksToWait );
}
//------------------------------------------------------------------------------
int16_t drv_i2c_write( uint8_t ucDevAddr, uint16_t usMemAddr, uint8_t ucMemAddrLen,
                       const char *pvBuffer, uint16_t xBytes, TickType_t xTicksToWait )
{
    /* La HAL no distingue const en el buffer de escritura; el cast es seguro
       porque por ese camino sólo lo lee. */
    return prvTransferir( false, ucDevAddr, usMemAddr, ucMemAddrLen,
                          ( char * ) pvBuffer, xBytes, xTicksToWait );
}
//------------------------------------------------------------------------------
bool drv_i2c_probe( uint8_t ucDevAddr )
{
    if( xMutex == NULL )
    {
        return false;
    }

    if( xSemaphoreTake( xMutex, portMAX_DELAY ) != pdTRUE )
    {
        return false;
    }

    pwr_lock_acquire( pwrLOCK_I2C );

    HAL_StatusTypeDef xEstado = HAL_I2C_IsDeviceReady( &hi2c2, ucDevAddr,
                                                       PROBE_INTENTOS, PROBE_TIMEOUT_MS );
    ulUltimoError = HAL_I2C_GetError( &hi2c2 );

    pwr_lock_release( pwrLOCK_I2C );
    ( void ) xSemaphoreGive( xMutex );

    return ( xEstado == HAL_OK );
}
//------------------------------------------------------------------------------
uint32_t drv_i2c_last_error( void )
{
    return ulUltimoError;
}
//------------------------------------------------------------------------------
bool drv_i2c_reset( void )
{
    /*
     * De-init + init deja el periférico limpio, pero NO destraba un esclavo que
     * esté reteniendo SDA en bajo: para eso hay que generar pulsos de clock a
     * mano con los pines en GPIO hasta que suelte. Si aparece esa falla en banco
     * se agrega acá; hoy no hay evidencia de que haga falta y el código sin
     * validar es deuda.
     */
    if( HAL_I2C_DeInit( &hi2c2 ) != HAL_OK )
    {
        return false;
    }

    if( HAL_I2C_Init( &hi2c2 ) != HAL_OK )
    {
        return false;
    }

    /*
     * Los filtros van aparte y HAL_I2C_Init() NO los repone: MX_I2C2_Init() los
     * aplica en dos llamadas propias, después del Init. Hoy los valores por
     * defecto de CR1 coinciden con los que queremos —analógico encendido,
     * digital en 0—, así que omitir esto "andaría"; pero andaría por casualidad,
     * y dejaría de andar el día que alguien cambie los filtros en CubeMX y no se
     * acuerde de que hay un segundo lugar donde se configuran.
     */
    if( HAL_I2CEx_ConfigAnalogFilter( &hi2c2, I2C_ANALOGFILTER_ENABLE ) != HAL_OK )
    {
        return false;
    }

    return ( HAL_I2CEx_ConfigDigitalFilter( &hi2c2, 0U ) == HAL_OK );
}

/*------------------------------------------------------------------------------
 * Callbacks de la HAL. Pisan las weak del driver, corren en contexto de ISR.
 *
 * Los cuatro hacen lo mismo: destrabar a la tarea que espera. La HAL tiene un
 * callback por sentido y por modo, y si falta alguno la transacción termina en
 * silencio y la tarea se come el timeout entero.
 *----------------------------------------------------------------------------*/

static void prvDespertar( I2C_HandleTypeDef *hi2c )
{
    if( hi2c->Instance != I2C2 )
    {
        return;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    ( void ) xSemaphoreGiveFromISR( xDone, &xHigherPriorityTaskWoken );

    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}

void HAL_I2C_MemTxCpltCallback( I2C_HandleTypeDef *hi2c )   { prvDespertar( hi2c ); }
void HAL_I2C_MemRxCpltCallback( I2C_HandleTypeDef *hi2c )   { prvDespertar( hi2c ); }
void HAL_I2C_MasterTxCpltCallback( I2C_HandleTypeDef *hi2c ){ prvDespertar( hi2c ); }
void HAL_I2C_MasterRxCpltCallback( I2C_HandleTypeDef *hi2c ){ prvDespertar( hi2c ); }

//------------------------------------------------------------------------------
void HAL_I2C_ErrorCallback( I2C_HandleTypeDef *hi2c )
{
    if( hi2c->Instance != I2C2 )
    {
        return;
    }

    /*
     * Se anota el error ANTES de dar el semáforo: si se hiciera al revés, una
     * tarea de prioridad más alta podría leer ulUltimoError antes de que se
     * escribiera y creer que la transacción salió bien.
     *
     * El error más común acá es HAL_I2C_ERROR_AF —un NACK—, que muchas veces no
     * es una falla: es "no hay nadie en esa dirección" durante un escaneo, o la
     * EEPROM ocupada en su ciclo de escritura.
     */
    ulUltimoError = HAL_I2C_GetError( hi2c );

    prvDespertar( hi2c );
}
//------------------------------------------------------------------------------
void HAL_I2C_AbortCpltCallback( I2C_HandleTypeDef *hi2c )
{
    prvDespertar( hi2c );
}
//------------------------------------------------------------------------------
