/*
 * cfg_counter.h
 *
 * Bloque de configuración del contador de pulsos. Portado de FWDLGX 2.0.12
 * (`SRC/XLIBS/contadores.c`).
 *
 *   magpp        cuántos litros representa un pulso
 *   QMAX         caudal máximo creíble; por encima de eso la medida se descarta
 *   modo_medida  CAUDAL (l/h instantáneos) o PULSOS (totalizador)
 *   alpha        constante del filtro exponencial del caudal
 *
 * ⚠ `alpha` y `aging_min_ms` NO están en la descripción funcional pero **sí
 * viajan en el hash** (`alpha`) o afectan la medida (`aging`), así que se portan
 * igual: sacarlos cambiaría el hash y el servidor pediría reconfigurar para
 * siempre. Ver cfg_hash.h.
 */

#ifndef APPLICATION_CONFIG_CFG_COUNTER_H_
#define APPLICATION_CONFIG_CFG_COUNTER_H_

#include <stdbool.h>
#include <stdint.h>

#include "cfg_ainputs.h"    /* CFG_PARAMNAME_LENGTH */

typedef enum { CFG_CNT_CAUDAL = 0, CFG_CNT_PULSOS } cfg_counter_modo_t;

typedef struct {
    bool               bEnabled;
    char               pcName[ CFG_PARAMNAME_LENGTH ];
    float              fMagPP;
    float              fQmax;
    float              fAlpha;
    cfg_counter_modo_t eModoMedida;
    uint32_t           ulAgingMinMs;
    uint8_t            ucChecksum;
} cfg_counter_t;

extern cfg_counter_t xCfgCounter;

void    cfg_counter_defaults( void );
void    cfg_counter_print( void );
uint8_t cfg_counter_hash( void );

/*
 * ⚠ El ORDEN de los argumentos es el del AVR: enable, name, magpp, **modo**,
 * qmax, alpha. El modo va TERCERO, no al final, y `alpha` es configurable —
 * viaja en el hash, así que si no se pudiera fijar quedaría siempre en 0.25 y
 * el hash no coincidiría con el que espera el servidor para un equipo afinado
 * distinto. Se alineó el 2026-09-08, después de leer `counter_config_channel()`
 * en `FWDLGX_tkCmd.c`.
 */
bool cfg_counter_set( const char *pcEnable, const char *pcName, const char *pcMagPP,
                      const char *pcModo, const char *pcQmax, const char *pcAlpha );

#endif /* APPLICATION_CONFIG_CFG_COUNTER_H_ */
