/*
 * drv_valvula.h
 *
 * Electroválvula TOYI: un servo (motor eléctrico) con dos señales, sin ninguna
 * realimentación de posición.
 *
 * ---------------------------------------------------------------------------
 * EL CIRCUITO Y LA SECUENCIA
 *
 *   PA6  EN_EV_TOYI   -> TPS22810 (load switch, EN=1 PRENDE, pull-down)
 *                        alimenta al servo
 *   PA7  CTL_EV_TOYI  -> dirección: 1 = ABRIR, 0 = CERRAR
 *
 * El servo no tiene "pulso de comando": mientras esté alimentado se mueve hacia
 * donde diga `CTL`, y una vez que llega al tope se queda ahí. El firmware sólo
 * tiene que darle **tiempo** y después cortarle la energía.
 *
 *   1. poner CTL en la dirección deseada       <- PRIMERO la dirección
 *   2. prender el load switch (EN = 1)         <- recién ahí se energiza
 *   3. esperar DRV_VALVULA_MS_RECORRIDO (5 s)  <- el recorrido completo
 *   4. apagar el load switch (EN = 0)
 *   5. dejar CTL en 0
 *
 * **El orden de 1 y 2 no es un detalle:** si se energizara primero, el servo
 * arrancaría hacia donde hubiera quedado `CTL` de la vez anterior y recién después
 * cambiaría de sentido. Un arranque en falso por cada movimiento.
 *
 * El paso 5 va **después** de cortar la energía, y por dos motivos: mientras el
 * servo está sin alimentar su entrada de control no manda nada, y dejar una señal
 * en alto contra un integrado apagado es back-powering — la misma trampa que ya
 * apareció con el SPI de la microSD. `CTL` en 0 es además el estado de "cerrar",
 * que es el seguro.
 *
 * ---------------------------------------------------------------------------
 * ⚠ NO HAY REALIMENTACIÓN: EL ESTADO ES UNA CREENCIA, NO UNA MEDICIÓN
 *
 * Nada en la placa dice si la válvula quedó abierta o cerrada. Lo que este driver
 * devuelve es **el último comando que ejecutó**, y eso vale mientras nada externo
 * la mueva y mientras el motor haya llegado al tope.
 *
 * Al arrancar el firmware la incógnita es total, y la política —definida por
 * Pablo— es **asumir ABIERTA y mandar a cerrar**. Asumir el estado peligroso y
 * corregirlo es lo correcto: si de verdad estaba abierta, el cierre la cierra; si
 * ya estaba cerrada, el comando es inofensivo porque el servo empuja contra el
 * tope y ahí se queda. Al revés —asumir cerrada— dejaría una válvula abierta que
 * el firmware cree cerrada, que es el único desenlace realmente malo.
 *
 * `drv_valvula_estado_asumido()` distingue las dos situaciones: es `true` hasta
 * que el driver ejecuta su primer movimiento de verdad. La capa de aplicación
 * debería tratar un estado asumido con la misma desconfianza con la que trata una
 * hora de RTC sin firma válida.
 *
 * ---------------------------------------------------------------------------
 * ENERGÍA
 *
 * En reposo el driver no consume nada: el load switch cortado deja al servo sin
 * alimentación, y los dos pines quedan en 0 contra sus pull-down. Lo que se paga
 * son los 5 segundos del movimiento, con el motor girando — es la corriente más
 * grande que consume el equipo y hay que medirla en banco.
 *
 * **Durante esos 5 segundos el micro puede dormir en Stop 2 sin problema**, así
 * que este driver **no toma ningún candado de energía**: los dos pines son GPIO y
 * el estado de un GPIO sobrevive al Stop. Es el mismo razonamiento que en el
 * INA3221 con su ventana de 1,4 s.
 *
 * ⚠ **La contracara es que un reset a mitad de movimiento deja la válvula a
 * mitad de camino**, con el load switch apagado por el propio reset. No hay forma
 * de evitarlo sin realimentación; lo que sí hace el arranque es mandar un cierre
 * completo, que saca a la válvula de esa posición indefinida.
 *
 * ---------------------------------------------------------------------------
 * LOS MOVIMIENTOS BLOQUEAN AL QUE LOS PIDE
 *
 * `drv_valvula_abrir()` y `drv_valvula_cerrar()` tardan más de 5 segundos y los
 * pasan **bloqueados en `vTaskDelay()`**, no girando: la tarea que llama queda
 * suspendida y el resto del sistema sigue andando. Si la llama `tkCmd`, la consola
 * no contesta durante el movimiento — los caracteres que se tipeen mientras tanto
 * no se pierden, quedan en el stream buffer de la USART.
 *
 * Dos llamadas simultáneas serían un desastre —`CTL` cambiando con el motor
 * energizado, o sea el servo invirtiendo el sentido a mitad de camino— así que un
 * mutex las serializa. El segundo en llegar **no espera: se le dice que no** con
 * `false`, porque encolar movimientos de una válvula no tiene ningún sentido.
 */

#ifndef APPLICATION_DRIVERS_DRV_VALVULA_H_
#define APPLICATION_DRIVERS_DRV_VALVULA_H_

#include <stdbool.h>
#include <stdint.h>

/*------------------------------------------------------------------------------
 * Tiempos.
 *
 * `RECORRIDO` es cuánto se deja energizado el servo para que complete la carrera
 * (dato de Pablo, 2026-08-18). Es un tiempo a ojo con margen: no hay señal de "ya
 * llegué", así que de más cuesta unos segundos de motor contra el tope y de menos
 * deja la válvula a medio camino.
 *
 * `DIRECCION` es lo que se deja asentar `CTL` antes de energizar. No lo pide
 * ninguna hoja de datos; es para que la dirección esté establecida antes de que
 * llegue la alimentación, y no cuesta nada.
 *
 * `DESCARGA` es lo que se espera después de cortar la energía antes de bajar
 * `CTL`, para que el riel del servo ya esté abajo y el cambio de `CTL` no lo
 * encuentre todavía alimentado.
 *----------------------------------------------------------------------------*/
#define DRV_VALVULA_MS_RECORRIDO    5000U
#define DRV_VALVULA_MS_DIRECCION      10U
#define DRV_VALVULA_MS_DESCARGA      100U

typedef enum {
    valvulaCERRADA = 0,
    valvulaABIERTA
} drv_valvula_estado_t;

/*------------------------------------------------------------------------------
 * Arranque. Deja los dos pines en 0 —servo sin alimentar, dirección en "cerrar"—
 * y el estado en **ABIERTA asumida**, que es la incógnita del arranque.
 *
 * **No mueve la válvula**: no puede, porque bloquearía 5 segundos y esto corre
 * antes de que haya consola. El cierre de arranque lo manda la tarea, llamando a
 * `drv_valvula_cerrar()` apenas puede bloquear.
 *----------------------------------------------------------------------------*/
void drv_valvula_init( void );

/*------------------------------------------------------------------------------
 * Los dos movimientos. Bloquean ~5,1 s y devuelven `true` si el movimiento se
 * ejecutó, `false` si había otro en curso (y en ese caso no tocaron ningún pin).
 *----------------------------------------------------------------------------*/
bool drv_valvula_abrir ( void );
bool drv_valvula_cerrar( void );

/*------------------------------------------------------------------------------
 * Qué cree el driver que pasó. Ver la advertencia de arriba: `estado()` es el
 * último comando ejecutado, y mientras `estado_asumido()` sea true ni siquiera es
 * eso — es la suposición del arranque.
 *----------------------------------------------------------------------------*/
drv_valvula_estado_t drv_valvula_estado( void );
bool                 drv_valvula_estado_asumido( void );

/* Cuántos movimientos completó desde el arranque. Es desgaste mecánico: sirve
   para el diagnóstico en banco y algún día para el mantenimiento. */
uint32_t drv_valvula_movimientos( void );

/*------------------------------------------------------------------------------
 * Los dos pines, sueltos y sin secuencia. Son **para el banco**: permiten separar
 * "el load switch no prende" de "el servo no se mueve" de "la dirección está al
 * revés".
 *
 * ⚠ Usarlas saltea la secuencia y el mutex, y deja el estado que informa el driver
 * diciendo cualquier cosa. No van en la aplicación.
 *----------------------------------------------------------------------------*/
void drv_valvula_pin_pwr( bool bOn );
void drv_valvula_pin_ctl( bool bOn );
bool drv_valvula_pin_pwr_estado( void );
bool drv_valvula_pin_ctl_estado( void );

#endif /* APPLICATION_DRIVERS_DRV_VALVULA_H_ */
