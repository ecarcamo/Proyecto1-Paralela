/* ===========================================================================
 *  metrics.h - Metricas de calidad de la distribucion sobre la esfera.
 *
 *  Estas funciones no dibujan nada: SIRVEN PARA DEMOSTRAR que la matematica
 *  esta bien antes de que exista una ventana. Los resultados van al informe
 *  como anexo, y la mas valiosa es la del teorema de las tres distancias:
 *  es una verificacion experimental de un teorema, hecha por nosotros.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#ifndef METRICS_H
#define METRICS_H

#include "sphere.h"

/* ---------------------------------------------------------------------------
 *  Estadisticas de la distancia al vecino mas cercano.
 *
 *  Es la medida estandar de uniformidad de un conjunto de puntos. Lo que
 *  importa es el COEFICIENTE DE VARIACION (sd/media): si todas las semillas
 *  tienen vecinos a la misma distancia, la distribucion es uniforme y el
 *  coeficiente tiende a cero. Una red lat-lon, en cambio, apelotona puntos en
 *  los polos y da un coeficiente enorme.
 *
 *  Costo O(N^2): es exactamente el mismo patron todos-contra-todos que la
 *  fisica del screensaver, asi que de paso sirve como estimador barato del
 *  costo de ese kernel en esta maquina.
 * ------------------------------------------------------------------------- */
typedef struct {
    double min;
    double max;
    double mean;
    double sd;
    double cv;      /* coeficiente de variacion = sd/mean  <- el numero clave */
} NeighborStats;

NeighborStats metrics_nearest_neighbor(const SeedSet *s);

/* ---------------------------------------------------------------------------
 *  Teorema de las tres distancias (Steinhaus / Sos / Swierczkowski).
 *
 *  Enunciado: si se colocan n puntos en la circunferencia en los angulos
 *  {alpha, 2*alpha, ..., n*alpha} (mod 1) con alpha irracional, los huecos
 *  entre puntos consecutivos toman A LO MAS 3 longitudes distintas.
 *
 *  Este es el resultado que garantiza que el espaciado angular NUNCA degenera,
 *  para ningun n. Lo verificamos numericamente: se ordenan los azimuts, se
 *  miden los huecos y se cuentan cuantos valores distintos hay (con una
 *  tolerancia, porque son flotantes).
 *
 *  Devuelve el numero de longitudes de hueco distintas encontradas, y si
 *  gap_out no es NULL escribe ahi hasta 'max_gaps' de esas longitudes.
 *
 *  Resultado esperado con el angulo aureo: exactamente 3.
 *  Con un angulo racional p/q: exactamente 1 (todos los huecos iguales, porque
 *  los puntos se apilan en q meridianos perfectamente espaciados) -- que es
 *  justo la degeneracion que hace inservible al angulo racional.
 * ------------------------------------------------------------------------- */
int metrics_three_distance(const SeedSet *s, double tol,
                           double *gap_out, int max_gaps);

/* ---------------------------------------------------------------------------
 *  Cuenta cuantos meridianos distintos ocupan las semillas.
 *
 *  Con el angulo aureo el resultado es N (cada semilla en su propio azimut).
 *  Con un angulo racional p/q el resultado es q: la demostracion cuantitativa
 *  de por que un angulo racional es un desastre.
 * ------------------------------------------------------------------------- */
int metrics_count_meridians(const SeedSet *s, double tol);

/* ---------------------------------------------------------------------------
 *  Distribucion de referencia lat-lon, para comparar contra Fibonacci.
 *
 *  Coloca n puntos en una malla de latitud/longitud lo mas cuadrada posible.
 *  Es la forma "obvia" de repartir puntos sobre una esfera, y apelotona
 *  fuertemente en los polos. Sirve para que el informe pueda decir cuanto
 *  mejor es Fibonacci con un numero medido, y no solo con una imagen.
 * ------------------------------------------------------------------------- */
void metrics_fill_latlon(SeedSet *s, int n);

#endif /* METRICS_H */
