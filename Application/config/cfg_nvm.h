/*
 * cfg_nvm.h
 *
 * Persistencia de la configuración en la EEPROM externa M24M01.
 *
 * ---------------------------------------------------------------------------
 * ⚠ POR QUÉ LA CONFIGURACIÓN VA A LA EEPROM EXTERNA Y NO "A LA NVM"
 *
 * En el AVR128DA64 la configuración vivía en la **EEPROM interna del micro** y
 * la EEPROM externa se usaba entera como filesystem. **El STM32L496 no tiene
 * EEPROM interna**: sólo flash, que para escrituras chicas y frecuentes obliga a
 * emulación —borrar una página de 2 KB para cambiar 16 bytes—.
 *
 * Así que las dos cosas conviven en la M24M01, que tiene 128 KB de sobra
 * (decisión de Pablo, 2026-09-07):
 *
 *     0x00000 - 0x00FFF    4 KB   configuración (este archivo)
 *     0x01000 - 0x1FFFF  124 KB   filesystem circular -> 1984 registros de 64 B
 *
 * La configuración real ocupa ~270 bytes; los 4 KB son para no tener que
 * remapear el día que aparezca un bloque nuevo. Y el filesystem queda con casi
 * el doble de registros que en el AVR (1984 contra 1024).
 *
 * ---------------------------------------------------------------------------
 * UN BLOQUE, UN CHECKSUM
 *
 * Cada bloque se guarda por separado con su checksum al final, igual que en el
 * AVR. Si uno se corrompe, **sólo ése cae a sus valores por defecto** y se avisa
 * por consola; los demás sobreviven. Es lo que ya hace el equipo en producción y
 * es la razón por la que el checksum va adentro de cada struct y no en una
 * cabecera común.
 *
 * ⚠ El checksum se calcula sobre `sizeof(struct) - 1`, o sea sobre la struct
 * entera **incluido el relleno que mete el compilador**. Por eso cada
 * `cfg_*_defaults()` hace `memset()` antes de llenar: si el padding quedara con
 * basura, el checksum de una configuración recién puesta no coincidiría con el
 * de la misma configuración releída. En el AVR el problema no se veía porque las
 * structs eran globales y arrancaban en cero.
 */

#ifndef APPLICATION_CONFIG_CFG_NVM_H_
#define APPLICATION_CONFIG_CFG_NVM_H_

#include <stdbool.h>
#include <stdint.h>

#include "cfg_base.h"
#include "cfg_ainputs.h"
#include "cfg_counter.h"
#include "cfg_modbus.h"
#include "cfg_consigna.h"

/*------------------------------------------------------------------------------
 * El mapa. Las direcciones son fijas y con hueco entre bloques: así un bloque
 * puede crecer sin mover a los que le siguen, que es lo que obligaría a
 * reconfigurar todos los equipos en campo.
 *----------------------------------------------------------------------------*/
#define CFG_NVM_BASE_ADDR       0x00000UL   /*  64 B reservados */
#define CFG_NVM_AINPUTS_ADDR    0x00040UL   /* 192 B reservados */
#define CFG_NVM_COUNTER_ADDR    0x00100UL   /*  64 B reservados */
#define CFG_NVM_MODBUS_ADDR     0x00140UL   /* 384 B reservados */
#define CFG_NVM_CONSIGNA_ADDR   0x002C0UL   /*  64 B reservados */

/* Dónde empieza el filesystem. Todavía no se usa: lo reserva esta etapa para que
   el día que se escriba el FS no haya que discutir el mapa. */
#define CFG_NVM_FS_ADDR         0x01000UL

/*------------------------------------------------------------------------------
 * Carga los cinco bloques. Cada uno que falle el checksum queda con sus valores
 * por defecto y se informa por consola.
 *
 * Devuelve true si los cinco cargaron bien. **Un false NO deja el equipo
 * inutilizable**: arranca con defaults, que es lo que corresponde en un equipo
 * desatendido — mejor midiendo con la configuración de fábrica que sin arrancar.
 *----------------------------------------------------------------------------*/
bool cfg_nvm_load_all( void );

/* Guarda los cinco bloques, recalculando checksums. */
bool cfg_nvm_save_all( void );

/* Todos los bloques a sus valores por defecto, EN RAM: no toca la EEPROM hasta
   que alguien haga `config save`. Es a propósito — así un `config default` mal
   tipeado se deshace con un `config load`. */
void cfg_nvm_defaults_all( void );

/* Imprime la configuración completa con sus hashes. */
void cfg_nvm_print_all( void );

/*------------------------------------------------------------------------------
 * Busca NOMBRES REPETIDOS entre los canales habilitados, e informa cada uno.
 * Devuelve cuántos encontró.
 *
 * ⚠ No es cosmético: **el nombre del canal ES la clave del campo en el frame**
 * (`&CAU0=12.345`). Dos canales habilitados con el mismo nombre emiten dos
 * campos iguales y el servidor se queda con uno de los dos — un dato se pierde
 * en silencio, y del lado del equipo todo parece correcto.
 *
 * Apareció en banco el 2026-09-08 con el contador y el canal Modbus 0 los dos
 * llamados `CAU0`. Y no es un descuido raro: es la configuración natural cuando
 * el caudal puede venir por pulsos **o** por Modbus, que son alternativas, no
 * cosas simultáneas — el propio FWDLGX trae los dos ejemplos con ese nombre.
 *
 * **Avisa, no impide.** Qué canales habilitar es decisión del operador, y un
 * equipo que se niega a guardar una configuración en el medio de una
 * instalación es peor que uno que advierte.
 *----------------------------------------------------------------------------*/
uint8_t cfg_nvm_chequear_nombres( void );

#endif /* APPLICATION_CONFIG_CFG_NVM_H_ */
