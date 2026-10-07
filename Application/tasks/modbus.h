/*
 * modbus.h
 *
 * **La capa de CANALES: de un registro Modbus a un float.**
 *
 * Arriba de `drv_modbus`, que hace la transacción y devuelve bytes crudos. Acá
 * viven las tres cosas que convierten esos bytes en la magnitud que viaja en el
 * frame: el **codec** (el orden de los bytes), el **tipo** (u16/i16/u32/i32/
 * float) y el **divisor** por potencia de 10.
 *
 * No toca hardware, así que no es un driver: por eso vive en `tasks/`, junto a
 * `wan_frame` y `fs_datos`, que tampoco son tareas sino lógica de aplicación.
 *
 * ---------------------------------------------------------------------------
 * ⚠ QUÉ CANAL ES QUÉ DISPOSITIVO NO SE DECIDE ACÁ
 *
 * Cada canal trae su propia dirección de esclavo, así que los cinco pueden
 * estar en dispositivos distintos. Este módulo poleá lo que diga la
 * configuración y nada más.
 */

#ifndef APPLICATION_TASKS_MODBUS_H_
#define APPLICATION_TASKS_MODBUS_H_

#include <stdbool.h>
#include <stdint.h>

#include "cfg_modbus.h"
#include "drv_modbus.h"

/*------------------------------------------------------------------------------
 * ⭐ TRES INTENTOS POR CANAL (criterio de Pablo, 2026-09-21)
 *
 * El AVR **no reintenta en el poleo**: una transacción y, si falla, NaN. Pero
 * sí reintenta 3 veces en `cpres.c`, que es el mismo bus. Sobre RS485 con ruido
 * se pierden tramas, y un canal perdido es una muestra perdida para siempre.
 *
 * ⚠ El costo es tiempo con el riel prendido: 5 canales × 3 intentos × 1 s de
 * timeout son **hasta 15 s** en el peor caso, que es justamente el caso en que
 * no hay nadie contestando.
 *----------------------------------------------------------------------------*/
#define MODBUS_INTENTOS             3U

/* Entre intentos, lo justo para que el bus se aquiete. No hace falta más: el
   timeout de la transacción ya le dio al esclavo su segundo entero. */
#define MODBUS_MS_ENTRE_INTENTOS  100U

/*------------------------------------------------------------------------------
 * ⚠ Cuánto tarda en arrancar el módulo externo tras darle energía.
 *
 * Son los **5 s del AVR**, y allá están repartidos de una forma que conviene no
 * copiar sin entender: `u_poll_data()` prende `EN_PWR_QMBUS` **antes** de medir
 * las analógicas y después espera 2 s más, de modo que el caudalímetro arranca
 * mientras el INA3221 hace su barrido de 1,4 s. O sea que el tiempo total es el
 * mismo pero **no se paga**: se solapa con trabajo que había que hacer igual.
 *
 * Acá vale como constante para el comando de consola, que no tiene nada con qué
 * solaparlo. El poleo del paso 6b debería repetir el truco del AVR.
 *----------------------------------------------------------------------------*/
#define MODBUS_MS_ARRANQUE_MODULO 5000U

/*------------------------------------------------------------------------------
 * Lee UN canal y deja la magnitud en `*pfValor`.
 *
 * ⛔ **Devuelve `false` en vez de un NaN**, y es un cambio deliberado respecto
 * del AVR. Allá el error se codifica como `0xFFFFFFFF` en el `raw`, o sea un
 * NaN flotante, que después viaja en el `dataRcd`. Eso tiene dos problemas:
 * impreso con `%.3f` sale como `nan` —y el servidor recibe una palabra donde
 * espera un número—, y **un NaN se propaga en silencio** por cualquier cuenta
 * que lo toque.
 *
 * Con un `bool` el llamador está obligado a decidir qué hacer, que es lo que
 * este firmware viene haciendo en todos lados: el centinela `-9999`, el
 * `usInvalidos` del registro, la firma del RTC. **Hacer visible lo que no se
 * sabe.**
 *
 * `peRes` (puede ser NULL) recibe el resultado de la última transacción, que en
 * el banco vale más que el éxito: separa "no contesta nadie" de "contesta y el
 * CRC falla" de "contesta y rechaza el registro".
 *----------------------------------------------------------------------------*/
bool modbus_leer_canal( const cfg_modbus_canal_t *pxCanal, float *pfValor,
                        mb_result_t *peRes );

/* Los nombres del tipo y del codec NO se declaran acá: ya los expone
   `cfg_modbus.h` (`cfg_modbus_tipo_str()` / `cfg_modbus_codec_str()`), que es
   donde viven las tablas. Una segunda copia se desincronizaría el día que
   alguien agregue un tipo. */

#endif /* APPLICATION_TASKS_MODBUS_H_ */
