/*
 * drv_ina3221.h
 *
 * INA3221: monitor de corriente de 3 canales en el bus I2C2, dirección 7 bits
 * `41` (A0 atado a VS) = `0x82` en 8 bits. Es el mismo chip y la misma dirección
 * que usaba FWDLGX (`DEVADDRESS_INA1`), así que el código se porta casi tal cual.
 *
 * Mide la caída sobre una resistencia shunt en serie con cada lazo de 4-20 mA.
 * El LSB del registro de shunt son **40 µV**, así que con el shunt de
 * `DRV_INA_RSHUNT_OHM` la conversión a corriente es directa.
 *
 * ---------------------------------------------------------------------------
 * DOS ALIMENTACIONES DISTINTAS, Y CONVIENE NO CONFUNDIRLAS
 *
 *   - **El INA3221** cuelga del 3V3 permanente, como la EEPROM y el RTC. No se
 *     apaga; se lo pone en *power-down* por software.
 *   - **Los sensores de presión** los alimenta una fuente lineal aparte, que se
 *     prende con `EN_PWR_SENS420` (PB12). Esa sí se corta.
 *
 * Las dos hay que encenderlas para medir, y las dos consumen de más si quedan
 * encendidas:
 *
 *   | Estado                        | INA3221  |
 *   |-------------------------------|----------|
 *   | Midiendo (modo continuo)      | ~350 µA  |
 *   | Power-down (`drv_ina_sleep`)  |   ~2 µA  |
 *
 * Contra los ~5 µA que consume el micro dormido, dejar el INA midiendo multiplica
 * por setenta el consumo de reposo del equipo. **Por eso el driver lo deja en
 * power-down apenas arranca** y sólo lo despierta durante la ventana de medida.
 *
 * ---------------------------------------------------------------------------
 * MEDIR NO ES INSTANTÁNEO: SON ~1,4 SEGUNDOS
 *
 *   asentamiento de la fuente lineal   500 ms   (DRV_INA_SETTLE_MS, heredado)
 *   barrido del INA                    845 ms   (128 promedios x 3 canales x
 *                                                (1,1 ms shunt + 1,1 ms bus))
 *
 * Es mucho tiempo, y es a propósito: 128 promedios es lo que hace que una lectura
 * de 4-20 mA sea estable y no un número que baila. Lo que importa es que durante
 * esa ventana **el micro puede dormir en Stop 2 sin problema**: el INA convierte
 * solo y el riel es un GPIO, que sobrevive al Stop. Por eso este driver **no toma
 * ningún candado de energía propio** — el único que se toma es `pwrLOCK_I2C`, y lo
 * hace `drv_i2c` por transacción.
 *
 * ---------------------------------------------------------------------------
 * EL SIGNO IMPORTA, Y FWDLGX LO PERDÍA
 *
 * El registro de shunt es **complemento a dos de 13 bits alineado en `[15:3]`**.
 * FWDLGX hacía `an_raw_val >> 3` sobre un `uint16_t`, así que una medida negativa
 * —lazo abierto, sensor al revés, cable cortado— salía como un número enorme y
 * positivo en vez de como un negativo. Con 4-20 mA sanos no se ve nunca; con un
 * sensor desconectado, sí. Acá el corrimiento se hace con extensión de signo.
 *
 * ---------------------------------------------------------------------------
 * QUÉ CANAL ES QUÉ ENTRADA: NO ES ASUNTO DE ESTE DRIVER
 *
 * Este driver habla de `inaCH1..inaCH3`, que son los canales del chip. FWDLGX
 * mapeaba al revés (entrada analógica 0 -> CH3, 1 -> CH2, 2 -> CH1) y usaba el
 * *bus voltage* de CH1 para medir la batería. Ese mapeo depende de cómo esté
 * cableada la placa y es **de la capa de aplicación**, que todavía no existe.
 */

#ifndef APPLICATION_DRIVERS_DRV_INA3221_H_
#define APPLICATION_DRIVERS_DRV_INA3221_H_

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

/*------------------------------------------------------------------------------
 * Registros del INA3221 (dirección interna de 1 byte, datos de 2 bytes big endian)
 *----------------------------------------------------------------------------*/
#define DRV_INA_REG_CONF        0x00U
#define DRV_INA_REG_CH1_SHV     0x01U
#define DRV_INA_REG_CH1_BUSV    0x02U
#define DRV_INA_REG_CH2_SHV     0x03U
#define DRV_INA_REG_CH2_BUSV    0x04U
#define DRV_INA_REG_CH3_SHV     0x05U
#define DRV_INA_REG_CH3_BUSV    0x06U
#define DRV_INA_REG_MASK_ENABLE 0x0FU
#define DRV_INA_REG_MFID        0xFEU
#define DRV_INA_REG_DIEID       0xFFU

/* Los dos valores fijos con los que se comprueba que el chip es el que decimos.
   'TI' en ASCII el primero, el número de parte el segundo. */
#define DRV_INA_MFID_ESPERADO   0x5449U
#define DRV_INA_DIEID_ESPERADO  0x3220U

/* Bit 0 del registro Mask/Enable: el barrido de los 3 canales terminó.
   OJO: leer ese registro lo LIMPIA. */
#define DRV_INA_CVRF            0x0001U

/*------------------------------------------------------------------------------
 * Palabras de configuración. Heredadas de FWDLGX, y vale la pena desarmarlas
 * porque de acá salen los tiempos de conversión:
 *
 *   0x7927 = 0 111 100 100 100 111
 *            │ │││ │││ │││ │││ └── MODE 111 : shunt + bus, continuo
 *            │ │││ │││ │││ └────── VSH CT 100 : 1,1 ms
 *            │ │││ │││ └────────── VBUS CT 100 : 1,1 ms
 *            │ │││ └────────────── AVG 100 : 128 promedios
 *            │ └────────────────── CH1/CH2/CH3 habilitados
 *            └──────────────────── RST
 *
 *   0x7920 : lo mismo con MODE 000 -> power-down, conservando la config.
 *----------------------------------------------------------------------------*/
#define DRV_INA_CONF_MEDIR      0x7927U
#define DRV_INA_CONF_SLEEP      0x7920U

/* Tiempos que salen de esa configuración. El barrido teórico son 845 ms; el
   margen es para no depender de la precisión del oscilador interno del chip. */
#define DRV_INA_SETTLE_MS       500U    /* asentamiento de la fuente lineal    */
#define DRV_INA_BARRIDO_MS      845U    /* 128 x 3 x (1,1 + 1,1)               */
#define DRV_INA_BARRIDO_TOUT_MS 1500U   /* techo esperando CVRF                */

/*------------------------------------------------------------------------------
 * Resistencia shunt de cada lazo de 4-20 mA. **7,32 Ω, confirmado por Pablo para
 * R001 el 2026-08-14**; es el mismo valor que usaba FWDLGX.
 *
 * Con 20 mA la caída es de 146 mV, que entra justo dentro del rango de ±163,8 mV
 * que admite el INA — o sea que el fondo de escala del chip está bien aprovechado
 * y no hay margen de sobra: una corriente de falla por encima de 22 mA satura la
 * medida en vez de leerse alta.
 *----------------------------------------------------------------------------*/
#define DRV_INA_RSHUNT_OHM      7.32f

/* LSB de cada registro, del datasheet. */
#define DRV_INA_SHUNT_LSB_UV    40      /* µV por cuenta en los registros SHV  */
#define DRV_INA_BUS_LSB_MV      8       /* mV por cuenta en los registros BUSV */

typedef enum {
    inaCH1 = 0,
    inaCH2,
    inaCH3,
    inaCH_COUNT
} ina_canal_t;

/*------------------------------------------------------------------------------
 * Arranque. Deja el riel de sensores APAGADO y el INA en power-down, que es el
 * estado de reposo del equipo. Devuelve false si el chip no contesta o si su
 * MFID/DIEID no son los esperados — o sea que además de un ACK comprueba que el
 * que contesta sea realmente un INA3221.
 *----------------------------------------------------------------------------*/
bool drv_ina_init( void );

/* Lo que averiguó init(): si el chip está y se identificó bien. */
bool drv_ina_presente( void );

/*------------------------------------------------------------------------------
 * Acceso crudo a registros. Los datos van y vienen en big endian; estas dos lo
 * resuelven y trabajan con la palabra ya armada.
 *----------------------------------------------------------------------------*/
bool drv_ina_reg_leer   ( uint8_t ucReg, uint16_t *pusVal );
bool drv_ina_reg_escribir( uint8_t ucReg, uint16_t usVal );

/*------------------------------------------------------------------------------
 * Modo de operación del chip.
 *
 * `awake` lo pone a convertir de forma continua; `sleep` lo baja a power-down
 * (~2 µA) conservando la configuración. **El estado de reposo es dormido.**
 *----------------------------------------------------------------------------*/
bool drv_ina_awake( void );
bool drv_ina_sleep( void );

/* Espera a que termine un barrido completo de los 3 canales (bit CVRF).
   Devuelve false si venció el timeout. */
bool drv_ina_esperar_conversion( void );

/*------------------------------------------------------------------------------
 * Lecturas de un canal. Todas devuelven false si la transacción I2C falló.
 *
 *   raw   : las 13 cuentas CON SIGNO, ya corridas
 *   uv    : la caída sobre el shunt, en microvolts
 *   ma    : la corriente del lazo, ya dividida por el shunt
 *   bus_mv: la tensión del nodo respecto de GND, en milivolts
 *----------------------------------------------------------------------------*/
bool drv_ina_shunt_raw( ina_canal_t eCanal, int16_t *psRaw    );
bool drv_ina_shunt_uv ( ina_canal_t eCanal, int32_t *plMicroV );
bool drv_ina_leer_ma  ( ina_canal_t eCanal, float   *pfMa     );
bool drv_ina_bus_mv   ( ina_canal_t eCanal, int32_t *plMiliV  );

/*------------------------------------------------------------------------------
 * Riel de los sensores 4-20 mA: la fuente lineal que se prende con PB12.
 *
 * Es sólo el GPIO. Quien lo prenda tiene que esperar `DRV_INA_SETTLE_MS` antes de
 * creerle a una medida, y acordarse de apagarlo: mientras esté encendido alimenta
 * transmisores de lazo, que consumen mucho más que todo el resto del equipo junto.
 *----------------------------------------------------------------------------*/
void drv_ina_pwr_sensores( bool bOn );
bool drv_ina_pwr_sensores_estado( void );

/*------------------------------------------------------------------------------
 * El ciclo completo de una medida, que es lo que va a usar la capa de arriba:
 *
 *   prende el riel -> espera el asentamiento -> despierta el INA -> espera el
 *   barrido -> lee los 3 canales -> duerme el INA -> apaga el riel
 *
 * `pfMa` tiene que apuntar a un arreglo de `inaCH_COUNT` floats. Tarda ~1,4 s, y
 * la tarea que llame queda bloqueada en `vTaskDelay()` casi todo ese tiempo: el
 * micro duerme mientras tanto.
 *
 * Si `bDejarEncendido` es true no apaga el riel al terminar — sirve para medir
 * varias veces seguidas en banco sin pagar el asentamiento cada vez.
 *----------------------------------------------------------------------------*/
bool drv_ina_medir( float *pfMa, bool bDejarEncendido );

#endif /* APPLICATION_DRIVERS_DRV_INA3221_H_ */
