/*
 * cfg_hash.h
 *
 * El hash de configuración: **Pearson de 8 bits sobre un string formateado**.
 *
 * ---------------------------------------------------------------------------
 * ⚠ ESTO ES PARTE DEL CONTRATO CON EL SERVIDOR, NO UN DETALLE INTERNO
 *
 * En cada sesión el equipo manda un hash por cada bloque de configuración
 * (`CONF_ALL&BH=..&AH=..&CH=..&MH=..&PH=..`) y **el servidor los compara contra
 * los que calcula él**. Si coinciden, no pide nada; si no, pide reconfigurar ese
 * bloque.
 *
 * De ahí lo que hay que entender antes de tocar una línea de este archivo:
 *
 *  - **El hash NO es un checksum de la struct.** Se calcula sobre un string
 *    armado con formatos exactos —`[TIMERPOLL:%03d]`, `[A0:TRUE,pA,4,20,0.00,
 *    10.00,0.00]`— así que depende del ancho de cada campo, de los ceros a la
 *    izquierda, de las mayúsculas de `TRUE`/`FALSE`, de las comas y de los
 *    corchetes.
 *  - **Un solo carácter distinto rompe la compatibilidad**, y el síntoma no se
 *    ve en el banco: el servidor pide reconfigurar el bloque **en cada sesión,
 *    para siempre**. Tráfico infinito y un equipo que nunca queda configurado.
 *  - Por eso la tabla de abajo y los strings de `cfg_*.c` se portaron **carácter
 *    por carácter** desde FWDLGX 2.0.12 (`SRC/ULIBS/utils.c`,
 *    `SRC/XLIBS`, funciones `..._hash()`), y no se cambian sin cambiar también el servidor.
 *
 * **Cómo se valida** (criterio de aceptación de esta etapa): poner la misma
 * configuración en un equipo AVR y en éste, y comparar los cinco hashes. No hace
 * falta ni modem ni servidor.
 *
 * ---------------------------------------------------------------------------
 * La función es un Pearson modificado: la tabla original son 256 números
 * aleatorios; acá es una permutación fija de 0..255. Ver
 * https://es.wikipedia.org/wiki/Pearson_hashing
 *
 * En el AVR la tabla vivía en PROGMEM y se leía con `pgm_read_byte()`. Acá no:
 * el Cortex-M4 tiene espacio de direcciones unificado, así que es un `const` en
 * `.rodata` y se indexa como cualquier arreglo. Es la misma conversión que
 * describe el checklist de portación de CLAUDE.md.
 */

#ifndef APPLICATION_CONFIG_CFG_HASH_H_
#define APPLICATION_CONFIG_CFG_HASH_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*------------------------------------------------------------------------------
 * 64 bytes, igual que en el AVR — y el tamaño IMPORTA: si un string no entra, se
 * trunca, y el hash cambia. Mantener el mismo límite es lo que garantiza que los
 * dos equipos se comporten igual también en el borde.
 *----------------------------------------------------------------------------*/
#define CFG_HASH_BUFFER_SIZE    64U

/* Acumula un carácter sobre el hash. Es la primitiva; `cfg_hash_string()` es lo
   que se usa normalmente. */
uint8_t cfg_hash_char( uint8_t ucSeed, char cCh );

/* Acumula una cadena entera sobre el hash. */
uint8_t cfg_hash_string( uint8_t ucSeed, const char *pcStr );

/*------------------------------------------------------------------------------
 * Append acotado a `pcBuf[*pusIdx..]`, con la MISMA semántica que el
 * `u_strbuf_append_P()` del AVR: nunca escribe fuera del buffer, y **avanza el
 * índice sólo si el contenido entró completo**. Devuelve false si se truncó, y
 * ahí el llamador avisa por consola.
 *
 * Se conserva esa semántica exacta —incluido que un truncado deje el texto a
 * medias en el buffer sin avanzar el índice— porque cualquier diferencia en el
 * borde da un hash distinto al del equipo en producción.
 *----------------------------------------------------------------------------*/
bool cfg_hash_append( char *pcBuf, uint16_t usBufSize, uint16_t *pusIdx,
                      const char *pcFmt, ... );

/*------------------------------------------------------------------------------
 * Checksum de un bloque de memoria: suma de bytes módulo 256. Es lo que protege
 * cada bloque de configuración en la EEPROM, y **no tiene nada que ver con el
 * hash de arriba**: el checksum es interno del equipo, el hash es del protocolo.
 *----------------------------------------------------------------------------*/
uint8_t cfg_checksum( const uint8_t *pucData, uint16_t usSize );

#endif /* APPLICATION_CONFIG_CFG_HASH_H_ */
