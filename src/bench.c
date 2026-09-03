/* bench.c - Bucle de medicion headless con estadisticas y salida CSV. */
#include "bench.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "physics.h"
#include "sphere.h"
#include "timing.h"

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

BenchStats bench_run(Framebuffer *fb, SeedSet *s, const Config *cfg)
{
    BenchStats st = {0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    if (fb == NULL || s == NULL || cfg == NULL) return st;

    int total  = (cfg->bench_frames > 0) ? cfg->bench_frames : 200;
    int warmup = (total > SS_DEF_BENCH_WARMUP) ? SS_DEF_BENCH_WARMUP : 0;
    int useful = total - warmup;
    if (useful <= 0) return st;

    /* Se guardan todos los tiempos: la mediana exige ordenar. */
    double *tiempos = (double *)malloc((size_t)useful * sizeof(double));
    if (tiempos == NULL) return st;                   /* sin memoria, no crash */

    PhysicsParams pp = { SS_DEF_PHYS_K, SS_DEF_PHYS_EPSILON,
                          SS_DEF_PHYS_GAMMA, SS_DEF_PHYS_MASS };

    /* Mismo limite de Verlet que el bucle con ventana: con N grande, 1/60 s
     * ya lo pasa y se mediria sobre una nube explotada. */
    const double dt_phys = (1.0 / 60.0 < physics_max_dt(s->n))
                         ? 1.0 / 60.0 : physics_max_dt(s->n);

    /* Con canones se salta el llenado: t0 = T_ciclo + 1/V es el primer instante
     * de regimen permanente (esfera completa, o carga constante si recircula). */
    CannonParams cp = cannon_params_from_config(cfg);
    double t = 0.0;
    if (cfg->cannon) {
        t = (double)cannon_rounds(cfg->n, cfg->cannons) / cfg->fire_rate
          + 1.0 / cfg->muzzle_speed;
    }

    int    idx = 0;
    for (int f = 0; f < total; ++f) {
        if (cfg->cannon) sphere_fill_cannon(s, &cp, t);

        double a = now_seconds();
        render_frame(fb, s, cfg, t);
        if (cfg->physics) physics_step(s, &pp, dt_phys);
        double b = now_seconds();

        t += 1.0 / 60.0;                    /* avance de tiempo simulado fijo */
        if (f >= warmup) tiempos[idx++] = (b - a) * 1000.0;
    }

    /* Media y sd en dos pasadas: mas claro que Welford y el costo no importa. */
    double suma = 0.0;
    for (int i = 0; i < useful; ++i) suma += tiempos[i];
    double media = suma / (double)useful;

    double sum2 = 0.0;
    for (int i = 0; i < useful; ++i) {
        double d = tiempos[i] - media;
        sum2 += d * d;
    }
    double sd = (useful > 1) ? sqrt(sum2 / (double)(useful - 1)) : 0.0;

    qsort(tiempos, (size_t)useful, sizeof(double), cmp_double);
    double mediana = (useful % 2 == 0)
        ? (tiempos[useful/2 - 1] + tiempos[useful/2]) / 2.0
        : tiempos[useful/2];

    st.frames  = useful;
    st.media   = media;
    st.mediana = mediana;
    st.min     = tiempos[0];
    st.max     = tiempos[useful - 1];
    st.sd      = sd;
    st.fps     = (media > 0.0) ? 1000.0 / media : 0.0;

    free(tiempos);
    return st;
}

void bench_print_human(const BenchStats *st, const Config *cfg)
{
    if (st == NULL || cfg == NULL) return;
    printf("N=%d  frames=%d  media=%.2f ms  mediana=%.2f  min=%.2f  max=%.2f  sd=%.2f  FPS=%.2f\n",
           cfg->n, st->frames, st->media, st->mediana, st->min, st->max, st->sd, st->fps);
}

void bench_print_csv(const BenchStats *st, const Config *cfg)
{
    if (st == NULL || cfg == NULL) return;
    /* n,width,height,frames,media,mediana,min,max,sd,fps */
    printf("%d,%d,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f\n",
           cfg->n, cfg->width, cfg->height, st->frames,
           st->media, st->mediana, st->min, st->max, st->sd, st->fps);
}
