/* metrics.h - Metricas de calidad de la distribucion sobre la esfera.
 * No dibujan nada: demuestran con numeros que la matematica esta bien. */
#ifndef METRICS_H
#define METRICS_H

#include "sphere.h"

/* Vecino mas cercano, O(N^2). El numero clave es el coeficiente de variacion:
 * tiende a 0 si es uniforme y es enorme en una malla lat-lon. */
typedef struct {
    double min;
    double max;
    double mean;
    double sd;
    double cv;      /* coeficiente de variacion = sd/mean */
} NeighborStats;

NeighborStats metrics_nearest_neighbor(const SeedSet *s);

/* Teorema de las tres distancias: con alpha irracional los huecos toman a lo
 * mas 3 longitudes. Devuelve cuantas hay: 3 con el aureo, 1 con un racional. */
int metrics_three_distance(const SeedSet *s, double tol,
                           double *gap_out, int max_gaps);

/* Meridianos ocupados: N con el angulo aureo, q con un angulo racional p/q. */
int metrics_count_meridians(const SeedSet *s, double tol);

/* Malla lat-lon de referencia: la forma "obvia", que apelotona en los polos. */
void metrics_fill_latlon(SeedSet *s, int n);

#endif /* METRICS_H */
