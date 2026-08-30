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

    /* Apagado por defecto: es una demo aparte, se enciende con --cannon 1. */
    cfg.cannon       = 0;
    cfg.fire_rate    = SS_DEF_FIRE_RATE;
    cfg.muzzle_speed = SS_DEF_MUZZLE_SPEED;
    cfg.trail        = SS_DEF_TRAIL;

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

    /* --- Deriva de color ----------------------------------------------
     * El signo se permite (invierte el sentido del recorrido del tono); lo que
     * se acota es la MAGNITUD: mas de SS_COLOR_SPEED_MAX vueltas por segundo
     * ya no es una transicion gradual sino un parpadeo de colores saturados a
     * pantalla completa, que ademas de verse mal es un riesgo real para gente
     * fotosensible. */
    if (!(fabs(cfg->color_speed) <= SS_COLOR_SPEED_MAX)) {
        fprintf(stderr,
                "error: --color-speed debe estar en [-%.1f, %.1f] vueltas/s\n"
                "       (se recibio %.3f). Usa 0 para dejar el color fijo.\n",
                SS_COLOR_SPEED_MAX, SS_COLOR_SPEED_MAX, cfg->color_speed);
        return -8;
    }
    if (!(cfg->color_spread >= 0.0 && cfg->color_spread <= 1.0)) {
        fprintf(stderr,
                "error: --color-spread debe estar en [0, 1] (se recibio %.3f)\n",
                cfg->color_spread);
        return -9;
    }

    if (cfg->bench_frames < 0) {
        fprintf(stderr, "error: --bench debe ser >= 0\n");
        return -7;
    }

    /* --- Modo canon ------------------------------------------------------
     * --physics asume que las semillas ya estan sobre la esfera para poder
     * repelerse; una bolita a mitad de vuelo no tiene esa geometria. En vez
     * de aplicar la fisica solo a las aterrizadas (que cambiaria el
     * significado de la demo de Douady-Couder), se rechaza la combinacion
     * de entrada: mas simple, mas explicito, sin sorpresas. */
    if (cfg->cannon && cfg->physics) {
        fprintf(stderr,
                "error: --cannon y --physics no se pueden usar juntos: la\n"
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
    if (cfg->cannon)
        printf("  canon             : encendido (fire-rate=%.1f/s  muzzle-speed=%.2f  trail=%d)\n",
               cfg->fire_rate, cfg->muzzle_speed, cfg->trail);
    if (cfg->bench_frames > 0)
        printf("  modo benchmark    : %d frames (descarta %d de calentamiento)\n",
               cfg->bench_frames, SS_DEF_BENCH_WARMUP);
    printf("------------------------------------------------------------\n");
}
