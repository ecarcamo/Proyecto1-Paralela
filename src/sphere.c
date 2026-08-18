/* ===========================================================================
 *  sphere.c - Generacion de la esfera de Fibonacci.
 *
 *  Son tres lineas de matematica. El resto del archivo es manejo de memoria
 *  a prueba de fallos y la medicion del angulo de divergencia.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#include "sphere.h"
#include "color.h"
#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 *  Memoria
 * -------------------------------------------------------------------------- */

int seedset_alloc(SeedSet *s, int capacity)
{
    if (s == NULL || capacity < 1) return -1;

    memset(s, 0, sizeof(*s));

    /* Un solo malloc para los diez arreglos en vez de diez mallocs sueltos:
     * garantiza que x[], y[] y z[] queden contiguos en memoria, que es
     * exactamente lo que quiere el prefetcher cuando el bucle del Voronoi los
     * recorre en paralelo. Ademas hay un solo puntero que liberar, asi que no
     * existe el camino de "fallo a la mitad y hay que deshacer parcialmente". */
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
    /* x apunta al inicio del bloque unico: liberarlo libera todo. */
    free(s->x);
    memset(s, 0, sizeof(*s));
}

/* --------------------------------------------------------------------------
 *  El nucleo: la esfera de Fibonacci
 * -------------------------------------------------------------------------- */

void sphere_fill_fibonacci(SeedSet *s, int n, double angle_rad, uint64_t seed)
{
    if (s == NULL || n < 1 || n > s->capacity) return;

    s->n = n;

    /* dz es el grosor de cada franja de area 4*pi/N.
     * El +1/2 del numerador centra la semilla en su franja (regla del punto
     * medio) en vez de pegarla al borde. Sin ese medio, las semillas de los
     * polos quedan corridas y la uniformidad empeora notablemente para N
     * pequeno. */
    const double dz = 2.0 / (double)n;

    for (int i = 0; i < n; i++) {
        /* --- altura: reparto de AREAS iguales (teorema de Arquimedes) ---- */
        double z = 1.0 - dz * ((double)i + 0.5);

        /* --- radio del paralelo a esa altura (Pitagoras sobre S^2) ------- */
        /* El fmax protege contra un z que por redondeo salga apenas fuera de
         * [-1,1]: sin el, sqrt de un negativo diminuto daria NaN y arruinaria
         * una semilla entera. */
        double rho2 = 1.0 - z * z;
        double rho  = rho2 > 0.0 ? sqrt(rho2) : 0.0;

        /* --- azimut: el angulo aureo acumulado ---------------------------
         * En double, no en float: n*psi con n en los miles pierde precision
         * rapido en float y el patron se degrada de forma visible.
         * El fmod mantiene el argumento chico para que sin/cos no pierdan
         * precision por reduccion de rango con n grande. */
        double theta = fmod((double)i * angle_rad, 2.0 * SS_PI);

        s->x[i] = (float)(rho * cos(theta));
        s->y[i] = (float)(rho * sin(theta));
        s->z[i] = (float)z;

        /* Estado de la fisica en reposo. Si --physics 0, nunca se toca. */
        s->vx[i] = s->vy[i] = s->vz[i] = 0.0f;
        s->ax[i] = s->ay[i] = s->az[i] = 0.0f;

        /* Color pseudoaleatorio. Funcion pura de (i, seed): sin estado, asi
         * que la version paralela produce exactamente los mismos colores. */
        s->color[i] = color_for_seed((uint32_t)i, seed);
    }
}

/* --------------------------------------------------------------------------
 *  Medicion del angulo de divergencia
 * -------------------------------------------------------------------------- */

double sphere_mean_divergence_deg(const SeedSet *s)
{
    if (s == NULL || s->n < 2) return 0.0;

    /* El angulo de divergencia es la diferencia de AZIMUT entre semillas
     * consecutivas, o sea el angulo entre sus proyecciones al plano XY.
     * Se promedia envolviendo a [0, 2pi) porque las diferencias crudas se
     * acumulan mas alla de una vuelta. */
    double sum   = 0.0;
    int    count = 0;

    for (int i = 0; i + 1 < s->n; i++) {
        double a0 = atan2((double)s->y[i],     (double)s->x[i]);
        double a1 = atan2((double)s->y[i + 1], (double)s->x[i + 1]);

        double d = a1 - a0;
        while (d < 0.0)          d += 2.0 * SS_PI;
        while (d >= 2.0 * SS_PI) d -= 2.0 * SS_PI;

        sum += d;
        count++;
    }

    if (count == 0) return 0.0;
    return (sum / (double)count) * 180.0 / SS_PI;
}
