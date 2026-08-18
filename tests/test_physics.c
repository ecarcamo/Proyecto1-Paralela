/* ===========================================================================
 *  test_physics.c - Verificacion de que la repulsion de Coulomb converge.
 *
 *  Corre SIN SDL. La metrica correcta para verificar esto es la ENERGIA
 *  POTENCIAL de Coulomb, no el angulo de divergencia: sphere_mean_divergence_deg
 *  mide el angulo entre la semilla i y la i+1 POR INDICE, y eso solo tiene
 *  sentido mientras el patron sigue siendo la espiral de Fibonacci sin tocar.
 *  En cuanto la fisica reordena las semillas libremente, el indice deja de
 *  corresponder a "vecino angular siguiente" y la metrica deja de ser un
 *  proxy valido de que tan buena es la configuracion.
 *
 *  Lo que la fisica SI optimiza, y lo que hay que medir, es la energia:
 *
 *      U = sum_{i<j} k / sqrt(|p_i - p_j|^2 + epsilon^2)
 *
 *  Se verifica que U baja y se estabiliza cerca de un minimo, y que ese
 *  minimo es comparable al de la propia configuracion aurea (mismo N, sin
 *  fisica) -- que es la firma real de que "las semillas se empujaron solas
 *  hacia un empaquetamiento tan bueno como el de Fibonacci", sin haber
 *  programado el angulo aureo (docs/01-FUNDAMENTO-MATEMATICO.md, seccion 5).
 *
 *      make test               (o)     ./bin/test_physics
 *
 *  Codigo de salida 0 si converge, 1 si no.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#include "sphere.h"
#include "physics.h"
#include "config.h"

#include <stdio.h>
#include <math.h>

static int g_fallos = 0;

static void check(const char *nombre, int ok, const char *detalle)
{
    printf("  [%s] %-52s %s\n", ok ? " OK " : "FALLA", nombre, detalle);
    if (!ok) g_fallos++;
}

/* Energia potencial de Coulomb con softening, todos-contra-todos (N^2/2
 * pares, sin el doble conteo: aca no hay condicion de carrera porque es un
 * escalar acumulado, no una fuerza por semilla -- physics_step si necesita el
 * N^2 completo por la razon que explica su propio comentario). */
static double energia_coulomb(const SeedSet *s, const PhysicsParams *p)
{
    const float eps2 = p->epsilon * p->epsilon;
    double u = 0.0;
    for (int i = 0; i < s->n; i++) {
        for (int j = i + 1; j < s->n; j++) {
            float dx = s->x[i] - s->x[j];
            float dy = s->y[i] - s->y[j];
            float dz = s->z[i] - s->z[j];
            float r2 = dx*dx + dy*dy + dz*dz + eps2;
            u += (double)p->k / sqrt((double)r2);
        }
    }
    return u;
}

int main(void)
{
    printf("=============================================================\n");
    printf(" Verificacion de la fisica - repulsion de Coulomb + Verlet\n");
    printf("=============================================================\n\n");

    /* N chico para que las miles de iteraciones corran rapido, y un angulo
     * racional (1/3 de vuelta) para arrancar bien lejos de un buen
     * empaquetamiento -- si la energia baja de todos modos, es la fisica la
     * que lo hizo. */
    const int n = 40;
    SeedSet s;
    if (seedset_alloc(&s, n) != 0) {
        fprintf(stderr, "error: no se pudo reservar memoria para %d semillas\n", n);
        return 1;
    }
    sphere_fill_fibonacci(&s, n, 2.0 * SS_PI * 1.0 / 3.0, SS_DEF_SEED);

    PhysicsParams pp = { SS_DEF_PHYS_K, SS_DEF_PHYS_EPSILON,
                          SS_DEF_PHYS_GAMMA, SS_DEF_PHYS_MASS };

    double u_inicial = energia_coulomb(&s, &pp);
    printf(" energia inicial (angulo racional 1/3): %.4f\n", u_inicial);

    const int    pasos = 20000;
    const double dt    = 0.01;
    for (int i = 0; i < pasos; i++) {
        physics_step(&s, &pp, dt);
    }

    double u_final = energia_coulomb(&s, &pp);
    printf(" energia tras %d pasos: %.4f\n", pasos, u_final);

    /* Config de referencia: la misma N, angulo aureo, SIN fisica -- el
     * "objetivo" al que un buen empaquetamiento deberia acercarse. */
    SeedSet ref;
    if (seedset_alloc(&ref, n) != 0) {
        fprintf(stderr, "error: no se pudo reservar memoria de referencia\n");
        seedset_free(&s);
        return 1;
    }
    sphere_fill_fibonacci(&ref, n, SS_GOLDEN_ANG, SS_DEF_SEED);
    double u_aureo = energia_coulomb(&ref, &pp);
    printf(" energia de referencia (Fibonacci aureo, sin fisica): %.4f\n\n", u_aureo);

    char msg[160];

    snprintf(msg, sizeof msg, "inicial=%.4f  final=%.4f (debe bajar)", u_inicial, u_final);
    check("La repulsion baja la energia potencial del sistema",
          u_final < u_inicial, msg);

    double vel2_max = 0.0;
    for (int i = 0; i < n; i++) {
        double v2 = (double)s.vx[i]*s.vx[i] + (double)s.vy[i]*s.vy[i] + (double)s.vz[i]*s.vz[i];
        if (v2 > vel2_max) vel2_max = v2;
    }
    snprintf(msg, sizeof msg, "|v|_max=%.2e (criterio < 1e-3)", sqrt(vel2_max));
    check("El sistema disipa su energia y se estabiliza (velocidad -> 0)",
          vel2_max < 1e-6, msg);

    /* Tolerancia generosa: no es el mismo minimo global (el problema de
     * Thomson tiene muchos), pero debe quedar en el mismo orden de magnitud
     * que el empaquetamiento de Fibonacci -- no en cualquier configuracion
     * apilada y mala. */
    double razon = u_final / u_aureo;
    snprintf(msg, sizeof msg, "final/aureo = %.3f (criterio < 1.10, o sea a lo sumo 10%% peor)",
             razon);
    check("El minimo alcanzado es comparable al de un buen empaquetamiento",
          razon < 1.10, msg);

    /* La geometria debe seguir intacta: la fisica renormaliza cada paso, asi
     * que las semillas nunca deberian salirse de la esfera unitaria. */
    double peor = 0.0;
    for (int i = 0; i < n; i++) {
        double L = sqrt((double)s.x[i]*s.x[i] + (double)s.y[i]*s.y[i] + (double)s.z[i]*s.z[i]);
        double e = fabs(L - 1.0);
        if (e > peor) peor = e;
    }
    snprintf(msg, sizeof msg, "error max %.2e (criterio 1e-4)", peor);
    check("Toda semilla sigue sobre la esfera unitaria tras la integracion",
          peor < 1e-4, msg);

    seedset_free(&ref);
    seedset_free(&s);

    printf("\n=============================================================\n");
    if (g_fallos == 0) printf(" TODOS LOS TESTS PASARON\n");
    else               printf(" %d TEST(S) FALLARON\n", g_fallos);
    printf("=============================================================\n");

    return g_fallos == 0 ? 0 : 1;
}
