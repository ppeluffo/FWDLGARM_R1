/*
 * drv_rtc79410.h
 *
 * RTC externo **MCP79410** en el bus I2C2, dirección de dispositivo `0xDE`.
 *
 * Es el mismo integrado que usaba FWDLGX en el AVR, así que la estructura de
 * tiempo se conserva byte a byte (`RtcTimeType_t`) para que el código de
 * aplicación heredado se porte sin tocarlo. Lo que cambia es lo de abajo: allá
 * eran registros del AVR, acá es `drv_i2c` sobre la HAL.
 *
 * El chip aparece DOS veces en el bus, y no es un error:
 *   `0xDE` (7 bits: 6f) -> reloj, SRAM de 64 bytes, alarmas
 *   `0xAE` (7 bits: 57) -> una EEPROM propia de 128 bytes y el número de serie
 * Este driver habla con el primero.
 *
 * ---------------------------------------------------------------------------
 * TODO EN BCD
 *
 * Los registros no son binarios: las 25 son 0x25, no 0x19. Un `hora + 1` sobre el
 * valor crudo da 0x26, que en BCD son las 26 — una hora que no existe. La
 * conversión se hace acá adentro y hacia afuera todo es binario común.
 *
 * ---------------------------------------------------------------------------
 * LOS TRES BITS QUE HAY QUE MIRAR SÍ O SÍ
 *
 * Viven en el registro RTCWKDAY (0x03), mezclados con el día de la semana:
 *
 *   OSCRUN  (bit 5, sólo lectura) 1 = el cristal está oscilando. Si es 0, el
 *           reloj NO está contando y la hora que devuelva es basura congelada.
 *   PWRFAIL (bit 4) 1 = se cortó la alimentación principal en algún momento. El
 *           chip guarda ADEMÁS la marca de tiempo de cuándo se cayó y cuándo
 *           volvió, en 0x18..0x1F. Es información de campo, no sólo de banco.
 *   VBATEN  (bit 3) 1 = habilitado el respaldo por pila. **Viene en 0 de
 *           fábrica**: si nadie lo pone en 1, el chip pierde la hora al cortar
 *           la alimentación aunque la pila esté soldada. Lo pone drv_rtc_init().
 *
 * Y el ST (bit 7 de RTCSEC, 0x00): el oscilador arranca detenido de fábrica. Hay
 * que ponerlo en 1 una vez. También lo hace drv_rtc_init().
 */

#ifndef APPLICATION_DRIVERS_DRV_RTC79410_H_
#define APPLICATION_DRIVERS_DRV_RTC79410_H_

#include <stdbool.h>
#include <stdint.h>

/* Misma estructura que FWDLGX, mismos campos y mismo orden. Todo en BINARIO:
   la conversión desde y hacia BCD es interna al driver.
   `year` son los dos últimos dígitos (0..99); el chip no tiene siglo. */
typedef struct {
    uint8_t sec;        /* 0..59 */
    uint8_t min;        /* 0..59 */
    uint8_t hour;       /* 0..23 - siempre 24 horas, el driver fuerza el modo */
    uint8_t weekDay;    /* 1..7, lo lleva el chip pero no lo calcula solo */
    uint8_t day;        /* 1..31 */
    uint8_t month;      /* 1..12 */
    uint8_t year;       /* 0..99 */
} RtcTimeType_t;

/* Estado del chip, para diagnóstico y para decidir si la hora sirve. */
typedef struct {
    bool bOscilando;    /* OSCRUN  */
    bool bFalloPower;   /* PWRFAIL */
    bool bPilaHab;      /* VBATEN  */
} rtc_estado_t;

/*
 * Arranca el oscilador si está detenido, habilita el respaldo por pila y fuerza
 * el modo de 24 horas. Es idempotente: si ya está todo bien, no escribe nada.
 *
 * Devuelve false si el chip no contesta. Que devuelva true NO garantiza que la
 * hora sea válida — para eso está drv_rtc_estado().
 */
bool drv_rtc_init( void );

bool drv_rtc_estado( rtc_estado_t *pxEstado );

/*
 * Lee la fecha y hora.
 *
 * Lee DOS VECES y compara los segundos: si el reloj avanza justo entre el byte
 * de los minutos y el de las horas, quedaría una hora imposible (las 10:59:59
 * leídas como las 11:59:59). Es un error de una vez por hora, silencioso, y
 * carísimo de encontrar en los datos meses después.
 */
bool drv_rtc_leer( RtcTimeType_t *pxHora );

/*
 * Fija la fecha y hora, siguiendo la secuencia que pide el datasheet: parar el
 * oscilador, escribir, arrancarlo de nuevo. Escribir los registros con el reloj
 * corriendo puede caer justo en un acarreo y dejar la hora corrida.
 */
bool drv_rtc_escribir( const RtcTimeType_t *pxHora );

/* Marca de tiempo del último corte de alimentación. `bCaida` elige entre cuándo
   se cayó y cuándo volvió. Sólo tienen sentido si PWRFAIL está en 1.
   El chip no guarda ni segundos ni año en estos registros. */
bool drv_rtc_leer_falla_power( bool bCaida, RtcTimeType_t *pxHora );

/* Baja PWRFAIL y borra las marcas de tiempo, para poder detectar el próximo corte. */
bool drv_rtc_limpiar_falla_power( void );

/* SRAM de 64 bytes respaldada por la pila: 0x00..0x3F en direcciones relativas.
   Es el lugar natural para una firma de "la configuración es válida". */
#define DRV_RTC_SRAM_SIZE       64U

int16_t drv_rtc_sram_leer   ( uint8_t ucAddr, char *pvBuffer, uint8_t ucBytes );
int16_t drv_rtc_sram_escribir( uint8_t ucAddr, const char *pvBuffer, uint8_t ucBytes );

#endif /* APPLICATION_DRIVERS_DRV_RTC79410_H_ */
