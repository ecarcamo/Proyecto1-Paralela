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

Vec3 sphere_dir_fib(int i, int n, double angle_rad)
{
    if (n < 1) n = 1;

    /* dz es el grosor de cada franja de area 4*pi/N.
     * El +1/2 del numerador centra la semilla en su franja (regla del punto
     * medio) en vez de pegarla al borde. Sin ese medio, las semillas de los
     * polos quedan corridas y la uniformidad empeora notablemente para N
     * pequeno. */
    const double dz = 2.0 / (double)n;

    /* --- altura: reparto de AREAS iguales (teorema de Arquimedes) -------- */
    double z = 1.0 - dz * ((double)i + 0.5);

    /* --- radio del paralelo a esa altura (Pitagoras sobre S^2) ----------- */
    /* El chequeo protege contra un z que por redondeo salga apenas fuera de
     * [-1,1]: sin el, sqrt de un negativo diminuto daria NaN y arruinaria
     * una semilla entera. */
    double rho2 = 1.0 - z * z;
    double rho  = rho2 > 0.0 ? sqrt(rho2) : 0.0;

    /* --- azimut: el angulo aureo acumulado -------------------------------
     * En double, no en float: n*psi con n en los miles pierde precision
     * rapido en float y el patron se degrada de forma visible.
     * El fmod mantiene el argumento chico para que sin/cos no pierdan
     * precision por reduccion de rango con n grande. */
    double theta = fmod((double)i * angle_rad, 2.0 * SS_PI);

    return v3((float)(rho * cos(theta)), (float)(rho * sin(theta)), (float)z);
}

void sphere_fill_fibonacci(SeedSet *s, int n, double angle_rad, uint64_t seed)
{
    if (s == NULL || n < 1 || n > s->capacity) return;

    s->n = n;

    for (int i = 0; i < n; i++) {
        Vec3 p = sphere_dir_fib(i, n, angle_rad);

        s->x[i] = p.x;
        s->y[i] = p.y;
        s->z[i] = p.z;

        /* Estado de la fisica en reposo. Si --physics 0, nunca se toca. */
        s->vx[i] = s->vy[i] = s->vz[i] = 0.0f;
        s->ax[i] = s->ay[i] = s->az[i] = 0.0f;

        /* Color pseudoaleatorio. Funcion pura de (i, seed): sin estado, asi
         * que la version paralela produce exactamente los mismos colores. */
        s->color[i] = color_for_seed((uint32_t)i, seed);
    }
}

/* --------------------------------------------------------------------------
 *  Modo canon: la esfera se construye a canonazos
 * -------------------------------------------------------------------------- */

int sphere_cannon_capacity(int n, double fire_rate, double muzzle_speed, int trail)
{
    if (n < 1) n = 1;
    if (!(fire_rate > 0.0) || !(muzzle_speed > 0.0) || trail < 0) return n;

    /* Cuantas bolitas siguen en vuelo en un instante cualquiera, en regimen
     * estable: el vuelo dura 1/muzzle_speed segundos, y en ese tiempo se
     * disparan fire_rate/muzzle_speed bolitas nuevas. ceil() para no quedarse
     * corto por redondeo. */
    double en_vuelo_d = fire_rate / muzzle_speed;
    int en_vuelo = (int)ceil(en_vuelo_d);
    if (en_vuelo < 0) en_vuelo = 0;

    long capacity = (long)n + (long)en_vuelo * (long)trail;
    if (capacity < n) capacity = n;             /* overflow defensivo */
    if (capacity > SS_N_MAX) capacity = SS_N_MAX; /* mismo techo que --n */
    return (int)capacity;
}

/* Delta de tiempo entre fantasmas consecutivos de la estela: reparte los
 * 'trail' fantasmas a lo largo del vuelo completo (1/muzzle_speed segundos),
 * en vez de un valor fijo sin relacion con la velocidad de vuelo -- con
 * muzzle_speed alto el vuelo es corto y una estela con delta fijo grande
 * saldria de la boca antes de que la bolita llegara a la mitad de camino. */
static double cannon_trail_delta(double muzzle_speed, int trail)
{
    return (1.0 / muzzle_speed) / (double)(trail + 1);
}

/* Posicion y "vivo/no vivo" de la semilla i en el instante t. No escribe
 * nada: el llamador decide que hacer con el resultado (semilla real o
 * fantasma de estela van al mismo lugar del SoA). */
static int cannon_pos_at(int i, int n, double angle_rad,
                         double fire_rate, double muzzle_speed, double t, Vec3 *out)
{
    double t_disparo = (double)i / fire_rate;
    double edad = t - t_disparo;
    if (edad < 0.0) return 0;                   /* todavia no se disparo */

    double radio = muzzle_speed * edad;
    if (radio > 1.0) radio = 1.0;
    /* radio >= 0 siempre: edad >= 0 y muzzle_speed > 0 (validado en config). */

    Vec3 dir = sphere_dir_fib(i, n, angle_rad);
    *out = v3_scale(dir, (float)radio);
    return 1;
}

void sphere_fill_cannon(SeedSet *s, int n, double angle_rad, uint64_t seed,
                        double fire_rate, double muzzle_speed, int trail, double t)
{
    if (s == NULL || n < 1 || !(fire_rate > 0.0) || !(muzzle_speed > 0.0) || trail < 0)
        return;

    const double delta = cannon_trail_delta(muzzle_speed, trail);
    int written = 0;
    const int cap = s->capacity;

    for (int i = 0; i < n && written < cap; i++) {
        Vec3 pos;
        if (!cannon_pos_at(i, n, angle_rad, fire_rate, muzzle_speed, t, &pos))
            continue;                            /* aun no se dispara */

        /* La bolita real (o ya aterrizada) de este instante. */
        s->x[written] = pos.x;
        s->y[written] = pos.y;
        s->z[written] = pos.z;
        s->vx[written] = s->vy[written] = s->vz[written] = 0.0f;
        s->ax[written] = s->ay[written] = s->az[written] = 0.0f;
        s->color[written] = color_for_seed((uint32_t)i, seed);
        written++;

        /* Fantasmas de estela: la MISMA formula evaluada en el pasado. Solo
         * mientras la bolita sigue en vuelo -- una vez aterrizada (radio=1
         * de forma sostenida) no tiene sentido dejarle una cola fija detras
         * apuntando al centro, se veria como un rayo clavado en la esfera. */
        double edad_actual = t - (double)i / fire_rate;
        if (edad_actual >= 1.0 / muzzle_speed) continue;   /* ya aterrizo */

        for (int j = 1; j <= trail && written < cap; j++) {
            double t_fantasma = t - (double)j * delta;
            Vec3 fpos;
            if (!cannon_pos_at(i, n, angle_rad, fire_rate, muzzle_speed,
                               t_fantasma, &fpos))
                break;                            /* mas atras que su propio disparo */

            /* Atenuar el color con la antiguedad del fantasma: el mas viejo,
             * el mas tenue, para que se lea como una cola y no como copias
             * identicas apiladas. */
            float k = 1.0f - (float)j / (float)(trail + 1);
            s->x[written] = fpos.x;
            s->y[written] = fpos.y;
            s->z[written] = fpos.z;
            s->vx[written] = s->vy[written] = s->vz[written] = 0.0f;
            s->ax[written] = s->ay[written] = s->az[written] = 0.0f;
            s->color[written] = rgb_mul(color_for_seed((uint32_t)i, seed), k);
            written++;
        }
    }

    s->n = written;
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
