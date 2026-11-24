/*
 * Test de host de la RECONCILIACION (prvServicioConsigna), con la funcion
 * EXTRAIDA del .c real (servicio.inc).  Lo unico simulado es el entorno:
 * el reloj, el tick y el resultado del envio Modbus.
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TKCTLPRES_CONSIGNA_MAX_INTENTOS   3U
#define TKCTLPRES_MS_REINTENTO            300000U
#define pdMS_TO_TICKS(ms)                 ((TickType_t)((ms)*512U/1000U))

typedef uint32_t TickType_t;
typedef enum {
    cpresCMD_NINGUNO = 0, cpresCMD_ABRIR_V0, cpresCMD_CERRAR_V0,
    cpresCMD_ABRIR_V1, cpresCMD_CERRAR_V1,
    cpresCMD_CONSIGNA_DIURNA, cpresCMD_CONSIGNA_NOCTURNA
} cpres_cmd_t;
typedef struct { int hour, min, year; } RtcTimeType_t;
typedef struct { bool bEnabled; uint16_t usDiurna, usNocturna; uint8_t ucChecksum; } cfg_consigna_t;
cfg_consigna_t xCfgConsigna;

/* -------- el entorno simulado -------- */
static TickType_t  xTickSim    = 0;
static bool        bHoraOk     = true;
static int         iHora = 12, iMin = 0;
static bool        bEnvioOk    = true;      /* que devuelve el Modbus */
static int         iEjecutados = 0;         /* cuantas veces se envio */
static cpres_cmd_t eUltimoEnviado = cpresCMD_NINGUNO;

TickType_t xTaskGetTickCount(void){ return xTickSim; }
static bool prvHoraUtilizable( RtcTimeType_t *p ){ p->hour=iHora; p->min=iMin; p->year=26; return bHoraOk; }
static bool prvEjecutar( cpres_cmd_t c ){ iEjecutados++; eUltimoEnviado=c; return bEnvioOk; }
static void xprintf( const char *f, ... ){ (void)f; }
bool cfg_consigna_valida(void){
    long d=xCfgConsigna.usDiurna, n=xCfgConsigna.usNocturna;
    if(d<0||d>2359||(d%100)>=60) return false;
    if(n<0||n>2359||(n%100)>=60) return false;
    return d!=n;
}
static const char *tkCtlPres_consigna_str( cpres_cmd_t c ){
    switch(c){ case cpresCMD_CONSIGNA_DIURNA: return "DIURNA";
               case cpresCMD_CONSIGNA_NOCTURNA: return "NOCTURNA";
               default: return "AMBIGUA"; }
}

/* -------- el estado, igual que en el .c -------- */
static cpres_cmd_t eAplicada  = cpresCMD_NINGUNO;
static uint16_t    usUltimaHhmm = 0U;
static cpres_cmd_t eObjetivo  = cpresCMD_NINGUNO;
static uint8_t     ucIntentos = 0U;
static TickType_t  xTickUltimoIntento = 0U;
static bool bConfInvalidaInformada = false;

#include "extraido.inc"
#include "servicio.inc"

/* -------- utilidades del test -------- */
static int fallos = 0;
static void chk( const char *q, bool ok ){
    printf("  %-58s %s\n", q, ok ? "OK" : "<<< FALLO");
    if(!ok) fallos++;
}
/* una vuelta del lazo: 45 s de tick */
static void vuelta(void){ xTickSim += pdMS_TO_TICKS(45000); prvServicioConsigna(); }
static void reset_estado(void){
    eAplicada=cpresCMD_NINGUNO; eObjetivo=cpresCMD_NINGUNO; ucIntentos=0;
    xTickUltimoIntento=0; bConfInvalidaInformada=false; xTickSim=0;
    iEjecutados=0; bEnvioOk=true; bHoraOk=true;
    xCfgConsigna.bEnabled=true; xCfgConsigna.usDiurna=630; xCfgConsigna.usNocturna=2300;
}

int main(void)
{
    printf("=== 1. ARRANQUE: aplicada AMBIGUA -> aplica la de la franja ===\n");
    reset_estado(); iHora=12; iMin=0;             /* mediodia -> DIURNA */
    vuelta();
    chk("se envio UNA orden", iEjecutados==1);
    chk("y fue la DIURNA", eUltimoEnviado==cpresCMD_CONSIGNA_DIURNA);
    chk("queda aplicada DIURNA", eAplicada==cpresCMD_CONSIGNA_DIURNA);
    chk("y sin intentos pendientes", ucIntentos==0);

    printf("\n=== 2. YA APLICADA: no se envia nada (el caso normal) ===\n");
    for(int i=0;i<50;i++) vuelta();               /* ~37 min de equipo andando */
    chk("50 vueltas mas y NINGUN envio nuevo", iEjecutados==1);

    printf("\n=== 3. CAMBIO DE FRANJA: aplica sola, sin depender del minuto exacto ===\n");
    iHora=23; iMin=47;                            /* 23:00 ya paso hace 47 min */
    vuelta();
    chk("se envio la NOCTURNA aunque el minuto 2300 YA PASO",
        iEjecutados==2 && eUltimoEnviado==cpresCMD_CONSIGNA_NOCTURNA);
    chk("queda aplicada NOCTURNA", eAplicada==cpresCMD_CONSIGNA_NOCTURNA);

    printf("\n=== 4. FALLO: queda AMBIGUA, y los 3 intentos van separados 5 min ===\n");
    reset_estado(); iHora=12; iMin=0; bEnvioOk=false;
    vuelta();
    chk("1er intento enviado", iEjecutados==1 && ucIntentos==1);
    chk("la aplicada queda AMBIGUA (no se afirma lo que no se sabe)",
        eAplicada==cpresCMD_NINGUNO);
    for(int i=0;i<6;i++) vuelta();                /* 6 x 45 s = 4,5 min */
    chk("a los 4,5 min NO reintenta todavia", iEjecutados==1);
    vuelta(); vuelta();                           /* se pasan los 5 min */
    chk("pasados los 5 min: 2do intento", iEjecutados==2 && ucIntentos==2);
    for(int i=0;i<7;i++) vuelta();
    chk("y a los 5 min siguientes: 3er intento", iEjecutados==3 && ucIntentos==3);
    for(int i=0;i<200;i++) vuelta();              /* 2,5 h */
    chk("agotados los 3: NO insiste mas (2,5 h sin un envio)", iEjecutados==3);
    chk("y sigue AMBIGUA, que es lo que status muestra", eAplicada==cpresCMD_NINGUNO);

    printf("\n=== 5. UN CAMBIO DE FRANJA reinicia la serie agotada ===\n");
    bEnvioOk=true; iHora=23; iMin=5;
    vuelta();
    chk("vuelve a intentar con la consigna nueva",
        iEjecutados==4 && eUltimoEnviado==cpresCMD_CONSIGNA_NOCTURNA);
    chk("y esta vez queda aplicada", eAplicada==cpresCMD_CONSIGNA_NOCTURNA);

    printf("\n=== 6. FALLO y despues EXITO: el reintento cierra el caso ===\n");
    reset_estado(); iHora=12; iMin=0; bEnvioOk=false;
    vuelta();
    chk("falla el 1ro", eAplicada==cpresCMD_NINGUNO && ucIntentos==1);
    bEnvioOk=true;
    for(int i=0;i<8;i++) vuelta();
    chk("el 2do sale bien", eAplicada==cpresCMD_CONSIGNA_DIURNA);
    chk("y los intentos se reinician", ucIntentos==0);
    int ej = iEjecutados;
    for(int i=0;i<50;i++) vuelta();
    chk("y no vuelve a enviar nunca mas", iEjecutados==ej);

    printf("\n=== 7. HORA NO CONFIABLE: no se aplica nada, y se recupera sola ===\n");
    reset_estado(); bHoraOk=false; iHora=12; iMin=0;
    for(int i=0;i<20;i++) vuelta();
    chk("sin hora confiable NO se envia nada", iEjecutados==0);
    chk("y la aplicada sigue AMBIGUA", eAplicada==cpresCMD_NINGUNO);
    bHoraOk=true;                                 /* llega el CLOCK= del servidor */
    vuelta();
    chk("en cuanto hay hora, la vuelta siguiente la aplica",
        iEjecutados==1 && eAplicada==cpresCMD_CONSIGNA_DIURNA);

    printf("\n=== 8. CONFIGURACION INVALIDA: no se aplica nada, y avisa UNA vez ===\n");
    reset_estado(); xCfgConsigna.usDiurna=630; xCfgConsigna.usNocturna=630;
    for(int i=0;i<20;i++) vuelta();
    chk("con las dos horas iguales no se envia nada", iEjecutados==0);
    chk("el aviso se marca como ya informado", bConfInvalidaInformada==true);
    xCfgConsigna.usNocturna=2300;                 /* el server la corrige */
    vuelta();
    chk("corregida: se aplica, y el aviso se rearma",
        iEjecutados==1 && bConfInvalidaInformada==false);

    printf("\n=== 9. EL CASO DE CAMPO: la vuelta se ESTIRA y pasa el minuto ===\n");
    /* Lo que perdia la consigna en el AVR: nadie muestrea el minuto exacto. */
    reset_estado(); iHora=6; iMin=0;
    vuelta();                                     /* 06:00 -> NOCTURNA, aplica */
    chk("06:00 aplica la NOCTURNA", eAplicada==cpresCMD_CONSIGNA_NOCTURNA);
    iHora=6; iMin=31;                             /* el minuto 0630 NO se muestreo */
    vuelta();
    chk("el minuto 0630 nunca se vio, y la DIURNA se aplica igual",
        eAplicada==cpresCMD_CONSIGNA_DIURNA);

    printf("\n%s\n", fallos ? "*** HAY FALLOS ***" : "*** TODO OK ***");
    return fallos ? 1 : 0;
}
