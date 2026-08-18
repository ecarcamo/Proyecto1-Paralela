/* ===========================================================================
 *  config.h - Contrato de configuracion compartido por todos los modulos.
 *
 *  ESTE ARCHIVO ES EL CONTRATO DEL EQUIPO. Toda la configuracion del programa
 *  viaja en una sola estructura Config que se llena UNA vez (al parsear los
 *  argumentos) y de ahi en adelante se pasa como 'const *' a todos lados.
 *
 *  Motivo: el enunciado penaliza el hard-coding y exige parametrizar leyendo
 *  argumentos del comando. Centralizar todo aqui hace que agregar un
 *  parametro sea tocar un solo lugar, y que ningun modulo tenga constantes
 *  magicas escondidas.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* --------------------------------------------------------------------------
 *  Constantes matematicas fundamentales.
 *
 *  El angulo aureo NO se escribe como 137.50776 a mano: se DERIVA de phi.
 *  Es la diferencia entre "usamos una constante magica que copiamos de
 *  internet" y "sabemos de donde sale". Ver docs/01-FUNDAMENTO-MATEMATICO.md
 * -------------------------------------------------------------------------- */
#define SS_PI          3.14159265358979323846
#define SS_PHI         1.61803398874989484820   /* (1 + sqrt(5)) / 2          */
#define SS_GOLDEN_ANG  (2.0 * SS_PI / (SS_PHI * SS_PHI))  /* ~2.39996 rad     */
                                                          /* ~137.50776 grados */

/* --------------------------------------------------------------------------
 *  Limites para la programacion defensiva (docs/02-PARAMETRO-N.md, seccion 4)
 * -------------------------------------------------------------------------- */
#define SS_N_MIN            1
#define SS_N_MAX            5000000  /* ~200 MB en SoA: el techo defendible   */
#define SS_N_WARN           50000    /* arriba de esto se advierte, no se aborta */
#define SS_WIDTH_MIN        640      /* minimo exigido por el enunciado       */
#define SS_HEIGHT_MIN       480      /* minimo exigido por el enunciado       */
#define SS_DIM_MAX          16384
#define SS_FPS_TARGET       30.0     /* umbral del enunciado: no bajar de 30  */

/* --------------------------------------------------------------------------
 *  Valores por defecto. Todos sobreescribibles por linea de comandos.
 * -------------------------------------------------------------------------- */
/* El default es el N critico MEDIDO a 1280x720 (ver docs/02-PARAMETRO-N.md
 * seccion 3): con este valor el binario SECUENCIAL corre justo en ~30 FPS,
 * que es el piso que exige el enunciado. Arrancar por defecto en 1000 dejaria
 * al secuencial en 4 FPS y pareceria que el programa esta roto.
 * Subir N a proposito por encima de esto es la demo del proyecto. */
#define SS_DEF_N            128
#define SS_DEF_WIDTH        1280
#define SS_DEF_HEIGHT       720
#define SS_DEF_SEED         12345ULL
#define SS_DEF_ROT_SPEED    0.35     /* rad/s del giro principal              */
#define SS_DEF_FILL         0.84     /* fraccion de la altura que ocupa la esfera */
#define SS_DEF_BENCH_WARMUP 10       /* frames descartados por calentamiento  */

/* ==========================================================================
 *  Config - una sola estructura para todo el programa.
 * ========================================================================== */
typedef struct {
    /* --- el parametro N del enunciado --------------------------------- */
    int      n;             /* --n        semillas sobre la esfera          */

    /* --- canvas ------------------------------------------------------- */
    int      width;         /* --width    ancho  en pixeles (>= 640)        */
    int      height;        /* --height   alto   en pixeles (>= 480)        */

    /* --- geometria y apariencia --------------------------------------- */
    double   angle_rad;     /* --angle    angulo de divergencia, en RADIANES */
    double   rot_speed;     /* --rot      velocidad de giro, rad/s           */
    double   sphere_frac;   /* --fill     fraccion de pantalla que ocupa     */
    uint64_t seed;          /* --seed     semilla del PRNG (determinismo)    */

    /* --- que kernels se activan --------------------------------------- */
    int      physics;       /* --physics  0|1  repulsion Douady-Couder       */
    int      voronoi;       /* --voronoi  0|1  celdas (1) o puntos (0)       */

    /* --- modo medicion ------------------------------------------------ */
    int      bench_frames;  /* --bench K  0 = modo ventana normal            */
    int      headless;      /* --no-render  1 = sin ventana                  */
    int      csv;           /* --csv      1 = salida en CSV                  */

    /* --- misc --------------------------------------------------------- */
    int      vsync;         /* --vsync    0|1  (0 para medir sin tope)       */
    int      paused;        /* estado en vivo, no es argumento               */
} Config;

/* Devuelve una Config con todos los valores por defecto ya puestos.
 * args_parse() parte de aqui y solo sobreescribe lo que venga en argv. */
Config config_defaults(void);

/* Chequeo de coherencia posterior al parseo. Devuelve 0 si todo esta bien,
 * o un numero negativo y un mensaje en stderr si algo quedo invalido.
 * Se llama SIEMPRE, incluso si el parseo no reporto errores: hay reglas que
 * solo se pueden verificar cuando ya estan todos los campos llenos. */
int  config_validate(const Config *cfg);

/* Imprime la configuracion efectiva. Sirve para (a) que el usuario confirme
 * que sus argumentos se leyeron como esperaba, y (b) que quede registrado en
 * los logs de las mediciones con que parametros se corrio cada prueba. */
void config_print(const Config *cfg);

#endif /* CONFIG_H */
