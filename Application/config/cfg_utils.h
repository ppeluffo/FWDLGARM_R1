/*
 * cfg_utils.h
 *
 * Dos ayudas que usan todos los bloques de configuración.
 *
 * `cfg_strlcpy()` existe porque **newlib no trae `strlcpy`** —es de BSD—, y el
 * checklist de portación de CLAUDE.md la marca como una de las cosas que hay que
 * implementar a mano al traer código de FWDLGX. La diferencia con `strncpy`
 * importa acá: `strncpy` **no termina la cadena** si el origen llena el buffer,
 * y un nombre de canal sin terminar se iría al hash y al frame arrastrando lo
 * que hubiera al lado en memoria.
 */

#ifndef APPLICATION_CONFIG_CFG_UTILS_H_
#define APPLICATION_CONFIG_CFG_UTILS_H_

#include <stdbool.h>
#include <stddef.h>

/* Copia con truncado seguro y NUL garantizado. Devuelve el largo del origen, o
   sea que `>= xSize` significa que hubo truncado. */
size_t cfg_strlcpy( char *pcDst, const char *pcSrc, size_t xSize );

/* Acepta true/false, si/no, 1/0, on/off — en cualquier combinación de
   mayúsculas. Devuelve false si el texto no es ninguno de ésos, en cuyo caso
   *pbOut no se toca: un texto inválido NO puede quedar como "false" silencioso,
   porque deshabilitaría un canal sin que nadie se entere. */
bool cfg_str2bool( const char *pcStr, bool *pbOut );

#endif /* APPLICATION_CONFIG_CFG_UTILS_H_ */
