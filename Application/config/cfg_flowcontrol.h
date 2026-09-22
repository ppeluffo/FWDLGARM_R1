/*
 * cfg_flowcontrol.h
 *
 * **FLOWCONTROL: una tabla semanal de horarios para la electroválvula TOYI
 * interna.** Portado de `XLIBS/flowcontrol.c` de FWDLGX.
 *
 * Hasta **14 slots**, cada uno `{día de la semana, hora, abrir|cerrar}`. A esa
 * hora de ese día, el equipo mueve la válvula.
 *
 * ---------------------------------------------------------------------------
 * ⚠ EL NOMBRE ENGAÑA: NO TIENE NADA QUE VER CON EL CAUDAL
 *
 * "Flowcontrol" suena a *control de caudal* y durante un tiempo quedó anotado en
 * el plan como que dependía del paso 2b (el EMA del contador). **Es falso**: no
 * mira ningún caudal, es un temporizador semanal.
 *
 * Estructuralmente es **lo mismo que `tkCtlPres`**: tabla de horarios, igualdad
 * exacta de `hhmm`, acción sobre una válvula. Lo que cambia es que el slot lleva
 * además el día, y que la válvula es la interna (un GPIO) en vez de un
 * dispositivo Modbus.
 *
 * ---------------------------------------------------------------------------
 * 🔨 ALCANCE DE ESTA ETAPA (Pablo, 2026-09-22)
 *
 * Textual: *"Con tkFlow vamos a hacer sólo configuración y luego sólo
 * implementamos las órdenes que se pueden mandar por tkWAN. No implementamos
 * ahora la apertura y cierre de acuerdo a la tabla de horarios."*
 *
 * O sea que **la tabla se configura, se guarda y viaja en el hash, pero NO se
 * ejecuta todavía**.
 *
 * ⚠ **Y eso hay que gritarlo en la consola**, porque si no un técnico configura
 * slots, los ve guardados, y espera que la válvula se mueva sola. Un equipo que
 * *parece* hacer algo que no hace es peor que uno que no lo ofrece: por eso
 * `config` lo dice cada vez que imprime la tabla.
 *
 * ---------------------------------------------------------------------------
 * ⭐ CON ESTE BLOQUE SE CIERRA EL `FLOWC` QUE EL SERVIDOR PIDE SIEMPRE
 *
 * Es el **sexto hash**, el `FH`. Hasta ahora el equipo mandaba cinco y el
 * servidor tomaba uno por defecto para éste, así que **`CONF_ALL` nunca podía
 * contestar `CONFIG=OK`** y pedía reconfigurar `FLOWC` en todas las sesiones
 * (autorizado por Pablo el 2026-09-11, como situación transitoria). Con el
 * bloque implementado eso se termina.
 */

#ifndef APPLICATION_CONFIG_CFG_FLOWCONTROL_H_
#define APPLICATION_CONFIG_CFG_FLOWCONTROL_H_

#include <stdbool.h>
#include <stdint.h>

#define CFG_FLOW_NRO_SLOTS      14U

/*------------------------------------------------------------------------------
 * ⚠ EL DÍA VIAJA COMO NÚMERO EN EL HASH, así que estos valores son contrato.
 *
 * `LU`=1 … `DO`=7, y **8 = el slot está deshabilitado** — que es el default y lo
 * que el AVR pone ante cualquier string que no reconozca. Su servicio saltea
 * todo lo que sea `dow > 7`.
 *----------------------------------------------------------------------------*/
#define CFG_FLOW_DOW_LIBRE      8U

typedef struct {
    uint8_t  ucDow;         /* 1..7 = lu..do ; 8 = slot sin usar */
    uint16_t usPtime;       /* hhmm                              */
    bool     bAbrir;        /* true = OPEN, false = CLOSE        */
} cfg_flow_slot_t;

typedef struct {
    bool            bEnabled;
    cfg_flow_slot_t xSlot[ CFG_FLOW_NRO_SLOTS ];
    uint8_t         ucChecksum;
} cfg_flowcontrol_t;

extern cfg_flowcontrol_t xCfgFlow;

void    cfg_flowcontrol_defaults( void );
void    cfg_flowcontrol_print( void );

/*------------------------------------------------------------------------------
 * ⚠ EL HASH ES CONTRATO. Los strings salen **carácter por carácter** de
 * `flowControl_hash()` del AVR:
 *
 *     [TRUE]  o  [FALSE]
 *     [SLOT00:08,0000,OPEN]   ... uno por cada uno de los 14 slots
 *
 * Como en todos los demás bloques, **cada campo se hashea por separado** sobre
 * un buffer que se limpia entre uno y otro: no es el hash de un string único con
 * todo concatenado, que daría distinto.
 *----------------------------------------------------------------------------*/
uint8_t cfg_flowcontrol_hash( void );

bool cfg_flowcontrol_set_enable( const char *pcEnable );

/*------------------------------------------------------------------------------
 * Configura un slot. `pcDow` admite `LU|MA|MI|JU|VI|SA|DO`; **cualquier otra
 * cosa deja el slot LIBRE** (dow = 8), que es como el AVR desactiva uno.
 *
 * ⚠ El índice se valida con `>=`, no con `>`. En el AVR ese chequeo estaba mal
 * —`slot > MAX` dejaba pasar `slot == 14` y escribía fuera del array— y el dato
 * viene de `atoi()` de un comando **o del servidor**, o sea entrada no confiable.
 * Está corregido allá y acá nace bien.
 *----------------------------------------------------------------------------*/
bool cfg_flowcontrol_set_slot( uint8_t ucSlot, const char *pcDow,
                               const char *pcPtime, const char *pcAccion );

/* El nombre del día, para imprimir. */
const char *cfg_flowcontrol_dow_str( uint8_t ucDow );

#endif /* APPLICATION_CONFIG_CFG_FLOWCONTROL_H_ */
