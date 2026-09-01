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
 *  Modo canon: la esfera se construye a canonazos, para siempre
 *
 *  Todo lo de aqui abajo es funcion pura de (i, t). No hay una sola variable
 *  que sobreviva entre llamadas: eso es lo que permite (a) pausar sin codigo
 *  extra, (b) que el bench arranque en cualquier instante, y (c) que la
 *  version paralela produzca el mismo framebuffer bit a bit.
 * -------------------------------------------------------------------------- */

CannonParams cannon_params_from_config(const Config *cfg)
{
    CannonParams p;

    if (cfg == NULL) {
        memset(&p, 0, sizeof p);
        return p;
    }

    p.n             = cfg->n;
    p.angle_rad     = cfg->angle_rad;
    p.seed          = cfg->seed;
    p.fire_rate     = cfg->fire_rate;
    p.muzzle_speed  = cfg->muzzle_speed;
    p.trail         = cfg->trail;
    p.cannons       = cfg->cannons;
    p.layout        = cfg->cannon_layout;
    p.muzzle_radius = cfg->muzzle_radius;
    p.recirculate   = cfg->recirculate;
    return p;
}

int cannon_rounds(int n, int cannons)
{
    if (n < 1) n = 1;
    if (cannons < 1) cannons = 1;
    if (cannons > n) cannons = n;
    return (n + cannons - 1) / cannons;          /* techo(n / K) */
}

void cannon_slot(int i, int n, int cannons, int layout,
                 int *cannon_out, int *round_out)
{
    if (n < 1) n = 1;
    if (cannons < 1) cannons = 1;
    if (cannons > n) cannons = n;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;

    int c, r;

    if (layout == SS_CANNON_BLOCKS) {
        /* Bloques contiguos: el canon c se queda con [base(c), base(c+1)),
         * base(c) = techo(c*n/K). Se usa ESE reparto y no piso(c*n/K) porque
         * su inverso es exacto y O(1): con base = techo, el canon del indice
         * i es piso(i*K/n) -- con piso el inverso no cierra y habria que
         * buscar el bloque. Los tamanos quedan en {piso(n/K), techo(n/K)}. */
        long li = (long)i, lk = (long)cannons, ln = (long)n;
        c = (int)((li * lk) / ln);
        if (c >= cannons) c = cannons - 1;        /* defensivo ante redondeo */
        long base = ((long)c * ln + lk - 1) / lk; /* techo(c*n/K)            */
        r = (int)(li - base);
        if (r < 0) r = 0;
    } else {
        /* Round-robin: el canon c toma uno de cada K indices. Como los
         * indices consecutivos estan separados por el angulo aureo, los K
         * chorros se entremezclan y la esfera se puebla pareja. */
        c = i % cannons;
        r = i / cannons;
    }

    if (cannon_out != NULL) *cannon_out = c;
    if (round_out  != NULL) *round_out  = r;
}

Vec3 cannon_muzzle(int c, const CannonParams *p)
{
    if (p == NULL) return v3_zero();

    int k = (p->cannons < 1) ? 1 : p->cannons;
    if (k > p->n && p->n >= 1) k = p->n;

    double r0 = p->muzzle_radius;
    if (!(r0 > 0.0)) return v3_zero();            /* todas las bocas al origen */

    /* Misma construccion de Fibonacci que las semillas, con K puntos en vez
     * de N: los canones quedan repartidos parejo sobre una esfera chica sin
     * una sola linea de matematica nueva. */
    return v3_scale(sphere_dir_fib(c, k, p->angle_rad), (float)r0);
}

int sphere_cannon_capacity(const CannonParams *p)
{
    if (p == NULL) return 1;

    int n = (p->n < 1) ? 1 : p->n;
    if (!(p->fire_rate > 0.0) || !(p->muzzle_speed > 0.0) || p->trail < 0)
        return n;

    int k = (p->cannons < 1) ? 1 : p->cannons;
    if (k > n) k = n;

    /* Bolitas en vuelo en regimen permanente: la fraccion del ciclo que una
     * bolita pasa volando es (1/V)/T_ciclo, y multiplicada por las n del
     * patron da K*R/V. El "+ k" es margen: cada canon puede tener una bolita
     * de mas en el aire segun donde caiga el redondeo de la ronda dentro de
     * la ventana de vuelo. */
    double en_vuelo_d = (double)k * p->fire_rate / p->muzzle_speed;
    if (!(en_vuelo_d >= 0.0)) en_vuelo_d = 0.0;           /* NaN defensivo */
    if (en_vuelo_d > (double)n) en_vuelo_d = (double)n;   /* tope duro: no hay
                                                           * mas bolitas que
                                                           * indices */
    long en_vuelo = (long)ceil(en_vuelo_d) + (long)k;
    if (en_vuelo > (long)n) en_vuelo = n;

    long capacity = (long)n + en_vuelo * (long)p->trail;
    if (capacity < n) capacity = n;                 /* overflow defensivo */
    if (capacity > SS_N_MAX) capacity = SS_N_MAX;   /* mismo techo que --n */
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

/* Fase del indice i en el instante t: cuanto hace que salio del canon.
 *
 * Devuelve 0 si la bolita todavia no tuvo su primer disparo (t < t_disparo);
 * en ese caso *fase_out queda sin tocar. A partir del primer disparo la
 * bolita ya nunca deja de existir.
 *
 * Con recirculate = 0 (el default) la fase es la edad cruda y crece sin
 * techo: cannon_pos_at_phase() satura el radio en 1, asi que la bolita
 * aterriza y se queda. Es lo que hace que la esfera se COMPLETE y se quede
 * completa en n bolitas.
 *
 * Con recirculate = 1 se envuelve al periodo T_ciclo y el slot vuelve a
 * salir de la boca: carga constante para medir, pero solo una fraccion
 * 1 - K*R/(V*n) de la esfera puesta en cualquier instante. */
static int cannon_phase(int i, const CannonParams *p, double t_ciclo, double t,
                        double *fase_out)
{
    int ronda;
    cannon_slot(i, p->n, p->cannons, p->layout, NULL, &ronda);

    double t_disparo = (double)ronda / p->fire_rate;
    double edad = t - t_disparo;
    if (edad < 0.0) return 0;                     /* todavia no se disparo */

    double fase = edad;

    if (p->recirculate) {
        /* La recirculacion entera es este fmod. fmod de un no-negativo es
         * no-negativo, pero se envuelve igual por si el redondeo devuelve
         * algo apenas < 0. */
        fase = fmod(edad, t_ciclo);
        if (fase < 0.0) fase += t_ciclo;
    }

    *fase_out = fase;
    return 1;
}

/* Posicion del indice i cuando lleva 'fase' segundos de vuelo. La bolita sale
 * de la boca de SU canon y va en linea recta a su lugar de Fibonacci: sigue
 * siendo forma cerrada, solo que ahora el origen del segmento no es el centro.
 *
 * Con muzzle_radius = 0 la boca es el origen exacto y esto se reduce, bit a
 * bit, al  dir * radio  del canon unico. */
static Vec3 cannon_pos_at_phase(int i, const CannonParams *p, double fase)
{
    double radio = p->muzzle_speed * fase;
    if (radio > 1.0) radio = 1.0;
    if (radio < 0.0) radio = 0.0;

    Vec3 destino = sphere_dir_fib(i, p->n, p->angle_rad);
    if (radio >= 1.0) return destino;             /* aterrizada: exacta */

    int c;
    cannon_slot(i, p->n, p->cannons, p->layout, &c, NULL);
    Vec3 boca = cannon_muzzle(c, p);

    /* lerp(boca, destino, radio) = boca + (destino - boca) * radio */
    return v3_madd(boca, v3_sub(destino, boca), (float)radio);
}

/* Escribe un slot del SoA. Semilla real y fantasma de estela van al mismo
 * lugar: el renderer no distingue, y por eso la estela no lo obliga a
 * cambiar nada. */
static void cannon_write(SeedSet *s, int slot, Vec3 pos, uint32_t color)
{
    s->x[slot] = pos.x;
    s->y[slot] = pos.y;
    s->z[slot] = pos.z;
    s->vx[slot] = s->vy[slot] = s->vz[slot] = 0.0f;
    s->ax[slot] = s->ay[slot] = s->az[slot] = 0.0f;
    s->color[slot] = color;
}

void sphere_fill_cannon(SeedSet *s, const CannonParams *p, double t)
{
    if (s == NULL || p == NULL) return;
    if (p->n < 1 || !(p->fire_rate > 0.0) || !(p->muzzle_speed > 0.0) ||
        p->trail < 0)
        return;

    const int    n         = p->n;
    const double t_vuelo   = 1.0 / p->muzzle_speed;
    const double t_ciclo   = (double)cannon_rounds(n, p->cannons) / p->fire_rate;
    const double delta     = cannon_trail_delta(p->muzzle_speed, p->trail);
    const int    cap       = s->capacity;

    int written = 0;

    for (int i = 0; i < n && written < cap; i++) {
        double fase;
        if (!cannon_phase(i, p, t_ciclo, t, &fase))
            continue;                              /* aun no se dispara */

        uint32_t base_color = color_for_seed((uint32_t)i, p->seed);

        /* La bolita real (en vuelo o ya aterrizada) de este instante. */
        cannon_write(s, written, cannon_pos_at_phase(i, p, fase), base_color);
        written++;

        /* Fantasmas de estela: la MISMA formula evaluada en el pasado. Solo
         * mientras la bolita sigue en vuelo -- una vez aterrizada no tiene
         * sentido dejarle una cola fija detras apuntando al centro, se veria
         * como un rayo clavado en la esfera. */
        if (fase >= t_vuelo) continue;             /* ya aterrizo */

        for (int j = 1; j <= p->trail && written < cap; j++) {
            double fase_j = fase - (double)j * delta;
            /* La cola no cruza hacia atras del disparo que la genero. Sin
             * este corte, una bolita recien recirculada arrastraria fantasmas
             * del ciclo anterior, pegados a la superficie. */
            if (fase_j < 0.0) break;

            /* Atenuar el color con la antiguedad del fantasma: el mas viejo,
             * el mas tenue, para que se lea como una cola y no como copias
             * identicas apiladas. */
            float k = 1.0f - (float)j / (float)(p->trail + 1);
            cannon_write(s, written, cannon_pos_at_phase(i, p, fase_j),
                         rgb_mul(base_color, k));
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
