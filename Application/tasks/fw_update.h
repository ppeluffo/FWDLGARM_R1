/*
 * fw_update.h
 *
 * ACTUALIZAR EL FIRMWARE DESDE LA microSD.
 *
 * Para que un técnico de campo actualice un equipo con una tarjeta, sin
 * programador y sin abrir el gabinete. **No es OTA** — el día que se quiera,
 * el modem sólo tiene que dejar el archivo en la tarjeta y el resto de este
 * mecanismo no cambia.
 *
 * ──────────────────────────────────────────────────────────────────────────
 * LA ARQUITECTURA: dual bank, SIN bootloader propio
 * ──────────────────────────────────────────────────────────────────────────
 *
 *     banco 1 (512 KB)  <- corre el firmware actual
 *     banco 2 (512 KB)  <- se escribe el nuevo, MIENTRAS el otro corre
 *     commit = un option byte (BFB2)  ->  cambia el banco de arranque
 *
 * ⭐ **Es ATÓMICO, y ésa es la razón de todo el diseño.** Con un solo banco
 * hay una ventana de 10-20 s en que el equipo **no tiene firmware válido**, y
 * un corte de alimentación ahí lo deja muerto a 300 km. Acá el commit es una
 * escritura de option byte: hasta ese instante arranca el viejo, después el
 * nuevo. **El estado "a medio programar" no existe.**
 *
 * Lo verificado contra los documentos, que es lo que hace viable esto:
 *
 * | | |
 * |---|---|
 * | **RM0351 §3.3.8** | *"While executing a program code from bank 1, it is possible to perform a page erase operation on bank 2"* — se puede escribir el banco 2 con el equipo corriendo |
 * | ⭐⭐ **AN2606, Figura 102** | el *dual bank boot* del L496: si el primer word del banco 2 **no apunta a SRAM**, el micro **arranca del banco 1**. Es un FALLBACK AUTOMÁTICO del silicio |
 * | ⭐ **"Set Bank Swap to Bank2"** | el banco activo queda mapeado en `0x08000000`, así que **el mismo binario sirve para los dos bancos**: no hay que relinkear |
 * | ✅ El L496 usa el **Pattern 6** | ninguna de sus condiciones menciona `BFB2`, así que en operación normal **el bootloader de ROM no toma ningún pin** (su tabla de periféricos es del camino "Continue Bootloader execution", que sólo se recorre si NINGÚN banco es válido) |
 *
 * ⚠ **El chequeo del silicio es débil a propósito y por eso el CRC es nuestro:**
 * el bootloader sólo mira que *el primer word apunte al rango de SRAM*. Eso
 * distingue "banco en blanco" de "banco escrito", **no** "escrito bien" de
 * "escrito con basura". El reparto queda:
 *
 *     el bootloader de ST   evita el banco EN BLANCO
 *     nuestro CRC32         evita el banco con BASURA
 *     la marca EN PRUEBA    evita el firmware que ARRANCA y NO FUNCIONA
 *
 * ──────────────────────────────────────────────────────────────────────────
 * EL ARCHIVO
 * ──────────────────────────────────────────────────────────────────────────
 *
 * `FIRMWARE.BIN` en la raíz de la tarjeta: 64 bytes de header y la imagen
 * cruda. Lo arma `Tools/mkfw.py` a partir del `.elf`.
 *
 * ⛔ **NO sirve el `.elf`**: es un contenedor con headers, secciones, símbolos
 * y debug —288 KB en Debug contra 176 de imagen real— y cargarlo pediría
 * parsear sus *program headers*. El `.bin` se copia tal cual.
 *
 * ⛔ **Y la versión NO viene del nombre del archivo**: es manipulable y no
 * verificable. Viaja en el header, junto al CRC que la respalda.
 *
 * ⭐ **`fw_hw` no es decorativo**: evita que el firmware de otro producto entre
 * en este equipo. Mismo criterio que el IMEI falso — hacer el error imposible
 * en vez de confiar en que nadie se equivoque.
 *
 * ──────────────────────────────────────────────────────────────────────────
 * ⚠ CUALQUIER VERSIÓN DISTINTA, NO SÓLO UNA SUPERIOR
 * ──────────────────────────────────────────────────────────────────────────
 *
 * Decisión de Pablo, reconfirmada el 2026-10-30. Si sólo aceptara versiones
 * mayores, **volver atrás obligaría a fabricar un número de versión falso** — y
 * volver atrás es justo lo que se necesita cuando algo sale mal.
 *
 * ⭐ Lo que evita reprogramar en bucle no es la comparación: es que el archivo
 * se **renombra a `.OK`** después de aplicarlo.
 */

#ifndef APPLICATION_TASKS_FW_UPDATE_H_
#define APPLICATION_TASKS_FW_UPDATE_H_

#include <stdbool.h>
#include <stdint.h>

#define FW_UPDATE_ARCHIVO       "FIRMWARE.BIN"
#define FW_UPDATE_APLICADO      "FIRMWARE.OK"

/*------------------------------------------------------------------------------
 * ⭐⭐ ETAPA 6 — EL DISPARO AUTOMÁTICO AL ARRANCAR. Su interruptor.
 *
 * En 1 el equipo se actualiza solo: lee la tarjeta al arrancar y, si hay un
 * firmware válido y distinto, lo graba, lo aplica y resetea. En 0 el código
 * entra al binario pero `fw_update_auto()` **sale en su primera línea**, y el
 * ciclo sigue siendo el que tipea un técnico (`fw grabar` + `fw commit
 * aplicar`).
 *
 * ⚠ TIENE INTERRUPTOR PROPIO, COMO EL WATCHDOG, Y POR LA MISMA RAZÓN. Las
 * etapas 1 a 5 no tocan el camino de ARRANQUE —todo corre dentro de un comando,
 * con el técnico presente— y ésta sí. El arranque es **lo único que validaron
 * las 188 h de campo**, así que la bajada que TRAE este código no cambia nada y
 * encenderlo es una decisión aparte. Es la misma asimetría del `ARRANCA_WDG`:
 * una vez que el equipo se reprograma solo, un error ahí se ve desde afuera
 * como *"la placa no arranca"*.
 *
 * ⭐ Y el `#if` vive en el `.c`, no en el llamador: así `tkCmd` no necesita
 * ningún preprocesador y no se puede repetir la trampa de un `#if` sobre un
 * define cuyo `#include` está más abajo —que en este proyecto ya mordió TRES
 * veces, y una fue justamente con `main.h` en `tkCtl.c`—.
 */
#define FW_UPDATE_AUTO          1

#define FW_UPDATE_MAGIC         0x46515053UL    /* "SPQF" en little-endian   */
#define FW_UPDATE_HDR_VER       1U
#define FW_UPDATE_HDR_LEN       64U
#define FW_UPDATE_VER_LEN       16U
#define FW_UPDATE_HW_LEN        16U

/* 512 KB. ⚠ Y el `.ld` hay que acotarlo a esto: hoy dice 1024K, o sea los dos
   bancos, así que nada impide que la aplicación crezca sobre su propio
   respaldo. Con 176 KB no se cruza, pero el linker no lo garantiza. */
#define FW_UPDATE_BANCO_BYTES   ( 512UL * 1024UL )

/*------------------------------------------------------------------------------
 * ⭐ El resultado dice QUÉ pasó, no sólo que falló.
 *
 * Es el criterio del `mb_result_t` de Modbus y del `lteESC_*` del modem: en el
 * banco —y más todavía en campo, con un técnico al teléfono— saber si el
 * archivo está corrupto, si es de otro producto o si simplemente es la misma
 * versión manda a mirar lugares completamente distintos.
 */
typedef enum {
    fwHAY_NUEVO = 0,    /* ⭐ válido y DISTINTO del instalado: se puede aplicar */
    fwSIN_TARJETA,      /* no hay microSD                                      */
    fwNO_HAY,           /* la tarjeta no tiene FIRMWARE.BIN                    */
    fwNO_SE_PUDO_LEER,  /* está pero no se pudo leer                           */
    fwMAGIC,            /* no es un archivo de este mecanismo                  */
    fwHDR_VER,          /* un formato de header que este firmware no conoce    */
    fwHDR_CRC,          /* el header está corrupto                             */
    fwHW,               /* ⛔ es el firmware de OTRO producto                   */
    fwLARGO,            /* el largo no cierra, o no entra en el banco          */
    fwIMG_CRC,          /* ⛔ la imagen está corrupta: NO se escribe nada        */
    fwMISMA_VERSION     /* es la que ya está corriendo: no hay nada que hacer  */
} fw_result_t;

/*------------------------------------------------------------------------------
 * El resultado del COMMIT (copiar el banco 2 al banco 1).
 *
 * ⭐ Dice QUÉ falló, no sólo que falló — mismo criterio que `mb_result_t` y
 *    que los `lteESC_*`: en el banco, saber si falló el borrado o la
 *    verificación manda a mirar lugares distintos.
 *
 * ⚠ En TODOS los caminos de error el banco 1 queda SIN su primer doubleword a
 *   propósito, así que el equipo arranca el bootloader de ST en vez de ejecutar
 *   un firmware a medias. Es feo y es recuperable; lo otro no.
 */
typedef enum {
    fwCMT_OK = 0,        /* no se ve nunca: si sale bien, resetea y no vuelve  */
    fwCMT_SIN_IMAGEN,    /* el banco 2 está vacío o su CRC no cierra           */
    fwCMT_BORRADO,       /* falló el mass erase del banco 1                     */
    fwCMT_ESCRITURA,     /* falló programando un doubleword                     */
    fwCMT_VERIFICACION,  /* ⛔ se escribió y al releer NO coincide              */
    fwCMT_NO_EN_RAM      /* ⛔ una funcion del commit NO quedo en RAM          */
} fw_commit_t;

const char *fw_update_commit_str( fw_commit_t xRes );

/*==============================================================================
 * LA MARCA DE "EN PRUEBA" — el rollback (etapa 5)
 *------------------------------------------------------------------------------
 * ⛔ EL PROBLEMA QUE RESUELVE, Y EL QUE NO:
 *
 *   ✅ el firmware nuevo ARRANCA pero NO FUNCIONA — no transmite, se cuelga a
 *      los diez minutos, no habla con un sensor. **Ese es el caso de campo**:
 *      el técnico se fue, el equipo "anda", y nadie se entera por meses.
 *
 *   ⛔ el firmware nuevo NO ARRANCA en absoluto (HardFault antes del scheduler).
 *      Ahí **no corre nada**, así que ningún mecanismo en software puede ayudar:
 *      hace falta el ST-LINK o el bootloader de ST. No se promete lo contrario.
 *
 * ⭐ EL CRITERIO DE "FUNCIONA" YA EXISTE Y NO HAY QUE INVENTARLO: una sesión que
 *    llega a `CONFIG=OK` y transmite. Eso ejercita el modem, el I2C, la EEPROM,
 *    el RTC y el ADC de un saque — mejor que cualquier autotest que
 *    escribiéramos, y es lo que el equipo tiene que hacer igual.
 *
 * ⚠ DÓNDE VIVE: EEPROM `0x00400`. Idea de Pablo, y es mejor que la SRAM del
 *   MCP79410 por una razón concreta — la EEPROM **ya es dependencia crítica**
 *   (la configuración está ahí), así que no agrega un punto de falla; y si
 *   fallara **se grita**: checksums malos, defaults y el servidor pidiendo los
 *   cinco bloques. La SRAM del RTC, en cambio, ya falló dos veces acá —la FAT
 *   y `LOTE9041`— y su fallo es **silencioso**.
 *
 * ⚠ NO en los primeros bytes: `0x00000` es el bloque `base`, y moverlo
 *   invalidaría las configuraciones ya grabadas en los equipos.
 *============================================================================*/

#define FW_PRUEBA_ADDR          0x00400UL
#define FW_PRUEBA_MAGIC         0x42555250UL    /* "PRUB" en little-endian */

/* Cuántos arranques sin confirmar se toleran antes de dar el firmware por malo.
 * ⭐ Son ARRANQUES, no tiempo: un firmware que se cuelga da reset por watchdog y
 *    los gasta rápido; uno que anda confirma en el primero. */
#define FW_PRUEBA_MAX_ARRANQUES 5U

typedef struct __attribute__( ( packed ) ) {
    uint32_t ulMagic;
    char     pcVersionPrevia[ FW_UPDATE_VER_LEN ];  /* a la que se volvería   */
    uint8_t  ucArranques;                           /* sin confirmar          */
    uint8_t  ucChecksum;                            /* ⚠ con offsetof         */
} fw_prueba_t;

/* La pone `fw commit` justo antes de copiar. `pcVersionPrevia` es la que se
   está reemplazando, para poder nombrarla en los avisos. */
bool fw_prueba_poner( const char *pcVersionPrevia );

/* true si hay una marca válida. `pxM` puede ser NULL. */
bool fw_prueba_leer( fw_prueba_t *pxM );

/* Suma un arranque y lo graba. Devuelve cuántos van, o 0 si no hay marca. */
uint8_t fw_prueba_contar( void );

/* ⭐ El firmware se dio por bueno: borra la marca. Lo llama `tkWan` cuando una
   sesión llega a `CONFIG=OK`. Es idempotente: sin marca no hace nada. */
void fw_prueba_confirmar( void );


/*------------------------------------------------------------------------------
 * Pone en marcha el firmware del banco 2 COPIÁNDOLO al banco 1, y resetea.
 *
 * ⛔⛔ NO RETORNA si sale bien.
 *
 * ⭐ Por qué copiar y no saltar con `BFB2` o con `FB_MODE`: el salto mete código
 *    en el camino de ARRANQUE, que es lo único que validaron las 188 h de campo,
 *    y la asimetría decide — si falla el commit son 82 µs y un estado
 *    recuperable; si falla el arranque, el equipo no arranca, y eso pasa en
 *    campo a 300 km. (`BFB2` además NO funciona en esta placa: ver CLAUDE.md.)
 *
 * ⚠ `ulLargo` son los bytes a copiar, múltiplo de 8. Con 0 copia el banco
 *   entero salteándose lo que ya está borrado, que da lo mismo en tiempo y no
 *   depende de que el llamador sepa el largo.
 */
fw_commit_t fw_update_commit( uint32_t ulLargo );

typedef struct {
    char     pcVersion[ FW_UPDATE_VER_LEN ];
    char     pcHw[ FW_UPDATE_HW_LEN ];
    uint32_t ulLargo;       /* bytes de la imagen, sin el header */
    uint32_t ulCrc;         /* el que declara el header          */
    uint32_t ulCrcLeido;    /* el que se calculó al leerla       */
} fw_info_t;

/*------------------------------------------------------------------------------
 * Mira la tarjeta y dice si hay un firmware para aplicar. **No escribe nada.**
 *
 * Lee el archivo entero para verificar el CRC de la imagen, así que tarda unos
 * segundos — y es a propósito: **un CRC que no se verifica no sirve de nada**,
 * y el único momento barato para verificarlo es antes de tocar la flash.
 *
 * `pcArchivo` admite NULL (usa `FIRMWARE.BIN`). `pxInfo` admite NULL. Se llena con lo que se haya podido leer, incluso
 * cuando el resultado no es `fwHAY_NUEVO`: en un archivo corrupto, ver la
 * versión que *dice* tener ya orienta.
 */
fw_result_t fw_update_examinar( const char *pcArchivo, fw_info_t *pxInfo );

/*------------------------------------------------------------------------------
 * ⭐ ETAPA 3: ESCRIBE LA IMAGEN EN EL **BANCO 2** y la verifica releyéndola.
 *
 * ⚠ **No cambia el banco de arranque.** Al terminar, el equipo sigue
 * arrancando del banco 1 — con éxito o sin él. Eso es lo que hace que esta
 * etapa no pueda dejar el equipo muerto: el commit es la etapa 4.
 *
 * El orden importa y no es el obvio:
 *
 *   1. examinar          <- el CRC del ARCHIVO, antes de tocar nada
 *   2. borrar el banco 2
 *   3. escribir
 *   4. ⭐ releer DESDE LA FLASH y recalcular el CRC
 *
 * ⭐⭐ El paso 4 es el que convierte esto en verificable: **no se le cree a que
 * `HAL_FLASH_Program()` devolvió OK**. Una celda que no tomó el dato, una
 * página que no se borró del todo o un bit que se cae con el tiempo devuelven
 * OK igual. Lo único que prueba que el banco 2 tiene el firmware es leerlo.
 *
 * Tarda ~10 s: borrar 122 páginas, escribir 249 KB de a 64 bits y releerlos.
 */
bool fw_update_grabar( const char *pcArchivo, fw_info_t *pxInfo,
                       fw_result_t *pxRes );

/*------------------------------------------------------------------------------
 * El CRC de lo que HAY en el banco 2 ahora, sobre `ulLargo` bytes. Para
 * verificar sin reescribir —y, en la etapa 4, para decidir si el banco 2 es
 * confiable antes de arrancar de él.
 */
uint32_t fw_update_crc_banco2( uint32_t ulLargo );

/*------------------------------------------------------------------------------
 * ⭐⭐ ¿EL BANCO 2 PARECE UNA TABLA DE VECTORES?
 *
 * Es un chequeo de cordura ANTES del commit: con `fw grabar` el CRC ya
 * garantizó que la flash tiene lo mismo que el archivo, pero no que el archivo
 * sea un firmware. Esto mira las dos primeras palabras —el stack pointer
 * inicial y el vector de reset—, que son lo único que un Cortex-M necesita
 * para arrancar.
 *
 * ⚠ **Avisa pero NO impide**, que es el criterio de todo este firmware.
 *
 * ⛔ LA PREGUNTA ERA OTRA HASTA EL 2026-10-30, y conviene saber por qué
 * cambió: era *"¿el bootloader va a aceptar el banco 2?"*, porque con
 * `BFB2 = 1` el silicio arrancaba del banco 2 sólo si su primer word apuntaba
 * a SRAM (AN2606, fig. 102) y **caía al banco 1 en silencio** si no. Con `BFB2`
 * abandonado y el commit copiando bytes, el bootloader ya no decide nada.
 *
 * ⭐ Y el cambio no fue cosmético: el criterio viejo —comparar el SP del banco
 * 2 contra el del banco 1— dio un **falso positivo medido en banco**. Ver
 * `fw_update_estado_banco2()` en el `.c`.
 */
typedef struct {
    uint32_t ulSpBanco1;    /* primer word del banco 1: el que SÍ arranca */
    uint32_t ulSpBanco2;    /* primer word del banco 2                    */
    uint32_t ulResetBanco2; /* su vector de reset, para el volcado        */
    bool     bEnBlanco;     /* el banco 2 está sin escribir (0xFFFFFFFF)  */
    bool     bPlausible;    /* SP en SRAM1 y reset en flash, impar       */
} fw_banco2_t;

void fw_update_estado_banco2( fw_banco2_t *pxEstado );

/*==============================================================================
 * ETAPA 4 — EL COMMIT: de qué banco arranca el equipo
 *============================================================================*/

/*------------------------------------------------------------------------------
 * ⭐⭐ DE QUÉ BANCO ESTÁ CORRIENDO AHORA MISMO.
 *
 * Lo dice el bit `FB_MODE` de `SYSCFG_MEMRMP`: el bootloader lo pone en 1
 * cuando salta al banco 2 ("Set Bank Swap to Bank2", AN2606 fig. 102), y ahí el
 * banco 2 queda mapeado en `0x08000000`.
 *
 * ⭐ **Es el único dato que no se puede deducir de otra cosa.** La versión del
 * firmware no sirve —los dos bancos pueden tener la misma— y la dirección
 * tampoco, porque con el swap el banco activo siempre se ve en `0x08000000`.
 *
 * Devuelve 1 ó 2.
 */
uint8_t fw_update_banco_activo( void );

/*------------------------------------------------------------------------------
 * De qué banco va a arrancar la PRÓXIMA vez: el option byte `BFB2`.
 *
 * ⚠ Comparar esto con `fw_update_banco_activo()` es el diagnóstico más útil de
 * todo el mecanismo:
 *
 *   BFB2 dice 2  y  estoy en 2  ->  todo normal
 *   BFB2 dice 2  y  estoy en 1  ->  ⛔ **EL FALLBACK ACTUÓ**: el bootloader no
 *                                   aceptó el banco 2 y cayó al 1. Sin esta
 *                                   comparación eso es invisible
 *   BFB2 dice 1  y  estoy en 1  ->  normal
 *
 * Devuelve 1 ó 2.
 */
uint8_t fw_update_banco_de_arranque( void );

/*------------------------------------------------------------------------------
 * ⛔⛔ CAMBIA EL BANCO DE ARRANQUE. **RESETEA EL EQUIPO Y NO RETORNA.**
 *
 * Escribe el option byte `BFB2` y llama a `HAL_FLASH_OB_Launch()`, que recarga
 * los option bytes **con un reset del sistema**. Todo lo que tenga que quedar
 * guardado hay que guardarlo ANTES.
 *
 * ⭐ **La simetría es lo que hace segura esta etapa**: como el mismo binario
 * corre en los dos bancos (el swap lo mapea en `0x08000000`), este comando
 * existe también del otro lado. Si el banco 2 arranca y no convence, `fw boot
 * 1` vuelve. Y si no arranca, el fallback del silicio trae el banco 1 — donde
 * el comando también está.
 *
 * ⚠ `MODIFY_REG` con máscara: escribir `BFB2` **no toca** `IWDG_SW`, `BOR_LEV`
 * ni el resto de los option bytes (verificado en `FLASH_OB_UserConfig()`).
 *
 * Devuelve false sólo si NO pudo escribir; si escribió, no vuelve.
 */
bool fw_update_arrancar_de( uint8_t ucBanco );

/*------------------------------------------------------------------------------
 * El texto de un resultado, para la consola y el registro de eventos.
 */
const char *fw_update_str( fw_result_t xRes );

/*==============================================================================
 * ETAPA 6 — EL CICLO COMPLETO, SOLO, AL ARRANCAR
 *============================================================================*/

/*------------------------------------------------------------------------------
 * Mira la tarjeta y, si hay un firmware válido y distinto, lo aplica.
 *
 * ⛔⛔ **NO RETORNA si aplica**: termina en `fw_update_commit()`, que resetea.
 *
 * Es la secuencia que hasta ahora tipeaba un técnico, con el orden que importa:
 *
 *     1. examinar            <- el CRC del archivo ENTERO, antes de tocar flash
 *     2. prórroga del watchdog
 *     3. grabar el banco 2   <- y verificarlo RELEYENDO la flash
 *     4. ¿parece una tabla de vectores?
 *     5. ⭐ RENOMBRAR a FIRMWARE.OK   <- ANTES del commit, y es obligatorio
 *     6. la marca de "a prueba" + el registro de eventos, volcado
 *     7. commit  ->  no vuelve
 *
 * ⭐⭐ EL PASO 5 VA ANTES DEL 7 PORQUE EL 7 NO VUELVE. Y si el renombrado
 * falla, **NO se commitea**: sin él no hay nada que corte el bucle de
 * reprogramación (ver `fs_sd_renombrar()`). Abortar deja el equipo exactamente
 * como estaba —el banco 1 intacto, el banco 2 con la imagen ya verificada— y el
 * técnico completa a mano con `fw commit aplicar`. La asimetría decide: un
 * aviso contra un equipo que se reprograma en cada arranque.
 *
 * ⚠ Se CALLA cuando no hay tarjeta o la tarjeta no tiene `FIRMWARE.BIN`, que es
 * el caso de todos los arranques de todos los equipos. Cualquier otra cosa
 * —archivo corrupto, de otro producto, imagen con el CRC mal— **se dice y no se
 * aplica**: es información de campo que de otro modo nadie vería.
 *
 * ⭐ Y de yapa sale gratis el rollback automático: si la marca de "a prueba" se
 * pasó del límite y el técnico dejó en la tarjeta el firmware anterior, este
 * mismo camino lo repone sin que nadie vaya al sitio.
 */
void fw_update_auto( void );

/*------------------------------------------------------------------------------
 * CRC-32, el de zlib (polinomio 0xEDB88320 reflejado, init y xorout
 * 0xFFFFFFFF). Se calcula por tramos: arrancar con `ulPrev = 0`.
 *
 * ⚠ **Por software y no con el periférico CRC del micro**, a propósito: el del
 * STM32 arranca con el polinomio de Ethernet sin reflejar y hay que configurar
 * `REV_IN`, `REV_OUT`, el valor inicial y el XOR final para que dé lo mismo que
 * zlib. Son cuatro campos que, puestos mal, dan un número **plausible y
 * distinto** — y encima habría que habilitar el periférico en CubeMX. Esta
 * tabla son 64 bytes de flash y se verifica contra `zlib.crc32` en el host.
 */
uint32_t fw_update_crc32( uint32_t ulPrev, const void *pvDatos, uint32_t ulLargo );

#endif /* APPLICATION_TASKS_FW_UPDATE_H_ */
