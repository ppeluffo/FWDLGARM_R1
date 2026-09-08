/*
 * cfg_consigna.h
 *
 * Bloque de configuración de la doble consigna de presión. Portado de FWDLGX
 * 2.0.12 (`SRC/XLIBS/consignas.c`).
 *
 * Son dos horas en formato HHMM: a la hora `diurna` se manda al equipo de
 * control de presión la consigna de día, y a la `nocturna` la de noche. El
 * diálogo con el equipo es Modbus y va por el RS485, sobre el riel
 * `EN_PWR_CPRES`.
 */

#ifndef APPLICATION_CONFIG_CFG_CONSIGNA_H_
#define APPLICATION_CONFIG_CFG_CONSIGNA_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     bEnabled;
    uint16_t usDiurna;
    uint16_t usNocturna;
    uint8_t  ucChecksum;
} cfg_consigna_t;

extern cfg_consigna_t xCfgConsigna;

void    cfg_consigna_defaults( void );
void    cfg_consigna_print( void );
uint8_t cfg_consigna_hash( void );

bool cfg_consigna_set( const char *pcEnable, const char *pcDiurna, const char *pcNocturna );

#endif /* APPLICATION_CONFIG_CFG_CONSIGNA_H_ */
