/* ===========================================================================
 *  test_sphere.c - Arnes de verificacion del nucleo matematico.
 *
 *  Corre SIN SDL: verifica que la matematica de la esfera de Fibonacci esta
 *  bien antes de que exista una sola ventana. Eso permite que el resto del
 *  equipo construya el renderizado sobre una base ya probada, y produce los
 *  numeros que van al anexo del informe.
 *
 *      make test               (o)     ./bin/test_sphere [N]
 *
 *  Codigo de salida 0 si todo pasa, 1 si algo falla.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
/* clock_gettime es POSIX, no C11: sin este macro, -std=c11 estricto no la
 * declara y el compilador la asume implicita. Va ANTES de cualquier include. */
#define _POSIX_C_SOURCE 199309L

#include "sphere.h"
#include "metrics.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

/* ------------------------------------------------------------- utilidades -- */

static int g_fallos = 0;

static void check(const char *nombre, int ok, const char *detalle)
{
    printf("  [%s] %-52s %s\n", ok ? " OK " : "FALLA", nombre, detalle);
    if (!ok) g_fallos++;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ===========================================================================
 *  Test 1 - Las semillas caen EXACTAMENTE sobre la esfera unitaria.
 *
 *  Es la identidad rho^2 + z^2 = (1 - z^2) + z^2 = 1, que se cumple por
 *  construccion. Si esto falla, hay un error de tipeo en la formula.
 * =========================================================================== */
static void test_en_la_esfera(SeedSet *s, int n)
{
    sphere_fill_fibonacci(s, n, SS_GOLDEN_ANG, SS_DEF_SEED);

    double peor = 0.0;
    for (int i = 0; i < n; i++) {
        double L = sqrt((double)s->x[i] * s->x[i] +
                        (double)s->y[i] * s->y[i] +
                        (double)s->z[i] * s->z[i]);
        double e = fabs(L - 1.0);
        if (e > peor) peor = e;
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "error max %.2e (criterio 1e-6)", peor);
    check("Toda semilla cumple |p| = 1", peor < 1e-6, msg);
}

/* ===========================================================================
 *  Test 2 - Uniformidad: distancia al vecino mas cercano.
 *
 *  Si la distribucion es uniforme, todas las semillas tienen su vecino mas
 *  cercano a una distancia parecida y el coeficiente de variacion es chico.
 * =========================================================================== */
static void test_uniformidad(SeedSet *s, int n)
{
    sphere_fill_fibonacci(s, n, SS_GOLDEN_ANG, SS_DEF_SEED);
    NeighborStats st = metrics_nearest_neighbor(s);

    /* Referencia teorica: si N semillas se reparten areas iguales sobre una
     * esfera de area 4*pi, a cada una le toca 4*pi/N. Un empaquetamiento
     * hexagonal ideal daria una separacion de ~sqrt(8*pi/(N*sqrt(3))). */
    double esperado = sqrt(8.0 * SS_PI / ((double)n * sqrt(3.0)));

    char msg[160];
    snprintf(msg, sizeof(msg),
             "cv=%.4f  media=%.5f rad (ideal hex %.5f)", st.cv, st.mean, esperado);
    check("Uniformidad del vecino mas cercano (cv < 0.10)", st.cv < 0.10, msg);

    snprintf(msg, sizeof(msg), "media/ideal = %.3f", st.mean / esperado);
    check("La separacion media se acerca al empaquetamiento hexagonal",
          st.mean / esperado > 0.85 && st.mean / esperado < 1.15, msg);
}

/* ===========================================================================
 *  Test 3 - TEOREMA DE LAS TRES DISTANCIAS.
 *
 *  El test mas valioso del arnes: es la verificacion experimental de un
 *  teorema (Steinhaus / Sos / Swierczkowski), hecha por nosotros.
 *
 *  Con alpha irracional los huecos angulares toman a lo mas 3 longitudes
 *  distintas, para CUALQUIER n. Es lo que garantiza que el espaciado nunca
 *  degenera.
 * =========================================================================== */
static void test_tres_distancias(SeedSet *s)
{
    const int casos[] = { 100, 1000, 5000 };
    const double tol  = 1e-6;

    for (int c = 0; c < 3; c++) {
        int n = casos[c];
        sphere_fill_fibonacci(s, n, SS_GOLDEN_ANG, SS_DEF_SEED);

        double huecos[8];
        int    d = metrics_three_distance(s, tol, huecos, 8);

        char msg[192];
        int  off = snprintf(msg, sizeof(msg), "n=%-5d -> %d huecos:", n, d);
        for (int i = 0; i < d && i < 3 && off > 0 && off < (int)sizeof(msg); i++)
            off += snprintf(msg + off, sizeof(msg) - (size_t)off,
                            " %.6f", huecos[i]);

        check("Teorema de las tres distancias (a lo mas 3)", d <= 3 && d >= 1, msg);
    }
}

/* ===========================================================================
 *  Test 4 - Un angulo RACIONAL degenera, y se puede contar cuanto.
 *
 *  Si psi = (p/q)*2pi entonces theta_{n+q} = theta_n (mod 2pi): todas las
 *  semillas se apilan en exactamente q meridianos. Es la demostracion
 *  cuantitativa de por que hace falta un irracional.
 * =========================================================================== */
static void test_angulo_racional(SeedSet *s)
{
    struct { int p, q; } casos[] = { {1,3}, {3,8}, {5,13} };

    for (int c = 0; c < 3; c++) {
        int p = casos[c].p, q = casos[c].q;
        sphere_fill_fibonacci(s, 600, 2.0 * SS_PI * p / q, SS_DEF_SEED);

        int m = metrics_count_meridians(s, 1e-4);

        char msg[128];
        snprintf(msg, sizeof(msg), "psi = %d/%d de vuelta -> %d meridianos", p, q, m);
        check("Angulo racional colapsa en q meridianos", m == q, msg);
    }

    /* Y el contraste: el angulo aureo NO colapsa. */
    sphere_fill_fibonacci(s, 600, SS_GOLDEN_ANG, SS_DEF_SEED);
    int m = metrics_count_meridians(s, 1e-4);

    char msg[128];
    snprintf(msg, sizeof(msg), "angulo aureo -> %d meridianos de %d semillas", m, 600);
    check("El angulo aureo NO colapsa (un meridiano por semilla)", m == 600, msg);
}

/* ===========================================================================
 *  Test 5 - Fibonacci le gana a la malla lat-lon.
 *
 *  La forma "obvia" de repartir puntos sobre una esfera es una malla de
 *  latitud/longitud, que apelotona brutalmente en los polos. Medirlo da un
 *  numero concreto para el informe en vez de solo una imagen.
 * =========================================================================== */
static void test_contra_latlon(SeedSet *s, int n)
{
    sphere_fill_fibonacci(s, n, SS_GOLDEN_ANG, SS_DEF_SEED);
    NeighborStats fib = metrics_nearest_neighbor(s);

    metrics_fill_latlon(s, n);
    NeighborStats ll = metrics_nearest_neighbor(s);

    char msg[160];
    snprintf(msg, sizeof(msg), "cv Fibonacci=%.4f  vs  cv lat-lon=%.4f  (%.1fx mejor)",
             fib.cv, ll.cv, ll.cv / (fib.cv > 0 ? fib.cv : 1e-9));
    check("Fibonacci es mas uniforme que la malla lat-lon", fib.cv < ll.cv, msg);
}

/* ===========================================================================
 *  Test 6 - Estimacion del costo por frame Y del N critico de ESTA maquina.
 *
 *  No es un test que pase o falle: es la medicion que alimenta
 *  docs/02-PARAMETRO-N.md. Se cronometra el bucle "producto punto contra todas
 *  las semillas", que es exactamente el bucle interno del Voronoi, y de ahi
 *  sale el N donde el secuencial deberia caer debajo de 30 FPS.
 * =========================================================================== */
static void estimar_n_critico(SeedSet *s)
{
    const int n = 2000;
    sphere_fill_fibonacci(s, n, SS_GOLDEN_ANG, SS_DEF_SEED);

    /* Se simulan 20000 "pixeles": cada uno recorre las n semillas buscando el
     * producto punto maximo. Es el kernel del Voronoi sin el sombreado. */
    const int pixeles = 20000;
    volatile float sumidero = 0.0f;

    double t0 = now_s();
    for (int px = 0; px < pixeles; px++) {
        /* Una direccion cualquiera sobre la esfera, distinta en cada iteracion
         * para que el compilador no saque el bucle interno como invariante. */
        float qx = s->x[px % n], qy = s->y[px % n], qz = s->z[px % n];

        float b1 = -2.0f, b2 = -2.0f;
        for (int i = 0; i < n; i++) {
            float d = qx * s->x[i] + qy * s->y[i] + qz * s->z[i];
            if      (d > b1) { b2 = b1; b1 = d; }
            else if (d > b2) { b2 = d; }
        }
        sumidero += b1 - b2;
    }
    double t1 = now_s();
    (void)sumidero;

    double evaluaciones = (double)pixeles * (double)n;
    double tasa         = evaluaciones / (t1 - t0);       /* evaluaciones/s */

    /* Silueta de la esfera a 1280x720 ocupando el 84% de la altura. */
    const double W = 1280.0, H = 720.0;
    double radio_px = 0.42 * H;
    double P        = SS_PI * radio_px * radio_px;
    double a_fijo   = 0.0015;                             /* 1.5 ms estimados */

    double b        = P / tasa;                           /* s por semilla    */
    double n_crit   = (1.0 / SS_FPS_TARGET - a_fijo) / b;

    printf("\n  --- Estimacion para ESTA maquina ---------------------------\n");
    printf("    tasa de producto punto  : %.3e evaluaciones/s (1 hilo)\n", tasa);
    printf("    pixeles de silueta @%.0fx%.0f: %.0f  (%.1f%% de la pantalla)\n",
           W, H, P, 100.0 * P / (W * H));
    printf("    costo por semilla       : %.4f ms/frame\n", b * 1000.0);
    printf("    >> N critico secuencial : %.0f semillas para %.0f FPS\n",
           n_crit, SS_FPS_TARGET);
    printf("    (docs/02-PARAMETRO-N.md predice ~330; ver seccion 3)\n");
    printf("  ------------------------------------------------------------\n");
}

/* =========================================================================== */

int main(int argc, char **argv)
{
    int n = 2000;
    if (argc > 1) {
        char *fin = NULL;
        long v = strtol(argv[1], &fin, 10);
        if (fin == argv[1] || *fin != '\0' || v < 4 || v > 200000) {
            fprintf(stderr, "uso: %s [N]   (4 <= N <= 200000)\n", argv[0]);
            return 1;
        }
        n = (int)v;
    }

    printf("=============================================================\n");
    printf(" Verificacion del nucleo matematico - esfera de Fibonacci\n");
    printf(" N de trabajo = %d   |   angulo aureo = %.9f grados\n",
           n, SS_GOLDEN_ANG * 180.0 / SS_PI);
    printf("=============================================================\n\n");

    SeedSet s;
    int cap = n > 5000 ? n : 5000;
    if (seedset_alloc(&s, cap) != 0) {
        fprintf(stderr, "error: no se pudo reservar memoria para %d semillas\n", cap);
        return 1;
    }

    printf(" 1. Geometria\n");
    test_en_la_esfera(&s, n);

    printf("\n 2. Uniformidad de la distribucion\n");
    test_uniformidad(&s, n);

    printf("\n 3. Teoria de numeros\n");
    test_tres_distancias(&s);

    printf("\n 4. Degeneracion con angulo racional\n");
    test_angulo_racional(&s);

    printf("\n 5. Comparacion contra la malla lat-lon\n");
    test_contra_latlon(&s, n);

    printf("\n 6. Costo computacional\n");
    estimar_n_critico(&s);

    seedset_free(&s);

    printf("\n=============================================================\n");
    if (g_fallos == 0) printf(" TODOS LOS TESTS PASARON\n");
    else               printf(" %d TEST(S) FALLARON\n", g_fallos);
    printf("=============================================================\n");

    return g_fallos == 0 ? 0 : 1;
}
