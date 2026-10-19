/*
 * eventos.h
 *
 * EL REGISTRO DE EVENTOS EN LA microSD, para estudiar un equipo en campo.
 *
 * Pedido de Pablo (2026-10-19): en campo sólo se tienen los datos que llegan al
 * servidor, y lo que más importa en una primera instalación es justamente lo que
 * NO llega —cuánto tardó una sesión, en qué paso falló, por qué se reinició—.
 *
 * ---------------------------------------------------------------------------
 * ⭐ CÓMO SE HABILITA: UN ARCHIVO EN LA TARJETA (decisión de Pablo)
 *
 *     Al arrancar, si hay microSD y tiene un archivo EVENTOS.ON, se loguea en
 *     EVENTOS.TXT de esa misma tarjeta.
 *
 * El archivo puede estar vacío: sólo importa que exista. Es para casos
 * excepcionales, un datalogger "a estudio", y por eso el control lo lleva la
 * tarjeta: **llevarse la tarjeta apaga el log**, y un equipo normal nunca paga
 * nada por esto. Se decide UNA vez, al arrancar; para cambiarlo, poner o sacar el
 * archivo y resetear.
 *
 * ---------------------------------------------------------------------------
 * ⭐ LOS EVENTOS SE ACUMULAN EN RAM Y SE ESCRIBEN DE A TANDAS
 *
 * Escribir la tarjeta en cada evento sería prenderla decenas de veces por día.
 * Se acumulan en un buffer y se vuelcan:
 *   - al final de cada sesión del modem (`tkWan`, al pasar a APAGADO), que es
 *     cuando el equipo ya está despierto;
 *   - desde el poleo (`tkSys`) cuando el buffer pasa la mitad, o tras el
 *     resumen diario — que es lo que cubre el modo CONTINUO, donde no hay fin de
 *     sesión.
 *
 * ⭐⭐ EL BUFFER VIVE EN LA SRAM2, QUE EL STARTUP NO INICIALIZA (`.ram2_noinit`),
 * así que **sobrevive a un reset por watchdog o por software**. Lo que pasó justo
 * antes de un reset —que es lo más valioso del log— se escribe en el arranque
 * siguiente, delante de la línea `ARRANQUE`. Tras un power-on el contenido es
 * basura y un encabezado con magia lo descarta.
 *
 * ---------------------------------------------------------------------------
 * ⚠ EL LOG NUNCA PUEDE TRABAR AL EQUIPO: sin tarjeta, con la tarjeta ocupada o
 * con un error de escritura, los eventos quedan en RAM y se reintenta. Si el
 * buffer se llena, los eventos NUEVOS se descartan y se cuentan, y el conteo se
 * escribe en cuanto se pueda — hacer visible lo que se perdió.
 *
 * El archivo rota a EVENTOS.OLD al pasar 1 MB: a ~30 líneas por día son años.
 */

#ifndef APPLICATION_TASKS_EVENTOS_H_
#define APPLICATION_TASKS_EVENTOS_H_

#include <stdbool.h>
#include <stdint.h>

#define EVENTOS_ACTIVADOR       "EVENTOS.ON"
#define EVENTOS_ARCHIVO         "EVENTOS.TXT"
#define EVENTOS_ARCHIVO_VIEJO   "EVENTOS.OLD"

#define EVENTOS_BUFFER_BYTES    8192U
#define EVENTOS_ARCHIVO_MAX     ( 1024UL * 1024UL )

typedef enum {
    eventosDECIDIENDO = 0,  /* todavía no se miró la tarjeta: se acumula */
    eventosACTIVO,          /* hay EVENTOS.ON: se registra               */
    eventosINACTIVO         /* no hay: no se registra nada               */
} eventos_estado_t;

/*
 * Mira la tarjeta y decide. Va en una TAREA (usa FatFs y su mutex), una vez,
 * después de que todo arrancó: la llama `tkSys`. Si queda activo escribe la
 * línea de ARRANQUE y vuelca enseguida, así lo que quedó de antes del reset
 * llega a la tarjeta sin esperar a la primera sesión.
 */
void eventos_init( void );

/*
 * Registra un evento con fecha, hora y segundos desde el arranque. printf-like,
 * una línea, sin `\r\n` (lo agrega). Desde tareas, NUNCA desde una ISR.
 * Con el registro inactivo no hace nada y no cuesta nada.
 */
void eventos_log( const char *pcFmt, ... ) __attribute__( ( format( printf, 1, 2 ) ) );

/*
 * Igual, pero SIN leer el RTC: sólo los segundos desde el arranque. Es para el
 * juez del watchdog, que no puede quedarse esperando el bus I2C justo cuando
 * alguna tarea está colgada — quizás con el bus tomado.
 */
void eventos_log_sin_hora( const char *pcFmt, ... ) __attribute__( ( format( printf, 1, 2 ) ) );

/* Escribe lo acumulado a la tarjeta. true si no quedó nada pendiente. */
bool eventos_volcar( void );

/* Para `tkSys`: hay bastante acumulado como para que valga la pena volcar. */
bool eventos_conviene_volcar( void );

eventos_estado_t eventos_estado( void );
const char      *eventos_estado_str( void );
uint32_t         eventos_pendientes_bytes( void );
uint32_t         eventos_perdidos( void );

#endif /* APPLICATION_TASKS_EVENTOS_H_ */
