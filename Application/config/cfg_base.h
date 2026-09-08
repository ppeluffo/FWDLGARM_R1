/*
 * cfg_base.h
 *
 * Bloque de configuración BASE: los tiempos del ciclo de trabajo y la política
 * de energía del modem. Portado de FWDLGX 2.0.12 (`SRC/XLIBS/base.c`).
 *
 *   timerpoll     cada cuántos segundos se toma una muestra (lo usa tkSys)
 *   timerdial     cada cuántos segundos se disca en modo DISCRETO
 *   pwr_modo      CONTINUO | DISCRETO | MIXTO
 *   pwr_hhmm_on   hora (HHMM) en que MIXTO pasa a continuo
 *   pwr_hhmm_off  hora (HHMM) en que MIXTO vuelve a discreto
 *
 * ⚠ `PWR_RTU` y `PWR_SILENT` existen en el AVR y **quedaron afuera a propósito**
 * (decisión de Pablo, 2026-09-07). Los valores del enum se conservan igual que
 * allá para que el número que viaja en el hash —`[PWRMODO:%d]`— siga
 * significando lo mismo: si algún día se reponen, entran por el final.
 */

#ifndef APPLICATION_CONFIG_CFG_BASE_H_
#define APPLICATION_CONFIG_CFG_BASE_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PWR_CONTINUO = 0,
    PWR_DISCRETO,
    PWR_MIXTO
    /* 3 = PWR_RTU y 4 = PWR_SILENT en el AVR: no reutilizar esos números. */
} pwr_modo_t;

/*
 * ⚠ El `checksum` va SIEMPRE último: se calcula sobre `sizeof(struct) - 1`, o
 * sea sobre todo lo anterior. Es el mismo criterio que el AVR y vale para los
 * cinco bloques.
 */
typedef struct {
    uint16_t   usTimerPoll;
    uint16_t   usTimerDial;
    pwr_modo_t ePwrModo;
    uint16_t   usPwrHhmmOn;
    uint16_t   usPwrHhmmOff;
    uint8_t    ucChecksum;
} cfg_base_t;

extern cfg_base_t xCfgBase;

void    cfg_base_defaults( void );
void    cfg_base_print( void );
uint8_t cfg_base_hash( void );

/* Devuelven false si el valor no es válido; en ese caso no tocan la config. */
bool cfg_base_set_timerpoll( const char *pcVal );
bool cfg_base_set_timerdial( const char *pcVal );
bool cfg_base_set_pwrmodo  ( const char *pcVal );
bool cfg_base_set_pwron    ( const char *pcVal );
bool cfg_base_set_pwroff   ( const char *pcVal );

const char *cfg_base_pwrmodo_str( void );

#endif /* APPLICATION_CONFIG_CFG_BASE_H_ */
