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
 * declara y el compilador la asume implicita. Va ANTES de cualquier include.
 * Se usa 200809L (POSIX.1-2008) y no 199309L: en macOS la libc oculta snprintf
 * por debajo de ese nivel de feature-test, y 200809L sigue proveyendo
 * clock_gettime/CLOCK_MONOTONIC. */
#define _POSIX_C_SOURCE 200809L

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
    printf("    (referencia medida en el equipo: ~130 @1280x720, ver doc 2 sec 3)\n");
    printf("    Ojo: este numero varia ~10%% entre corridas por turbo y por que\n");
    printf("    otros procesos comparten la cache. Por eso el enunciado pide 10\n");
    printf("    mediciones por punto: una sola corrida NO es un dato.\n");
    printf("  ------------------------------------------------------------\n");
}

/* ===========================================================================
 *  Test 7 - Modo canon, Plan 1: convergencia, pureza, dominio y conteo.
 *
 *  Verifica la propiedad central del modo --cannon: la posicion es una
 *  formula cerrada de t, sin ningun estado que integrar.
 *
 *  Se corre con K = 1 y radio de boca 0 (el canon unico en el centro) para
 *  medir exactamente lo que medía antes de que existieran los K canones.
 * =========================================================================== */

/* Parametros base de los tests del canon: un solo canon en el origen. Cada
 * test copia esto y cambia lo que le interesa, asi no hay diez literales
 * sueltos repartidos por el archivo.
 *
 * recirculate = 1 y NO el default del programa (0) a proposito: las secciones
 * 7 y 8 se escribieron para verificar la recirculacion, asi que la piden
 * explicitamente. El comportamiento por defecto -- aterrizar y quedarse -- lo
 * cubre la seccion 9, que apaga el flag. */
static CannonParams cp_base(int n, double R, double V, int L)
{
    CannonParams p;
    p.n             = n;
    p.angle_rad     = SS_GOLDEN_ANG;
    p.seed          = SS_DEF_SEED;
    p.fire_rate     = R;
    p.muzzle_speed  = V;
    p.trail         = L;
    p.cannons       = 1;
    p.layout        = SS_CANNON_ROUNDROBIN;
    p.muzzle_radius = 0.0;
    p.recirculate   = 1;
    return p;
}

static double radio_de(const SeedSet *s, int i)
{
    return sqrt((double)s->x[i]*s->x[i] + (double)s->y[i]*s->y[i] +
                (double)s->z[i]*s->z[i]);
}

static void test_cannon(void)
{
    /* V = 6 y no 1.5: con recirculacion, el vuelo tiene que ser CORTO frente
     * al ciclo. Con 1/V = T_ciclo (que es lo que daba V=1.5 para n=40, R=60)
     * ninguna bolita llega a quedarse quieta -- aterriza justo cuando le toca
     * volver a salir -- y "la esfera llena" no existe como instante.
     * Aca: T_ciclo = 0.667 s, vuelo = 0.167 s, 10 de las 40 en el aire. */
    const int n = 40;
    const double R = 60.0, V = 6.0;
    const int L = 6;

    CannonParams p = cp_base(n, R, V, L);
    int cap = sphere_cannon_capacity(&p);
    SeedSet s;
    if (seedset_alloc(&s, cap) != 0) {
        check("Modo canon: reserva de memoria", 0, "seedset_alloc fallo");
        return;
    }

    const double t_ciclo = (double)cannon_rounds(n, 1) / R;

    SeedSet ref;
    int ref_ok = (seedset_alloc(&ref, n) == 0);
    if (ref_ok) sphere_fill_fibonacci(&ref, n, SS_GOLDEN_ANG, SS_DEF_SEED);

    /* --- 7a. Convergencia, primera mitad: con el vuelo instantaneo, el
     *         conjunto COMPLETO es la esfera de Fibonacci de siempre.
     *
     * Es la version fuerte de la propiedad: si V es tan alto que ninguna
     * bolita esta en el aire en el instante muestreado, lo que queda tiene
     * que ser sphere_fill_fibonacci() exacto. El canon no invento una
     * geometria propia: construye la misma esfera analitica, a pedazos. */
    if (ref_ok) {
        CannonParams pf = cp_base(n, R, /*V*/6000.0, /*trail*/0);
        SeedSet sf;
        if (seedset_alloc(&sf, sphere_cannon_capacity(&pf)) == 0) {
            /* A mitad de camino entre dos disparos: nadie esta volando, porque
             * el vuelo dura 1/6000 s y el hueco entre disparos es 1/120 s. */
            sphere_fill_cannon(&sf, &pf, 3.0 * t_ciclo + 0.5 / R);

            double peor = 0.0;
            for (int i = 0; i < n && i < sf.n; i++) {
                double dx = (double)sf.x[i] - ref.x[i];
                double dy = (double)sf.y[i] - ref.y[i];
                double dz = (double)sf.z[i] - ref.z[i];
                double e = sqrt(dx*dx + dy*dy + dz*dz);
                if (e > peor) peor = e;
            }
            char msg[128];
            snprintf(msg, sizeof msg, "s->n=%d (esperado %d), error max %.2e",
                     sf.n, n, peor);
            check("Convergencia: con vuelo instantaneo == sphere_fill_fibonacci",
                  sf.n == n && peor == 0.0, msg);
            seedset_free(&sf);
        }
    }

    /* --- 7a. Segunda mitad: con V normal ya no existe el instante en que
     *         TODAS estan quietas (siempre hay K*R/V en el aire: eso es
     *         justamente la animacion infinita). La propiedad que sobrevive,
     *         y que es la que importa, es que cada bolita ATERRIZADA esta
     *         exactamente en su posicion de Fibonacci.
     *
     * Con trail = 0 el slot i del SoA es el indice i, asi que la comparacion
     * es directa. Se muestrea en regimen permanente, con todos los indices ya
     * disparados al menos una vez. */
    if (ref_ok) {
        CannonParams p0 = p;
        p0.trail = 0;
        SeedSet s0;
        if (seedset_alloc(&s0, sphere_cannon_capacity(&p0)) == 0) {
            int aterrizadas = 0, exactas = 1;
            double peor = 0.0;
            for (int k = 0; k < 20; k++) {
                sphere_fill_cannon(&s0, &p0, 5.0 * t_ciclo + (double)k * t_ciclo / 20.0);
                if (s0.n != n) { exactas = 0; break; }
                for (int i = 0; i < n; i++) {
                    if (radio_de(&s0, i) < 1.0 - 1e-6) continue;   /* en vuelo */
                    aterrizadas++;
                    double e = fabs((double)s0.x[i] - ref.x[i])
                             + fabs((double)s0.y[i] - ref.y[i])
                             + fabs((double)s0.z[i] - ref.z[i]);
                    if (e > peor) peor = e;
                    if (e != 0.0) exactas = 0;
                }
            }
            char msg[128];
            snprintf(msg, sizeof msg, "%d aterrizajes verificados, error max %.2e",
                     aterrizadas, peor);
            check("Convergencia: toda bolita aterrizada esta en su lugar de Fibonacci",
                  exactas && aterrizadas > 0, msg);
            seedset_free(&s0);
        }
    }
    if (ref_ok) seedset_free(&ref);

    /* --- 7b. Pureza: mismo t, dos veces, resultado bit a bit identico ---- */
    SeedSet a, b;
    int igual = 0;
    if (seedset_alloc(&a, cap) == 0 && seedset_alloc(&b, cap) == 0) {
        sphere_fill_cannon(&a, &p, 1.2345);
        sphere_fill_cannon(&b, &p, 1.2345);
        igual = (a.n == b.n);
        for (int i = 0; igual && i < a.n; i++) {
            if (a.x[i] != b.x[i] || a.y[i] != b.y[i] || a.z[i] != b.z[i] ||
                a.color[i] != b.color[i])
                igual = 0;
        }
        char msg[64];
        snprintf(msg, sizeof msg, "a.n=%d b.n=%d", a.n, b.n);
        check("Pureza: mismo t produce el mismo resultado bit a bit", igual, msg);
        seedset_free(&a);
        seedset_free(&b);
    }

    /* --- 7c. Dominio: el radio de la primera semilla es monotono y en [0,1].
     *         Se mide durante su PRIMER vuelo, antes de que recircule. */
    double r_prev = -1.0;
    int monotono = 1, en_rango = 1;
    for (double tt = 0.0; tt <= 1.0 / V + 0.02; tt += 0.005) {
        sphere_fill_cannon(&s, &p, tt);
        if (s.n < 1) continue;                     /* semilla 0 aun no disparo */
        double r = radio_de(&s, 0);
        if (r < -1e-4 || r > 1.0 + 1e-4) en_rango = 0;
        if (r < r_prev - 1e-4) monotono = 0;        /* tolerancia al ruido de float */
        r_prev = r;
    }
    check("Dominio: el radio de la semilla 0 es monotono creciente en [0,1]",
          monotono && en_rango, monotono ? "en rango" : "salio de [0,1] o no es monotono");

    /* --- 7d. Conteo: vivas(t) arranca en 1, satura en n, nunca pasa capacity.
     *
     * Con trail = 0 los slots son exactamente las bolitas vivas, asi que
     * s->n ES vivas(t) y la saturacion se lee directo. */
    CannonParams pc = p;
    pc.trail = 0;
    int cap_c = sphere_cannon_capacity(&pc);
    SeedSet sc;
    if (seedset_alloc(&sc, cap_c) == 0) {
        sphere_fill_cannon(&sc, &pc, 0.0);
        int arranca_en_1 = (sc.n == 1);

        int nunca_excede = 1, monotono_vivas = 1, prev = 0;
        for (double tt = 0.0; tt < t_ciclo; tt += 0.002) {
            sphere_fill_cannon(&sc, &pc, tt);
            if (sc.n > cap_c) nunca_excede = 0;
            if (sc.n < prev) monotono_vivas = 0;    /* vivas() no decrece nunca */
            prev = sc.n;
        }
        sphere_fill_cannon(&sc, &pc, 5.0 * t_ciclo);
        int satura_en_n = (sc.n == n);

        char msg[112];
        snprintf(msg, sizeof msg, "arranca=%d  satura_en_n=%d  cap=%d",
                 arranca_en_1, satura_en_n, cap_c);
        check("Conteo: vivas(t) arranca en 1, crece hasta n y ahi se queda",
              arranca_en_1 && satura_en_n && nunca_excede && monotono_vivas, msg);
        seedset_free(&sc);
    }

    seedset_free(&s);
}

/* ===========================================================================
 *  Test 8 - Modo canon, Plan 2: K canones y animacion infinita.
 *
 *  Lo que hay que demostrar aca es distinto de lo del Plan 1. Alla la
 *  pregunta era "la geometria es la correcta"; aca es:
 *
 *    (a) generalizar a K no rompio el caso K = 1,
 *    (b) el reparto de indices entre canones es una biyeccion,
 *    (c) la animacion NO se termina: a t arbitrariamente grande siguen
 *        saliendo bolitas y la carga se queda plana,
 *    (d) las K bocas estan donde deben y son distinguibles,
 *    (e) la recirculacion no deja estelas colgando del ciclo anterior.
 * =========================================================================== */

/* --- 8a. No-regresion: con K = 1 y boca en el origen, el resultado tiene que
 *         ser el del Plan 1, bit a bit.
 *
 * La referencia NO es una llamada a otra funcion nuestra (eso seria comparar
 * el codigo contra si mismo): se recalcula aca la formula cerrada del Plan 1
 * a mano. Se usa trail = 0 para que el slot k del SoA sea exactamente el
 * indice k y la comparacion sea directa. */
static void test_k1_no_regresion(void)
{
    const int n = 64;
    const double R = 60.0, V = 3.0;

    CannonParams p = cp_base(n, R, V, /*trail*/0);
    SeedSet s;
    if (seedset_alloc(&s, sphere_cannon_capacity(&p)) != 0) {
        check("K=1: reserva de memoria", 0, "seedset_alloc fallo");
        return;
    }

    /* Dentro del primer ciclo: ahi el Plan 1 y el Plan 2 tienen que coincidir
     * exactamente (el fmod todavia no envolvio a nadie). */
    int identico = 1, revisados = 0;
    double peor = 0.0;

    for (double t = 0.0; t < (double)n / R; t += 0.017) {
        sphere_fill_cannon(&s, &p, t);

        int esperadas = 0;
        for (int i = 0; i < n; i++) {
            /* ---- formula del Plan 1, escrita de nuevo ---- */
            double edad = t - (double)i / R;
            if (edad < 0.0) continue;
            double radio = V * edad;
            if (radio > 1.0) radio = 1.0;
            Vec3 d = sphere_dir_fib(i, n, SS_GOLDEN_ANG);
            float ex = (float)(radio >= 1.0 ? d.x : d.x * (float)radio);
            float ey = (float)(radio >= 1.0 ? d.y : d.y * (float)radio);
            float ez = (float)(radio >= 1.0 ? d.z : d.z * (float)radio);

            if (esperadas >= s.n) { identico = 0; break; }
            double dd = fabs((double)s.x[esperadas] - ex)
                      + fabs((double)s.y[esperadas] - ey)
                      + fabs((double)s.z[esperadas] - ez);
            if (dd > peor) peor = dd;
            if (dd != 0.0) identico = 0;
            esperadas++;
        }
        if (esperadas != s.n) identico = 0;
        revisados++;
    }

    char msg[128];
    snprintf(msg, sizeof msg, "%d instantes comparados, desviacion max %.2e",
             revisados, peor);
    check("No-regresion: K=1 reproduce el Plan 1 bit a bit", identico, msg);
    seedset_free(&s);
}

/* --- 8b. Cobertura: la union de los K canones cubre los N indices
 *         EXACTAMENTE una vez, sin huecos ni repetidos, en los dos layouts.
 *
 * Es la propiedad que hace que la esfera siga siendo la esfera de Fibonacci
 * completa y no un patron con agujeros. Ademas se verifica que la ronda cae
 * en [0, rondas): de eso depende que los K canones compartan periodo, que es
 * lo que mantiene la recirculacion sincronizada. */
static void test_cobertura(int layout, const char *nombre)
{
    const int n = 97, K = 7;                 /* K no divide a n: el caso feo */
    const int rondas = cannon_rounds(n, K);

    char *visto = (char *)calloc((size_t)K * (size_t)rondas, 1);
    int  *por_canon = (int *)calloc((size_t)K, sizeof(int));
    if (visto == NULL || por_canon == NULL) {
        check("Cobertura: reserva de memoria", 0, "calloc fallo");
        free(visto); free(por_canon);
        return;
    }

    int ok = 1;
    for (int i = 0; i < n; i++) {
        int c, r;
        cannon_slot(i, n, K, layout, &c, &r);
        if (c < 0 || c >= K || r < 0 || r >= rondas) { ok = 0; break; }
        if (visto[(size_t)c * rondas + r]) { ok = 0; break; }  /* repetido */
        visto[(size_t)c * rondas + r] = 1;
        por_canon[c]++;
    }

    /* Ningun canon puede quedar vacio ni llevarse mas de una ronda de
     * diferencia respecto de otro: si eso pasara, unos canones terminarian su
     * ciclo antes que otros y la esfera latiria en vez de mantenerse llena. */
    int menor = n + 1, mayor = -1;
    for (int c = 0; c < K; c++) {
        if (por_canon[c] < menor) menor = por_canon[c];
        if (por_canon[c] > mayor) mayor = por_canon[c];
    }
    if (menor < 1 || mayor - menor > 1) ok = 0;

    char msg[128];
    snprintf(msg, sizeof msg,
             "n=%d K=%d rondas=%d -> por canon entre %d y %d", n, K, rondas, menor, mayor);
    char titulo[128];
    snprintf(titulo, sizeof titulo, "Cobertura: %s reparte los N indices una sola vez", nombre);
    check(titulo, ok, msg);

    free(visto);
    free(por_canon);
}

/* --- 8c. Animacion infinita: el punto entero del Plan 2.
 *
 * Se evalua a cientos de ciclos de distancia y se exige que (i) siga habiendo
 * bolitas en vuelo -- o sea que los canones no pararon -- y (ii) la poblacion
 * dibujada sea la misma que un ciclo antes, que es lo que permite que el
 * benchmark mida algo estable y que los FPS se queden planos. */
static void test_animacion_infinita(void)
{
    const int n = 200, K = 5, L = 4;
    const double R = 60.0, V = 6.0;

    CannonParams p = cp_base(n, R, V, L);
    p.cannons = K;
    p.muzzle_radius = SS_DEF_MUZZLE_RADIUS;

    int cap = sphere_cannon_capacity(&p);
    SeedSet s;
    if (seedset_alloc(&s, cap) != 0) {
        check("Animacion infinita: reserva de memoria", 0, "seedset_alloc fallo");
        return;
    }

    const double t_ciclo = (double)cannon_rounds(n, K) / R;

    /* Muestreo lejano: 300 ciclos adentro, y 800 ciclos adentro. */
    int    n_lejos = 0, n_mas_lejos = 0;
    int    en_vuelo_lejos = 0;
    int    carga_plana = 1, siempre_en_vuelo = 1, nunca_excede = 1;
    int    dibujadas_min = cap + 1, dibujadas_max = -1;

    for (int k = 0; k < 40; k++) {
        double t = 300.0 * t_ciclo + (double)k * t_ciclo / 40.0;
        sphere_fill_cannon(&s, &p, t);

        if (s.n > cap) nunca_excede = 0;
        if (s.n < dibujadas_min) dibujadas_min = s.n;
        if (s.n > dibujadas_max) dibujadas_max = s.n;

        int vuelan = 0;
        for (int i = 0; i < s.n; i++)
            if (radio_de(&s, i) < 0.99) vuelan++;
        if (vuelan == 0) siempre_en_vuelo = 0;

        if (k == 0) { n_lejos = s.n; en_vuelo_lejos = vuelan; }
    }

    sphere_fill_cannon(&s, &p, 800.0 * t_ciclo);
    n_mas_lejos = s.n;

    /* "Plana" con tolerancia: la poblacion oscila unas pocas unidades porque
     * los disparos son discretos y el instante muestreado cae en cualquier
     * lado del intervalo 1/R. Lo que NO puede pasar es que decaiga a n. */
    if (dibujadas_max - dibujadas_min > 2 * K * (L + 1)) carga_plana = 0;
    if (dibujadas_min <= n) carga_plana = 0;

    char msg[160];
    snprintf(msg, sizeof msg,
             "t=300 ciclos: %d dibujadas (%d en vuelo)  |  t=800 ciclos: %d  |  cap=%d",
             n_lejos, en_vuelo_lejos, n_mas_lejos, cap);
    check("Recirculacion: a 300 y 800 ciclos la animacion sigue y la carga es plana",
          siempre_en_vuelo && carga_plana && nunca_excede && n_mas_lejos > n, msg);

    /* Modelo de carga del informe:
     *
     *      dibujadas = n + (K*R/V) * (L/2)
     *
     * Ojo con dos correcciones respecto de la version ingenua del plan:
     *
     *  1) Las bolitas en vuelo NO se suman aparte de n. Con recirculacion
     *     cada indice esta o aterrizado o volando, nunca las dos cosas: los
     *     reales son siempre n. Lo unico que se agrega son los fantasmas.
     *  2) Los fantasmas por bolita en vuelo promedian L/2, no L. Una bolita
     *     recien salida todavia no tiene cola (los fantasmas no cruzan hacia
     *     atras de su propio disparo), y como delta = vuelo/(L+1) la cantidad
     *     de fantasmas crece linealmente con la fase: promedia la mitad.
     *
     * Si esto se rompe, el grafico "costo por frame vs bolitas dibujadas" del
     * informe deja de tener sentido, asi que se exige menos de 10% de error. */
    double pred = (double)n + ((double)K * R / V) * ((double)L / 2.0);
    double err  = fabs(pred - (double)n_lejos) / pred;
    snprintf(msg, sizeof msg, "predicho %.0f, medido %d (error %.1f%%)",
             pred, n_lejos, err * 100.0);
    check("Modelo de carga: dibujadas ~ n + (K*R/V)*(L/2)", err < 0.10, msg);

    seedset_free(&s);
}

/* --- 8d. Las bocas: K puntos distintos sobre la esfera de radio r0. */
static void test_bocas(void)
{
    const int n = 120, K = 6;
    CannonParams p = cp_base(n, 60.0, 6.0, 0);
    p.cannons = K;
    p.muzzle_radius = 0.25;

    int en_la_esfera = 1, distintas = 1;
    double peor = 0.0;
    for (int c = 0; c < K; c++) {
        Vec3 b = cannon_muzzle(c, &p);
        double r = sqrt((double)b.x*b.x + (double)b.y*b.y + (double)b.z*b.z);
        double e = fabs(r - p.muzzle_radius);
        if (e > peor) peor = e;
        if (e > 1e-6) en_la_esfera = 0;

        for (int d = 0; d < c; d++) {
            Vec3 o = cannon_muzzle(d, &p);
            if (v3_len2(v3_sub(b, o)) < 1e-8) distintas = 0;
        }
    }

    char msg[128];
    snprintf(msg, sizeof msg, "K=%d bocas a r0=%.2f, error max %.2e", K, p.muzzle_radius, peor);
    check("Bocas: las K estan sobre la esfera de radio r0 y son distintas",
          en_la_esfera && distintas, msg);

    /* Con r0 = 0 todas colapsan al origen: es el canon unico del Plan 1, y es
     * lo que hace que el test 8a pueda comparar bit a bit. */
    p.muzzle_radius = 0.0;
    int todas_al_origen = 1;
    for (int c = 0; c < K; c++)
        if (v3_len2(cannon_muzzle(c, &p)) != 0.0f) todas_al_origen = 0;
    check("Bocas: con r0 = 0 todas colapsan al origen", todas_al_origen,
          "el canon unico del Plan 1 es el caso r0 = 0");
}

/* --- 8e. La estela no cruza hacia atras del disparo que la genero.
 *
 * Es el unico bug que introduce la recirculacion: una bolita que acaba de
 * volver a salir de la boca tiene fase ~0, y si los fantasmas se calcularan
 * con un fmod sobre (fase - j*delta) apareceria una cola pegada a la
 * superficie -- restos del ciclo anterior.
 *
 * Se detecta sin mirar el codigo: con r0 = 0 todo lo que pertenece al indice
 * 0 (su bolita y sus fantasmas) es paralelo a dir_fib(0). Se cuenta cuantos
 * puntos escritos son paralelos a esa direccion y se exige que ninguno este
 * mas lejos del origen que la bolita real. Con el bug, habria un fantasma a
 * radio ~1 mientras la bolita esta a radio ~0. */
static void test_estela_no_recircula(void)
{
    const int n = 40, L = 6;
    const double R = 60.0, V = 6.0;

    CannonParams p = cp_base(n, R, V, L);      /* K=1, r0=0 */
    SeedSet s;
    if (seedset_alloc(&s, sphere_cannon_capacity(&p)) != 0) {
        check("Estela: reserva de memoria", 0, "seedset_alloc fallo");
        return;
    }

    const double t_ciclo = (double)cannon_rounds(n, 1) / R;
    Vec3 d0 = sphere_dir_fib(0, n, SS_GOLDEN_ANG);

    int  ok = 1;
    double peor_exceso = 0.0;

    /* Justo despues de que el indice 0 recircula, y un poco mas adelante ya
     * con la cola desplegada. */
    const double offs[] = { 1e-3, 0.01, 0.05, 0.10 };
    for (size_t k = 0; k < sizeof offs / sizeof offs[0]; k++) {
        double t = 10.0 * t_ciclo + offs[k];
        sphere_fill_cannon(&s, &p, t);

        double r_real = -1.0;
        for (int i = 0; i < s.n; i++) {
            Vec3 q = v3(s.x[i], s.y[i], s.z[i]);
            double r = radio_de(&s, i);
            if (r < 1e-9) continue;                     /* en la boca exacta */
            /* paralelo a d0? el coseno del angulo tiene que ser ~1 */
            double cos_ang = (double)v3_dot(q, d0) / r;
            if (cos_ang < 1.0 - 1e-5) continue;         /* es de otro indice */

            if (r_real < 0.0) r_real = r;               /* la real va primero */
            else if (r > r_real + 1e-5) {               /* fantasma por DELANTE */
                double exceso = r - r_real;
                if (exceso > peor_exceso) peor_exceso = exceso;
                ok = 0;
            }
        }
        /* La bolita real del indice 0 tiene que estar en pleno vuelo, no
         * aterrizada: si no, el instante elegido no prueba nada. */
        if (!(r_real >= 0.0 && r_real < 1.0)) ok = 0;
    }

    char msg[128];
    snprintf(msg, sizeof msg, "exceso maximo de un fantasma por delante: %.2e", peor_exceso);
    check("Estela: los fantasmas nunca cruzan hacia atras del disparo", ok, msg);
    seedset_free(&s);
}

/* --- 8f. Capacidad: con K grande y estela larga, s->n nunca pasa capacity. */
static void test_capacidad_k(void)
{
    const int n = 300, K = 12, L = 8;
    const double R = 45.0, V = 5.0;

    CannonParams p = cp_base(n, R, V, L);
    p.cannons = K;
    p.muzzle_radius = SS_DEF_MUZZLE_RADIUS;

    int cap = sphere_cannon_capacity(&p);
    SeedSet s;
    if (seedset_alloc(&s, cap) != 0) {
        check("Capacidad K: reserva de memoria", 0, "seedset_alloc fallo");
        return;
    }

    const double t_ciclo = (double)cannon_rounds(n, K) / R;
    int ok = 1, pico = 0;
    /* Tres ciclos completos, con paso fino: si la capacidad estuviera
     * subestimada, el bucle de escritura truncaria y se veria como bolitas
     * que desaparecen. */
    for (double t = 0.0; t < 3.0 * t_ciclo; t += t_ciclo / 500.0) {
        sphere_fill_cannon(&s, &p, t);
        if (s.n > pico) pico = s.n;
        if (s.n > cap) ok = 0;
    }
    /* La reserva es deliberadamente la cota del PEOR caso -- todas las
     * bolitas en vuelo con la cola completa de L fantasmas -- mientras que el
     * pico real ronda la mitad, porque en cualquier instante solo la fraccion
     * mas vieja de las que vuelan alcanzo a desplegar los L fantasmas. Esa
     * holgura es a proposito: seedset_alloc() hace UN solo malloc y no hay
     * camino de crecimiento, asi que quedarse corto no significa "usar mas
     * memoria de la ideal" sino truncar la escritura y que se vean bolitas
     * desaparecer. Lo que si se exige es que la holgura no sea un cheque en
     * blanco: no mas del triple del pico observado. */
    int razonable = (cap <= 3 * pico);

    char msg[144];
    snprintf(msg, sizeof msg, "K=%d L=%d -> pico %d de cap %d (%.0f%%, cota del peor caso)",
             K, L, pico, cap, 100.0 * pico / cap);
    check("Capacidad: el pico cabe y la reserva es una cota, no un cheque en blanco",
          ok && razonable, msg);

    seedset_free(&s);
}

/* ===========================================================================
 *  9. Modo canon por defecto: --recirculate 0, la esfera se COMPLETA.
 *
 *  Esta es la semantica que ve el usuario si no pide nada: N es el tope, los
 *  canones disparan los N indices una sola vez y cada bolita se queda donde
 *  aterrizo. Lo que se verifica aca es justo lo que la recirculacion rompia:
 *  que pasado el tiempo de llenado la esfera este COMPLETA y siga completa.
 * =========================================================================== */

/* --- 9a. La esfera se completa y se queda completa. ---------------------- */
static void test_sin_recirculacion_completa(void)
{
    const int n = 240, K = 6;
    const double R = 60.0, V = 4.0;

    CannonParams p = cp_base(n, R, V, /*trail*/0);
    p.cannons     = K;
    p.recirculate = 0;

    SeedSet s;
    if (seedset_alloc(&s, sphere_cannon_capacity(&p)) != 0) {
        check("Sin recirculacion: reserva de memoria", 0, "seedset_alloc fallo");
        return;
    }

    /* Llenado = ultima ronda + el vuelo de esa ultima bolita. */
    const double t_lleno = (double)cannon_rounds(n, K) / R + 1.0 / V;

    /* Mucho despues del llenado -- incluidos instantes que con recirculacion
     * caian en pleno rebote -- tienen que estar las n, todas sobre la
     * superficie. */
    int completa = 1, todas_en_superficie = 1;
    double peor_radio = 0.0;
    const double ts[] = { 1.01, 2.0, 5.0, 50.0, 500.0 };

    for (unsigned k = 0; k < sizeof ts / sizeof *ts; k++) {
        sphere_fill_cannon(&s, &p, t_lleno * ts[k]);
        if (s.n != n) completa = 0;
        for (int i = 0; i < s.n; i++) {
            double d = fabs(radio_de(&s, i) - 1.0);
            if (d > peor_radio) peor_radio = d;
            if (d > 1e-5) todas_en_superficie = 0;
        }
    }

    char msg[160];
    snprintf(msg, sizeof msg,
             "n=%d K=%d: a 1.01x, 2x, 5x, 50x y 500x el llenado (%.2fs) hay %d/%d, "
             "error de radio max %.2e", n, K, t_lleno, s.n, n, peor_radio);
    check("Completitud: sin recirculacion la esfera llega a n y se queda ahi",
          completa && todas_en_superficie, msg);

    seedset_free(&s);
}

/* --- 9b. El conteo de aterrizadas nunca retrocede. -----------------------
 * Con recirculacion esta propiedad NO se cumple: cada T_ciclo el slot vuelve
 * a la boca y la cuenta de aterrizadas baja. Es exactamente el sintoma de que
 * la esfera se ve agujereada, escrito como aserto. */
static void test_sin_recirculacion_monotona(void)
{
    const int n = 180, K = 4;
    const double R = 60.0, V = 3.0;

    CannonParams p = cp_base(n, R, V, /*trail*/0);
    p.cannons     = K;
    p.recirculate = 0;

    SeedSet s;
    if (seedset_alloc(&s, sphere_cannon_capacity(&p)) != 0) {
        check("Monotonia: reserva de memoria", 0, "seedset_alloc fallo");
        return;
    }

    const double t_lleno = (double)cannon_rounds(n, K) / R + 1.0 / V;
    int monotona = 1, prev = 0, pico = 0;

    for (double t = 0.0; t < 4.0 * t_lleno; t += t_lleno / 400.0) {
        sphere_fill_cannon(&s, &p, t);
        int aterrizadas = 0;
        for (int i = 0; i < s.n; i++)
            if (radio_de(&s, i) > 1.0 - 1e-6) aterrizadas++;
        if (aterrizadas < prev) monotona = 0;
        prev = aterrizadas;
        if (aterrizadas > pico) pico = aterrizadas;
    }

    char msg[144];
    snprintf(msg, sizeof msg,
             "n=%d K=%d: aterrizadas nunca decrece y termina en %d (pico %d)",
             n, K, prev, pico);
    check("Monotonia: una bolita aterrizada no vuelve a salir nunca",
          monotona && prev == n, msg);

    seedset_free(&s);
}

/* --- 9c. Antes del primer T_ciclo, recirculate 0 y 1 son identicos. ------
 * La recirculacion solo puede actuar cuando el fmod envuelve, o sea a partir
 * de T_ciclo. Antes de eso las dos ramas tienen que dar el MISMO framebuffer
 * bit a bit: es lo que garantiza que apagar el flag no cambio la construccion
 * de la esfera, solo lo que pasa despues. */
static void test_recirculacion_no_altera_el_llenado(void)
{
    const int n = 120, K = 5, L = 4;
    const double R = 60.0, V = 5.0;

    CannonParams p0 = cp_base(n, R, V, L);
    p0.cannons = K;
    p0.muzzle_radius = SS_DEF_MUZZLE_RADIUS;
    p0.recirculate = 0;

    CannonParams p1 = p0;
    p1.recirculate = 1;

    SeedSet a, b;
    if (seedset_alloc(&a, sphere_cannon_capacity(&p0)) != 0 ||
        seedset_alloc(&b, sphere_cannon_capacity(&p1)) != 0) {
        check("Llenado identico: reserva de memoria", 0, "seedset_alloc fallo");
        return;
    }

    const double t_ciclo = (double)cannon_rounds(n, K) / R;
    int identico = 1, instantes = 0;
    double peor = 0.0;

    for (double t = 0.0; t < t_ciclo; t += t_ciclo / 200.0) {
        sphere_fill_cannon(&a, &p0, t);
        sphere_fill_cannon(&b, &p1, t);
        instantes++;
        if (a.n != b.n) { identico = 0; break; }
        for (int i = 0; i < a.n; i++) {
            double d = fabs((double)a.x[i] - b.x[i])
                     + fabs((double)a.y[i] - b.y[i])
                     + fabs((double)a.z[i] - b.z[i]);
            if (d > peor) peor = d;
            if (d != 0.0) identico = 0;
        }
    }

    char msg[144];
    snprintf(msg, sizeof msg,
             "%d instantes en [0, T_ciclo), desviacion max %.2e", instantes, peor);
    check("Llenado: apagar la recirculacion no cambia el primer ciclo bit a bit",
          identico, msg);

    seedset_free(&a);
    seedset_free(&b);
}

/* --- 9d. Con r0 = 0 toda bolita en vuelo sale del centro hacia afuera. ---
 * O sea: su posicion es COLINEAL con su destino de Fibonacci y apunta en el
 * mismo sentido. Es la propiedad que se pide de verdad -- "las bolitas salen
 * del centro y forman la esfera" -- escrita como aserto y no como una captura
 * de pantalla. */
static void test_vuelo_radial_desde_el_centro(void)
{
    const int n = 150, K = 3;
    const double R = 40.0, V = 2.0;

    CannonParams p = cp_base(n, R, V, /*trail*/0);
    p.cannons       = K;
    p.muzzle_radius = 0.0;             /* todas las bocas en el origen */
    p.recirculate   = 0;

    SeedSet s;
    if (seedset_alloc(&s, sphere_cannon_capacity(&p)) != 0) {
        check("Vuelo radial: reserva de memoria", 0, "seedset_alloc fallo");
        return;
    }

    const double t_lleno = (double)cannon_rounds(n, K) / R + 1.0 / V;
    int radial = 1, vistas_en_vuelo = 0;
    double peor_cruz = 0.0, peor_radio = 0.0;

    for (double t = 0.0; t < t_lleno; t += t_lleno / 300.0) {
        sphere_fill_cannon(&s, &p, t);

        /* Cada slot escrito tiene que caer sobre el rayo origen->destino de
         * ALGUN indice. Se busca el destino mas alineado y se exige que el
         * producto cruz sea nulo: eso es "va derecho hacia afuera". */
        for (int i = 0; i < s.n; i++) {
            double r = radio_de(&s, i);
            if (r < 1e-9) continue;                 /* recien salida del centro */
            if (r > 1.0 - 1e-6) continue;           /* ya aterrizo */
            vistas_en_vuelo++;

            double mejor = 1e9;
            for (int j = 0; j < n; j++) {
                Vec3 d = sphere_dir_fib(j, n, SS_GOLDEN_ANG);
                /* |p x d| / |p| : distancia angular al rayo del destino j */
                double cx = (double)s.y[i]*d.z - (double)s.z[i]*d.y;
                double cy = (double)s.z[i]*d.x - (double)s.x[i]*d.z;
                double cz = (double)s.x[i]*d.y - (double)s.y[i]*d.x;
                double cruz = sqrt(cx*cx + cy*cy + cz*cz) / r;
                if (cruz < mejor) mejor = cruz;
            }
            if (mejor > peor_cruz) peor_cruz = mejor;
            if (mejor > 1e-5) radial = 0;
            if (r > peor_radio) peor_radio = r;
        }
    }

    char msg[176];
    snprintf(msg, sizeof msg,
             "%d muestras en vuelo, desalineacion max %.2e, radio max en vuelo %.4f",
             vistas_en_vuelo, peor_cruz, peor_radio);
    check("Vuelo: con r0 = 0 toda bolita viaja del centro hacia su lugar",
          radial && vistas_en_vuelo > 0, msg);

    seedset_free(&s);
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

    printf("\n 7. Modo canon (Plan 1: formula cerrada)\n");
    test_cannon();

    printf("\n 8. Modo canon (Plan 2: K canones, recirculacion opcional)\n");
    test_k1_no_regresion();
    test_cobertura(SS_CANNON_ROUNDROBIN, "round-robin");
    test_cobertura(SS_CANNON_BLOCKS,     "bloques contiguos");
    test_animacion_infinita();
    test_bocas();
    test_estela_no_recircula();
    test_capacidad_k();

    printf("\n 9. Modo canon por defecto (--recirculate 0: la esfera se completa)\n");
    test_sin_recirculacion_completa();
    test_sin_recirculacion_monotona();
    test_recirculacion_no_altera_el_llenado();
    test_vuelo_radial_desde_el_centro();

    seedset_free(&s);

    printf("\n=============================================================\n");
    if (g_fallos == 0) printf(" TODOS LOS TESTS PASARON\n");
    else               printf(" %d TEST(S) FALLARON\n", g_fallos);
    printf("=============================================================\n");

    return g_fallos == 0 ? 0 : 1;
}
