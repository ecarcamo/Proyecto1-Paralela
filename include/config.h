/* config.h - Una sola Config, llenada al parsear y pasada como 'const *'. */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* El angulo aureo se DERIVA de phi, no se copia como 137.50776 a mano. */
#define SS_PI          3.14159265358979323846
#define SS_PHI         1.61803398874989484820   /* (1 + sqrt(5)) / 2          */
#define SS_GOLDEN_ANG  (2.0 * SS_PI / (SS_PHI * SS_PHI))  /* ~2.39996 rad     */
                                                          /* ~137.50776 grados */

/* Limites de la programacion defensiva (docs/02-PARAMETRO-N.md, seccion 4). */
#define SS_N_MIN            1
#define SS_N_MAX            5000000  /* ~200 MB en SoA: el techo defendible   */
#define SS_N_WARN           50000    /* arriba de esto se advierte, no se aborta */
#define SS_WIDTH_MIN        640      /* minimo exigido por el enunciado       */
#define SS_HEIGHT_MIN       480      /* minimo exigido por el enunciado       */
#define SS_DIM_MAX          16384
#define SS_FPS_TARGET       30.0     /* umbral del enunciado: no bajar de 30  */

/* Defaults. El de N es el N critico MEDIDO a 1280x720 (docs/02, seccion 3):
 * con este valor el binario SECUENCIAL corre justo en ~30 FPS. */
#define SS_DEF_N            128
#define SS_DEF_WIDTH        1280
#define SS_DEF_HEIGHT       720
#define SS_DEF_SEED         12345ULL
#define SS_DEF_ROT_SPEED    0.35     /* rad/s del giro principal              */
#define SS_DEF_FILL         0.84     /* fraccion de la altura que ocupa la esfera */
#define SS_DEF_BENCH_WARMUP 10       /* frames descartados por calentamiento  */

/* Deriva de color: 0.06 = una vuelta cada ~17 s. El tope es de seguridad
 * fotosensible, no de rendimiento. */
#define SS_DEF_COLOR_SPEED  0.06     /* vueltas del circulo de tono por segundo */
#define SS_DEF_COLOR_SPREAD 0.65     /* dispersion del ritmo entre semillas, 0..1 */
#define SS_COLOR_SPEED_MAX  2.0      /* tope defendible en |vueltas/s|         */

/* Fisica (docs/01 seccion 5). No son argumentos de CLI: se calibraron para que
 * el sistema converja en pocos segundos sin oscilar para siempre. */
#define SS_DEF_PHYS_K        1.0f    /* constante de Coulomb                  */
#define SS_DEF_PHYS_EPSILON  0.05f   /* softening: evita la singularidad en r=0 */
#define SS_DEF_PHYS_GAMMA    0.5f    /* friccion viscosa, sin ella no converge */
#define SS_DEF_PHYS_MASS     1.0f    /* masa de cada semilla                  */

/* Constante de dt_max = SAFETY/sqrt(N); tabla medida en include/physics.h. */
#define SS_PHYS_DT_SAFETY    0.35

/* Modo canon: la esfera se construye a canonazos (ver sphere_fill_cannon). */
#define SS_DEF_FIRE_RATE     60.0    /* disparos por segundo                  */
#define SS_DEF_MUZZLE_SPEED  1.5     /* radios por segundo; el vuelo dura 1/V */
#define SS_DEF_TRAIL         6       /* fantasmas de estela por bolita en vuelo */

/* 0 = que decida OpenMP. En screensaver_seq se parsea pero nunca se lee. */
#define SS_DEF_THREADS      0

/* --cannons K enciende el modo canon (K = 0 lo apaga). --recirculate 1 vuelve
 * a disparar cada slot: carga constante, pero solo deja puesta una fraccion
 * 1 - K*R/(V*N) de la esfera, y por eso el default es 0. */
#define SS_DEF_CANNONS       0       /* --cannons K: 0 = modo canon apagado   */
#define SS_DEF_MUZZLE_RADIUS 0.12    /* --muzzle-radius r0: donde estan las bocas */
#define SS_MUZZLE_RADIUS_MAX 0.95    /* r0 >= 1 dispararia desde afuera       */
#define SS_DEF_RECIRCULATE   0       /* --recirculate: 0 = aterrizan y se quedan */

/* --cannon-layout: ROUNDROBIN entremezcla los chorros y mantiene la
 * uniformidad; BLOCKS da K frentes por bandas, mas vistoso y menos uniforme. */
#define SS_CANNON_ROUNDROBIN 0
#define SS_CANNON_BLOCKS     1
#define SS_DEF_CANNON_LAYOUT SS_CANNON_ROUNDROBIN

/* Una sola estructura para todo el programa. */
typedef struct {
    /* --- el parametro N del enunciado --------------------------------- */
    int      n;             /* --n        semillas sobre la esfera          */

    /* --- geometria: lo unico configurable de la apariencia ------------ */
    double   angle_rad;     /* --angle    angulo de divergencia, en RADIANES */

    /* --- fijos: identidad visual, sin bandera, pero leidos del Config --- */
    int      width;         /* canvas: ancho  en pixeles                    */
    int      height;        /* canvas: alto   en pixeles                    */
    double   rot_speed;     /* velocidad de giro, rad/s                     */
    double   sphere_frac;   /* fraccion de pantalla que ocupa la esfera     */
    uint64_t seed;          /* semilla del PRNG de colores (determinismo)   */
    double   color_speed;   /* deriva de tono: vueltas por segundo          */
    double   color_spread;  /* dispersion de ese ritmo entre semillas, 0..1 */

    /* --- que kernels se activan --------------------------------------- */
    int      physics;       /* --physics  0|1  repulsion Douady-Couder       */
    int      voronoi;       /* --voronoi  0|1  celdas (1) o bolitas (0)      */
    int      raster;        /* --raster   0|1  bolitas rasterizadas (plan B) */

    /* --- modo canon: la esfera se construye a canonazos ---------------- */
    int      cannon;        /* DERIVADO de cannons: 1 si cannons >= 1        */
    double   fire_rate;     /* --fire-rate   disparos por segundo            */
    double   muzzle_speed;  /* --muzzle-speed radios por segundo             */
    int      trail;         /* --trail       fantasmas de estela por bolita  */
    int      cannons;       /* --cannons     K canones simultaneos (>= 1)    */
    int      cannon_layout; /* --cannon-layout  SS_CANNON_ROUNDROBIN|BLOCKS  */
    double   muzzle_radius; /* --muzzle-radius  radio de la esfera de bocas  */
    int      recirculate;   /* --recirculate 0|1  0 = aterrizan y se quedan  */

    /* --- paralelismo: existe en los dos binarios, lo lee solo el omp --- */
    int      threads;       /* --threads  T  hilos de OpenMP, 0 = automatico */

    /* --- modo medicion ------------------------------------------------ */
    int      bench_frames;  /* --bench K  0 = modo ventana normal            */
    int      headless;      /* --no-render  1 = sin ventana                  */
    int      csv;           /* --csv      1 = salida en CSV                  */

    /* --- verificacion: cmp entre dos volcados en el mismo t ------------ */
    int      dump_frame;    /* --dump-frame T  1 = modo activo               */
    double   dump_frame_t;  /* T: instante a volcar, en segundos             */

    /* --- misc --------------------------------------------------------- */
    int      vsync;         /* --vsync    0|1  (0 para medir sin tope)       */
    int      paused;        /* estado en vivo, no es argumento               */
} Config;

/* Config con todos los defaults puestos; args_parse() parte de aqui. */
Config config_defaults(void);

/* Chequeo de coherencia posterior al parseo: 0 si esta bien, negativo y un
 * mensaje en stderr si no. Se llama SIEMPRE, aunque el parseo no falle. */
int  config_validate(const Config *cfg);

/* Imprime la configuracion efectiva: confirma lo leido y queda en los logs. */
void config_print(const Config *cfg);

#endif /* CONFIG_H */
