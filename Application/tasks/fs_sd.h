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

/*------------------------------------------------------------------------------
 * Leer un lote para transmitirlo: abrir / leer línea / cerrar.
 *
 * `fs_sd_lote_abrir()` toma **el más viejo** y deja el archivo abierto y la
 * tarjeta encendida; `fs_sd_lote_leer()` devuelve la línea siguiente **sin el
 * CRLF** (el frame no lo lleva); `fs_sd_lote_cerrar( bBorrar )` cierra, borra el
 * archivo si se le dice, y apaga la tarjeta.
 *
 * ⚠ **El archivo queda abierto durante todo el envío** —pueden ser 1984 líneas
 * y varios minutos— y es deliberado: mientras se transmite el **modem consume
 * decenas de mA** contra los 0,2-1 mA de la microSD, así que remontar la
 * tarjeta cada pocas líneas sería pagar ~200 ciclos de montaje por lote para
 * ahorrar ruido. Leer el lote entero a RAM tampoco es opción: son ~300 KB
 * contra los 256 KB del micro.
 *
 * ⚠ **`bBorrar` va en true sólo si el lote ENTERO se confirmó.** Si se corta en
 * el medio, el archivo queda y se retransmite completo — con duplicados de lo
 * que ya había llegado, que son inofensivos porque el servidor indexa por la
 * fecha de cada frame. Es la decisión acordada con Pablo el 2026-09-11: un
 * puntero de línea persistente sería un estado más que se puede corromper, para
 * evitar algo que no hace daño.
 *----------------------------------------------------------------------------*/
bool fs_sd_lote_abrir ( char *pcNombre, uint16_t usSize );
bool fs_sd_lote_leer  ( char *pcLinea,  uint16_t usSize );
void fs_sd_lote_cerrar( bool bBorrar );
bool fs_sd_borrar_lote   ( const char *pcNombre );

void fs_sd_stats( fs_sd_stats_t *pxStats );

/*------------------------------------------------------------------------------
 * FORMATEA la tarjeta en FAT, con tabla de particiones, desde el propio equipo.
 *
 * Existe porque el criterio de Pablo (2026-09-09) es que **las tarjetas se
 * trabajen sólo en el datalogger**: un técnico que cambia una microSD en el
 * campo no tiene una PC al lado, y las tarjetas nuevas de más de 32 GB vienen
 * en **exFAT**, que esta configuración de FatFs no entiende (`_FS_EXFAT = 0`) y
 * rechaza con `FR_NO_FILESYSTEM`.
 *
 * ⛔ **BORRA TODO LO QUE HAYA EN LA TARJETA**, incluidos los lotes que todavía no
 * se transmitieron. Por eso el comando de consola exige una palabra de
 * confirmación: un `fs sd format` tipeado de más no puede llevarse los datos de
 * una instalación.
 *
 * ⚠ **Tarda**: escribe las dos copias de la FAT sector por sector —`drv_sd` no
 * expone escritura múltiple— así que en una tarjeta grande son varios segundos
 * con la tarea bloqueada. Es una operación de mantenimiento, no de campo.
 *----------------------------------------------------------------------------*/
bool fs_sd_format( void );

/*------------------------------------------------------------------------------
 * Indicador de actividad para las operaciones largas.
 *
 * Lo llama `USER_write()` del diskio cada vez que escribe un sector — es el único
 * punto que sabe que `f_mkfs()` está avanzando, porque desde afuera la llamada es
 * un bloque opaco de varios segundos.
 *
 * ⚠ El diskio sólo **avisa**; qué mostrar (y si mostrar algo) se decide acá. Así
 * la capa que toca el hardware no sabe nada de la consola.
 *
 * Fuera de una operación larga no hace nada, así que el costo en el camino normal
 * es una comparación.
 *----------------------------------------------------------------------------*/
void fs_sd_progreso( void );

/* Lista los lotes por consola. */
void fs_sd_listar( void );

/* Muestra las primeras `usLineas` de un lote, para verificar sin sacar la
   tarjeta. */
void fs_sd_ver( const char *pcNombre, uint16_t usLineas );

/*------------------------------------------------------------------------------
 * Vuelca la traza de pulsos (`caudal_log`) a un `PULSOSnn.CSV`.
 *
 * ⚠ Si la tarjeta no está, **la traza NO se pierde**: queda en RAM y se
 * reintenta. Lo mismo que hace la ventana de datos.
 *----------------------------------------------------------------------------*/
bool fs_sd_volcar_pulsos( void );

#endif /* APPLICATION_TASKS_FS_SD_H_ */
