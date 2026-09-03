/* ===========================================================================
 *  physics.c - Repulsion de Coulomb con softening, proyectada a la esfera e
 *  integrada con Velocity-Verlet.
 *
 *  El punto central del diseno (y va documentado, no es casualidad): el N^2
 *  de la repulsion se hace COMPLETO. Aprovechar F_ji = -F_ij ahorraria la
 *  mitad del trabajo, pero obligaria a escribir en la fuerza de j desde la
 *  iteracion de i -- una condicion de carrera en cuanto esto se paralelice.
 *  Con el N^2 completo cada semilla escribe solo su propia fuerza, asi que
 *  el kernel ya sale libre de sincronia sin que nadie tenga que arreglarlo
 *  despues (docs/01-FUNDAMENTO-MATEMATICO.md, seccion 5.1).
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#include "physics.h"

#include <math.h>
#include <stdlib.h>

#include "config.h"
#include "vec3.h"

/* ==========================================================================
 *  physics_max_dt - limite de estabilidad ~ 1/sqrt(N). La derivacion y la
 *  tabla de medidas que fijan la constante estan en physics.h.
 * ========================================================================== */
double physics_max_dt(int n)
{
    if (n < 2) return 1.0;                 /* con 0 o 1 semilla no hay fuerzas */
    return SS_PHYS_DT_SAFETY / sqrt((double)n);
}

/* ==========================================================================
 *  physics_step
 *
 *  Un solo calculo de fuerzas por llamada (Verlet clasico):
 *
 *    1. p(t+dt) = p(t) + v(t)*dt + 0.5*a(t)*dt^2      -- a(t) es el viejo,
 *       ya persistido en s->ax/ay/az desde la llamada anterior
 *    2. renormalizar p(t+dt) a la esfera unitaria
 *    3. recalcular fuerzas EN p(t+dt) -> a(t+dt), N^2 completo
 *    4. v(t+dt) = v(t) + 0.5*(a(t) + a(t+dt))*dt
 *    5. persistir a(t+dt) en s->ax/ay/az para la proxima llamada
 *
 *  Hace falta un buffer temporal de posiciones nuevas: todas las posiciones
 *  tienen que estar actualizadas ANTES de recalcular cualquier fuerza (si no,
 *  la fuerza sobre la semilla 5 usaria la posicion vieja de la 3 pero la
 *  nueva de la 2, y el resultado dependeria del orden del bucle).
 * ========================================================================== */
/* Debajo de este N, armar el equipo de hilos cuesta mas que el bucle: los
 * 'parallel for' de abajo se saltan solos via la clausula if(). Sin -fopenmp
 * la clausula es inerte igual que el pragma, asi que screensaver_seq no ve
 * ninguna diferencia. */
#define PHYS_PAR_MIN 256

void physics_step(SeedSet *s, const PhysicsParams *p, double dt)
{
    if (s == NULL || p == NULL || s->n < 2 || dt <= 0.0) return;

    const int n = s->n;
    const float fdt = (float)dt;

    float *nx = (float *)malloc((size_t)n * sizeof(float));
    float *ny = (float *)malloc((size_t)n * sizeof(float));
    float *nz = (float *)malloc((size_t)n * sizeof(float));
    if (nx == NULL || ny == NULL || nz == NULL) {
        free(nx); free(ny); free(nz);
        return;                                   /* sin memoria, no crash */
    }

    /* --- 1) posicion nueva con la aceleracion vieja, y 2) renormalizar ---
     *
     * Cada 'i' lee y escribe SOLO su propio indice: no hay dependencia entre
     * iteraciones. Mismo schedule que el O(N^2) de abajo y por la misma razon
     * (ver ahi): la carga es uniforme pero los cores no. El 'if' evita pagar
     * el fork/join cuando N es tan chico que armar el equipo cuesta mas que
     * el bucle entero. */
    #pragma omp parallel for schedule(dynamic, 1) if(n >= PHYS_PAR_MIN)
    for (int i = 0; i < n; ++i) {
        Vec3 pos = seed_pos(s, i);
        Vec3 vel = v3(s->vx[i], s->vy[i], s->vz[i]);
        Vec3 acc_old = v3(s->ax[i], s->ay[i], s->az[i]);

        Vec3 pos_new = v3_add(pos, v3_scale(vel, fdt));
        pos_new = v3_madd(pos_new, acc_old, 0.5f * fdt * fdt);
        pos_new = v3_norm(pos_new);               /* de vuelta a la esfera unitaria */

        nx[i] = pos_new.x;
        ny[i] = pos_new.y;
        nz[i] = pos_new.z;
    }

    for (int i = 0; i < n; ++i) {
        s->x[i] = nx[i];
        s->y[i] = ny[i];
        s->z[i] = nz[i];
    }
    free(nx); free(ny); free(nz);

    /* --- 3) fuerzas en las posiciones nuevas: Coulomb con softening, N^2 completo --- */
    const float eps2 = p->epsilon * p->epsilon;

    /* El unico O(N^2) del programa y el bucle mas caro cuando --physics 1.
     *
     * Por que no hay carrera: cada 'i' escribe unicamente s->vx/vy/vz[i] y
     * s->ax/ay/az[i]. Lo que LEE de las demas semillas son x/y/z, que ya
     * quedaron fijas en el paso 2 y nadie modifica dentro de este bucle. No
     * hace falta reduccion ni atomicos: el acumulador 'F' es local a la
     * iteracion, no compartido.
     *
     * schedule(dynamic, 1) y no static, aunque la carga sea uniforme.
     *
     * Este bucle es el caso de libro para static: cada 'i' hace EXACTAMENTE N
     * iteraciones internas, no hay ni una rama que dependa de los datos. Por
     * eso llevaba static. Medirlo mostro que static pierde por 15%:
     *
     *   N=16000 --raster 1 --physics 1, 24 hilos, 4 reps (solo la fisica):
     *     static      58.90 ms      dynamic,1   50.32 ms  <- elegido
     *     static,1    59.55 ms      dynamic,4   51.82 ms
     *     static,8    59.91 ms      dynamic,16  51.37 ms
     *     auto        60.88 ms      guided      55.20 ms
     *
     * La razon no esta en el bucle, esta en la CPU: 8 P-cores + 16 E-cores.
     * TRABAJO uniforme no es TIEMPO uniforme cuando la mitad de los cores es
     * mas lenta. Con static el E-core recibe el mismo bloque que el P-core y
     * tarda casi el doble; los P-cores terminan y se quedan en la barrera.
     * dynamic lo absorbe solo: el que termina vuelve por mas.
     *
     * Corolario para el informe: en una CPU hibrida, "la carga es uniforme"
     * ya no alcanza para justificar static. Habria que volver a medirlo en
     * una maquina homogenea, donde static deberia recuperar la ventaja. */
    #pragma omp parallel for schedule(dynamic, 1) if(n >= PHYS_PAR_MIN)
    for (int i = 0; i < n; ++i) {
        Vec3 pi = seed_pos(s, i);
        Vec3 F  = v3_zero();

        for (int j = 0; j < n; ++j) {
            if (j == i) continue;

            Vec3  d  = v3_sub(pi, seed_pos(s, j));
            float r2 = v3_len2(d) + eps2;
            float inv_r3 = 1.0f / (r2 * sqrtf(r2));       /* (r^2)^(3/2) = r^3 */

            F = v3_madd(F, d, p->k * inv_r3);
        }

        /* proyectar al plano tangente: la semilla solo se mueve sobre S^2 */
        F = v3_reject(F, pi);

        Vec3 vel = v3(s->vx[i], s->vy[i], s->vz[i]);
        Vec3 acc_new = v3_sub(v3_scale(F, 1.0f / p->mass), v3_scale(vel, p->gamma));

        /* --- 4) velocidad con el promedio de aceleraciones -------------- */
        Vec3 acc_old = v3(s->ax[i], s->ay[i], s->az[i]);
        Vec3 vel_new = v3_madd(vel, v3_add(acc_old, acc_new), 0.5f * fdt);

        s->vx[i] = vel_new.x; s->vy[i] = vel_new.y; s->vz[i] = vel_new.z;

        /* --- 5) persistir a(t+dt) para el proximo paso ------------------ */
        s->ax[i] = acc_new.x; s->ay[i] = acc_new.y; s->az[i] = acc_new.z;
    }
}
