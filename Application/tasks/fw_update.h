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
 * `pxInfo` admite NULL. Se llena con lo que se haya podido leer, incluso
 * cuando el resultado no es `fwHAY_NUEVO`: en un archivo corrupto, ver la
 * versión que *dice* tener ya orienta.
 */
fw_result_t fw_update_examinar( fw_info_t *pxInfo );

/*------------------------------------------------------------------------------
 * El texto de un resultado, para la consola y el registro de eventos.
 */
const char *fw_update_str( fw_result_t xRes );

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
