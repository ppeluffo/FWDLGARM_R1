/*
 * cfg_modbus.h
 *
 * Bloque de configuración Modbus. Portado de FWDLGX 2.0.12
 * (`SRC/XLIBS/modbus.c`).
 *
 * Son dos cosas: la configuración LOCAL (si el Modbus está habilitado y con qué
 * dirección se presenta el equipo en el bus) y 5 CANALES REMOTOS, cada uno con
 * el registro que hay que ir a leer en qué esclavo y cómo interpretarlo.
 *
 * ---------------------------------------------------------------------------
 * ⚠ EN R001 HAY UN SOLO BUS FÍSICO, CON DOS RAMAS ALIMENTADAS POR SEPARADO
 *
 * Un único transceiver SP3485 (USART3), y dos ramas cuya alimentación se
 * conmuta aparte: `EN_PWR_QMBUS` para los caudalímetros y `EN_PWR_CPRES` para el
 * equipo de control de presión (dato de Pablo, 2026-09-07).
 *
 * Como el transceiver es uno solo, **las dos ramas son el mismo bus eléctrico**,
 * y de ahí salen dos reglas que esta configuración no puede hacer cumplir sola:
 *
 *  1. **Las direcciones de esclavo no se pueden repetir entre ramas.** Si un
 *     caudalímetro y el control de presión son los dos el esclavo 2, con las dos
 *     ramas encendidas contestan juntos y la trama sale corrupta.
 *  2. **El acceso hay que serializarlo.** No se puede polear un caudalímetro
 *     mientras se le habla al control de presión.
 *
 * Eso se resuelve en la capa de Modbus (paso 6), no acá; queda anotado porque es
 * donde alguien va a venir a buscarlo.
 *
 * ---------------------------------------------------------------------------
 * `type` dice cómo interpretar los bytes; `codec` en qué orden vienen. Los dos
 * viajan en el hash como texto (`FLOAT`, `C1032`), así que **los valores del enum
 * tienen que mantener su orden**: cambiarlos cambiaría lo que se guardó en la
 * EEPROM de los equipos ya configurados.
 */

#ifndef APPLICATION_CONFIG_CFG_MODBUS_H_
#define APPLICATION_CONFIG_CFG_MODBUS_H_

#include <stdbool.h>
#include <stdint.h>

#include "cfg_ainputs.h"    /* CFG_PARAMNAME_LENGTH */

#define CFG_MODBUS_NRO_CANALES  5U

typedef enum { CFG_MB_U16 = 0, CFG_MB_I16, CFG_MB_U32, CFG_MB_I32, CFG_MB_FLOAT } cfg_modbus_tipo_t;
typedef enum { CFG_MB_C0123 = 0, CFG_MB_C1032, CFG_MB_C3210, CFG_MB_C2301 }       cfg_modbus_codec_t;

typedef struct {
    bool               bEnabled;
    char               pcName[ CFG_PARAMNAME_LENGTH ];
    uint8_t            ucSlaveAddress;
    uint16_t           usRegAddress;
    uint8_t            ucNroRegs;      /* cada registro son 2 bytes */
    uint8_t            ucFcode;
    cfg_modbus_tipo_t  eTipo;
    cfg_modbus_codec_t eCodec;
    uint8_t            ucDivisorP10;   /* potencia de 10 por la que se divide */
} cfg_modbus_canal_t;

typedef struct {
    bool               bEnabled;
    uint8_t            ucLocalAddr;
    cfg_modbus_canal_t xCanal[ CFG_MODBUS_NRO_CANALES ];
    uint8_t            ucChecksum;
} cfg_modbus_t;

extern cfg_modbus_t xCfgModbus;

void    cfg_modbus_defaults( void );
void    cfg_modbus_print( void );
uint8_t cfg_modbus_hash( void );

bool cfg_modbus_set_enable   ( const char *pcVal );
bool cfg_modbus_set_localaddr( const char *pcVal );

bool cfg_modbus_set_canal( uint8_t ucCh, const char *pcEnable, const char *pcName,
                           const char *pcSlaveAddr, const char *pcRegAddr,
                           const char *pcNroRegs, const char *pcFcode,
                           const char *pcTipo, const char *pcCodec,
                           const char *pcDivisor );

const char *cfg_modbus_tipo_str ( cfg_modbus_tipo_t  eTipo  );
const char *cfg_modbus_codec_str( cfg_modbus_codec_t eCodec );

#endif /* APPLICATION_CONFIG_CFG_MODBUS_H_ */
