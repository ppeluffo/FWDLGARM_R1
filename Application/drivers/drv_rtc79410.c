/*
 * drv_rtc79410.c  -  ver drv_rtc79410.h
 */

#include "drv_rtc79410.h"
#include "drv_i2c.h"

/*------------------------------------------------------------------------------
 * Mapa de registros del MCP79410 (bloque 0xDE)
 *----------------------------------------------------------------------------*/
#define REG_RTCSEC          0x00U
#define REG_RTCMIN          0x01U
#define REG_RTCHOUR         0x02U
#define REG_RTCWKDAY        0x03U
#define REG_RTCDATE         0x04U
#define REG_RTCMTH          0x05U
#define REG_RTCYEAR         0x06U
#define REG_CONTROL         0x07U

/* Marcas de tiempo del corte de alimentación: 4 bytes cada una, sin segundos ni
   año. Orden: minutos, horas, día, (día de semana | mes). */
#define REG_PWRDOWN         0x18U   /* cuándo se cayó   */
#define REG_PWRUP           0x1CU   /* cuándo volvió    */

#define REG_SRAM            0x20U   /* 64 bytes, respaldados por la pila */

/* Bits sueltos */
#define BIT_ST              0x80U   /* RTCSEC:   1 = oscilador habilitado        */
#define BIT_12H             0x40U   /* RTCHOUR:  1 = formato 12 horas            */
#define BIT_PM              0x20U   /* RTCHOUR:  1 = PM, pero SÓLO en modo 12h   */
#define BIT_OSCRUN          0x20U   /* RTCWKDAY: 1 = oscilando (sólo lectura)    */
#define BIT_PWRFAIL         0x10U   /* RTCWKDAY: 1 = hubo corte                  */
#define BIT_VBATEN          0x08U   /* RTCWKDAY: 1 = respaldo por pila activo    */
#define MASK_WKDAY          0x07U

#define RTC_TIMEOUT_MS      250U
#define RTC_TICKS           pdMS_TO_TICKS( RTC_TIMEOUT_MS )

/*
 * Firma de "la hora es confiable", en la SRAM respaldada por la pila.
 *
 * Cuatro bytes de magia más uno de versión. Con cuatro bytes, que la SRAM caiga
 * por casualidad en este patrón al arrancar en frío es 1 en 4 mil millones: el
 * modo de falla real es perderlo todo, no que se dé vuelta un bit suelto.
 *
 * La versión está para el día que la aplicación quiera guardar más cosas en la
 * SRAM y necesite saber con qué formato las escribió el firmware anterior.
 */
static const char pcFirma[ 5 ] = { 'S', 'P', 'Q', '\x01', 0x01 };

#define FIRMA_LARGO         ( sizeof( pcFirma ) )

/* La define la sección de validez, al final; la usa drv_rtc_escribir(), acá arriba. */
static bool prvEscribirFirma( void );

/*------------------------------------------------------------------------------
 * BCD. Los registros no son binarios: las 25 se guardan como 0x25.
 *----------------------------------------------------------------------------*/
static inline uint8_t prvDeBcd( uint8_t ucBcd )
{
    return ( uint8_t ) ( ( ( ucBcd >> 4 ) * 10U ) + ( ucBcd & 0x0FU ) );
}

static inline uint8_t prvABcd( uint8_t ucBin )
{
    return ( uint8_t ) ( ( ( ucBin / 10U ) << 4 ) | ( ucBin % 10U ) );
}

//------------------------------------------------------------------------------
uint8_t drv_rtc_dia_de_semana( uint8_t ucAnio2, uint8_t ucMes, uint8_t ucDia )
{
    /* Sakamoto. La tabla son los corrimientos de cada mes respecto de enero; el
       ajuste de enero y febrero es porque el día bisiesto va al final del año
       juliano, no en medio. */
    static const uint8_t ucCorrimiento[ 12 ] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };

    if( ( ucMes < 1U ) || ( ucMes > 12U ) )
    {
        return 1U;
    }

    uint16_t usAnio = ( uint16_t ) ( 2000U + ucAnio2 );

    if( ucMes < 3U )
    {
        usAnio--;
    }

    uint32_t ulDia = ( uint32_t ) usAnio
                   + ( usAnio / 4U ) - ( usAnio / 100U ) + ( usAnio / 400U )
                   + ucCorrimiento[ ucMes - 1U ] + ucDia;

    /* Sakamoto da 0 = domingo; el chip y esta casa usan 1 = domingo. */
    return ( uint8_t ) ( ( ulDia % 7U ) + 1U );
}

/*------------------------------------------------------------------------------
 * Acceso a registros
 *----------------------------------------------------------------------------*/
static bool prvLeerReg( uint8_t ucReg, char *pcDatos, uint8_t ucBytes )
{
    return ( drv_i2c_read( DRV_I2C_ADDR_RTC, ucReg, 1U, pcDatos, ucBytes, RTC_TICKS ) == ( int16_t ) ucBytes );
}

static bool prvEscribirReg( uint8_t ucReg, const char *pcDatos, uint8_t ucBytes )
{
    return ( drv_i2c_write( DRV_I2C_ADDR_RTC, ucReg, 1U, pcDatos, ucBytes, RTC_TICKS ) == ( int16_t ) ucBytes );
}

//------------------------------------------------------------------------------
bool drv_rtc_estado( rtc_estado_t *pxEstado )
{
    char cWkday;

    if( ( pxEstado == NULL ) || ( prvLeerReg( REG_RTCWKDAY, &cWkday, 1U ) == false ) )
    {
        return false;
    }

    pxEstado->bOscilando  = ( ( ( uint8_t ) cWkday & BIT_OSCRUN  ) != 0U );
    pxEstado->bFalloPower = ( ( ( uint8_t ) cWkday & BIT_PWRFAIL ) != 0U );
    pxEstado->bPilaHab    = ( ( ( uint8_t ) cWkday & BIT_VBATEN  ) != 0U );

    return true;
}
//------------------------------------------------------------------------------
bool drv_rtc_init( void )
{
    /*
     * Todo el arranque se hace con el bus tomado: son varias lecturas y
     * escrituras que dependen unas de otras, y si otra tarea escribiera un
     * registro en el medio quedaría un estado inconsistente.
     */
    if( drv_i2c_bus_take( RTC_TICKS ) == false )
    {
        return false;
    }

    bool bOk = false;
    char cVal;

    do
    {
        /* --- Respaldo por pila. Viene DESHABILITADO de fábrica: sin este paso el
               chip pierde la hora al cortar la alimentación, tenga pila o no. --- */
        if( prvLeerReg( REG_RTCWKDAY, &cVal, 1U ) == false )
        {
            break;
        }

        if( ( ( uint8_t ) cVal & BIT_VBATEN ) == 0U )
        {
            /* OSCRUN y PWRFAIL no se tocan: el primero es de sólo lectura y el
               segundo se limpia aparte, a propósito. */
            cVal = ( char ) ( ( uint8_t ) cVal | BIT_VBATEN );

            if( prvEscribirReg( REG_RTCWKDAY, &cVal, 1U ) == false )
            {
                break;
            }
        }

        /* --- Formato de 24 horas. En 12 horas el bit 5 pasa a ser AM/PM y la
               lectura de la hora daría cualquier cosa. --- */
        if( prvLeerReg( REG_RTCHOUR, &cVal, 1U ) == false )
        {
            break;
        }

        if( ( ( uint8_t ) cVal & BIT_12H ) != 0U )
        {
            /* Se convierte el valor, no se borra el bit y listo: si estaba en 12
               horas, el campo tiene 1..12 más AM/PM y hay que traducirlo. */
            uint8_t ucHora12 = prvDeBcd( ( uint8_t ) cVal & 0x1FU );
            bool    bPm      = ( ( ( uint8_t ) cVal & BIT_PM ) != 0U );
            uint8_t ucHora24 = ( ucHora12 % 12U ) + ( bPm ? 12U : 0U );

            cVal = ( char ) prvABcd( ucHora24 );

            if( prvEscribirReg( REG_RTCHOUR, &cVal, 1U ) == false )
            {
                break;
            }
        }

        /* --- Oscilador. De fábrica viene DETENIDO. --- */
        if( prvLeerReg( REG_RTCSEC, &cVal, 1U ) == false )
        {
            break;
        }

        if( ( ( uint8_t ) cVal & BIT_ST ) == 0U )
        {
            cVal = ( char ) ( ( uint8_t ) cVal | BIT_ST );

            if( prvEscribirReg( REG_RTCSEC, &cVal, 1U ) == false )
            {
                break;
            }
        }

        bOk = true;

    } while( false );

    drv_i2c_bus_give();

    return bOk;
}
//------------------------------------------------------------------------------
bool drv_rtc_leer( RtcTimeType_t *pxHora )
{
    if( pxHora == NULL )
    {
        return false;
    }

    if( drv_i2c_bus_take( RTC_TICKS ) == false )
    {
        return false;
    }

    char cBuf[ 7 ];
    bool bOk = false;

    /*
     * Se lee, se relee el segundero y si cambió se vuelve a leer todo.
     *
     * Sin esto, un acarreo que caiga entre el byte de los minutos y el de las
     * horas devuelve una hora imposible: las 10:59:59 leídas como las 11:59:59.
     * Pasa una vez por hora como mucho, es mudo, y en un datalogger aparece meses
     * después como una muestra fuera de lugar que nadie sabe explicar.
     *
     * Dos vueltas alcanzan: la ventana de riesgo es el ~1 ms que dura la lectura,
     * y dos acarreos seguidos dentro de esa ventana no existen.
     */
    for( uint32_t i = 0U; i < 2U; i++ )
    {
        char cSegundosDespues;

        if( prvLeerReg( REG_RTCSEC, cBuf, 7U ) == false )
        {
            break;
        }

        if( prvLeerReg( REG_RTCSEC, &cSegundosDespues, 1U ) == false )
        {
            break;
        }

        if( cSegundosDespues == cBuf[ 0 ] )
        {
            bOk = true;
            break;
        }
    }

    drv_i2c_bus_give();

    if( bOk == false )
    {
        return false;
    }

    /* Cada registro trae bits de control además del valor; hay que enmascararlos
       ANTES de convertir de BCD o salen números disparatados. */
    pxHora->sec     = prvDeBcd( ( uint8_t ) cBuf[ 0 ] & 0x7FU );   /* sin ST      */
    pxHora->min     = prvDeBcd( ( uint8_t ) cBuf[ 1 ] & 0x7FU );
    pxHora->hour    = prvDeBcd( ( uint8_t ) cBuf[ 2 ] & 0x3FU );   /* sin 12/24   */
    pxHora->weekDay = ( uint8_t ) cBuf[ 3 ] & MASK_WKDAY;          /* sin OSCRUN/PWRFAIL/VBATEN */
    pxHora->day     = prvDeBcd( ( uint8_t ) cBuf[ 4 ] & 0x3FU );
    pxHora->month   = prvDeBcd( ( uint8_t ) cBuf[ 5 ] & 0x1FU );   /* sin LPYR    */
    pxHora->year    = prvDeBcd( ( uint8_t ) cBuf[ 6 ] );

    return true;
}
//------------------------------------------------------------------------------
bool drv_rtc_escribir( const RtcTimeType_t *pxHora )
{
    if( pxHora == NULL )
    {
        return false;
    }

    /* El día de la semana NO se valida porque no se usa: se calcula abajo. */
    if( ( pxHora->sec > 59U ) || ( pxHora->min > 59U ) || ( pxHora->hour > 23U ) ||
        ( pxHora->day < 1U ) || ( pxHora->day > 31U ) ||
        ( pxHora->month < 1U ) || ( pxHora->month > 12U ) ||
        ( pxHora->year > 99U ) )
    {
        return false;
    }

    if( drv_i2c_bus_take( RTC_TICKS ) == false )
    {
        return false;
    }

    bool bOk = false;
    char cVal;
    char cBuf[ 6 ];

    do
    {
        /*
         * Secuencia del datasheet: parar el oscilador, escribir, arrancarlo.
         *
         * Escribir con el reloj corriendo puede caer justo en un acarreo, y
         * entonces el chip incrementa un registro que acabamos de fijar. Parar
         * primero cuesta los pocos milisegundos que dura la escritura.
         */
        cVal = 0;   /* ST = 0 y segundos en 0: detiene el oscilador */

        if( prvEscribirReg( REG_RTCSEC, &cVal, 1U ) == false )
        {
            break;
        }

        /* Hay que preservar VBATEN, que vive mezclado con el día de la semana. */
        if( prvLeerReg( REG_RTCWKDAY, &cVal, 1U ) == false )
        {
            break;
        }

        cBuf[ 0 ] = ( char ) prvABcd( pxHora->min );
        cBuf[ 1 ] = ( char ) prvABcd( pxHora->hour );          /* bit 12/24 en 0 */
        /* El día de la semana se CALCULA de la fecha, no se copia de lo que
           mandó el llamador: es un dato derivado, y uno derivado que se ingresa
           a mano termina mal tarde o temprano. Ver drv_rtc_dia_de_semana(). */
        uint8_t ucDs = drv_rtc_dia_de_semana( pxHora->year, pxHora->month, pxHora->day );

        cBuf[ 2 ] = ( char ) ( ( ( uint8_t ) cVal & BIT_VBATEN ) | ( ucDs & MASK_WKDAY ) );
        cBuf[ 3 ] = ( char ) prvABcd( pxHora->day );
        cBuf[ 4 ] = ( char ) prvABcd( pxHora->month );         /* LPYR es de sólo lectura */
        cBuf[ 5 ] = ( char ) prvABcd( pxHora->year );

        if( prvEscribirReg( REG_RTCMIN, cBuf, 6U ) == false )
        {
            break;
        }

        /* Los segundos van al final, y con ST: es lo que vuelve a largar el
           oscilador, así que el reloj arranca justo en el valor pedido. */
        cVal = ( char ) ( BIT_ST | prvABcd( pxHora->sec ) );

        if( prvEscribirReg( REG_RTCSEC, &cVal, 1U ) == false )
        {
            break;
        }

        /*
         * Y recién ahora la firma. El orden importa: si se escribiera primero y
         * fallara la puesta en hora, el equipo quedaría afirmando que una hora
         * incorrecta es confiable, que es el único desenlace realmente malo.
         * Al revés lo peor que pasa es un arranque frío de más.
         */
        bOk = prvEscribirFirma();

    } while( false );

    drv_i2c_bus_give();

    return bOk;
}
//------------------------------------------------------------------------------
bool drv_rtc_leer_falla_power( bool bCaida, RtcTimeType_t *pxHora )
{
    if( pxHora == NULL )
    {
        return false;
    }

    char cBuf[ 4 ];

    if( prvLeerReg( bCaida ? REG_PWRDOWN : REG_PWRUP, cBuf, 4U ) == false )
    {
        return false;
    }

    /* Estos registros NO traen segundos ni año: el chip guarda sólo minutos,
       horas, día y mes. Se dejan en 0 para que quede claro que no se midieron. */
    pxHora->sec     = 0U;
    pxHora->year    = 0U;
    pxHora->min     = prvDeBcd( ( uint8_t ) cBuf[ 0 ] & 0x7FU );
    pxHora->hour    = prvDeBcd( ( uint8_t ) cBuf[ 1 ] & 0x3FU );
    pxHora->day     = prvDeBcd( ( uint8_t ) cBuf[ 2 ] & 0x3FU );
    pxHora->month   = prvDeBcd( ( uint8_t ) cBuf[ 3 ] & 0x1FU );
    pxHora->weekDay = ( uint8_t ) ( ( ( uint8_t ) cBuf[ 3 ] >> 5 ) & MASK_WKDAY );

    return true;
}
//------------------------------------------------------------------------------
bool drv_rtc_limpiar_falla_power( void )
{
    /*
     * Escribir un 0 en PWRFAIL baja la bandera Y borra las dos marcas de tiempo
     * de una vez: son un solo juego de registros que el chip vuelve a llenar en
     * el próximo corte. Por eso hay que leerlas ANTES de limpiar.
     */
    char cVal;

    if( drv_i2c_bus_take( RTC_TICKS ) == false )
    {
        return false;
    }

    bool bOk = false;

    if( prvLeerReg( REG_RTCWKDAY, &cVal, 1U ) )
    {
        cVal = ( char ) ( ( uint8_t ) cVal & ~BIT_PWRFAIL );
        bOk  = prvEscribirReg( REG_RTCWKDAY, &cVal, 1U );
    }

    drv_i2c_bus_give();

    return bOk;
}
//------------------------------------------------------------------------------
int16_t drv_rtc_sram_leer( uint8_t ucAddr, char *pvBuffer, uint8_t ucBytes )
{
    if( ( ( uint32_t ) ucAddr + ucBytes ) > DRV_RTC_SRAM_SIZE )
    {
        return -1;
    }

    return drv_i2c_read( DRV_I2C_ADDR_RTC, ( uint16_t ) ( REG_SRAM + ucAddr ), 1U,
                         pvBuffer, ucBytes, RTC_TICKS );
}
//------------------------------------------------------------------------------
int16_t drv_rtc_sram_escribir( uint8_t ucAddr, const char *pvBuffer, uint8_t ucBytes )
{
    /* La zona de la firma no se toca desde acá: si la aplicación la pisara sin
       querer, el equipo pasaría a creer que la hora es basura —o peor, a creer
       que es buena cuando no lo es— y el síntoma aparecería en otro lado. */
    if( ucAddr < DRV_RTC_SRAM_USUARIO )
    {
        return -1;
    }

    if( ( ( uint32_t ) ucAddr + ucBytes ) > DRV_RTC_SRAM_SIZE )
    {
        return -1;
    }

    /* La SRAM no tiene ciclo de escritura ni páginas: es RAM de verdad, se
       escribe de corrido y contesta enseguida. Nada del baile de la EEPROM. */
    return drv_i2c_write( DRV_I2C_ADDR_RTC, ( uint16_t ) ( REG_SRAM + ucAddr ), 1U,
                          pvBuffer, ucBytes, RTC_TICKS );
}

/*------------------------------------------------------------------------------
 * Firma de validez
 *----------------------------------------------------------------------------*/

/* Interna: escribe la firma sin pasar por el guardarraíl de drv_rtc_sram_escribir. */
static bool prvEscribirFirma( void )
{
    return ( drv_i2c_write( DRV_I2C_ADDR_RTC, REG_SRAM, 1U,
                            pcFirma, FIRMA_LARGO, RTC_TICKS ) == ( int16_t ) FIRMA_LARGO );
}

//------------------------------------------------------------------------------
rtc_validez_t drv_rtc_validez( void )
{
    char cLeido[ FIRMA_LARGO ];

    if( drv_rtc_sram_leer( 0U, cLeido, FIRMA_LARGO ) != ( int16_t ) FIRMA_LARGO )
    {
        return rtcHORA_SIN_RTC;
    }

    for( uint32_t i = 0U; i < FIRMA_LARGO; i++ )
    {
        if( cLeido[ i ] != pcFirma[ i ] )
        {
            return rtcHORA_ARRANQUE_FRIO;
        }
    }

    /*
     * La firma está, pero falta un chequeo que no cuesta nada: si el oscilador
     * NO está corriendo, la hora está congelada por más que la SRAM se haya
     * conservado. Es un caso raro —alguien bajó ST a mano— pero devolver
     * "válida" ahí sería mentir.
     */
    rtc_estado_t xEstado;

    if( drv_rtc_estado( &xEstado ) == false )
    {
        return rtcHORA_SIN_RTC;
    }

    return xEstado.bOscilando ? rtcHORA_VALIDA : rtcHORA_ARRANQUE_FRIO;
}
//------------------------------------------------------------------------------
bool drv_rtc_invalidar( void )
{
    char cCeros[ FIRMA_LARGO ] = { 0 };

    return ( drv_i2c_write( DRV_I2C_ADDR_RTC, REG_SRAM, 1U,
                            cCeros, FIRMA_LARGO, RTC_TICKS ) == ( int16_t ) FIRMA_LARGO );
}
//------------------------------------------------------------------------------
