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

/* --------------------------------------------------------------------------
 *  Modo canon: la esfera arranca vacia y un canon en el centro dispara las
 *  semillas hacia afuera hasta que aterrizan en su posicion de Fibonacci.
 *  Ver sphere_fill_cannon() en sphere.h para la formula cerrada.
 * -------------------------------------------------------------------------- */
#define SS_DEF_FIRE_RATE     60.0    /* disparos por segundo                  */
#define SS_DEF_MUZZLE_SPEED  1.5     /* radios por segundo; el vuelo dura 1/V */
#define SS_DEF_TRAIL         6       /* fantasmas de estela por bolita en vuelo */

/* --------------------------------------------------------------------------
 *  Modo canon, segunda parte: K canones y animacion infinita.
 *
 *  Con un solo canon y sin recirculacion la animacion TIENE FINAL: cuando
 *  aterriza la bolita N queda una esfera estatica y no pasa nada mas. Con K
 *  canones eso empeora, porque se llena K veces mas rapido. Por eso el slot i
 *  no se queda quieto para siempre: se vuelve a disparar cada T_ciclo
 *  segundos (ver sphere_fill_cannon()). La esfera se mantiene llena, los
 *  canones no paran nunca, y la formula sigue siendo cerrada -- solo se le
 *  agrega un fmod.
 *
 *  Efecto lateral que importa para el informe: con recirculacion la carga
 *  dibujada es CONSTANTE en regimen permanente, asi que K pasa a ser una
 *  perilla de carga de verdad, ortogonal a N. Se puede subir el costo por
 *  frame sin tocar la geometria de la esfera.
 *
 *  El default de K es 1 para que el modo canon se siga viendo igual que
 *  antes salvo por la recirculacion, y el radio de boca es > 0 porque con
 *  todas las bocas en el origen exacto las bolitas recien disparadas se
 *  apilan y se solapan feo (y con K > 1 los canones no se distinguirian).
 * -------------------------------------------------------------------------- */
#define SS_DEF_CANNONS       1       /* --cannons K: canones simultaneos      */
#define SS_DEF_MUZZLE_RADIUS 0.12    /* --muzzle-radius r0: donde estan las bocas */
#define SS_MUZZLE_RADIUS_MAX 0.95    /* r0 >= 1 dispararia desde afuera       */

/* Reparto de indices entre los K canones (--cannon-layout).
 *
 *  ROUNDROBIN: el canon c se queda con los indices c, c+K, c+2K, ...  Como
 *  los indices consecutivos estan separados por el angulo aureo, los chorros
 *  se entremezclan y la esfera se puebla pareja. Es el default: mantiene la
 *  uniformidad, que es justamente el punto del patron.
 *
 *  BLOCKS: el canon c se hace cargo de un rango contiguo de indices y se ven
 *  K frentes avanzando por bandas. Mas espectacular, menos uniforme.
 *
 *  Las dos reparten los N indices EXACTAMENTE una vez, sin huecos ni
 *  repetidos (lo verifica el test de cobertura). */
#define SS_CANNON_ROUNDROBIN 0
#define SS_CANNON_BLOCKS     1
#define SS_DEF_CANNON_LAYOUT SS_CANNON_ROUNDROBIN

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
    int      voronoi;       /* --voronoi  0|1  celdas (1) o bolitas (0)      */
    int      raster;        /* --raster   0|1  bolitas rasterizadas (plan B) */

    /* --- modo canon: la esfera se construye a canonazos ---------------- */
    int      cannon;        /* --cannon      0|1  enciende el modo canon     */
    double   fire_rate;     /* --fire-rate   disparos por segundo            */
    double   muzzle_speed;  /* --muzzle-speed radios por segundo             */
    int      trail;         /* --trail       fantasmas de estela por bolita  */
    int      cannons;       /* --cannons     K canones simultaneos (>= 1)    */
    int      cannon_layout; /* --cannon-layout  SS_CANNON_ROUNDROBIN|BLOCKS  */
    double   muzzle_radius; /* --muzzle-radius  radio de la esfera de bocas  */

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
