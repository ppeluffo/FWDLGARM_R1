/*
 * test_franjas.c  -  las FRANJAS de la doble consigna, sin hardware.
 *
 * Lo corre Tools/verificar_consigna.sh, que NO copia codigo: extrae
 * prvConsignaDeseada() del tkCtlPres.c real a extraido.inc en cada corrida, asi
 * el test no se puede desactualizar respecto del firmware. Es el mismo criterio
 * con que se validaron la tabla de Pearson del hash y el CRC32 del autoupdate.
 *
 * Con -DCON_VERSION_VIEJA compila tambien vieja.inc (la implementacion anterior,
 * sacada de git) y mide en cuantos minutos difieren: ahi se ve el bug de borde.
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    cpresCMD_NINGUNO = 0, cpresCMD_ABRIR_V0, cpresCMD_CERRAR_V0,
    cpresCMD_ABRIR_V1, cpresCMD_CERRAR_V1,
    cpresCMD_CONSIGNA_DIURNA, cpresCMD_CONSIGNA_NOCTURNA
} cpres_cmd_t;

typedef struct { bool bEnabled; uint16_t usDiurna, usNocturna; uint8_t ucChecksum; } cfg_consigna_t;
cfg_consigna_t xCfgConsigna;

/* La misma regla que cfg_consigna.c */
static bool prvHhmmValido( long v ) { return (v>=0) && (v<=2359) && ((v%100)<60); }
bool cfg_consigna_valida( void ) {
    return prvHhmmValido(xCfgConsigna.usDiurna) && prvHhmmValido(xCfgConsigna.usNocturna)
        && (xCfgConsigna.usDiurna != xCfgConsigna.usNocturna);
}

#include "extraido.inc"
#ifdef CON_VERSION_VIEJA
#include "vieja.inc"
#endif

static const char *nom( cpres_cmd_t c ) {
    switch(c){ case cpresCMD_CONSIGNA_DIURNA: return "DIURNA";
               case cpresCMD_CONSIGNA_NOCTURNA: return "NOCTURNA";
               default: return "NINGUNO"; }
}
static uint16_t mins2hhmm( int m ){ return (uint16_t)((m/60)*100 + (m%60)); }

static int fallos = 0;
static void chk( const char *q, uint16_t hhmm, cpres_cmd_t esp ) {
    cpres_cmd_t r = prvConsignaDeseada( hhmm );
    bool ok = ( r == esp );
    if(!ok) fallos++;
    printf("  %-28s %04u -> %-8s  %s\n", q, hhmm, nom(r), ok ? "OK" : "<<< FALLO");
}

int main(void)
{
    printf("=== 1. diurna 0630 / nocturna 2300 (el caso tipico) ===\n");
    xCfgConsigna.usDiurna = 630; xCfgConsigna.usNocturna = 2300;
    chk("medianoche",            0,    cpresCMD_CONSIGNA_NOCTURNA);
    chk("un minuto antes",       629,  cpresCMD_CONSIGNA_NOCTURNA);
    chk("EL BORDE de la diurna", 630,  cpresCMD_CONSIGNA_DIURNA);
    chk("un minuto despues",     631,  cpresCMD_CONSIGNA_DIURNA);
    chk("mediodia",              1200, cpresCMD_CONSIGNA_DIURNA);
    chk("un minuto antes",       2259, cpresCMD_CONSIGNA_DIURNA);
    chk("EL BORDE de la noct.",  2300, cpresCMD_CONSIGNA_NOCTURNA);
    chk("fin del dia",           2359, cpresCMD_CONSIGNA_NOCTURNA);

    printf("\n=== 2. nocturna 0100 / diurna 0800 (franja que cruza medianoche) ===\n");
    xCfgConsigna.usDiurna = 800; xCfgConsigna.usNocturna = 100;
    chk("medianoche",            0,    cpresCMD_CONSIGNA_DIURNA);
    chk("un minuto antes",       59,   cpresCMD_CONSIGNA_DIURNA);
    chk("EL BORDE de la noct.",  100,  cpresCMD_CONSIGNA_NOCTURNA);
    chk("un minuto antes",       759,  cpresCMD_CONSIGNA_NOCTURNA);
    chk("EL BORDE de la diurna", 800,  cpresCMD_CONSIGNA_DIURNA);
    chk("la tarde",              1800, cpresCMD_CONSIGNA_DIURNA);
    chk("fin del dia",           2359, cpresCMD_CONSIGNA_DIURNA);

    printf("\n=== 3. configuracion INVALIDA -> NINGUNO (no se aplica nada) ===\n");
    xCfgConsigna.usDiurna = 630;  xCfgConsigna.usNocturna = 630;  chk("las dos IGUALES",   1200, cpresCMD_NINGUNO);
    xCfgConsigna.usDiurna = 2575; xCfgConsigna.usNocturna = 2300; chk("hora fuera de HHMM",1200, cpresCMD_NINGUNO);
    xCfgConsigna.usDiurna = 630;  xCfgConsigna.usNocturna = 1270; chk("minutos > 59",      1200, cpresCMD_NINGUNO);
    xCfgConsigna.usDiurna = 0;    xCfgConsigna.usNocturna = 1200; chk("diurna a las 00:00",   0, cpresCMD_CONSIGNA_DIURNA);

    /* ---- 4. Cobertura: los 1440 minutos del dia, con TODOS los pares ---- */
    printf("\n=== 4. COBERTURA: los 1440 minutos, para los 1440x1439 pares posibles ===\n");
    long pares = 0, huecos = 0, transic_mal = 0;
    for( int d = 0; d < 1440; d++ ) {
      for( int n = 0; n < 1440; n++ ) {
        if( d == n ) continue;
        xCfgConsigna.usDiurna = mins2hhmm(d); xCfgConsigna.usNocturna = mins2hhmm(n);
        pares++;
        int transiciones = 0;
        cpres_cmd_t prev = prvConsignaDeseada( mins2hhmm(1439) );
        for( int m = 0; m < 1440; m++ ) {
            cpres_cmd_t c = prvConsignaDeseada( mins2hhmm(m) );
            if( c == cpresCMD_NINGUNO ) { huecos++; }     /* ningun minuto sin consigna */
            if( c != prev ) {
                transiciones++;
                /* la transicion tiene que caer EXACTAMENTE en un horario configurado */
                if( !( ( m == d && c == cpresCMD_CONSIGNA_DIURNA ) ||
                       ( m == n && c == cpresCMD_CONSIGNA_NOCTURNA ) ) ) transic_mal++;
            }
            prev = c;
        }
        if( transiciones != 2 ) transic_mal++;            /* exactamente 2 cambios por dia */
      }
    }
    printf("  pares probados          : %ld\n", pares);
    printf("  minutos sin consigna    : %ld   %s\n", huecos, huecos?"<<< FALLO":"OK (ninguno)");
    printf("  transiciones incorrectas: %ld   %s\n", transic_mal, transic_mal?"<<< FALLO":"OK (2 por dia, en su minuto exacto)");
    if( huecos || transic_mal ) fallos++;

#ifdef CON_VERSION_VIEJA
    /* ---- 5. El bug de borde de la version VIEJA ---- */
    printf("\n=== 5. EL BUG QUE ESTO CORRIGE (vs la implementacion anterior) ===\n");
    long difs = 0; int ej_d=0, ej_n=0, ej_m=0;
    for( int d = 0; d < 1440; d++ ) {
      for( int n = 0; n < 1440; n++ ) {
        if( d == n ) continue;
        xCfgConsigna.usDiurna = mins2hhmm(d); xCfgConsigna.usNocturna = mins2hhmm(n);
        for( int m = 0; m < 1440; m++ ) {
            uint16_t h = mins2hhmm(m);
            if( prvConsignaDeseada(h) != prvConsignaQueCorresponde(h) ) {
                if(!difs){ ej_d=d; ej_n=n; ej_m=m; }
                difs++;
            }
        }
      }
    }
    printf("  minutos en que difieren : %ld\n", difs);
    xCfgConsigna.usDiurna = mins2hhmm(ej_d); xCfgConsigna.usNocturna = mins2hhmm(ej_n);
    printf("  primer ejemplo: diurna=%04u nocturna=%04u a las %04u\n",
           xCfgConsigna.usDiurna, xCfgConsigna.usNocturna, mins2hhmm(ej_m));
    printf("     la VIEJA decia : %s\n", nom(prvConsignaQueCorresponde(mins2hhmm(ej_m))));
    printf("     la NUEVA dice  : %s\n", nom(prvConsignaDeseada(mins2hhmm(ej_m))));

    /*
     * El numero no es casual y conviene entenderlo: difieren en EXACTAMENTE un
     * minuto por cada par (d,n) posible, y ese minuto es el del horario
     * configurado. O sea que el bug afectaba el unico minuto en que la decision
     * importaba.
     */
    if( difs != pares ) {
        printf("  [!] se esperaba exactamente 1 minuto por par (%ld)\n", pares);
        fallos++;
    } else {
        printf("  == exactamente 1 minuto por par: el borde, y nada mas\n");
    }
#endif

    printf("\n%s\n", fallos ? "*** HAY FALLOS ***" : "*** TODO OK ***");
    return fallos ? 1 : 0;
}
