/*
 * caudal_log.h
 *
 * **La traza de cada pulso, para depurar el caudal en campo.**
 *
 * ⭐ POR QUÉ EXISTE, Y POR QUÉ GUARDA LOS dT CRUDOS
 *
 * El algoritmo del caudal se simplificó (ver `caudal.h`) apostando a que el
 * filtro pasa-bajos del hardware frena el ruido que antes se colaba como
 * pulsos. Esta traza es **la evidencia de si esa apuesta fue correcta**.
 *
 * Y guarda el `dT` de cada pulso, no sólo el caudal calculado, por una razón
 * práctica: **con los dT crudos se reprocesan cien variantes de alpha en la
 * oficina**, con los datos reales de la instalación que falló. Si sólo se
 * guardara el caudal, cada ajuste exigiría otra visita a campo.
 *
 * `Q_ema` va además para verificar que el reprocesado coincide con lo que de
 * verdad hizo el equipo: si divergen, el bug está en el firmware y no en el
 * análisis.
 *
 * ---------------------------------------------------------------------------
 * ⚠ SE ACUMULA EN RAM Y SE VUELCA DE A BLOQUES
 *
 * Es el mismo patrón que la ventana de la EEPROM: el costo real de la microSD
 * no es escribir, es **encender, montar, escribir, desmontar y apagar**. Un
 * volcado por pulso serían miles de ciclos por día.
 *
 * ⛔ Pero a diferencia de las muestras, **esto va en RAM y no en la EEPROM**, y
 * la distinción importa:
 *
 *   - las muestras son el dato del cliente: un corte no puede llevárselas, por
 *     eso viven en la EEPROM;
 *   - esto es instrumento: si un reset se lleva la traza, se repite la prueba.
 *     Y escribirlo en EEPROM serían ~5 ms y desgaste por pulso, sobre un chip
 *     de vida finita, para guardar algo descartable.
 *
 * ⚠ **Activable por comando, nunca permanente.** A caudal alto son ~2000
 * pulsos/hora: perfecto para una sesión de depuración en el sitio, no para algo
 * corriendo en los 1000 equipos.
 */

#ifndef APPLICATION_TASKS_CAUDAL_LOG_H_
#define APPLICATION_TASKS_CAUDAL_LOG_H_

#include <stdbool.h>
#include <stdint.h>

/* 1000 registros x 17 B = 17 KB. A 500 pulsos/hora (caudal 50, magpp 0,1) son
   2 horas de traza; a 2000/hora, media hora. */
#define CAUDAL_LOG_REGISTROS      1000U

/* Se vuelca al 90 %, no al llenarse: deja aire para reintentar si la tarjeta
   no está. Mismo criterio que la ventana de la EEPROM. */
#define CAUDAL_LOG_UMBRAL_PCT       90U

typedef enum {
    cauEV_OK = 0,      /* pulso válido                                      */
    cauEV_CORTO,       /* descartado por el anti-rebote de 500 ms           */
    cauEV_QMAX         /* descartado: implicaba un caudal imposible         */
} caudal_ev_t;

typedef struct {
    uint32_t ulTicks;    /* cuándo, en ticks del kernel                     */
    uint32_t ulDtMs;     /* el tiempo desde el pulso anterior               */
    float    fQInst;     /* magpp·3.600.000/dT — 0 si se descartó por corto */
    float    fQEma;      /* el EMA después de este pulso                    */
    uint8_t  ucEvento;   /* caudal_ev_t                                     */
} __attribute__( ( packed ) ) caudal_log_reg_t;

void     caudal_log_habilitar ( bool bOn );
bool     caudal_log_activo    ( void );

/* ⚠ Desde la ISR. No hace más que escribir en el buffer circular. */
void     caudal_log_agregar_desde_isr( uint32_t ulTicks, uint32_t ulDtMs,
                                       float fQInst, float fQEma,
                                       caudal_ev_t eEvento );

uint16_t caudal_log_pendientes( void );
uint32_t caudal_log_pisados   ( void );
bool     caudal_log_lleno     ( void );   /* llegó al umbral: hora de volcar */

/* FIFO: saca el más viejo. Lo usa `fs_sd_volcar_pulsos()`. */
bool     caudal_log_sacar( caudal_log_reg_t *pxOut );
void     caudal_log_vaciar( void );

const char *caudal_log_evento_str( uint8_t ucEvento );

#endif /* APPLICATION_TASKS_CAUDAL_LOG_H_ */
