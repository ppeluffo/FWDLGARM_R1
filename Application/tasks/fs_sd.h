/*
 * fs_sd.h
 *
 * La microSD como EXTENSIÓN del almacén: cuando la ventana en EEPROM se llena,
 * su contenido se vuelca a un archivo de la tarjeta.
 *
 * Diseño acordado con Pablo el 2026-09-08. Ver CLAUDE.md, "Paso 4b".
 *
 * ---------------------------------------------------------------------------
 * POR QUÉ HAY UNA VENTANA Y NO SE ESCRIBE DIRECTO A LA SD
 *
 * El problema de la SD no es la capacidad: es **el costo de cada acceso**.
 * Encenderla, re-inicializar la tarjeta, montar, escribir, desmontar y apagar
 * son cientos de milisegundos y bastante energía, contra los 5 ms de una
 * escritura I2C a la EEPROM. Poleando cada minuto, escribir la SD en cada
 * muestra serían **1440 ciclos por día**.
 *
 * Con la ventana, la SD se toca **una vez cada 33 horas** —1984 registros a una
 * muestra por minuto— en una operación grande y previsible.
 *
 * Y hay una razón de robustez además de la de energía: **FAT es frágil ante un
 * corte de alimentación a mitad de escritura**. No se pierde el último registro,
 * se puede perder la tabla de asignación entera. La EEPROM no tiene estructura
 * que corromper, así que absorbe el ciclo de cada muestra y la SD sólo recibe
 * volcados espaciados, que es cuando ese riesgo se puede acotar.
 *
 * ⚠ **Si no hay tarjeta, no pasa nada malo**: la ventana sigue funcionando como
 * buffer circular y el equipo transmite desde ahí, exactamente como antes de que
 * la SD existiera. Es una extensión, no una dependencia.
 *
 * ---------------------------------------------------------------------------
 * QUÉ SE GUARDA: LOS FRAMES YA ARMADOS, EN TEXTO
 *
 * Un frame por línea. Cuesta unas 3 veces más espacio que el registro binario,
 * lo que en una tarjeta de GB es irrelevante, y a cambio:
 *
 *   - los archivos se leen en cualquier PC, sin herramientas;
 *   - **desaparece el problema de "configuración nueva con datos viejos"**: el
 *     frame guardado ya lleva los nombres con los que se midió, así que cambiar
 *     la configuración no reinterpreta lo que ya está en la tarjeta;
 *   - al transmitir no hay que rearmar nada: la línea se manda tal cual.
 *
 * ---------------------------------------------------------------------------
 * LOS NOMBRES: UN CONTADOR SECUENCIAL, NO LA FECHA
 *
 * `LOTE0001.DAT`, `LOTE0002.DAT`… El contador vive junto a la FAT en la SRAM del
 * RTC. **No se usa la fecha a propósito**: si el reloj arrancó frío, dos lotes
 * distintos tendrían el mismo nombre y uno pisaría al otro. La fecha ya viaja
 * adentro de cada frame, que es donde importa.
 */

#ifndef APPLICATION_TASKS_FS_SD_H_
#define APPLICATION_TASKS_FS_SD_H_

#include <stdbool.h>
#include <stdint.h>

/* 8.3, que es lo que admite FatFs con _USE_LFN = 0. */
#define FS_SD_PREFIJO           "LOTE"
#define FS_SD_EXTENSION         ".DAT"
#define FS_SD_NOMBRE_LARGO      13U     /* 8 + '.' + 3 + NUL */

typedef struct {
    bool     bPresente;         /* hay tarjeta y se pudo montar   */
    uint16_t usLotes;           /* archivos de datos sin transmitir */
    uint32_t ulProximoLote;     /* el número que va a llevar el que viene */
    uint32_t ulLibreKB;         /* espacio libre                  */
} fs_sd_stats_t;

/*------------------------------------------------------------------------------
 * Vuelca a un archivo nuevo TODO lo que haya en la ventana, y recién si eso sale
 * bien la vacía.
 *
 * ⚠ El orden importa y es la única garantía real: si se vaciara la ventana antes
 * de confirmar que el archivo cerró bien, un corte de alimentación en el medio
 * se llevaría los datos de los dos lados a la vez.
 *
 * Devuelve false si no hay tarjeta o si algo falló; en ese caso **la ventana
 * queda intacta**, que es lo que corresponde: los datos siguen estando.
 *----------------------------------------------------------------------------*/
bool fs_sd_volcar_ventana( void );

/*------------------------------------------------------------------------------
 * Para la transmisión (paso 5). El orden de la sesión lo fijó Pablo: **primero
 * la ventana y después los archivos**. No es cronológico y está bien —el
 * servidor indexa por la fecha que viaja en cada frame— y de paso libera la
 * ventana al principio de la sesión, justo antes de la parte larga.
 *----------------------------------------------------------------------------*/
bool fs_sd_lote_mas_viejo( char *pcNombre, uint16_t usSize );
bool fs_sd_borrar_lote   ( const char *pcNombre );

void fs_sd_stats( fs_sd_stats_t *pxStats );

/* Lista los lotes por consola. */
void fs_sd_listar( void );

/* Muestra las primeras `usLineas` de un lote, para verificar sin sacar la
   tarjeta. */
void fs_sd_ver( const char *pcNombre, uint16_t usLineas );

#endif /* APPLICATION_TASKS_FS_SD_H_ */
