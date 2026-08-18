/*
 * drv_lte.h
 *
 * Modem LTE, ETAPA 1: **sólo las fuentes y el pin de encendido**.
 *
 * Acá no hay comunicación con el modem: no se manda un AT, no se lee una
 * respuesta, no se genera el pulso de `PWRKEY`. Esta etapa pone y saca niveles en
 * tres pines y nada más, que es exactamente lo que hay que validar antes de
 * confiar en cualquier cosa que venga después. Las líneas de datos (UART4,
 * PA0/PA1) y el ritual de encendido son la etapa 2.
 *
 * ---------------------------------------------------------------------------
 * LOS TRES PINES
 *
 *   PC13  EN_LTE_DCIN  -> TPS22810 (load switch, EN=1 PRENDE): los 12 V del modem
 *   PA4   EN_LTE_3V8   -> TPS62130 (step-down, EN=1 PRENDE): el riel de 3,8 V
 *   PA5   LTE_PWR      -> base de un transistor cuyo colector, con pull-up, va al
 *                         PWRKEY del módulo
 *
 * **⚠ `LTE_PWR` está INVERTIDO por el transistor.** PA5 en 1 hace conducir al
 * transistor, que hunde el colector: el `PWRKEY` del módulo ve un **BAJO**, que es
 * el nivel activo de casi todos estos módulos. O sea:
 *
 *     PA5 = 1  ->  transistor conduciendo  ->  PWRKEY en BAJO  ->  "botón apretado"
 *     PA5 = 0  ->  transistor cortado      ->  PWRKEY en ALTO  ->  botón suelto (reposo)
 *
 * Y por eso el reposo es **PA5 = 0**, no sólo por el nivel lógico: con PA5 en 1 el
 * transistor drena permanentemente la corriente del pull-up del colector.
 *
 * ⏳ **El PULSO de encendido no se genera acá** (decidido por Pablo el
 * 2026-08-18): cuánto dura y cuándo se manda depende del módulo y de la política
 * de sesiones, y eso es de la capa de aplicación. Este driver deja poner el nivel
 * a mano —`drv_lte_pwrkey()`— para poder cronometrarlo en el banco.
 *
 * ---------------------------------------------------------------------------
 * EL ORDEN DE ENCENDIDO, Y POR QUÉ NO ES OPCIONAL
 *
 * El TPS62130 es un step-down, así que **su entrada tiene que estar por encima de
 * 3,8 V**: no puede salir del riel de 3,3 V, sale de los 12 V. Lo que todavía no
 * está confirmado es si su entrada cuelga **de DCIN conmutado o del riel de 12 V
 * crudo**, y eso cambia dos cosas:
 *
 *   - si cuelga de DCIN, prender el 3V8 con DCIN apagado no hace nada;
 *   - si cuelga del riel crudo, el TPS62130 sigue consumiendo su corriente de
 *     shutdown (unos µA) aunque DCIN esté cortado.
 *
 * **Lo contesta el banco**: `lte 3v8 on` con DCIN apagado y el tester en el riel
 * de 3,8 V. Mientras tanto el orden que este driver usa —**DCIN primero, 3V8
 * después, y al apagar al revés**— es correcto en las dos topologías, así que no
 * hay que esperar la respuesta para andar.
 *
 * ---------------------------------------------------------------------------
 * ⚠ PC13 NO ES UN GPIO CUALQUIERA
 *
 * Está en el dominio de backup, alimentado a través del power switch. El DS11585
 * es explícito: *"the switch only sinks a limited amount of current (3 mA)… the
 * speed should not exceed 2 MHz with a maximum load of 30 pF… must not be used as
 * current sources"*.
 *
 * Para el `EN` de un TPS22810 está perfecto —entrada de alta impedancia, ~1 µA, y
 * una señal continua— pero **el día que alguien quiera colgar otra cosa de PC13 la
 * respuesta puede ser distinta**.
 *
 * Y hay un segundo detalle, menos obvio: *"After a Backup domain power-up, PC13,
 * PC14 and PC15 operate as GPIOs. Their function then depends on the content of
 * the RTC registers, **which are not reset by the system reset**."* Si alguna vez
 * se habilita la salida del RTC (`RTC_OUT`, la calibración, `TAMP1` o `WKUP2`),
 * **PC13 deja de ser GPIO y pasa a manejarlo el RTC** — y esa configuración
 * sobrevive al reset. El síntoma sería un modem que se prende o se apaga solo sin
 * que ninguna línea de código lo toque. Hoy está limpio
 * (`hrtc.Init.OutPut = RTC_OUTPUT_DISABLE`); conviene no tocarlo.
 *
 * ---------------------------------------------------------------------------
 * ENERGÍA
 *
 * El modem va a ser **el consumidor más grande del equipo**, con picos de
 * corriente en la transmisión que ningún otro periférico de esta placa se acerca a
 * pedir. En reposo, en cambio, este driver no tiene que costar nada: los tres
 * pines en 0 dejan el load switch cortado, el step-down en shutdown y el
 * transistor sin conducir.
 *
 * **Ese "nada" hay que medirlo**, y es el criterio de aceptación de esta etapa: si
 * el reposo sube respecto de los 6 µA de siempre, el sospechoso es el TPS62130
 * alimentado desde el riel crudo (ver arriba) o un pin que quedó donde no debía.
 *
 * Este driver **no toma candado de energía**: son tres GPIO y su estado sobrevive
 * al Stop 2. El `pwrLOCK_WAN` ya existe reservado en `pwr_lock.h` y le va a hacer
 * falta a la etapa 2, cuando haya una UART transmitiendo.
 */

#ifndef APPLICATION_DRIVERS_DRV_LTE_H_
#define APPLICATION_DRIVERS_DRV_LTE_H_

#include <stdbool.h>

/*------------------------------------------------------------------------------
 * Lo que se espera entre prender DCIN y prender el 3V8: el soft-start del
 * TPS22810 más un margen. No lo pide ninguna hoja de datos —es el criterio de no
 * habilitar un regulador antes de que su entrada esté arriba— y no cuesta nada.
 *----------------------------------------------------------------------------*/
#define DRV_LTE_MS_ENTRE_RIELES     50U

/*------------------------------------------------------------------------------
 * Arranque. Deja los tres pines en 0: modem sin alimentar por los dos lados y
 * `PWRKEY` suelto. Es el estado de reposo y el de menor consumo.
 *----------------------------------------------------------------------------*/
void drv_lte_init( void );

/*------------------------------------------------------------------------------
 * Los rieles, en el orden correcto: al prender DCIN y después 3V8, al apagar al
 * revés. **No toca `PWRKEY`**: alimentar al modem y encenderlo son dos cosas
 * distintas, y esta función hace sólo la primera.
 *
 * Bloquea DRV_LTE_MS_ENTRE_RIELES al prender.
 *----------------------------------------------------------------------------*/
void drv_lte_rieles( bool bOn );

/*------------------------------------------------------------------------------
 * Los tres pines sueltos, para el banco. `drv_lte_rieles()` ya maneja los dos
 * primeros en orden; estas quedan para poder probarlos de a uno, que es lo que
 * separa "el load switch no prende" de "el step-down no arranca".
 *
 * ⚠ `drv_lte_pwrkey( true )` deja el `PWRKEY` apretado hasta que alguien lo
 * suelte: acá no hay ningún temporizador. El pulso lo hace la aplicación.
 *----------------------------------------------------------------------------*/
void drv_lte_dcin  ( bool bOn );
void drv_lte_3v8   ( bool bOn );
void drv_lte_pwrkey( bool bApretado );

/* Nivel eléctrico de cada pin, leído del pin y no de una variable: si algo los
   moviera por afuera del driver, acá se vería. */
bool drv_lte_dcin_estado  ( void );
bool drv_lte_3v8_estado   ( void );
bool drv_lte_pwrkey_estado( void );

#endif /* APPLICATION_DRIVERS_DRV_LTE_H_ */
