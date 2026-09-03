/* args.c - Parseo defensivo de argumentos.
 * strtol/strtod y nunca atoi: atoi("abc") devuelve 0 sin avisar y no detecta
 * ni la basura sobrante (endptr) ni el desbordamiento (ERANGE). */
#include "args.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Conversores: 0 si la conversion es limpia, -1 y mensaje en stderr si no. */
static int parse_long(const char *s, const char *opt, long *out)
{
    if (s == NULL || *s == '\0') {
        fprintf(stderr, "error: la opcion %s recibio un valor vacio\n", opt);
        return -1;
    }

    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);

    /* end == s: ni un digito. *end != 0: sobro texto ("3.5", "12x"). */
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

/* Toma argv[i+1] y avanza el indice; si falta el valor, error y no crash. */
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
"Geometria:\n"
"  --angle GRADOS   angulo de divergencia en GRADOS (def aureo ~137.508)\n"
"\n"
"  El canvas (%dx%d), el giro, el encuadre, la semilla del PRNG y la deriva\n"
"  de color son fijos: son la identidad visual del screensaver, no perillas.\n"
"\n"
"Kernels:\n"
"  --physics 0|1    repulsion Douady-Couder          (def 0)\n"
"  --voronoi 0|1    celdas (1) o bolitas (0)          (def 0)\n"
"  --raster 0|1     bolitas rasterizadas: plan B barato, NO escala con N\n"
"\n"
"Canon (la esfera se construye a canonazos; incompatible con --physics):\n"
"  --cannons K         K canones, 1..N. SIN esta bandera (o con 0) el modo\n"
"                      canon esta apagado y la esfera aparece ya hecha.\n"
"  --fire-rate R       disparos por segundo               (def %.1f)\n"
"  --muzzle-speed V    radios por segundo (vuelo dura 1/V) (def %.2f)\n"
"  --trail L           fantasmas de estela por bolita      (def %d)\n"
"  --cannon-layout M   roundrobin | blocks                 (def roundrobin)\n"
"  --muzzle-radius R0  radio de la esfera de bocas, [0,%.2f] (def %.2f)\n"
"  --recirculate 0|1   0 = aterrizan y se quedan: la esfera se completa\n"
"                      1 = el slot se redispara cada T_ciclo (carga plana\n"
"                          para medir, pero la esfera queda al 1-K*R/(V*N))\n"
"\n"
"Medicion:\n"
"  --bench K        mide K frames y termina, 0 = ventana\n"
"  --no-render      corre sin abrir ventana (headless)\n"
"  --csv            salida de mediciones en CSV\n"
"  --vsync 0|1      sincronia vertical, 0 para medir  (def 1)\n"
"  --threads T      hilos de OpenMP, 0 = automatico  (def %d).\n"
"                   Solo screensaver_omp lo usa; en el secuencial se acepta\n"
"                   y no hace nada.\n"
"  --dump-frame T   renderiza un unico frame en el instante T (segundos),\n"
"                   escribe el framebuffer crudo (ARGB8888) a stdout y\n"
"                   termina. Sirve para comparar seq vs omp con 'cmp'.\n"
"\n"
"  -h, --help       muestra esta ayuda\n"
"\n"
"Teclas en la ventana: ESC/Q salir, [ ] barren el angulo, P pausa, R reinicia.\n",
        prog,
        SS_DEF_N, SS_N_MAX,
        SS_DEF_WIDTH, SS_DEF_HEIGHT,
        SS_DEF_FIRE_RATE,
        SS_DEF_MUZZLE_SPEED,
        SS_DEF_TRAIL,
        SS_MUZZLE_RADIUS_MAX,
        SS_DEF_MUZZLE_RADIUS,
        SS_DEF_THREADS);
}

ArgsStatus args_parse(int argc, char **argv, Config *cfg)
{
    if (cfg == NULL) return ARGS_ERROR;

    *cfg = config_defaults();
    const char *prog = (argc > 0) ? argv[0] : "screensaver";

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];

        /* Ayuda. */
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            args_usage(prog);
            return ARGS_HELP;
        }

        /* Flags sin valor. */
        else if (strcmp(a, "--no-render") == 0) {
            cfg->headless = 1;
        }
        else if (strcmp(a, "--csv") == 0) {
            cfg->csv = 1;
        }

        /* Enteros. */
        else if (strcmp(a, "--n") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            long v;
            if (s == NULL || parse_long(s, a, &v) != 0) goto bad;
            cfg->n = (int)v;
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
        else if (strcmp(a, "--threads") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            long v;
            if (s == NULL || parse_long(s, a, &v) != 0) goto bad;
            cfg->threads = (int)v;
        }
        /* Reales. */
        else if (strcmp(a, "--angle") == 0) {
            /* El usuario escribe grados; adentro todo va en radianes. */
            const char *s = take_value(argc, argv, &i, a);
            double deg;
            if (s == NULL || parse_double(s, a, &deg) != 0) goto bad;
            cfg->angle_rad = deg * SS_PI / 180.0;
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
        else if (strcmp(a, "--dump-frame") == 0) {
            const char *s = take_value(argc, argv, &i, a);
            double v;
            if (s == NULL || parse_double(s, a, &v) != 0) goto bad;
            cfg->dump_frame   = 1;
            cfg->dump_frame_t = v;
        }

        /* Enumerado por nombre: '--cannon-layout blocks' se lee solo. */
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

        /* Booleanos 0|1. */
        else if (strcmp(a, "--physics") == 0) {
            if (take_bool(argc, argv, &i, a, &cfg->physics) != 0) goto bad;
        }
        else if (strcmp(a, "--voronoi") == 0) {
            if (take_bool(argc, argv, &i, a, &cfg->voronoi) != 0) goto bad;
        }
        else if (strcmp(a, "--raster") == 0) {
            if (take_bool(argc, argv, &i, a, &cfg->raster) != 0) goto bad;
        }
        else if (strcmp(a, "--recirculate") == 0) {
            if (take_bool(argc, argv, &i, a, &cfg->recirculate) != 0) goto bad;
        }
        else if (strcmp(a, "--vsync") == 0) {
            if (take_bool(argc, argv, &i, a, &cfg->vsync) != 0) goto bad;
        }

        /* Cualquier otra cosa es un error. */
        else {
            fprintf(stderr, "error: opcion desconocida '%s'\n", a);
            goto bad;
        }
    }

    /* El modo canon lo enciende --cannons, derivado una sola vez aca; K
     * negativo lo rechaza config_validate(), no se corrige en silencio. */
    cfg->cannon = (cfg->cannons >= 1);

    return ARGS_OK;

bad:
    args_usage(prog);
    return ARGS_ERROR;
}
