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

/*------------------------------------------------------------------------------
 * ⭐ ¿La configuración GUARDADA es usable? (`consigna_conf_valida()` del AVR
 * 2.0.14.)
 *
 * `cfg_consigna_set()` ya valida lo que entra, así que esto parece redundante —
 * y no lo es: **la configuración también llega desde la EEPROM**, grabada
 * quizá por un firmware anterior que no validaba, o con el checksum apenas
 * bueno. Validar sólo en la entrada deja ese camino sin cubrir.
 *
 * Mira lo mismo que el setter: las dos horas en formato HHMM y distintas entre
 * sí. Con `false`, `tkCtlPres` **no aplica ninguna consigna** en vez de aplicar
 * una decidida a partir de un número sin sentido.
 *----------------------------------------------------------------------------*/
bool cfg_consigna_valida( void );

#endif /* APPLICATION_CONFIG_CFG_CONSIGNA_H_ */
