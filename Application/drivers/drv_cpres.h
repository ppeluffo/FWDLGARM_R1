/*
 * drv_cpres.h
 *
 * **El equipo de CONTROL DE PRESIÓN: el diálogo, no la política.**
 *
 * Es un dispositivo Modbus en el RS485, esclavo **`0x64`** fijo, alimentado por
 * el riel `EN_PWR_CPRES` (PB15). Acá vive cómo se le manda una orden y cómo se
 * sabe que terminó; **cuándo** mandarla es de `tkCtlPres`.
 *
 * Portado de `ULIBS/cpres.c` de FWDLGX, con una diferencia de fondo: aquel
 * **arma las tramas Modbus byte a byte a mano**, con su propio llamado a
 * `modbus_CRC16()`, porque el motor de allá está atado a la struct de un canal
 * configurado. Acá usa `drv_modbus_leer/escribir()`, que es exactamente para lo
 * que esa capa se separó en el paso 6.
 *
 * ---------------------------------------------------------------------------
 * ⭐ UNA CONSIGNA NO ES UNA ESCRITURA: ES UNA SECUENCIA QUE TARDA
 *
 * El dato que lo explica lo dio Pablo (2026-09-22): **las electroválvulas se
 * mueven de a una, por consumo**, nunca simultáneamente. O sea que escribir el
 * comando sólo *arranca* el trabajo; el dispositivo después se toma su tiempo.
 *
 * Por eso el registro tiene el bit **RUN** (b7): es lo único que dice cuándo
 * terminó. El diálogo completo son tres pasos, y ninguno sobra:
 *
 *   1. leer el status y **esperar a que esté IDLE** — si estaba trabajando, una
 *      orden nueva se pisaría con la anterior;
 *   2. escribir el comando (FC06);
 *   3. esperar y **volver a leer hasta IDLE** — recién ahí la consigna se
 *      aplicó de verdad.
 *
 * Una consigna completa tarda **~14 s** si todo sale al primer intento, y hasta
 * ~30 s con reintentos. Casi todo eso es el movimiento.
 *
 * ---------------------------------------------------------------------------
 * ⛔ EL DISPOSITIVO NO RECUERDA DÓNDE QUEDARON LAS VÁLVULAS
 *
 * Dato de Pablo (2026-09-22), y condiciona todo lo de arriba: **al perder
 * alimentación olvida la posición**. Al encenderlo no sabe si las válvulas están
 * abiertas o cerradas; **lo sabe recién después de una orden**.
 *
 * Eso explica un valor del status que parecía un caso raro: los bits de posición
 * admiten `2` y `3` = *desconocido*, y **ése es el estado normal cada vez que se
 * lo enciende**, no una anomalía.
 *
 * Tres consecuencias:
 *
 *   - **La posición leída NO sirve como fuente de verdad**, así que este driver
 *     la informa para diagnóstico y nada más.
 *   - **La orden es siempre absoluta**, nunca diferencial: se manda la consigna
 *     que corresponde, sin preguntar en cuál está.
 *   - ⭐ **El único que puede saber qué consigna está aplicada es el
 *     datalogger**. Ver `tkCtlPres.h`, donde se explica por qué eso NO se guarda
 *     en memoria persistente aunque se pueda.
 *
 * ⚠ **La posición física sí sobrevive al corte** —las válvulas quedan donde las
 * dejaron, confirmado por Pablo— así que lo que se pierde es quién lo sabe, no
 * el estado del proceso.
 */

#ifndef APPLICATION_DRIVERS_DRV_CPRES_H_
#define APPLICATION_DRIVERS_DRV_CPRES_H_

#include <stdbool.h>
#include <stdint.h>

#include "drv_modbus.h"

/*------------------------------------------------------------------------------
 * ⚠ ESTE ENUM ES CONTRATO CON EL DISPOSITIVO
 *
 * El número del comando **es el valor que se escribe en el registro**, así que
 * reordenarlo cambia lo que hace el equipo de presión. Es la misma clase de cosa
 * que el `[PWRMODO:%d]` del hash con el servidor: un enum que parece interno y
 * no lo es. Copiado literal de `cmd_enum` en `cpres.h` del AVR.
 *----------------------------------------------------------------------------*/
typedef enum {
    cpresCMD_NINGUNO = 0,
    cpresCMD_ABRIR_V0,
    cpresCMD_CERRAR_V0,
    cpresCMD_ABRIR_V1,
    cpresCMD_CERRAR_V1,
    cpresCMD_CONSIGNA_DIURNA,
    cpresCMD_CONSIGNA_NOCTURNA
} cpres_cmd_t;

const char *drv_cpres_cmd_str( cpres_cmd_t eCmd );

/*------------------------------------------------------------------------------
 * La dirección y el registro. Fijos en el dispositivo, no configurables.
 *----------------------------------------------------------------------------*/
#define DRV_CPRES_SLAVE             0x64U
#define DRV_CPRES_REG               1U
#define DRV_CPRES_BIT_RUN           7U      /* 1 = trabajando, 0 = idle */

/*------------------------------------------------------------------------------
 * ⭐ LOS TIEMPOS: LA ESPERA LARGA ES POR EL MOVIMIENTO, NO POR EL ARRANQUE
 *
 * Dato de Pablo (2026-09-22), y corrige lo que estaba puesto: *"en la medida que
 * NO movemos las electroválvulas, no hay que esperar más de 1 segundo para que
 * el micro de la doble consigna se active y responda. La espera larga es sólo
 * cuando damos algún comando que mueve las válvulas."*
 *
 * ⛔ **Lo que había estaba mal repartido.** El AVR espera **10 s antes del primer
 * diálogo** (`cpres_send_command()`), y yo lo había copiado suponiendo que el
 * dispositivo tardaba en estar listo —su tarea de RS485 aguarda un
 * `starting_flag`, lo cual parecía confirmarlo—. **No es así**: arranca y
 * contesta en un segundo, y esos 10 s eran precaución heredada.
 *
 * La consecuencia es concreta: **un ciclo completo pasa de ~45 s a ~20 s**, y
 * una lectura de status sin mover nada, de 12 s a 1.
 *
 * ⏳ Lo que sigue sin medir es `MS_EJECUCION` — cuánto tarda de verdad el
 * movimiento. Pablo lo dejó así por ahora: *"son sólo 2 movimientos al día."*
 *----------------------------------------------------------------------------*/
#define DRV_CPRES_MS_ARRANQUE    1000U   /* dar energía y que conteste: 1 s      */
#define DRV_CPRES_MS_EJECUCION   10000U  /* ⏳ tras el FC06: el MOVIMIENTO        */
#define DRV_CPRES_MS_ENTRE_IDLE   5000U  /* entre reintentos de "¿ya terminaste?" */
#define DRV_CPRES_MS_APAGADO     2000U   /* tras cortar, antes de devolver        */
#define DRV_CPRES_INTENTOS_IDLE      3U

/*------------------------------------------------------------------------------
 * ⭐ Manda una orden y espera a que el dispositivo la termine.
 *
 * Hace TODO el ciclo: prende el riel, dialoga y lo apaga — también en el camino
 * de error, por la misma razón que `drv_ina_medir()` duerme el INA3221 aunque
 * falle: un fallo aislado no puede dejar un consumidor encendido para siempre.
 *
 * ⚠ **NO toma el bus RS485**: lo hace el llamador, y a propósito. Quien pide una
 * consigna tiene que esperar el bus **sin timeout corto**, y esa decisión es de
 * política, no de driver. Ver `drv_rs485_tomar_bus()`.
 *
 * Bloquea ~14 s en el caso bueno. Durante casi todo ese tiempo el micro
 * **duerme**: lo único que hay encendido es un GPIO, que sobrevive al Stop 2. Es
 * el mismo razonamiento del INA3221 y de la válvula TOYI.
 *----------------------------------------------------------------------------*/
bool drv_cpres_comando( cpres_cmd_t eCmd );

/*------------------------------------------------------------------------------
 * Lee el registro de status crudo. Para el comando de diagnóstico.
 *
 * ⚠ **Asume el riel YA ENCENDIDO y el bus tomado**: sirve para mirar qué está
 * pasando en medio de una sesión, no como operación suelta.
 *----------------------------------------------------------------------------*/
mb_result_t drv_cpres_leer_status( uint16_t *pusStatus );

/* Decodifican el status. La posición sólo vale para diagnóstico — ver arriba. */
bool        drv_cpres_status_idle( uint16_t usStatus );
const char *drv_cpres_status_v0  ( uint16_t usStatus );
const char *drv_cpres_status_v1  ( uint16_t usStatus );

#endif /* APPLICATION_DRIVERS_DRV_CPRES_H_ */
