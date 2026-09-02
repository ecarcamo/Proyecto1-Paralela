/* ===========================================================================
 *  config.c - Valores por defecto, validacion cruzada e impresion.
 *
 *  La validacion vive aqui y no en args.c a proposito: args.c valida cada
 *  argumento POR SEPARADO mientras lo lee, pero hay reglas que solo se pueden
 *  comprobar cuando ya estan todos los campos llenos (por ejemplo, que el
 *  modo bench no tenga sentido con vsync activo). Esas van en
 *  config_validate() y se corren una sola vez al final.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#include "config.h"

#include <math.h>
#include <stdio.h>

Config config_defaults(void)
{
    Config cfg;

    cfg.n            = SS_DEF_N;
    cfg.width        = SS_DEF_WIDTH;
    cfg.height       = SS_DEF_HEIGHT;

    cfg.angle_rad    = SS_GOLDEN_ANG;   /* derivado de phi, no hard-coded */
    cfg.rot_speed    = SS_DEF_ROT_SPEED;
    cfg.sphere_frac  = SS_DEF_FILL;
    cfg.seed         = SS_DEF_SEED;

    cfg.color_speed  = SS_DEF_COLOR_SPEED;
    cfg.color_spread = SS_DEF_COLOR_SPREAD;

    /* Apagada por defecto: con la fisica encendida las semillas se relajan y
     * el patron aureo se convierte en un panal, o sea se deja de ver la esfera
     * de Fibonacci que es el objeto del proyecto. Es una demo aparte, se
     * enciende con --physics 1. */
    cfg.physics      = 0;
    /* Por defecto la esfera se dibuja con BOLITAS: es la figura de referencia
     * del proyecto (esfera de Fibonacci con una esferita por semilla). El
     * raycasting con celdas de Voronoi -- que es el kernel pesado a
     * paralelizar -- sigue disponible con --voronoi 1. */
    cfg.voronoi      = 0;
    /* Las bolitas se resuelven por RAYCASTING, no rasterizando discos. El
     * rasterizado sigue disponible con --raster 1, pero no es el baseline: su
     * costo es casi constante en N (el area total pintada no depende de N) y
     * no se paraleliza por semillas sin una carrera en el z-buffer. Ver el
     * comentario largo de render_balls_raycast(). */
    cfg.raster       = 0;

    /* Derivado de cannons al final de args_parse(). Aca solo tiene que ser
     * coherente con SS_DEF_CANNONS = 0: sin canones, modo apagado. */
    cfg.cannon       = 0;
    cfg.fire_rate    = SS_DEF_FIRE_RATE;
    cfg.muzzle_speed = SS_DEF_MUZZLE_SPEED;
    cfg.trail        = SS_DEF_TRAIL;
    cfg.cannons      = SS_DEF_CANNONS;
    cfg.cannon_layout= SS_DEF_CANNON_LAYOUT;
    cfg.muzzle_radius= SS_DEF_MUZZLE_RADIUS;
    cfg.recirculate  = SS_DEF_RECIRCULATE;

    cfg.threads      = SS_DEF_THREADS;

    cfg.bench_frames = 0;               /* 0 = modo ventana */
    cfg.headless     = 0;
    cfg.csv          = 0;

    cfg.dump_frame   = 0;
    cfg.dump_frame_t = 0.0;

    cfg.vsync        = 1;
    cfg.paused       = 0;

    return cfg;
}

int config_validate(const Config *cfg)
{
    if (cfg == NULL) {
        fprintf(stderr, "error: configuracion nula\n");
        return -1;
    }

    /* --- El parametro N del enunciado ---------------------------------- */
    if (cfg->n < SS_N_MIN) {
        fprintf(stderr, "error: N debe ser >= %d (se recibio %d)\n",
                SS_N_MIN, cfg->n);
        return -2;
    }
    if (cfg->n > SS_N_MAX) {
        fprintf(stderr,
                "error: N = %d excede el maximo permitido (%d).\n"
                "       El limite viene de la memoria: son 40 bytes por semilla\n"
                "       en la estructura SoA, o sea ~%d MB con N maximo.\n",
                cfg->n, SS_N_MAX, (int)((long)SS_N_MAX * 40 / (1024 * 1024)));
        return -3;
    }

    /* --- Canvas, encuadre y color: ya no se validan aca -----------------
     * width, height, sphere_frac, color_speed y color_spread perdieron su
     * bandera: los fija config_default() desde los SS_DEF_* y no hay camino
     * por el que el usuario les meta un valor. Validarlos seria codigo
     * inalcanzable, y peor: los mensajes nombraban --fill, --color-speed y
     * --color-spread, banderas que ya no existen.
     *
     * Los invariantes que esas ramas cuidaban (canvas >= 640x480 del
     * enunciado, encuadre en (0,1], magnitud del tono acotada por seguridad
     * fotosensible) siguen valiendo por construccion: se leen directamente de
     * las constantes en config.h. Si alguna vuelve a ser configurable, vuelve
     * su validacion con ella. */

    if (cfg->bench_frames < 0) {
        fprintf(stderr, "error: --bench debe ser >= 0\n");
        return -7;
    }

    /* 0 es "que decida OpenMP"; negativo es un error de tipeo, no una forma
     * valida de pedir algo. */
    if (cfg->threads < 0) {
        fprintf(stderr, "error: --threads debe ser >= 0 (se recibio %d);\n"
                        "       0 deja que OpenMP elija el numero de hilos.\n",
                cfg->threads);
        return -18;
    }

    /* --- Modo canon ------------------------------------------------------
     * --physics asume que las semillas ya estan sobre la esfera para poder
     * repelerse; una bolita a mitad de vuelo no tiene esa geometria. En vez
     * de aplicar la fisica solo a las aterrizadas (que cambiaria el
     * significado de la demo de Douady-Couder), se rechaza la combinacion
     * de entrada: mas simple, mas explicito, sin sorpresas. */
    if (cfg->cannon && cfg->physics) {
        fprintf(stderr,
                "error: --cannons y --physics no se pueden usar juntos: la\n"
                "       repulsion asume que las semillas ya estan sobre la\n"
                "       esfera, y en pleno vuelo no lo estan.\n");
        return -10;
    }
    if (cfg->cannon && !(cfg->fire_rate > 0.0)) {
        fprintf(stderr, "error: --fire-rate debe ser > 0 (se recibio %.3f)\n",
                cfg->fire_rate);
        return -11;
    }
    if (cfg->cannon && !(cfg->muzzle_speed > 0.0)) {
        fprintf(stderr, "error: --muzzle-speed debe ser > 0 (se recibio %.3f)\n",
                cfg->muzzle_speed);
        return -12;
    }
    if (cfg->cannon && cfg->trail < 0) {
        fprintf(stderr, "error: --trail debe ser >= 0 (se recibio %d)\n",
                cfg->trail);
        return -13;
    }
    /* K canones: no mas que indices para repartir. Con K > N habria canones
     * sin un solo indice asignado: no es un error sutil, es pedir algo que no
     * existe.
     *
     * El caso K < 1 se chequea SIEMPRE, no solo con el modo encendido: K = 0
     * es "sin canones" y es legitimo, pero K negativo es un error de tipeo que
     * no se puede interpretar como apagar el modo. Va aca y no dentro del
     * if (cfg->cannon) porque con K negativo cannon ya vale 0 y el guardia
     * dejaria pasar el disparate en silencio. */
    if (cfg->cannons < 0) {
        fprintf(stderr, "error: --cannons debe ser >= 0 (se recibio %d);\n"
                        "       0 apaga el modo canon, K >= 1 lo enciende.\n",
                cfg->cannons);
        return -14;
    }
    if (cfg->cannon && cfg->cannons > cfg->n) {
        fprintf(stderr,
                "error: --cannons %d es mayor que N = %d: sobrarian canones sin\n"
                "       ningun indice que disparar.\n",
                cfg->cannons, cfg->n);
        return -15;
    }
    /* La boca tiene que quedar ADENTRO de la esfera: con r0 >= 1 el canon
     * estaria sobre la superficie o afuera, y el "vuelo" iria hacia adentro. */
    if (cfg->cannon && (cfg->muzzle_radius < 0.0 ||
                        cfg->muzzle_radius > SS_MUZZLE_RADIUS_MAX)) {
        fprintf(stderr,
                "error: --muzzle-radius debe estar en [0, %.2f] (se recibio %.3f)\n",
                SS_MUZZLE_RADIUS_MAX, cfg->muzzle_radius);
        return -16;
    }
    if (cfg->cannon && cfg->cannon_layout != SS_CANNON_ROUNDROBIN &&
                       cfg->cannon_layout != SS_CANNON_BLOCKS) {
        fprintf(stderr, "error: --cannon-layout debe ser 'roundrobin' o 'blocks'\n");
        return -17;
    }

    /* --- Advertencias: no abortan, es decision del usuario ------------- */
    /* Bolitas en vuelo en regimen permanente = K*R/V. Si eso supera N, en
     * todo instante hay mas bolitas viajando que posiciones en el patron: la
     * esfera nunca termina de llenarse y lo que se ve es un chorro, no una
     * esfera densificandose. Es una eleccion legitima para forzar carga, asi
     * que se avisa y se sigue: la decision es del usuario. */
    if (cfg->cannon && cfg->muzzle_speed > 0.0) {
        double en_vuelo = (double)cfg->cannons * cfg->fire_rate / cfg->muzzle_speed;

        /* Sin recirculacion la esfera SIEMPRE termina completa: los indices se
         * disparan una vez y se quedan. Lo unico que puede pasar es que se
         * llene tan rapido que no se vea el llenado, y eso no es un problema
         * que amerite un aviso. Con recirculacion, en cambio, la fraccion
         * aterrizada en regimen permanente es 1 - K*R/(V*N) y puede quedar
         * ridiculamente baja sin que nada avise: el umbral viejo era
         * K*R/V > N, que solo pesca el caso extremo de fraccion <= 0. Con
         * N=400 K=8 R=60 V=1.5 daba 320 < 400 y no avisaba nada, pero la
         * esfera se quedaba en el 20% para siempre. */
        if (cfg->recirculate) {
            double frac = 1.0 - en_vuelo / (double)cfg->n;
            if (frac < 0.0) frac = 0.0;
            if (frac < 0.75) {
                fprintf(stderr,
                        "aviso: con --recirculate 1 solo el %.0f%% de la esfera va a estar\n"
                        "       puesta en regimen permanente (1 - K*R/(V*N), K*R/V = %.1f\n"
                        "       en vuelo contra N = %d): se ve un chorro, no una esfera.\n"
                        "       Para verla completa usa --recirculate 0 (el default), o\n"
                        "       baja --cannons/--fire-rate, o sube --muzzle-speed/--n.\n",
                        frac * 100.0, en_vuelo, cfg->n);
            }
        }
    }

    if (cfg->bench_frames > 0 && cfg->vsync) {
        fprintf(stderr,
                "aviso: --bench con vsync activo mide el tope del monitor, no\n"
                "       el costo real del frame. Agrega --vsync 0.\n");
    }

    if (cfg->bench_frames > 0 && cfg->bench_frames <= SS_DEF_BENCH_WARMUP) {
        fprintf(stderr,
                "aviso: --bench %d es menor o igual al calentamiento (%d frames);\n"
                "       no quedarian muestras utiles.\n",
                cfg->bench_frames, SS_DEF_BENCH_WARMUP);
    }

    return 0;
}

void config_print(const Config *cfg)
{
    if (cfg == NULL) return;

    printf("--- configuracion efectiva ---------------------------------\n");
    printf("  N (semillas)      : %d\n",        cfg->n);
    printf("  canvas            : %d x %d\n",   cfg->width, cfg->height);
    printf("  angulo divergencia: %.6f grados", cfg->angle_rad * 180.0 / SS_PI);
    if (cfg->angle_rad == SS_GOLDEN_ANG) printf("  (angulo aureo)");
    printf("\n");
    printf("  velocidad de giro : %.3f rad/s\n", cfg->rot_speed);
    if (cfg->color_speed != 0.0)
        printf("  deriva de color   : %.3f vueltas/s  (dispersion %.2f)\n",
               cfg->color_speed, cfg->color_spread);
    else
        printf("  deriva de color   : apagada (color fijo)\n");
    printf("  semilla PRNG      : %llu\n",       (unsigned long long)cfg->seed);
    printf("  fisica            : %s\n",         cfg->physics ? "si" : "no");
    printf("  kernel            : %s\n",
           cfg->voronoi ? "celdas de Voronoi (raycasting, O(P*N))"
                        : (cfg->raster ? "bolitas rasterizadas (plan B, ~O(1) en N)"
                                       : "bolitas por raycasting (O(P*N))"));
    if (cfg->cannon) {
        printf("  canon             : encendido (fire-rate=%.1f/s  muzzle-speed=%.2f  trail=%d)\n",
               cfg->fire_rate, cfg->muzzle_speed, cfg->trail);
        printf("  canones           : %d (%s)  radio de boca %.3f\n",
               cfg->cannons,
               cfg->cannon_layout == SS_CANNON_BLOCKS ? "bloques contiguos"
                                                      : "round-robin",
               cfg->muzzle_radius);
        /* El modelo de carga del informe, impreso con los numeros de ESTA
         * corrida. Sale aca y no en el informe a mano para que no se
         * desincronice del codigo:
         *
         *     dibujadas = n + (K*R/V) * (L/2)
         *
         * Las bolitas en vuelo NO se suman aparte de n: con recirculacion
         * cada indice esta o aterrizado o volando, nunca las dos cosas, asi
         * que las reales son siempre n y lo unico que se agrega son los
         * fantasmas. Y son L/2 y no L por bolita en vuelo, porque una recien
         * salida todavia no desplego la cola (los fantasmas no cruzan hacia
         * atras de su propio disparo) y la cantidad crece lineal con la fase.
         * Verificado contra el codigo en tests/test_sphere.c, seccion 8. */
        if (cfg->muzzle_speed > 0.0) {
            double t_llenado =
                (double)((cfg->n + cfg->cannons - 1) / cfg->cannons) / cfg->fire_rate;
            double en_vuelo = (double)cfg->cannons * cfg->fire_rate / cfg->muzzle_speed;
            if (en_vuelo > (double)cfg->n) en_vuelo = (double)cfg->n;

            if (cfg->recirculate) {
                printf("  recirculacion     : si  (T_ciclo=%.2f s; la esfera se queda al %.0f%%)\n",
                       t_llenado,
                       100.0 * (1.0 - en_vuelo / (double)cfg->n));
                printf("  carga             : en vuelo=%.0f   dibujadas~%.0f (constante)\n",
                       en_vuelo,
                       (double)cfg->n + en_vuelo * (double)cfg->trail / 2.0);
            } else {
                /* Sin recirculacion el regimen permanente es la esfera
                 * COMPLETA: n bolitas aterrizadas y cero en vuelo. El pico de
                 * carga ocurre durante el llenado, no al final. */
                printf("  recirculacion     : no  (la esfera se completa en %.2f s y se queda)\n",
                       t_llenado);
                printf("  carga             : pico ~%.0f durante el llenado; %d ya completa\n",
                       (double)cfg->n + en_vuelo * (double)cfg->trail / 2.0,
                       cfg->n);
            }
        }
    }
#ifdef _OPENMP
    /* En screensaver_seq esto no se compila: sin -fopenmp no hay hilos que
     * reportar, y config.c es el mismo archivo para los dos binarios. */
    if (cfg->threads > 0) printf("  hilos OpenMP      : %d\n", cfg->threads);
    else                  printf("  hilos OpenMP      : automatico\n");
#endif
    if (cfg->bench_frames > 0)
        printf("  modo benchmark    : %d frames (descarta %d de calentamiento)\n",
               cfg->bench_frames, SS_DEF_BENCH_WARMUP);
    printf("------------------------------------------------------------\n");
}
