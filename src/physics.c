/* physics.c - Repulsion de Coulomb sobre la esfera, integrada con Verlet. */
#include "physics.h"

#include <math.h>
#include <stdlib.h>

#include "config.h"
#include "vec3.h"

/* Debajo de este N el fork/join cuesta mas que el bucle (clausula if). */
#define PHYS_PAR_MIN 256

/* Limite de estabilidad ~ 1/sqrt(N); la tabla medida esta en physics.h. */
double physics_max_dt(int n)
{
    if (n < 2) return 1.0;                 /* con 0 o 1 semilla no hay fuerzas */
    return SS_PHYS_DT_SAFETY / sqrt((double)n);
}

/* Un paso de Velocity-Verlet: pos con a(t), renormalizar, fuerzas N^2, v y a(t+dt). */
void physics_step(SeedSet *s, const PhysicsParams *p, double dt)
{
    if (s == NULL || p == NULL || s->n < 2 || dt <= 0.0) return;

    const int n = s->n;
    const float fdt = (float)dt;

    /* Buffer aparte: todas las posiciones nuevas antes de cualquier fuerza. */
    float *nx = (float *)malloc((size_t)n * sizeof(float));
    float *ny = (float *)malloc((size_t)n * sizeof(float));
    float *nz = (float *)malloc((size_t)n * sizeof(float));
    if (nx == NULL || ny == NULL || nz == NULL) {
        free(nx); free(ny); free(nz);
        return;                                   /* sin memoria, no crash */
    }

    /* 1) p(t+dt) = p + v*dt + 0.5*a(t)*dt^2, y 2) de vuelta a la esfera. */
    #pragma omp parallel for schedule(dynamic, 1) if(n >= PHYS_PAR_MIN)
    for (int i = 0; i < n; ++i) {
        Vec3 pos = seed_pos(s, i);
        Vec3 vel = v3(s->vx[i], s->vy[i], s->vz[i]);
        Vec3 acc_old = v3(s->ax[i], s->ay[i], s->az[i]);

        Vec3 pos_new = v3_add(pos, v3_scale(vel, fdt));
        pos_new = v3_madd(pos_new, acc_old, 0.5f * fdt * fdt);
        pos_new = v3_norm(pos_new);

        nx[i] = pos_new.x;
        ny[i] = pos_new.y;
        nz[i] = pos_new.z;
    }

    for (int i = 0; i < n; ++i) {
        s->x[i] = nx[i];
        s->y[i] = ny[i];
        s->z[i] = nz[i];
    }
    free(nx); free(ny); free(nz);

    const float eps2 = p->epsilon * p->epsilon;

    /* 3) N^2 COMPLETO: cada i escribe solo lo suyo, asi no hay carrera.
     * dynamic,1 aunque la carga sea uniforme, porque la CPU es hibrida: a 24
     * hilos y N=16000, static da 58.9 ms y dynamic,1 50.3 (guided 55.2). */
    #pragma omp parallel for schedule(dynamic, 1) if(n >= PHYS_PAR_MIN)
    for (int i = 0; i < n; ++i) {
        Vec3 pi = seed_pos(s, i);
        Vec3 F  = v3_zero();

        for (int j = 0; j < n; ++j) {
            if (j == i) continue;

            Vec3  d  = v3_sub(pi, seed_pos(s, j));
            float r2 = v3_len2(d) + eps2;
            float inv_r3 = 1.0f / (r2 * sqrtf(r2));       /* (r^2)^(3/2) = r^3 */

            F = v3_madd(F, d, p->k * inv_r3);
        }

        /* Proyectar al plano tangente: la semilla solo se mueve sobre S^2. */
        F = v3_reject(F, pi);

        Vec3 vel = v3(s->vx[i], s->vy[i], s->vz[i]);
        Vec3 acc_new = v3_sub(v3_scale(F, 1.0f / p->mass), v3_scale(vel, p->gamma));

        /* 4) velocidad con el promedio de aceleraciones. */
        Vec3 acc_old = v3(s->ax[i], s->ay[i], s->az[i]);
        Vec3 vel_new = v3_madd(vel, v3_add(acc_old, acc_new), 0.5f * fdt);

        s->vx[i] = vel_new.x; s->vy[i] = vel_new.y; s->vz[i] = vel_new.z;

        /* 5) persistir a(t+dt) para el proximo paso. */
        s->ax[i] = acc_new.x; s->ay[i] = acc_new.y; s->az[i] = acc_new.z;
    }
}
