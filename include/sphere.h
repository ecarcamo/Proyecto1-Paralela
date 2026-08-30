/* ===========================================================================
 *  sphere.h - La esfera de Fibonacci: el nucleo matematico del screensaver.
 *
 *  Distribuye N semillas sobre la esfera unitaria usando el angulo aureo:
 *
 *        z_n = 1 - 2(n + 1/2)/N          <- reparte AREAS iguales
 *        rho_n = sqrt(1 - z_n^2)
 *        theta_n = n * psi               <- psi = 2*pi/phi^2
 *
 *        p_n = (rho*cos(theta), rho*sin(theta), z)
 *
 *  Por que z uniforme reparte areas iguales: por el teorema de Arquimedes, el
 *  area de una franja esferica entre dos planos paralelos es 2*pi*R*dz, o sea
 *  depende SOLO de dz y no de la latitud. El area es lineal en z, asi que
 *  repartir z uniformemente reparte area uniformemente.
 *
 *  Es el analogo exacto del r = c*sqrt(n) del caso plano: en 2D el area
 *  acumulada es cuadratica y al invertirla aparece la raiz; en 3D Arquimedes
 *  la vuelve lineal y la raiz desaparece.
 *
 *  Detalle completo en docs/01-FUNDAMENTO-MATEMATICO.md, seccion 2.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#ifndef SPHERE_H
#define SPHERE_H

#include <stdint.h>
#include "vec3.h"

/* ---------------------------------------------------------------------------
 *  SeedSet - las semillas, en SoA (Structure of Arrays).
 *
 *  Se usa SoA y no AoS (un arreglo de structs {x,y,z,...}) a proposito:
 *
 *   - El bucle interno del Voronoi recorre SOLO x[], y[], z[]. Con SoA esos
 *     tres arreglos se leen secuencialmente y cada linea de cache trae 16
 *     valores utiles. Con AoS, cada linea traeria tambien velocidad,
 *     aceleracion y color, que ese bucle no usa: ~60% del ancho de banda
 *     desperdiciado.
 *   - SoA es la unica forma de que el compilador vectorice el bucle. Con AoS
 *     las componentes quedan intercaladas y haria falta un gather.
 *
 *  Es una decision de localidad de cache que va a aparecer en el informe
 *  cuando midamos por que el kernel escala como escala.
 * ------------------------------------------------------------------------- */
typedef struct {
    int       n;         /* semillas en uso                                  */
    int       capacity;  /* semillas asignadas                               */

    float    *x, *y, *z;     /* posicion sobre la esfera unitaria (|p| = 1)  */
    float    *vx, *vy, *vz;  /* velocidad tangencial (solo si hay fisica)    */
    float    *ax, *ay, *az;  /* aceleracion         (solo si hay fisica)     */
    uint32_t *color;         /* color pseudoaleatorio ARGB8888               */
} SeedSet;

/* Reserva memoria para 'capacity' semillas. Devuelve 0 si todo bien, o un
 * valor negativo si fallo malloc (y en ese caso libera lo que ya habia
 * reservado, sin fugas). */
int  seedset_alloc(SeedSet *s, int capacity);

/* Libera y deja la estructura en cero. Seguro de llamar dos veces o sobre una
 * estructura ya limpia. */
void seedset_free(SeedSet *s);

/* ---------------------------------------------------------------------------
 *  sphere_dir_fib - la posicion i-esima del patron de Fibonacci, sola.
 *
 *  Es el nucleo matematico de sphere_fill_fibonacci(), extraido para que
 *  cualquier otra rutina que necesite "donde va la semilla i" (el cañon del
 *  modo --cannon, por ejemplo) lo calcule llamando a esto y no copiando la
 *  formula. Dos copias del angulo aureo tarde o temprano se desincronizan.
 *
 *    i          indice de la semilla, 0..n-1
 *    n          numero total de semillas (para el reparto de areas)
 *    angle_rad  angulo de divergencia en RADIANES
 *
 *  Devuelve un punto sobre la esfera UNITARIA (|p| = 1 exacto, salvo error
 *  de redondeo de float).
 * ------------------------------------------------------------------------- */
Vec3 sphere_dir_fib(int i, int n, double angle_rad);

/* ---------------------------------------------------------------------------
 *  Genera la esfera de Fibonacci.
 *
 *    s          conjunto ya reservado con capacity >= n
 *    n          numero de semillas (el parametro N del enunciado)
 *    angle_rad  angulo de divergencia EN RADIANES. Pasar SS_GOLDEN_ANG para el
 *               patron correcto; cualquier otro valor sirve para demostrar en
 *               vivo como degenera (es lo que hacen las teclas [ y ]).
 *    seed       semilla del PRNG para los colores.
 *
 *  El acumulador del angulo va en double aunque las posiciones se guarden en
 *  float: n*psi con n en los miles pierde precision rapido en float y el
 *  patron se degrada de forma visible.
 * ------------------------------------------------------------------------- */
void sphere_fill_fibonacci(SeedSet *s, int n, double angle_rad, uint64_t seed);

/* ---------------------------------------------------------------------------
 *  Cuantas semillas hay que reservar para el modo canon: las n aterrizadas
 *  mas los fantasmas de estela de las que estan en pleno vuelo.
 *
 *  en_vuelo = techo(fire_rate / muzzle_speed)   -- el vuelo dura 1/muzzle_speed
 *             segundos, y en ese tiempo se disparan fire_rate/muzzle_speed
 *             bolitas nuevas que still no aterrizaron.
 *  capacity = n + en_vuelo * trail
 *
 *  Se calcula ANTES de seedset_alloc(): esa funcion reparte diez punteros en
 *  un solo malloc y no hay camino de crecimiento despues.
 *
 *  Devuelve al menos n (si trail es 0 o los parametros son degenerados, no
 *  reserva de menos).
 * ------------------------------------------------------------------------- */
int sphere_cannon_capacity(int n, double fire_rate, double muzzle_speed, int trail);

/* ---------------------------------------------------------------------------
 *  sphere_fill_cannon - la esfera de Fibonacci, construyendose a canonazos.
 *
 *  Reescribe el SoA para el instante 't': cada semilla i sale disparada en
 *  t_disparo(i) = i/fire_rate y viaja en linea recta desde el centro hasta su
 *  posicion definitiva de Fibonacci (sphere_dir_fib), tardando 1/muzzle_speed
 *  segundos. Detras de cada bolita en vuelo se dibujan 'trail' fantasmas: el
 *  fantasma j es la MISMA formula evaluada en (t - j*delta), asi que la
 *  estela sigue siendo forma cerrada, sin ningun estado que integrar.
 *
 *  TODO sale de una funcion pura de (i, t): dos llamadas con el mismo t dan
 *  el mismo resultado bit a bit, sin importar cuantos frames se dibujaron
 *  antes. Es la propiedad que permite comparar el framebuffer secuencial
 *  contra el paralelo en el mismo instante.
 *
 *    s             conjunto ya reservado con capacity >= sphere_cannon_capacity(...)
 *    n             semillas del patron (el parametro N del enunciado)
 *    angle_rad     angulo de divergencia, en radianes
 *    seed          semilla del PRNG para los colores
 *    fire_rate     disparos por segundo (> 0)
 *    muzzle_speed  radios por segundo; el vuelo de una bolita dura 1/muzzle_speed (> 0)
 *    trail         fantasmas de estela por bolita en vuelo (>= 0)
 *    t             tiempo de la animacion, en segundos (t <= 0 => esfera vacia)
 *
 *  Deja s->n en (vivas + fantasmas dibujados este instante) y escribe
 *  vx/vy/vz/ax/ay/az en cero: el modo canon es incompatible con --physics
 *  (config_validate lo exige), pero el SoA no debe quedar con basura.
 * ------------------------------------------------------------------------- */
void sphere_fill_cannon(SeedSet *s, int n, double angle_rad, uint64_t seed,
                        double fire_rate, double muzzle_speed, int trail, double t);

/* Devuelve la semilla i como Vec3. */
static inline Vec3 seed_pos(const SeedSet *s, int i)
{
    return v3(s->x[i], s->y[i], s->z[i]);
}

/* ---------------------------------------------------------------------------
 *  Angulo de divergencia medio medido sobre las posiciones ACTUALES.
 *
 *  Con el patron generado analiticamente esto devuelve el angulo que se paso
 *  como parametro (es un test de consistencia). Su verdadero valor aparece
 *  cuando la fisica esta activa: ahi las semillas se mueven por repulsion y
 *  este numero se ve converger solo hacia 137.5 grados. Es el momento mas
 *  fuerte de la presentacion: no programamos el angulo aureo, programamos que
 *  las semillas se empujen y phi aparecio.
 *
 *  Devuelve el angulo en GRADOS.
 *
 *  NO filtra bolitas en vuelo ni fantasmas de estela: recorre s->n de punta a
 *  punta asumiendo que todas estan sobre la esfera. Eso esta bien porque el
 *  unico llamador (main.c) solo la invoca si cfg->physics esta activo, y
 *  config_validate() ya rechaza --cannon junto con --physics -- las dos
 *  situaciones nunca coinciden. Si algun dia se llega a llamar con --cannon,
 *  hay que filtrar por radio antes (ver rr[] en render.c).
 * ------------------------------------------------------------------------- */
double sphere_mean_divergence_deg(const SeedSet *s);

#endif /* SPHERE_H */
