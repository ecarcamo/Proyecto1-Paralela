/* sphere.h - Esfera de Fibonacci: z = 1 - 2(n+1/2)/N reparte AREAS iguales
 * (Arquimedes), rho = sqrt(1-z^2) y theta = n*psi. Ver docs/01 seccion 2. */
#ifndef SPHERE_H
#define SPHERE_H

#include <stdint.h>

#include "config.h"
#include "vec3.h"

/* SoA y no AoS: el Voronoi recorre solo x/y/z, asi cada linea de cache trae
 * 16 valores utiles y el compilador puede vectorizar sin gather. */
typedef struct {
    int       n;         /* semillas en uso                                  */
    int       capacity;  /* semillas asignadas                               */

    float    *x, *y, *z;     /* posicion sobre la esfera unitaria (|p| = 1)  */
    float    *vx, *vy, *vz;  /* velocidad tangencial (solo si hay fisica)    */
    float    *ax, *ay, *az;  /* aceleracion         (solo si hay fisica)     */
    uint32_t *color;         /* color pseudoaleatorio ARGB8888               */
} SeedSet;

/* Reserva 'capacity' semillas: 0 si todo bien, negativo si fallo (sin fugas). */
int  seedset_alloc(SeedSet *s, int capacity);

/* Libera y deja la estructura en cero. Seguro de llamar dos veces. */
void seedset_free(SeedSet *s);

/* La posicion i-esima del patron (esfera unitaria, angle_rad en RADIANES):
 * la usa tambien el canon, para no tener dos copias de la formula. */
Vec3 sphere_dir_fib(int i, int n, double angle_rad);

/* Genera la esfera en un SeedSet con capacity >= n; con un angle_rad distinto
 * de SS_GOLDEN_ANG se ve en vivo como degenera (teclas [ y ]). */
void sphere_fill_fibonacci(SeedSet *s, int n, double angle_rad, uint64_t seed);

/* Parametros del canon agrupados: se llama desde tres sitios distintos. */
typedef struct {
    int      n;             /* semillas del patron (el parametro N)          */
    double   angle_rad;     /* angulo de divergencia, en radianes            */
    uint64_t seed;          /* semilla del PRNG de colores                   */

    double   fire_rate;     /* R: disparos por segundo, POR CANON (> 0)      */
    double   muzzle_speed;  /* V: radios/s; el vuelo dura 1/V (> 0)          */
    int      trail;         /* L: fantasmas de estela por bolita (>= 0)      */

    int      cannons;       /* K: canones simultaneos (>= 1)                 */
    int      layout;        /* SS_CANNON_ROUNDROBIN o SS_CANNON_BLOCKS       */
    double   muzzle_radius; /* r0: radio de la esfera chica de bocas, [0, 1) */

    /* 0 = aterriza y se queda; 1 = se redispara cada T_ciclo (ver config.h). */
    int      recirculate;
} CannonParams;

/* Unico puente entre la Config del programa y el nucleo geometrico. */
CannonParams cannon_params_from_config(const Config *cfg);

/* A que canon toca el indice i y en que ronda: ROUNDROBIN da canon = i % K y
 * BLOCKS bloques con base(c) = techo(c*N/K). Los K cubren los N indices
 * exactamente una vez; cannon_out y round_out pueden ser NULL. */
void cannon_slot(int i, int n, int cannons, int layout,
                 int *cannon_out, int *round_out);

/* Rondas por ciclo: techo(n/K). El periodo es cannon_rounds(n,K) / fire_rate. */
int cannon_rounds(int n, int cannons);

/* Boca del canon c: Fibonacci con K puntos sobre la esfera de radio r0. */
Vec3 cannon_muzzle(int c, const CannonParams *p);

/* Semillas a reservar con canones: capacity = n + min(techo(K*R/V)+K, n)*L.
 * Es la cota del peor caso y se calcula ANTES de seedset_alloc(), que no
 * tiene camino de crecimiento. */
int sphere_cannon_capacity(const CannonParams *p);

/* Reescribe el SoA para el instante 't' en forma cerrada:
 *     fase(i,t) = t - ronda(i)/R,  radio = clamp(V*fase, 0, 1)
 *     pos(i,t)  = lerp(boca(canon(i)), dir_fib(i), radio)
 * La estela son 'trail' fantasmas: la misma formula en (fase - j*delta). Todo
 * es funcion pura de (i, t), que es lo que permite comparar seq contra omp.
 * Deja s->n en (vivas + fantasmas); pide capacity >= sphere_cannon_capacity(). */
void sphere_fill_cannon(SeedSet *s, const CannonParams *p, double t);

/* Devuelve la semilla i como Vec3. */
static inline Vec3 seed_pos(const SeedSet *s, int i)
{
    return v3(s->x[i], s->y[i], s->z[i]);
}

/* Angulo de divergencia medio en GRADOS: con --physics 1 se lo ve converger
 * solo hacia 137.5. Asume todas las semillas sobre la esfera. */
double sphere_mean_divergence_deg(const SeedSet *s);

#endif /* SPHERE_H */
