/*
 * cfg_ainputs.h
 *
 * Bloque de configuración de las 3 entradas de 4-20 mA. Portado de FWDLGX 2.0.12
 * (`SRC/XLIBS/ainputs.c`).
 *
 * Cada canal lleva su propia CALIBRACIÓN DE DOS PUNTOS: la recta que convierte
 * corriente en magnitud física queda definida por (imin, mmin) y (imax, mmax),
 * más un offset.
 *
 *     magnitud = mmin + (mmax - mmin) * (I_leida - imin) / (imax - imin) + offset
 *
 * ⚠ Es acá donde se absorbe el error de ganancia del INA3221 —el −0,23 % medido
 * el 2026-08-14— y por eso el driver NO lo corrige: metido en el driver quedaría
 * escondido donde nadie lo va a buscar el día que cambie una placa.
 *
 * `sensors_pwr_settle_time` son los segundos que hay que esperar tras encender
 * la fuente lineal de los sensores antes de creerle a una lectura. Existe porque
 * algunos sensores —los de ultrasonido de Dica— tardan bastante más que el resto.
 */

#ifndef APPLICATION_CONFIG_CFG_AINPUTS_H_
#define APPLICATION_CONFIG_CFG_AINPUTS_H_

#include <stdbool.h>
#include <stdint.h>

#define CFG_AINPUTS_NRO_CANALES     3U
#define CFG_PARAMNAME_LENGTH       12U   /* igual que el AVR: nombres de 11 + NUL */

typedef struct {
    bool    bEnabled;
    uint8_t ucImin;
    uint8_t ucImax;
    float   fMmin;
    float   fMmax;
    char    pcName[ CFG_PARAMNAME_LENGTH ];
    float   fOffset;
} cfg_ainput_canal_t;

typedef struct {
    cfg_ainput_canal_t xCanal[ CFG_AINPUTS_NRO_CANALES ];
    uint8_t            ucSensorsPwrSettleTime;
    uint8_t            ucChecksum;
} cfg_ainputs_t;

extern cfg_ainputs_t xCfgAinputs;

void    cfg_ainputs_defaults( void );
void    cfg_ainputs_print( void );
uint8_t cfg_ainputs_hash( void );

bool cfg_ainputs_set_canal( uint8_t ucCh, const char *pcEnable, const char *pcName,
                            const char *pcImin, const char *pcImax,
                            const char *pcMmin, const char *pcMmax,
                            const char *pcOffset );

bool cfg_ainputs_set_settle_time( const char *pcVal );

#endif /* APPLICATION_CONFIG_CFG_AINPUTS_H_ */
