/* sphere.c - Esfera de Fibonacci, modo canon y medicion de la divergencia. */
#include "sphere.h"
#include "color.h"
#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- memoria --- */

/* Un solo malloc para los diez arreglos: x/y/z quedan contiguos (lo que quiere
 * el prefetcher del Voronoi) y hay un unico puntero que liberar. */
int seedset_alloc(SeedSet *s, int capacity)
{
    if (s == NULL || capacity < 1) return -1;

    memset(s, 0, sizeof(*s));

    size_t nf    = (size_t)capacity;
    size_t bytes = nf * (9u * sizeof(float) + sizeof(uint32_t));

    void *block = malloc(bytes);
    if (block == NULL) return -2;

    float *f = (float *)block;
    s->x  = f + 0 * nf;   s->y  = f + 1 * nf;   s->z  = f + 2 * nf;
    s->vx = f + 3 * nf;   s->vy = f + 4 * nf;   s->vz = f + 5 * nf;
    s->ax = f + 6 * nf;   s->ay = f + 7 * nf;   s->az = f + 8 * nf;
    s->color = (uint32_t *)(f + 9 * nf);

    s->capacity = capacity;
    s->n        = 0;
    return 0;
}

void seedset_free(SeedSet *s)
{
    if (s == NULL) return;
    free(s->x);                      /* x es el inicio del bloque unico */
    memset(s, 0, sizeof(*s));
}

/* ---------------------------------------------------- esfera de Fibonacci - */

Vec3 sphere_dir_fib(int i, int n, double angle_rad)
{
    if (n < 1) n = 1;

    /* Grosor de cada franja de area 4*pi/N; el +1/2 centra la semilla. */
    const double dz = 2.0 / (double)n;

    /* Altura: reparto de AREAS iguales (teorema de Arquimedes). */
    double z = 1.0 - dz * ((double)i + 0.5);

    /* Radio del paralelo; el chequeo evita NaN si z se pasa de [-1,1]. */
    double rho2 = 1.0 - z * z;
    double rho  = rho2 > 0.0 ? sqrt(rho2) : 0.0;

    /* Azimut: angulo aureo acumulado en double, con fmod para no perder rango. */
    double theta = fmod((double)i * angle_rad, 2.0 * SS_PI);

    return v3((float)(rho * cos(theta)), (float)(rho * sin(theta)), (float)z);
}

void sphere_fill_fibonacci(SeedSet *s, int n, double angle_rad, uint64_t seed)
{
    if (s == NULL || n < 1 || n > s->capacity) return;

    s->n = n;

    for (int i = 0; i < n; i++) {
        Vec3 p = sphere_dir_fib(i, n, angle_rad);

        s->x[i] = p.x;
        s->y[i] = p.y;
        s->z[i] = p.z;

        /* Estado de la fisica en reposo; si --physics 0, nunca se toca. */
        s->vx[i] = s->vy[i] = s->vz[i] = 0.0f;
        s->ax[i] = s->ay[i] = s->az[i] = 0.0f;

        /* Color: funcion pura de (i, seed), igual en secuencial y paralelo. */
        s->color[i] = color_for_seed((uint32_t)i, seed);
    }
}

/* ---- modo canon: todo funcion pura de (i, t), sin estado entre llamadas --- */

CannonParams cannon_params_from_config(const Config *cfg)
{
    CannonParams p;

    if (cfg == NULL) {
        memset(&p, 0, sizeof p);
        return p;
    }

    p.n             = cfg->n;
    p.angle_rad     = cfg->angle_rad;
    p.seed          = cfg->seed;
    p.fire_rate     = cfg->fire_rate;
    p.muzzle_speed  = cfg->muzzle_speed;
    p.trail         = cfg->trail;
    p.cannons       = cfg->cannons;
    p.layout        = cfg->cannon_layout;
    p.muzzle_radius = cfg->muzzle_radius;
    p.recirculate   = cfg->recirculate;
    return p;
}

int cannon_rounds(int n, int cannons)
{
    if (n < 1) n = 1;
    if (cannons < 1) cannons = 1;
    if (cannons > n) cannons = n;
    return (n + cannons - 1) / cannons;          /* techo(n / K) */
}

void cannon_slot(int i, int n, int cannons, int layout,
                 int *cannon_out, int *round_out)
{
    if (n < 1) n = 1;
    if (cannons < 1) cannons = 1;
    if (cannons > n) cannons = n;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;

    int c, r;

    if (layout == SS_CANNON_BLOCKS) {
        /* Bloques contiguos con base(c) = techo(c*n/K): su inverso es exacto y
         * O(1), canon = piso(i*K/n). Con piso() habria que buscar el bloque. */
        long li = (long)i, lk = (long)cannons, ln = (long)n;
        c = (int)((li * lk) / ln);
        if (c >= cannons) c = cannons - 1;        /* defensivo ante redondeo */
        long base = ((long)c * ln + lk - 1) / lk; /* techo(c*n/K)            */
        r = (int)(li - base);
        if (r < 0) r = 0;
    } else {
        /* Round-robin: como los indices van separados por el angulo aureo,
         * los K chorros se entremezclan y la esfera se puebla pareja. */
        c = i % cannons;
        r = i / cannons;
    }

    if (cannon_out != NULL) *cannon_out = c;
    if (round_out  != NULL) *round_out  = r;
}

/* Bocas repartidas con la misma construccion de Fibonacci, pero con K puntos. */
Vec3 cannon_muzzle(int c, const CannonParams *p)
{
    if (p == NULL) return v3_zero();

    int k = (p->cannons < 1) ? 1 : p->cannons;
    if (k > p->n && p->n >= 1) k = p->n;

    double r0 = p->muzzle_radius;
    if (!(r0 > 0.0)) return v3_zero();            /* todas las bocas al origen */

    return v3_scale(sphere_dir_fib(c, k, p->angle_rad), (float)r0);
}

/* Capacidad = n + min(techo(K*R/V) + K, n) * L: el peor caso con la cola llena. */
int sphere_cannon_capacity(const CannonParams *p)
{
    if (p == NULL) return 1;

    int n = (p->n < 1) ? 1 : p->n;
    if (!(p->fire_rate > 0.0) || !(p->muzzle_speed > 0.0) || p->trail < 0)
        return n;

    int k = (p->cannons < 1) ? 1 : p->cannons;
    if (k > n) k = n;

    /* En regimen permanente vuelan K*R/V bolitas; el "+ k" es margen de redondeo. */
    double en_vuelo_d = (double)k * p->fire_rate / p->muzzle_speed;
    if (!(en_vuelo_d >= 0.0)) en_vuelo_d = 0.0;           /* NaN defensivo */
    if (en_vuelo_d > (double)n) en_vuelo_d = (double)n;   /* no hay mas que indices */
    long en_vuelo = (long)ceil(en_vuelo_d) + (long)k;
    if (en_vuelo > (long)n) en_vuelo = n;

    long capacity = (long)n + en_vuelo * (long)p->trail;
    if (capacity < n) capacity = n;                 /* overflow defensivo */
    if (capacity > SS_N_MAX) capacity = SS_N_MAX;   /* mismo techo que --n */
    return (int)capacity;
}

/* Separacion entre fantasmas: reparte 'trail' a lo largo del vuelo (1/V s). */
static double cannon_trail_delta(double muzzle_speed, int trail)
{
    return (1.0 / muzzle_speed) / (double)(trail + 1);
}

/* Cuanto hace que salio del canon; 0 si todavia no tuvo su primer disparo. */
static int cannon_phase(int i, const CannonParams *p, double t_ciclo, double t,
                        double *fase_out)
{
    int ronda;
    cannon_slot(i, p->n, p->cannons, p->layout, NULL, &ronda);

    double t_disparo = (double)ronda / p->fire_rate;
    double edad = t - t_disparo;
    if (edad < 0.0) return 0;                     /* todavia no se disparo */

    double fase = edad;

    /* Recirculacion = este fmod; sin el la fase crece y la bolita se queda. */
    if (p->recirculate) {
        fase = fmod(edad, t_ciclo);
        if (fase < 0.0) fase += t_ciclo;
    }

    *fase_out = fase;
    return 1;
}

/* Vuelo en linea recta de la boca a su lugar de Fibonacci: sigue siendo cerrada. */
static Vec3 cannon_pos_at_phase(int i, const CannonParams *p, double fase)
{
    double radio = p->muzzle_speed * fase;
    if (radio > 1.0) radio = 1.0;
    if (radio < 0.0) radio = 0.0;

    Vec3 destino = sphere_dir_fib(i, p->n, p->angle_rad);
    if (radio >= 1.0) return destino;             /* aterrizada: exacta */

    int c;
    cannon_slot(i, p->n, p->cannons, p->layout, &c, NULL);
    Vec3 boca = cannon_muzzle(c, p);

    /* lerp(boca, destino, radio) = boca + (destino - boca) * radio */
    return v3_madd(boca, v3_sub(destino, boca), (float)radio);
}

/* Semilla real y fantasma de estela van al mismo slot: el renderer no distingue. */
static void cannon_write(SeedSet *s, int slot, Vec3 pos, uint32_t color)
{
    s->x[slot] = pos.x;
    s->y[slot] = pos.y;
    s->z[slot] = pos.z;
    s->vx[slot] = s->vy[slot] = s->vz[slot] = 0.0f;
    s->ax[slot] = s->ay[slot] = s->az[slot] = 0.0f;
    s->color[slot] = color;
}

void sphere_fill_cannon(SeedSet *s, const CannonParams *p, double t)
{
    if (s == NULL || p == NULL) return;
    if (p->n < 1 || !(p->fire_rate > 0.0) || !(p->muzzle_speed > 0.0) ||
        p->trail < 0)
        return;

    const int    n         = p->n;
    const double t_vuelo   = 1.0 / p->muzzle_speed;
    const double t_ciclo   = (double)cannon_rounds(n, p->cannons) / p->fire_rate;
    const double delta     = cannon_trail_delta(p->muzzle_speed, p->trail);
    const int    cap       = s->capacity;

    int written = 0;

    for (int i = 0; i < n && written < cap; i++) {
        double fase;
        if (!cannon_phase(i, p, t_ciclo, t, &fase))
            continue;                              /* aun no se dispara */

        uint32_t base_color = color_for_seed((uint32_t)i, p->seed);

        /* La bolita real (en vuelo o ya aterrizada) de este instante. */
        cannon_write(s, written, cannon_pos_at_phase(i, p, fase), base_color);
        written++;

        if (fase >= t_vuelo) continue;             /* aterrizada: no lleva cola */

        /* Fantasmas: la misma formula evaluada en el pasado. */
        for (int j = 1; j <= p->trail && written < cap; j++) {
            double fase_j = fase - (double)j * delta;
            if (fase_j < 0.0) break;               /* la cola no cruza su disparo */

            /* El fantasma mas viejo, el mas tenue. */
            float k = 1.0f - (float)j / (float)(p->trail + 1);
            cannon_write(s, written, cannon_pos_at_phase(i, p, fase_j),
                         rgb_mul(base_color, k));
            written++;
        }
    }

    s->n = written;
}

/* ------------------------------------------------ angulo de divergencia --- */

/* Diferencia media de azimut entre semillas consecutivas, en grados. */
double sphere_mean_divergence_deg(const SeedSet *s)
{
    if (s == NULL || s->n < 2) return 0.0;

    double sum   = 0.0;
    int    count = 0;

    for (int i = 0; i + 1 < s->n; i++) {
        double a0 = atan2((double)s->y[i],     (double)s->x[i]);
        double a1 = atan2((double)s->y[i + 1], (double)s->x[i + 1]);

        /* Envolver a [0, 2pi): las diferencias crudas pasan de una vuelta. */
        double d = a1 - a0;
        while (d < 0.0)          d += 2.0 * SS_PI;
        while (d >= 2.0 * SS_PI) d -= 2.0 * SS_PI;

        sum += d;
        count++;
    }

    if (count == 0) return 0.0;
    return (sum / (double)count) * 180.0 / SS_PI;
}
