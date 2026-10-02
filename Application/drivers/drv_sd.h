/*
 * drv_sd.h
 *
 * Tarjeta microSD por SPI3 (PC10 SCK, PC11 MISO, PC12 MOSI, PA15 CS), con la
 * energía conmutada por `EN_PWR_SD` (PB3) y detección de presencia por `SD_DET`
 * (PD2).
 *
 * Es el driver de la TARJETA, no del sistema de archivos: mueve sectores de 512
 * bytes. FatFs va una capa más arriba y se engancha por `user_diskio.c`.
 *
 * ---------------------------------------------------------------------------
 * ⚠ `EN_PWR_SD` ES DE LÓGICA INVERTIDA — al revés que los rieles del RS485
 *
 * PB3 maneja el gate de un **SI2301, que es un MOSFET de canal P** usado como
 * switch de lado alto, y tiene un **pull-up de 100 kΩ a 3V3**:
 *
 *      PB3 = 0        -> el MOSFET conduce -> tarjeta ALIMENTADA
 *      PB3 = 1 o Hi-Z -> el MOSFET corta    -> tarjeta apagada
 *
 * El pull-up es lo que garantiza que **durante el reset, y antes de que el
 * firmware configure el pin, la tarjeta esté apagada**. O sea que el estado
 * seguro está puesto por el hardware y el firmware no tiene que llegar a tiempo
 * a nada.
 *
 * De ahí sale una trampa concreta en CubeMX: el *Output Level* por defecto es
 * **Low**, y acá Low significa PRENDER. Tiene que quedar en **High**.
 *
 * ---------------------------------------------------------------------------
 * POR QUÉ SE LE CORTA LA ENERGÍA, Y QUÉ ARRASTRA ESO
 *
 * Una microSD consume **0,2 a 1 mA en idle** y hasta 100 mA en un pico de
 * escritura. Contra los ~5 µA del micro dormido son tres órdenes de magnitud: si
 * queda alimentada, **la tarjeta sola define la autonomía del equipo** y todo el
 * trabajo del tickless no sirve de nada. Peor, una tarjeta "quieta" puede seguir
 * haciendo recolección de basura interna cientos de ms después de un write.
 *
 * Cortarla tiene tres consecuencias que no son opcionales:
 *
 * 1. **Hay que reinicializarla en cada ciclo.** Al cortar pierde todo su estado,
 *    así que `drv_sd_arrancar()` hay que llamarlo de nuevo: 74 clocks, CMD0,
 *    ACMD41 hasta que salga de idle. Cuesta de decenas a cientos de ms.
 * 2. **Por eso la política NO puede ser "escribir cada muestra".** Hay que
 *    acumular en RAM y volcar de a bloques; cuántas muestras se toleran perder
 *    define el tamaño del buffer. Eso es de la capa de aplicación.
 * 3. **Los pines tienen que quedar en alta impedancia al apagar.** Si el micro
 *    deja SCK, MOSI o CS en alto, **le inyecta corriente a la tarjeta por los
 *    diodos de protección y el corte no corta nada**: se mide 0 V en el riel y
 *    la tarjeta sigue comiendo. Es un clásico y cuesta verlo con el tester. Este
 *    driver los pasa a modo ANALÓGICO, que es el de menor fuga del STM32.
 *
 * ---------------------------------------------------------------------------
 * EL RELOJ SE MUEVE EN DOS TIEMPOS
 *
 * La norma SD exige inicializar **entre 100 y 400 kHz**; recién cuando la tarjeta
 * salió de idle se puede subir. Por eso el `.ioc` deja SPI3 con prescaler 256
 * (60 MHz / 256 = 234 kHz) y `drv_sd_arrancar()` lo sube a `DRV_SD_BR_RAPIDO`
 * cuando terminó. Si alguna vez una tarjeta falla de forma errática al leer pero
 * inicializa bien, **bajar ese prescaler es lo primero que hay que probar**.
 *
 * ---------------------------------------------------------------------------
 * EL CS VA POR SOFTWARE, Y NO ES UN CAPRICHO
 *
 * El NSS por hardware suelta el pin entre transferencias. El protocolo SD-SPI
 * necesita lo contrario: mantener el CS **bajo** a lo largo de varias
 * transferencias seguidas (comando, respuesta, token, 512 bytes, CRC) y **alto**
 * durante los 74 pulsos de reloj iniciales. Con NSS por hardware el bring-up no
 * arranca. Por eso PA15 es un GPIO común y el `.ioc` tiene NSS en `Disable`.
 */

#ifndef APPLICATION_DRIVERS_DRV_SD_H_
#define APPLICATION_DRIVERS_DRV_SD_H_

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#define DRV_SD_SECTOR_BYTES     512U

/* Prescaler del SPI una vez inicializada la tarjeta: 60 MHz / 8 = 7,5 MHz. Es
   conservador a propósito —la norma admite hasta 25 MHz— porque en esta etapa
   interesa que ande, no que corra. */
#define DRV_SD_BR_RAPIDO        SPI_BAUDRATEPRESCALER_8

typedef enum {
    sdTIPO_NINGUNA = 0,
    sdTIPO_MMC,             /* MMC v3 - direcciona por byte                  */
    sdTIPO_SDV1,            /* SD v1  - direcciona por byte                  */
    sdTIPO_SDV2,            /* SD v2 de capacidad estándar, por byte         */
    sdTIPO_SDHC             /* SDHC/SDXC - direcciona por BLOQUE             */
} sd_tipo_t;

/*------------------------------------------------------------------------------
 * Arranque del driver. NO toca la tarjeta: sólo deja el riel apagado y los pines
 * del bus en alta impedancia, que es el estado de reposo.
 *----------------------------------------------------------------------------*/
bool drv_sd_init( void );

/*------------------------------------------------------------------------------
 * Energía de la tarjeta. Prender toma `pwrLOCK_SD` y baja los pines a su función
 * alternada; apagar hace lo simétrico y lo suelta.
 *
 * Prender NO inicializa la tarjeta: para eso está `drv_sd_arrancar()`.
 *----------------------------------------------------------------------------*/
void drv_sd_power( bool bOn );
bool drv_sd_power_estado( void );

/*
 * ¿Hay tarjeta en la ranura? El contacto va a GND cuando la hay.
 *
 * ⚠ **SÓLO VALE CON EL RIEL ENCENDIDO. Con el riel apagado devuelve false.**
 *
 * No es una limitación, es la decisión de diseño: el pull-up del pin de detección
 * vive y muere con el riel, porque dejarlo fijo costaba **89 µA medidos con sólo
 * insertar la tarjeta**, sin encenderla siquiera (ver `prvPinesBus()` en el `.c`).
 *
 * Y no se pierde nada, porque **saber si hay tarjeta sólo sirve justo antes de
 * usarla, y para usarla hay que prenderla igual**. El orden correcto es siempre:
 *
 *      drv_sd_power( true );  ->  drv_sd_presente();  ->  drv_sd_arrancar();
 *
 * Preguntar con el riel apagado no habilita ninguna decisión que el equipo pueda
 * tomar: es el mismo criterio por el que `TERM_SENSE` se polea en vez de ir por
 * EXTI.
 */
bool drv_sd_presente( void );

/*------------------------------------------------------------------------------
 * La secuencia de inicialización de la norma SD: 74 clocks con CS alto, CMD0,
 * CMD8, ACMD41 hasta que salga de idle, CMD58 para saber si direcciona por
 * bloque, y CMD16 en las que direccionan por byte. Al terminar sube el reloj.
 *
 * Hay que llamarla DESPUÉS de `drv_sd_power(true)` y cada vez que se le haya
 * cortado la energía. Devuelve false si no hay tarjeta o si no contestó.
 *----------------------------------------------------------------------------*/
bool drv_sd_arrancar( void );

sd_tipo_t   drv_sd_tipo( void );
const char *drv_sd_tipo_texto( void );

/* Cantidad de sectores de 512 bytes, sacada de la CSD. 0 si no se pudo leer. */
uint32_t drv_sd_sectores( void );

/*------------------------------------------------------------------------------
 * Un sector de 512 bytes. `ulSector` es SIEMPRE un número de sector: la
 * diferencia entre las tarjetas que direccionan por byte y las que direccionan
 * por bloque la resuelve el driver.
 *----------------------------------------------------------------------------*/
bool drv_sd_leer_sector   ( uint32_t ulSector, uint8_t *pucBuffer );
bool drv_sd_escribir_sector( uint32_t ulSector, const uint8_t *pucBuffer );

/* Los 16 bytes crudos de CID (identificación: fabricante, modelo, serie) y de
   CSD (geometría). Para diagnóstico por consola. */
bool drv_sd_cid( uint8_t *pucBuffer16 );
bool drv_sd_csd( uint8_t *pucBuffer16 );

#endif /* APPLICATION_DRIVERS_DRV_SD_H_ */
