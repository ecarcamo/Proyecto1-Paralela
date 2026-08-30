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

/* --------------------------------------------------------------------------
 *  Deriva de color de las semillas ya colocadas (ColorAnim en color.h).
 *
 *  El default NO es 0: con el color congelado la esfera queda con la misma
 *  paleta desde el primer frame hasta el ultimo. A 0.06 vueltas/s cada semilla
 *  tarda ~17 s en dar la vuelta completa al circulo de tono -- lento como para
 *  leerse como una transicion y no como un parpadeo -- y con el spread cada
 *  una lo hace a su propio ritmo, asi que la esfera se va poblando de tonos
 *  distintos en vez de cambiar en bloque.
 *
 *  El tope de velocidad es de seguridad, no de rendimiento: por arriba de una
 *  vuelta por segundo esto deja de ser una transicion gradual y pasa a ser un
 *  estroboscopio de N colores.
 * -------------------------------------------------------------------------- */
#define SS_DEF_COLOR_SPEED  0.06     /* vueltas del circulo de tono por segundo */
#define SS_DEF_COLOR_SPREAD 0.65     /* dispersion del ritmo entre semillas, 0..1 */
#define SS_COLOR_SPEED_MAX  2.0      /* tope defendible en |vueltas/s|         */

/* --------------------------------------------------------------------------
 *  Fisica: repulsion de Coulomb con softening + Velocity-Verlet.
 *  Ver docs/01-FUNDAMENTO-MATEMATICO.md seccion 5. No son argumentos de CLI
 *  (el enunciado no lo pide para esto): se calibraron a ojo para que el
 *  sistema converja en pocos segundos sin oscilar para siempre.
 * -------------------------------------------------------------------------- */
#define SS_DEF_PHYS_K        1.0f    /* constante de Coulomb                  */
#define SS_DEF_PHYS_EPSILON  0.05f   /* softening: evita la singularidad en r=0 */
#define SS_DEF_PHYS_GAMMA    0.5f    /* friccion viscosa, sin ella no converge */
#define SS_DEF_PHYS_MASS     1.0f    /* masa de cada semilla                  */

/* Constante del limite de estabilidad dt_max = SAFETY/sqrt(N) de Verlet.
 * Ver la tabla medida en include/physics.h (physics_max_dt). */
#define SS_PHYS_DT_SAFETY    0.35

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

    /* --- deriva de color de las semillas ya colocadas ------------------ */
    double   color_speed;   /* --color-speed  vueltas de tono por segundo    */
    double   color_spread;  /* --color-spread dispersion del ritmo, 0..1     */

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
