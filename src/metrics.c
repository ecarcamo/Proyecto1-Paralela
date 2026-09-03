/* metrics.c - Verificacion numerica de la calidad de la distribucion. */
#include "metrics.h"
#include "config.h"
#include "color.h"

#include <math.h>
#include <stdlib.h>

/* Distancia al vecino mas cercano, O(N^2). */
NeighborStats metrics_nearest_neighbor(const SeedSet *s)
{
    NeighborStats st = { 0.0, 0.0, 0.0, 0.0, 0.0 };
    if (s == NULL || s->n < 2) return st;

    const int n = s->n;

    double sum = 0.0, sum2 = 0.0;
    double gmin = 1e300, gmax = 0.0;

    for (int i = 0; i < n; i++) {
        /* Mayor producto punto == menor geodesica (acos es decreciente). */
        float xi = s->x[i], yi = s->y[i], zi = s->z[i];
        double best = -2.0;

        for (int j = 0; j < n; j++) {
            if (j == i) continue;
            double d = (double)xi * s->x[j] + (double)yi * s->y[j]
                     + (double)zi * s->z[j];
            if (d > best) best = d;
        }

        /* El acos se paga una sola vez por semilla, para dar radianes de arco. */
        if (best >  1.0) best =  1.0;      /* redondeo puede pasarse */
        if (best < -1.0) best = -1.0;
        double dist = acos(best);

        sum  += dist;
        sum2 += dist * dist;
        if (dist < gmin) gmin = dist;
        if (dist > gmax) gmax = dist;
    }

    double nn   = (double)n;
    st.mean     = sum / nn;
    double var  = sum2 / nn - st.mean * st.mean;
    st.sd       = var > 0.0 ? sqrt(var) : 0.0;
    st.min      = gmin;
    st.max      = gmax;
    st.cv       = st.mean > 0.0 ? st.sd / st.mean : 0.0;

    return st;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Teorema de las tres distancias: cuenta longitudes de hueco distintas. */
int metrics_three_distance(const SeedSet *s, double tol,
                           double *gap_out, int max_gaps)
{
    if (s == NULL || s->n < 3) return 0;

    const int n = s->n;

    double *ang = (double *)malloc((size_t)n * sizeof(double));
    if (ang == NULL) return -1;

    /* Azimut de cada semilla, envuelto a [0, 2pi). */
    for (int i = 0; i < n; i++) {
        double a = atan2((double)s->y[i], (double)s->x[i]);
        if (a < 0.0) a += 2.0 * SS_PI;
        ang[i] = a;
    }

    qsort(ang, (size_t)n, sizeof(double), cmp_double);

    /* Huecos entre azimuts consecutivos, incluyendo el que cierra el circulo. */
    double *gap = (double *)malloc((size_t)n * sizeof(double));
    if (gap == NULL) { free(ang); return -1; }

    for (int i = 0; i < n - 1; i++) gap[i] = ang[i + 1] - ang[i];
    gap[n - 1] = (2.0 * SS_PI - ang[n - 1]) + ang[0];

    qsort(gap, (size_t)n, sizeof(double), cmp_double);

    /* Ya ordenados: basta comparar con el ultimo representante aceptado. */
    int    distinct = 0;
    double last     = -1e300;

    for (int i = 0; i < n; i++) {
        if (gap[i] - last > tol) {
            if (gap_out != NULL && distinct < max_gaps) gap_out[distinct] = gap[i];
            distinct++;
            last = gap[i];
        }
    }

    free(gap);
    free(ang);
    return distinct;
}

/* Meridianos ocupados: N con el angulo aureo, q con un angulo racional p/q. */
int metrics_count_meridians(const SeedSet *s, double tol)
{
    if (s == NULL || s->n < 1) return 0;

    const int n = s->n;

    double *ang = (double *)malloc((size_t)n * sizeof(double));
    if (ang == NULL) return -1;

    for (int i = 0; i < n; i++) {
        double a = atan2((double)s->y[i], (double)s->x[i]);
        if (a < 0.0) a += 2.0 * SS_PI;
        ang[i] = a;
    }

    qsort(ang, (size_t)n, sizeof(double), cmp_double);

    int    distinct = 0;
    double last     = -1e300;

    for (int i = 0; i < n; i++) {
        if (ang[i] - last > tol) { distinct++; last = ang[i]; }
    }

    /* El primero y el ultimo pueden ser el mismo meridiano, visto desde los
     * dos lados del corte en 0. */
    if (distinct > 1 &&
        (2.0 * SS_PI - ang[n - 1]) + ang[0] < tol) distinct--;

    free(ang);
    return distinct;
}

/* Referencia lat-lon (malla n_lat ~ sqrt(n/2)): la forma "obvia", para comparar. */
void metrics_fill_latlon(SeedSet *s, int n)
{
    if (s == NULL || n < 1 || n > s->capacity) return;

    s->n = n;

    int n_lat = (int)(sqrt((double)n / 2.0) + 0.5);
    if (n_lat < 1) n_lat = 1;
    int n_lon = (n + n_lat - 1) / n_lat;

    int k = 0;
    for (int a = 0; a < n_lat && k < n; a++) {
        double phi = SS_PI * ((double)a + 0.5) / (double)n_lat;   /* colatitud */
        double sp  = sin(phi), cp = cos(phi);

        for (int b = 0; b < n_lon && k < n; b++) {
            double lam = 2.0 * SS_PI * (double)b / (double)n_lon;

            s->x[k] = (float)(sp * cos(lam));
            s->y[k] = (float)(sp * sin(lam));
            s->z[k] = (float)cp;
            s->vx[k] = s->vy[k] = s->vz[k] = 0.0f;
            s->ax[k] = s->ay[k] = s->az[k] = 0.0f;
            s->color[k] = color_for_seed((uint32_t)k, 1u);
            k++;
        }
    }

    s->n = k;
}
