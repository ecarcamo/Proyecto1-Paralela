/* ===========================================================================
 *  physics.h - Repulsion tipo Coulomb sobre la esfera, integrada con Verlet.
 *
 *  Reproduce el experimento de Douady y Couder (1992): gotas de ferrofluido
 *  que se repelen y, sin que nadie les diga el numero, terminan acomodadas
 *  en el angulo aureo. Aca las "gotas" son las semillas de la esfera y la
 *  fuerza es un Coulomb con softening.
 *
 *  Ver docs/01-FUNDAMENTO-MATEMATICO.md seccion 5 para las formulas.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#ifndef PHYSICS_H
#define PHYSICS_H

#include "sphere.h"

/* ---------------------------------------------------------------------------
 *  Parametros del sistema fisico. Solo los que aparecen en las formulas del
 *  doc 1 seccion 5 -- nada mas, para no inventar constantes sin respaldo.
 * ------------------------------------------------------------------------- */
typedef struct {
    float k;        /* constante de Coulomb                                  */
    float epsilon;  /* softening: evita la singularidad cuando |pi-pj| -> 0   */
    float gamma;    /* friccion viscosa; sin ella el sistema oscila para siempre */
    float mass;     /* masa de cada semilla                                  */
} PhysicsParams;

/* ---------------------------------------------------------------------------
 *  Avanza un paso de tiempo 'dt' con repulsion de Coulomb + Verlet.
 *
 *  IMPORTANTE sobre 's->ax/ay/az': se usan como el a(t) YA CALCULADO en la
 *  llamada anterior (Verlet de un solo calculo de fuerza por paso, reusando
 *  la aceleracion vieja). sphere_fill_fibonacci() ya los deja en cero, asi
 *  que el primer paso arranca bien. No hay que "resetearlos" antes de llamar.
 *
 *  El N^2 de la repulsion se hace COMPLETO, sin aprovechar F_ji = -F_ij:
 *  escribir en F_j desde la iteracion de i seria una condicion de carrera en
 *  la version paralela. Cada semilla escribe solo su propia fuerza (docs/01
 *  seccion 5.1).
 *
 *  No-op seguro si s es NULL, p es NULL, hay menos de 2 semillas, o dt <= 0.
 * ------------------------------------------------------------------------- */
void physics_step(SeedSet *s, const PhysicsParams *p, double dt);

#endif /* PHYSICS_H */
