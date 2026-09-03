/* physics.h - Repulsion de Coulomb sobre la esfera con Verlet: reproduce
 * Douady-Couder (1992). Formulas en docs/01 seccion 5. */
#ifndef PHYSICS_H
#define PHYSICS_H

#include "sphere.h"

/* Solo los parametros que aparecen en las formulas del doc 1, seccion 5. */
typedef struct {
    float k;        /* constante de Coulomb                                  */
    float epsilon;  /* softening: evita la singularidad cuando |pi-pj| -> 0   */
    float gamma;    /* friccion viscosa; sin ella el sistema oscila para siempre */
    float mass;     /* masa de cada semilla                                  */
} PhysicsParams;

/* Avanza un paso 'dt'; s->ax/ay/az entran como el a(t) del paso anterior.
 * El N^2 va COMPLETO (sin F_ji = -F_ij) para que no haya carrera al
 * paralelizar. No-op seguro con NULL, menos de 2 semillas o dt <= 0. */
void physics_step(SeedSet *s, const PhysicsParams *p, double dt);

/* Paso de tiempo maximo estable, en segundos: la aceleracion tipica crece con
 * N, asi que el limite va como 1/sqrt(N). Medido, dt*sqrt(N) se queda plano en
 * ~0.7 (N=5000) a 1.13 (N=128); la constante toma la mitad del peor caso. */
double physics_max_dt(int n);

#endif /* PHYSICS_H */
