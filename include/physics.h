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

/* ---------------------------------------------------------------------------
 *  Paso de tiempo maximo ESTABLE para N semillas, en segundos.
 *
 *  Verlet es estable solo si dt es chico frente a la escala de la fuerza, y
 *  esa escala CRECE con N: cada semilla siente la repulsion de las otras N-1,
 *  asi que la aceleracion tipica va como N y el limite de estabilidad va como
 *  1/sqrt(N). Medido en esta maquina (60 pasos, se considera inestable si
 *  |v|max supera 3 o si el angulo medio se despega de 137.5):
 *
 *      N        dt maximo estable     dt*sqrt(N)
 *      128          0.100               1.13
 *      600          0.035               0.86
 *      2000         0.0167              0.75
 *      5000         0.010               0.71
 *
 *  El producto dt*sqrt(N) se queda plano en ~0.7-1.1, que es justo la ley
 *  1/sqrt(N). La constante de abajo se toma en la mitad del peor caso medido
 *  para dejar margen.
 *
 *  Sin este limite, con N grande el dt real del frame (que ademas es enorme
 *  porque el frame tarda) hace explotar la integracion: las velocidades se
 *  van a 500, las semillas se dispersan y el patron de Fibonacci desaparece
 *  en menos de un segundo.
 * ------------------------------------------------------------------------- */
double physics_max_dt(int n);

#endif /* PHYSICS_H */
