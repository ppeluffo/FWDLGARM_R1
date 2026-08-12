/*
 * drv_eeprom.h
 *
 * EEPROM externa **M24M01** en el bus I2C2: 1 Mbit = **128 KB**.
 *
 * Confirmado en banco el 2026-08-12 con `i2c scan`, y NO es lo que decía el
 * comentario heredado de FWDLGX (que hablaba de una M24M02 de 256 KB). La firma
 * que lo delató: el chip contesta en las direcciones de 7 bits `50` y `51`, y su
 * página de identificación en `58` y `59`. Una M24M02 tendría que contestar en
 * `50..53`, porque A17/A16 los decodifica adentro y no son patas que se puedan
 * atar. Dos bloques y no cuatro = 1 Mbit.
 *
 * ---------------------------------------------------------------------------
 * DIRECCIÓN PLANA DE 17 BITS, Y POR QUÉ NO SE PUEDE PORTAR FWDLGX TAL CUAL
 *
 * Los 128 KB no entran en un byte de dirección de 16 bits: el bit A16 viaja
 * **dentro del byte de dispositivo** (`0xA0` para el bloque bajo, `0xA2` para el
 * alto). Este driver esconde eso y expone una dirección plana `0x00000..0x1FFFF`.
 *
 * `eeprom.c` de FWDLGX usa `uint16_t` para la dirección y habla siempre a `0xA0`:
 * o sea que el firmware del AVR **usaba sólo los primeros 64 KB**, la mitad del
 * chip. No era un error allá —le alcanzaba—, pero significa que su
 * direccionamiento no sirve acá si se quiere la memoria entera.
 *
 * ---------------------------------------------------------------------------
 * LAS DOS TRAMPAS DE ESCRIBIR EN UNA EEPROM I2C
 *
 * Las resuelve este driver; se documentan porque son silenciosas y clásicas.
 *
 * 1. **La escritura de página NO desborda: da la vuelta.** Si se escriben 300
 *    bytes desde la dirección 200, los primeros 56 llenan hasta el final de la
 *    página y los 244 restantes **pisan el principio de ESA MISMA página**, no la
 *    siguiente. No hay error, no hay aviso: quedan datos corruptos. Por eso toda
 *    escritura se parte en los bordes de página de 256 bytes.
 *
 * 2. **Después de escribir, el chip queda sordo unos 5 ms.** Durante su ciclo
 *    interno NACKea todo, incluso su propia dirección. Un NACK ahí NO es un
 *    error: hay que reintentar hasta que conteste (acknowledge polling). Sin
 *    esto, escribir dos páginas seguidas falla siempre en la segunda.
 *
 * Y una tercera que es del bus, no del chip: la lectura secuencial tampoco cruza
 * el borde de los 64 KB —el contador interno da la vuelta al principio del
 * bloque—, así que las lecturas también se parten ahí.
 */

#ifndef APPLICATION_DRIVERS_DRV_EEPROM_H_
#define APPLICATION_DRIVERS_DRV_EEPROM_H_

#include <stdbool.h>
#include <stdint.h>

#define DRV_EEPROM_SIZE         0x20000UL   /* 128 KB */
#define DRV_EEPROM_PAGE_SIZE    256UL       /* página de escritura de la M24M01 */
#define DRV_EEPROM_BLOCK_SIZE   0x10000UL   /* 64 KB por dirección de dispositivo */

/*
 * Devuelven la cantidad de bytes movidos, o -1.
 *
 * ⚠ El tipo es `int32_t` y no el `int16_t` del resto de la casa: con 17 bits de
 * espacio de direcciones, un `int16_t` no alcanza ni para expresar una longitud
 * que cruce la memoria. Es la excepción, y es a propósito.
 *
 * `ulAddr + ulBytes` tiene que caber en DRV_EEPROM_SIZE; si se pasa, devuelven -1
 * en vez de dar la vuelta en silencio.
 */
int32_t drv_eeprom_read ( uint32_t ulAddr, char *pvBuffer, uint32_t ulBytes );
int32_t drv_eeprom_write( uint32_t ulAddr, const char *pvBuffer, uint32_t ulBytes );

/* ¿Terminó el ciclo interno de escritura? Es el acknowledge polling suelto, por
   si alguna capa de arriba lo necesita. drv_eeprom_write() ya lo hace solo. */
bool drv_eeprom_lista( void );

#endif /* APPLICATION_DRIVERS_DRV_EEPROM_H_ */
