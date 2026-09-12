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

/*
 * ⚠ **Los NÚMEROS son parte del contrato, no los nombres.** El hash de `base`
 * lleva `[PWRMODO:%d]` —el valor del enum— así que `RTU` tiene que ser 3 y
 * `SILENT` 4, igual que en el AVR. Reordenar este enum cambia el hash de todos
 * los equipos y el servidor pediría reconfigurar para siempre.
 *
 * `RTU` y `SILENT` entraron el **2026-09-12**, a pedido de Pablo; hasta entonces
 * estuvieron fuera de alcance y sus números quedaron reservados justamente para
 * poder agregarlos sin invalidar nada.
 */
typedef enum {
    PWR_CONTINUO = 0,
    PWR_DISCRETO = 1,
    PWR_MIXTO    = 2,
    /*
     * Polea y transmite **si hay enlace**; si no lo hay, **descarta el dato: no
     * lo guarda nunca**. Es una unidad remota, no un datalogger — el modem queda
     * permanentemente encendido ("(RTU) continuo" en el AVR).
     *
     * ⚠ Descartar es la única situación en la que este equipo pierde datos a
     * propósito, así que el descarte **se cuenta y se informa**: un RTU con el
     * enlace caído se ve igual que uno andando salvo por ese contador.
     */
    PWR_RTU      = 3,
    /*
     * Polea y **almacena**; el modem **no se enciende nunca** y no se transmite
     * nada. En el AVR la tarea WAN entra en APAGADO y se queda ahí para siempre.
     *
     * Acá los datos terminan en la **microSD**: se sigue usando la ventana de la
     * EEPROM como buffer —escribir la SD en cada muestra serían ~1440 ciclos de
     * montaje por día contra uno cada 33 h, y FAT es frágil justo ante el corte—
     * y como en este modo la ventana no se vacía nunca por transmisión, **siempre
     * llega al umbral y siempre vuelca**.
     *
     * ⛔ **Sin tarjeta, en este modo los datos SE PIERDEN** cuando la ventana da
     * la vuelta: es el único modo donde la microSD deja de ser una extensión y
     * pasa a ser el destino final.
     */
    PWR_SILENT   = 4
} pwr_modo_t;

/* true si en este modo el equipo NUNCA enciende el modem. */
bool cfg_base_modo_sin_modem( void );

/* true si en este modo un dato que no se pudo transmitir se DESCARTA. */
bool cfg_base_modo_sin_memoria( void );

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
