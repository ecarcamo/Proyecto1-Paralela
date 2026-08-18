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

    cfg.physics      = 1;
    cfg.voronoi      = 1;

    cfg.bench_frames = 0;               /* 0 = modo ventana */
    cfg.headless     = 0;
    cfg.csv          = 0;

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

    /* --- Canvas: el enunciado exige un minimo de 640x480 --------------- */
    if (cfg->width < SS_WIDTH_MIN || cfg->height < SS_HEIGHT_MIN) {
        fprintf(stderr,
                "error: el canvas minimo es %dx%d (se recibio %dx%d)\n",
                SS_WIDTH_MIN, SS_HEIGHT_MIN, cfg->width, cfg->height);
        return -4;
    }
    if (cfg->width > SS_DIM_MAX || cfg->height > SS_DIM_MAX) {
        fprintf(stderr, "error: el canvas maximo es %dx%d\n",
                SS_DIM_MAX, SS_DIM_MAX);
        return -5;
    }

    /* --- Geometria ----------------------------------------------------- */
    if (cfg->sphere_frac <= 0.0 || cfg->sphere_frac > 1.0) {
        fprintf(stderr, "error: --fill debe estar en (0, 1] (se recibio %.3f)\n",
                cfg->sphere_frac);
        return -6;
    }

    if (cfg->bench_frames < 0) {
        fprintf(stderr, "error: --bench debe ser >= 0\n");
        return -7;
    }

    /* --- Advertencias: no abortan, es decision del usuario ------------- */
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
    printf("  semilla PRNG      : %llu\n",       (unsigned long long)cfg->seed);
    printf("  fisica            : %s\n",         cfg->physics ? "si" : "no");
    printf("  voronoi           : %s\n",         cfg->voronoi ? "celdas" : "puntos");
    if (cfg->bench_frames > 0)
        printf("  modo benchmark    : %d frames (descarta %d de calentamiento)\n",
               cfg->bench_frames, SS_DEF_BENCH_WARMUP);
    printf("------------------------------------------------------------\n");
}
