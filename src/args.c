/* ===========================================================================
 *  args.c - Implementacion del parseo defensivo de argumentos.
 *
 *  Regla de oro: se usa strtol / strtod, NUNCA atoi. atoi("abc") devuelve 0
 *  sin avisar y no puede distinguir un error de un cero legitimo, ni detectar
 *  desbordamiento. strtol nos da el puntero 'endptr' (para saber si sobro
 *  basura como en "3.5") y errno == ERANGE (para el desbordamiento).
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#include "args.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 *  Conversores de una sola cadena. Devuelven 0 si la conversion es limpia, o
 *  -1 (con mensaje en stderr) si el texto no es un numero valido del tipo
 *  esperado. El nombre de la opcion se pasa solo para el mensaje de error.
 * -------------------------------------------------------------------------- */
static int parse_long(const char *s, const char *opt, long *out)
{
    if (s == NULL || *s == '\0') {
        fprintf(stderr, "error: la opcion %s recibio un valor vacio\n", opt);
        return -1;
    }

    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);

    /* end == s: no habia ni un digito. *end != '\0': sobro texto, p.ej. "3.5"
     * o "12x". Cualquiera de los dos es un entero mal formado. */
    if (end == s || *end != '\0') {
        fprintf(stderr, "error: la opcion %s espera un entero, se recibio '%s'\n",
                opt, s);
        return -1;
    }
    if (errno == ERANGE) {
        fprintf(stderr, "error: la opcion %s: '%s' desborda el rango entero\n",
                opt, s);
        return -1;
    }

    *out = v;
    return 0;
}

static int parse_ull(const char *s, const char *opt, unsigned long long *out)
{
    if (s == NULL || *s == '\0') {
        fprintf(stderr, "error: la opcion %s recibio un valor vacio\n", opt);
        return -1;
    }

    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);

    if (end == s || *end != '\0') {
        fprintf(stderr, "error: la opcion %s espera un entero sin signo, se recibio '%s'\n",
                opt, s);
        return -1;
    }
    if (errno == ERANGE) {
        fprintf(stderr, "error: la opcion %s: '%s' desborda el rango\n", opt, s);
        return -1;
    }

    *out = v;
    return 0;
}

static int parse_double(const char *s, const char *opt, double *out)
{
    if (s == NULL || *s == '\0') {
        fprintf(stderr, "error: la opcion %s recibio un valor vacio\n", opt);
        return -1;
    }

    errno = 0;
    char *end = NULL;
    double v = strtod(s, &end);

    if (end == s || *end != '\0') {
        fprintf(stderr, "error: la opcion %s espera un numero, se recibio '%s'\n",
                opt, s);
        return -1;
    }
    if (errno == ERANGE) {
        fprintf(stderr, "error: la opcion %s: '%s' esta fuera de rango\n", opt, s);
        return -1;
    }

    *out = v;
    return 0;
}

/* --------------------------------------------------------------------------
 *  Toma el valor que sigue a una opcion (argv[i+1]) y avanza el indice. Si la
 *  opcion es la ultima de argv, el valor falta: es un error, no un crash.
 * -------------------------------------------------------------------------- */
static const char *take_value(int argc, char **argv, int *i, const char *opt)
{
    if (*i + 1 >= argc) {
        fprintf(stderr, "error: la opcion %s requiere un valor\n", opt);
        return NULL;
    }
    return argv[++(*i)];
}

/* Azucar para un flag booleano "--x 0|1". Acepta 0 o 1 y nada mas. */
static int take_bool(int argc, char **argv, int *i, const char *opt, int *out)
{
    const char *s = take_value(argc, argv, i, opt);
    if (s == NULL) return -1;

    long v;
    if (parse_long(s, opt, &v) != 0) return -1;
    if (v != 0 && v != 1) {
        fprintf(stderr, "error: la opcion %s espera 0 o 1, se recibio '%s'\n",
                opt, s);
        return -1;
    }
    *out = (int)v;
    return 0;
}

void args_usage(const char *prog)
{
    if (prog == NULL) prog = "screensaver";

    fprintf(stderr,
"Uso: %s [opciones]\n"
"\n"
"  Screensaver: esfera de Fibonacci animada.\n"
"\n"
"Parametro principal:\n"
"  --n N            semillas sobre la esfera        (def %d, 1..%d)\n"
"\n"
"Canvas:\n"
"  --width W        ancho en pixeles                (def %d, min %d)\n"
"  --height H       alto  en pixeles                (def %d, min %d)\n"
"\n"
"Geometria y apariencia:\n"
"  --angle GRADOS   angulo de divergencia en GRADOS (def aureo ~137.508)\n"
"  --rot RAD_S      velocidad de giro en rad/s       (def %.2f)\n"
"  --fill FRAC      fraccion de pantalla (0,1]       (def %.2f)\n"
"  --seed S         semilla del PRNG de colores       (def %llu)\n"
"\n"
"Deriva de color (las semillas ya colocadas cambian de tono con el tiempo):\n"
"  --color-speed V  vueltas de tono por segundo       (def %.2f, 0 = fijo)\n"
"  --color-spread F dispersion del ritmo entre semillas 0..1 (def %.2f)\n"
"\n"
"Kernels:\n"
"  --physics 0|1    repulsion Douady-Couder          (def 0)\n"
"  --voronoi 0|1    celdas (1) o bolitas (0)          (def 0)\n"
"  --raster 0|1     bolitas rasterizadas: plan B barato, NO escala con N\n"
"\n"
"Canon (la esfera se construye a canonazos; incompatible con --physics):\n"
"  --cannon 0|1        enciende el modo canon             (def 0)\n"
"  --fire-rate R       disparos por segundo               (def %.1f)\n"
"  --muzzle-speed V    radios por segundo (vuelo dura 1/V) (def %.2f)\n"
"  --trail L           fantasmas de estela por bolita      (def %d)\n"
"  --cannons K         canones simultaneos, 1..N           (def %d)\n"
"  --cannon-layout M   roundrobin | blocks                 (def roundrobin)\n"
"  --muzzle-radius R0  radio de la esfera de bocas, [0,%.2f] (def %.2f)\n"
"                      (los slots recirculan: la animacion no se termina)\n"
"\n"
"Medicion:\n"
"  --bench K        mide K frames y termina, 0 = ventana\n"
"  --no-render      corre sin abrir ventana (headless)\n"
"  --csv            salida de mediciones en CSV\n"
"  --vsync 0|1      sincronia vertical, 0 para medir  (def 1)\n"
"\n"
"  -h, --help       muestra esta ayuda\n"
"\n"
"Teclas en la ventana: ESC/Q salir, [ ] barren el angulo, P pausa, R reinicia.\n",
        prog,
        SS_DEF_N, SS_N_MAX,
        SS_DEF_WIDTH, SS_WIDTH_MIN,
        SS_DEF_HEIGHT, SS_HEIGHT_MIN,
        SS_DEF_ROT_SPEED,
        SS_DEF_FILL,
        (unsigned long long)SS_DEF_SEED,
        SS_DEF_COLOR_SPEED,
        SS_DEF_COLOR_SPREAD,
        SS_DEF_FIRE_RATE,
        SS_DEF_MUZZLE_SPEED,
        SS_DEF_TRAIL,
        SS_DEF_CANNONS,
        SS_MUZZLE_RADIUS_MAX,
        SS_DEF_MUZZLE_RADIUS);
}

ArgsStatus args_parse(int argc, char **argv, Config *cfg)
{
    if (cfg == NULL) return ARGS_ERROR;

    *cfg = config_defaults();
    const char *prog = (argc > 0) ? argv[0] : "screensaver";

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];

        /* --- ayuda ------------------------------------------------------- */
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            args_usage(prog);
            return ARGS_HELP;
        }

        /* --- flags sin valor -------------------------------------------- */
        else if (strcmp(a, "--no-render") == 0) {
            cfg->headless = 1;
        }
        else if (strcmp(a, "--csv") == 0) {
            cfg->csv = 1;
        }

        /* --- enteros ----------------------------------------------------- */
        else if (strcmp(a, "--n") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            long v;
            if (s == NULL || parse_long(s, a, &v) != 0) goto bad;
            cfg->n = (int)v;
        }
        else if (strcmp(a, "--width") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            long v;
            if (s == NULL || parse_long(s, a, &v) != 0) goto bad;
            cfg->width = (int)v;
        }
        else if (strcmp(a, "--height") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            long v;
            if (s == NULL || parse_long(s, a, &v) != 0) goto bad;
            cfg->height = (int)v;
        }
        else if (strcmp(a, "--bench") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            long v;
            if (s == NULL || parse_long(s, a, &v) != 0) goto bad;
            cfg->bench_frames = (int)v;
        }
        else if (strcmp(a, "--trail") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            long v;
            if (s == NULL || parse_long(s, a, &v) != 0) goto bad;
            cfg->trail = (int)v;
        }
        else if (strcmp(a, "--cannons") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            long v;
            if (s == NULL || parse_long(s, a, &v) != 0) goto bad;
            cfg->cannons = (int)v;
        }
        /* --- reales ------------------------------------------------------ */
        else if (strcmp(a, "--angle") == 0) {
            /* El usuario piensa en grados (137.5); adentro todo va en radianes. */
            const char *s = take_value(argc, argv, &i, a);
            double deg;
            if (s == NULL || parse_double(s, a, &deg) != 0) goto bad;
            cfg->angle_rad = deg * SS_PI / 180.0;
        }
        else if (strcmp(a, "--rot") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            double v;
            if (s == NULL || parse_double(s, a, &v) != 0) goto bad;
            cfg->rot_speed = v;
        }
        else if (strcmp(a, "--fill") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            double v;
            if (s == NULL || parse_double(s, a, &v) != 0) goto bad;
            cfg->sphere_frac = v;
        }

        else if (strcmp(a, "--color-speed") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            double v;
            if (s == NULL || parse_double(s, a, &v) != 0) goto bad;
            cfg->color_speed = v;
        }
        else if (strcmp(a, "--color-spread") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            double v;
            if (s == NULL || parse_double(s, a, &v) != 0) goto bad;
            cfg->color_spread = v;
        }
        else if (strcmp(a, "--fire-rate") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            double v;
            if (s == NULL || parse_double(s, a, &v) != 0) goto bad;
            cfg->fire_rate = v;
        }
        else if (strcmp(a, "--muzzle-speed") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            double v;
            if (s == NULL || parse_double(s, a, &v) != 0) goto bad;
            cfg->muzzle_speed = v;
        }
        else if (strcmp(a, "--muzzle-radius") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            double v;
            if (s == NULL || parse_double(s, a, &v) != 0) goto bad;
            cfg->muzzle_radius = v;
        }

        /* --- enumerados por nombre --------------------------------------
         * El reparto se pide por nombre y no por 0|1: 'blocks' se lee solo,
         * '--cannon-layout 1' obligaria a ir al header a ver cual era cual. */
        else if (strcmp(a, "--cannon-layout") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            if (s == NULL) goto bad;
            if (strcmp(s, "roundrobin") == 0)   cfg->cannon_layout = SS_CANNON_ROUNDROBIN;
            else if (strcmp(s, "blocks") == 0)  cfg->cannon_layout = SS_CANNON_BLOCKS;
            else {
                fprintf(stderr,
                        "error: --cannon-layout espera 'roundrobin' o 'blocks', "
                        "se recibio '%s'\n", s);
                goto bad;
            }
        }

        /* --- sin signo de 64 bits --------------------------------------- */
        else if (strcmp(a, "--seed") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            unsigned long long v;
            if (s == NULL || parse_ull(s, a, &v) != 0) goto bad;
            cfg->seed = (uint64_t)v;
        }

        /* --- booleanos 0|1 ---------------------------------------------- */
        else if (strcmp(a, "--physics") == 0) {
            if (take_bool(argc, argv, &i, a, &cfg->physics) != 0) goto bad;
        }
        else if (strcmp(a, "--voronoi") == 0) {
            if (take_bool(argc, argv, &i, a, &cfg->voronoi) != 0) goto bad;
        }
        else if (strcmp(a, "--raster") == 0) {
            if (take_bool(argc, argv, &i, a, &cfg->raster) != 0) goto bad;
        }
        else if (strcmp(a, "--cannon") == 0) {
            if (take_bool(argc, argv, &i, a, &cfg->cannon) != 0) goto bad;
        }
        else if (strcmp(a, "--vsync") == 0) {
            if (take_bool(argc, argv, &i, a, &cfg->vsync) != 0) goto bad;
        }

        /* --- cualquier otra cosa es un error ---------------------------- */
        else {
            fprintf(stderr, "error: opcion desconocida '%s'\n", a);
            goto bad;
        }
    }

    return ARGS_OK;

bad:
    args_usage(prog);
    return ARGS_ERROR;
}
